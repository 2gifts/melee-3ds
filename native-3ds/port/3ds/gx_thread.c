#include <3ds.h>
#include <string.h>
#include "gx_thread.h"

/* Native owner of the GX translator thread (engine side: gx_fifo.c). The
 * thread runs BE8 engine code through an ENGINEBRIDGE and returns to the SDK
 * for rendering. It is the only producer for the renderer while it runs. */
extern unsigned mp_game_gx_enable(void);
extern void mp_game_gx_translate(unsigned spin),mp_game_gx_stop(void),mp_game_gx_disable(void);
extern void mp_renderer_begin(void);
extern void mp_native_log(const char*);
extern void mp_native_panic(const char*) __attribute__((noreturn));
#define GX_SPIN 16384u
static Thread translator;
static LightEvent events[2],go;
static unsigned failed,failed_reported;
static char failure[256];
unsigned mp_gx_translator_core=~0u,mp_gx_translator_active;
#ifdef MP_SMOKE_TEST
volatile unsigned mp_test_gx_direct;
#endif

static void translator_main(void*unused){
    (void)unused;
    mp_gx_translator_core=svcGetProcessorID();
    /* Wait until the renderer worker exists: this thread is its producer. */
    LightEvent_Wait(&go);
    mp_renderer_begin();
    /* Spin before sleeping only on a core of its own (never core 0). */
    mp_game_gx_translate(mp_gx_translator_core==2?GX_SPIN:0);
}
int mp_gx_thread_create(int is_new){
    if(translator||!is_new)return 0;
#ifdef MP_SMOKE_TEST
    if(mp_test_gx_direct){mp_native_log("GX translator disabled by test control; direct GX\n");return 0;}
#endif
    LightEvent_Init(&events[0],RESET_ONESHOT);LightEvent_Init(&events[1],RESET_ONESHOT);LightEvent_Init(&go,RESET_STICKY);
    failed=failed_reported=0;failure[0]=0;
    if(!mp_game_gx_enable()){mp_native_log("GX FIFO allocation failed; direct GX\n");return 0;}
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    translator=threadCreate(translator_main,NULL,256*1024,priority,2,false);
    if(!translator){mp_game_gx_disable();mp_native_log("Core 2 unavailable for the GX translator; direct GX\n");return 0;}
    mp_gx_translator_active=1;
    mp_native_log("GX translator thread created with core-2 affinity; engine thread records GX commands\n");
    return 1;
}
void mp_gx_thread_go(void){if(translator)LightEvent_Signal(&go);}
int mp_gx_thread_is_current(void){return translator&&threadGetCurrent()==translator;}
u64 mp_gx_sleep_ticks[2]; /* 0: engine waiting for FIFO space, 1: translator waiting for records */
/* Translator thread activity for the stall report (log_io.c): 1 waiting for
 * commands, 2 GPU frame begin (wait), 3 GPU frame end, 4 draw, 5 texture
 * upload, 6 VRAM promotion copy; 0 replaying other GX commands. */
volatile unsigned mp_translator_site;
void mp_native_gx_sleep(unsigned who){
    unsigned site=mp_translator_site;if(who&1)mp_translator_site=1;
    u64 start=svcGetSystemTick();LightEvent_Wait(&events[who&1]);mp_gx_sleep_ticks[who&1]+=svcGetSystemTick()-start;
    if(who&1)mp_translator_site=site;
}
void mp_native_gx_wake(unsigned who){LightEvent_Signal(&events[who&1]);}
void mp_gx_thread_stop(void){
    if(!translator||mp_gx_thread_is_current())return;
    LightEvent_Signal(&go);
    if(!__atomic_load_n(&failed,__ATOMIC_ACQUIRE))mp_game_gx_stop();
    threadJoin(translator,U64_MAX);threadFree(translator);translator=NULL;
    mp_gx_translator_active=0;mp_game_gx_disable();
}
/* Engine thread: report a translator failure through the ordinary panic. */
void mp_native_gx_check(void){
    if(!__atomic_load_n(&failed,__ATOMIC_ACQUIRE)||failed_reported)return;
    failed_reported=1;mp_gx_thread_stop();mp_native_panic(failure);
}
void mp_gx_thread_panic(const char*message){
    unsigned n=0;
    while(n+1<sizeof(failure)&&message[n]){failure[n]=message[n];++n;}
    failure[n]=0;
    __atomic_store_n(&failed,1,__ATOMIC_RELEASE);
    LightEvent_Signal(&events[1]);
    threadExit(1);
}
