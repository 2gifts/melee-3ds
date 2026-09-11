#ifndef MP_TIMING_H
#define MP_TIMING_H
#include <stdint.h>
/* The GameCube sampled pads independently of drawing. Preserve timer phase
 * when a cooperative poll is late; retain at most eight missed periods after
 * blocking asset I/O, rather than building an unbounded input backlog. */
static int64_t mp_periodic_start(int64_t now,int64_t start,int64_t period){
    return start>now?start:start+((now-start)/period+1)*period;
}
static int64_t mp_periodic_advance(int64_t now,int64_t fire,int64_t period){
    int64_t next=fire+period;
    if(now>=next&&(now-next)/period>=8)
        next+=((now-next)/period-7)*period;
    return next;
}
#endif
