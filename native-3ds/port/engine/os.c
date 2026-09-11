#include <dolphin/os.h>
#include <dolphin/os/OSAlloc.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include "native.h"
static u32 virtual_msr;
u32 PPCMfmsr(void){return virtual_msr;}
void PPCMtmsr(u32 value){virtual_msr=value;}
static OSErrorHandler error_handlers[32];
OSErrorHandler OSSetErrorHandler(OSError error,OSErrorHandler handler){if(error>=32)return NULL;OSErrorHandler old=error_handlers[error];error_handlers[error]=handler;return old;}
void OSTicksToCalendarTime(OSTime ticks,OSCalendarTime*out){
    s64 seconds=ticks/40500000;int days=seconds/86400,year=2000;
    memset(out,0,sizeof(*out));out->sec=seconds%60;out->min=seconds/60%60;out->hour=seconds/3600%24;
    out->wday=(days+6)%7;out->msec=(ticks/40500)%1000;out->usec=(ticks*2/81)%1000;
    for(;;){int leap=year%4==0&&(year%100!=0||year%400==0);if(days<365+leap)break;days-=365+leap;++year;}
    out->year=year;out->yday=days;static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int month=0;for(;month<11;++month){int n=lengths[month]+(month==1&&year%4==0&&(year%100!=0||year%400==0));if(days<n)break;days-=n;}
    out->mon=month;out->mday=days+1;
}
#define ARENA_BYTES (24*1024*1024)
typedef struct Block{struct Block*next;u32 size,magic,pad[5];}Block;
typedef struct{u8*low,*high;Block*free;int active;}Heap;
static Heap heaps[16];static u8*arena,*arena_low,*arena_high;
volatile OSHeapHandle __OSCurrHeap=-1;
static BOOL interrupts=1;static int pumping;
static u32 tick_high,last_tick,progressive,sound_mode;
static OSContext context,*current_context=&context;
void OSInit(void){if(!arena){arena=mp_platform_alloc(ARENA_BYTES);HSD_ASSERT(1,arena);arena_low=arena;arena_high=arena+ARENA_BYTES;}}
void*OSGetArenaLo(void){OSInit();return arena_low;}void*OSGetArenaHi(void){OSInit();return arena_high;}
void OSSetArenaLo(void*p){arena_low=p;}void OSSetArenaHi(void*p){arena_high=p;}
void*OSAllocFromArenaLo(u32 size,u32 align){OSInit();uintptr_t p=((uintptr_t)arena_low+align-1)&~(uintptr_t)(align-1);HSD_ASSERT(1,p+size<=(uintptr_t)arena_high);arena_low=(u8*)(p+size);return(void*)p;}
void*OSAllocFromArenaHi(u32 size,u32 align){OSInit();uintptr_t p=((uintptr_t)arena_high-size)&~(uintptr_t)(align-1);HSD_ASSERT(1,p>=(uintptr_t)arena_low);arena_high=(u8*)p;return(void*)p;}
u32 OSGetConsoleSimulatedMemSize(void){return ARENA_BYTES;}u32 OSGetPhysicalMemSize(void){return ARENA_BYTES;}
unsigned long OSGetConsoleType(void){return 0x10000002;}
void*OSInitAlloc(void*start,void*end,int max){(void)end;HSD_ASSERT(1,max<=16);memset(heaps,0,sizeof(heaps));return(void*)(((uintptr_t)start+31)&~31u);}
int OSCreateHeap(void*start,void*end){for(int i=0;i<16;++i)if(!heaps[i].active){Heap*h=&heaps[i];h->low=(u8*)(((uintptr_t)start+31)&~31u);h->high=(u8*)((uintptr_t)end&~31u);HSD_ASSERT(1,h->high>h->low+32);h->free=(Block*)h->low;h->free->size=h->high-h->low;h->free->next=NULL;h->active=1;return i;}return-1;}
void OSDestroyHeap(int heap){HSD_ASSERT(1,heap>=0&&heap<16);heaps[heap].active=0;}
static int valid_heap(int id){if(id<0){OSInit();if(!heaps[0].active)OSCreateHeap(arena_low,arena_high);id=0;}HSD_ASSERT(1,id<16&&heaps[id].active);return id;}
void*OSAllocFromHeap(int id,unsigned long size){id=valid_heap(id);u32 need=(size+63)&~31u;Block**link=&heaps[id].free;while(*link){Block*b=*link;if(b->size>=need){if(b->size-need>=64){Block*tail=(Block*)((u8*)b+need);tail->size=b->size-need;tail->next=b->next;*link=tail;b->size=need;}else *link=b->next;b->magic=0x4d504850;b->next=(Block*)(uintptr_t)id;return b+1;}link=&b->next;}return NULL;}
void OSFreeToHeap(int id,void*p){if(!p)return;Block*b=(Block*)p-1;HSD_ASSERT(1,b->magic==0x4d504850);id=(int)(uintptr_t)b->next;Block**link=&heaps[id].free;while(*link&&*link<b)link=&(*link)->next;b->next=*link;*link=b;b->magic=0;Block*cur=heaps[id].free;while(cur&&cur->next){if((u8*)cur+cur->size==(u8*)cur->next){cur->size+=cur->next->size;cur->next=cur->next->next;}else cur=cur->next;}}
long OSCheckHeap(int id){id=valid_heap(id);long sum=0;for(Block*b=heaps[id].free;b;b=b->next)sum+=b->size-32;return sum;}
unsigned long OSReferentSize(void*p){return((Block*)p)[-1].size-32;}
int OSSetCurrentHeap(int h){int old=__OSCurrHeap;__OSCurrHeap=h;return old;}
BOOL OSDisableInterrupts(void){BOOL old=interrupts;interrupts=0;return old;}
BOOL OSRestoreInterrupts(BOOL enabled){extern void mp_engine_poll(void);BOOL old=interrupts;interrupts=enabled;if(enabled)mp_engine_poll();return old;}
int mp_interrupts_enabled(void){return interrupts;}
BOOL OSEnableInterrupts(void){return OSRestoreInterrupts(1);}
OSTick OSGetTick(void){return mp_platform_ticks();}
OSTime OSGetTime(void){u32 low=mp_platform_ticks();if(low<last_tick)++tick_high;last_tick=low;return((u64)tick_high<<32)|low;}
unsigned long OSGetProgressiveMode(void){return progressive;}void OSSetProgressiveMode(u32 x){progressive=x;}
u32 OSGetSoundMode(void){return sound_mode;}void OSSetSoundMode(u32 x){sound_mode=x;}
OSContext*OSGetCurrentContext(void){return current_context;}void OSSetCurrentContext(OSContext*c){current_context=c;}
void OSClearContext(OSContext*c){memset(c,0,sizeof(*c));}void OSSaveFPUContext(OSContext*c){(void)c;}void OSLoadFPUContext(OSContext*c){(void)c;}
BOOL DBIsDebuggerPresent(void){return 0;}unsigned long OSGetResetCode(void){return 0;}BOOL OSGetResetSwitchState(void){return 0;}
void OSResetSystem(int reset,u32 code,BOOL menu){(void)reset;(void)code;(void)menu;mp_platform_panic("Engine requested reset");}
void OSPanic(char*file,int line,char*fmt,...){HSD_Panic(file,line,fmt);}
/* Indexed source data can change between draws (e.g. rebuilt refraction
 * lists). Honor the game's visibility boundary before reusing a snapshot. */
extern void mp_gx_invalidate_sources(void);
extern void mp_platform_texture_dirty(void*,u32,u32);
void DCFlushRange(void*p,u32 n){if(n){mp_gx_invalidate_sources();mp_platform_texture_dirty(p,n,1);}}
void DCStoreRange(void*p,u32 n){if(n){mp_gx_invalidate_sources();mp_platform_texture_dirty(p,n,1);}}
void DCInvalidateRange(void*p,u32 n){if(n){mp_gx_invalidate_sources();mp_platform_texture_dirty(p,n,0);}}
u32 VIGetNextField(void){return 0;}
