#ifndef MP_EARLY_QUEUE_H
#define MP_EARLY_QUEUE_H
/* In-frame submission. A completed command prefix uses
 * immutable storage; the caller flushes all newly written GPU resources.
 * The adapted SDK publishes all presentation transfers atomically to its
 * completion callback. Original-SDK builds retain the final stop/clear fence. */
extern unsigned mp_early_queue_starts,mp_early_queue_finishes;
extern unsigned mp_early_queue_runs,mp_early_queue_appends,mp_early_queue_capacity;
extern u64 mp_early_queue_wait_ticks,mp_early_queue_span_ticks;
int mp_early_queue_submit(void);
int mp_early_queue_append(void);
void mp_early_queue_finish(void);
#ifdef MP_ASYNC_PRESENTATION
extern unsigned mp_early_queue_deferrals,mp_early_queue_retirements;
void mp_early_queue_defer(void);
void mp_early_queue_retire(void);
void mp_early_queue_capacity_completed(u64 wait_ticks);
#endif
#endif
