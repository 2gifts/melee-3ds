#include <dolphin/ar.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include "native.h"
static u8*memory;static u32 top=0x4000,allocations[1024],depth;
const u8*mp_aram_data(u32 addr,unsigned n){return memory&&addr<=16*1024*1024-n?memory+addr:NULL;}
u32 ARInit(u32*stack,u32 count){(void)stack;(void)count;if(!memory)memory=mp_platform_alloc(16*1024*1024);HSD_ASSERT(1,memory);return 0x4000;}
u32 ARGetSize(void){return 16*1024*1024;}u32 ARGetBaseAddress(void){return 0x4000;}
u32 ARAlloc(u32 n){HSD_ASSERT(1,depth<1024&&top+n<=ARGetSize());u32 addr=top;allocations[depth++]=n;top+=n;return addr;}
u32 ARFree(u32*n){HSD_ASSERT(1,depth);u32 size=allocations[--depth];if(n)*n=size;top-=size;return top;}
void ARQInit(void){}
static struct{ARQRequest*q;u32 type,src,dst,n;ARQCallback cb;}pending[128];static unsigned head,tail;static int pumping;
void ARQPostRequest(ARQRequest*q,u32 owner,u32 type,u32 priority,u32 src,u32 dst,u32 n,ARQCallback cb){q->owner=owner;q->type=type;q->priority=priority;q->source=src;q->dest=dst;q->length=n;q->callback=cb;HSD_ASSERT(1,tail-head<128);unsigned i=tail++&127;pending[i].q=q;pending[i].type=type;pending[i].src=src;pending[i].dst=dst;pending[i].n=n;pending[i].cb=cb;}
void mp_ar_pump(void){if(pumping||head==tail)return;pumping=1;unsigned i=head++&127;ARQRequest*q=pending[i].q;u32 type=pending[i].type,src=pending[i].src,dst=pending[i].dst,n=pending[i].n;ARQCallback cb=pending[i].cb;HSD_ASSERT(1,memory);if(type==ARAM_DIR_MRAM_TO_ARAM){HSD_ASSERT(1,dst+n<=ARGetSize());memcpy(memory+dst,(void*)src,n);}else{HSD_ASSERT(1,src+n<=ARGetSize());memcpy((void*)dst,memory+src,n);}if(cb)cb(q);pumping=0;}
