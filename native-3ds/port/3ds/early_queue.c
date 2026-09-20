#include <3ds.h>
#include <citro3d.h>
#include "early_queue.h"
#include "vendor/citro3d_internal.h"
static int active;
static unsigned pending;
static u64 started;
unsigned mp_early_queue_starts,mp_early_queue_finishes;
unsigned mp_early_queue_runs,mp_early_queue_appends,mp_early_queue_capacity;
u64 mp_early_queue_wait_ticks,mp_early_queue_span_ticks;
#ifdef MP_ASYNC_PRESENTATION
static unsigned deferred;
unsigned mp_early_queue_deferrals,mp_early_queue_retirements;
void mp_early_queue_capacity_completed(u64 wait_ticks){
    mp_early_queue_wait_ticks+=wait_ticks;
    if(deferred){mp_early_queue_finishes+=deferred;deferred=0;++mp_early_queue_retirements;
        mp_early_queue_span_ticks+=svcGetSystemTick()-started;}
    if(active){mp_early_queue_span_ticks+=svcGetSystemTick()-started;
        mp_early_queue_finishes+=pending;pending=0;active=0;}
}
void mp_early_queue_defer(void){
    if(!active)return;
    if(deferred)svcBreak(USERBREAK_PANIC);
    deferred=pending;pending=0;active=0;++mp_early_queue_deferrals;
}
/* Called only after the next successful SDK FrameBegin has joined both GPU
 * work and its display callback. It does not permit earlier resource reuse. */
void mp_early_queue_retire(void){
    if(!deferred)return;
    mp_early_queue_finishes+=deferred;
    mp_early_queue_span_ticks+=svcGetSystemTick()-started;
    deferred=0;++mp_early_queue_retirements;
}
#endif
static int submit(int append){
    if(active&&!append)return 0;
    gxCmdQueue_s*q=&C3Di_GetContext()->gxQueue;
    /* Reserve eight entries for incidental transfers/clears and the final
     * presentation. At capacity the caller simply keeps recording its tail.
     * Never clear entries or recycle command/resource memory while active. */
    if(q->numEntries+8>=q->maxEntries){++mp_early_queue_capacity;return 0;}
    unsigned before=q->numEntries;
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
    if(q->numEntries==before)return 0;
    ++pending;++mp_early_queue_starts;
    if(active){
        /* libctru gxCmdQueueAdd publishes under queueLock and restarts a
         * drained active queue. Completion only observes committed entries.
         * The SDK's swap flags are still clear until our final fence. */
        ++mp_early_queue_appends;
    }else{
        started=svcGetSystemTick();active=1;++mp_early_queue_runs;
        gxCmdQueueRun(q);
    }
    return 1;
}
int mp_early_queue_submit(void){return submit(0);}
int mp_early_queue_append(void){return submit(1);}
void mp_early_queue_finish(void){
    if(!active)return;
    u64 wait=svcGetSystemTick();
#ifdef MP_ASYNC_PRESENTATION
    /* The candidate joins callback completion as well as GPU completion. */
    C3Di_RenderQueueWaitDone();
#else
    gxCmdQueue_s*q=&C3Di_GetContext()->gxQueue;
    gxCmdQueueWait(q,-1);
    gxCmdQueueStop(q);gxCmdQueueClear(q);
#endif
    u64 now=svcGetSystemTick();
    mp_early_queue_wait_ticks+=now-wait;
    /* Includes CPU work and possible queue starvation; this is an elapsed
     * overlap span, NOT an isolated GPU execution-time measurement. */
    mp_early_queue_span_ticks+=now-started;
    active=0;mp_early_queue_finishes+=pending;pending=0;
}
