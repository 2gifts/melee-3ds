#ifndef MP_FRAME_STATS_H
#define MP_FRAME_STATS_H
#include <stdint.h>
#define MP_FRAME_STATS_COUNT 120
typedef struct {
    uint32_t previous, count, ticks[MP_FRAME_STATS_COUNT];
    unsigned ready;
} MPFrameStats;
/* Use end-to-end frame timestamps: include the companion UI, logging and
 * pacing, rather than excluding those costs from the reported intervals.
 * Unsigned subtraction retains the native 40.5 MHz clock's wrap behavior. */
static inline int mp_frame_stats_sample(MPFrameStats*s,uint32_t tick){
    if(!s->ready){s->previous=tick;s->ready=1;return 0;}
    uint32_t elapsed=tick-s->previous;s->previous=tick;
    s->ticks[s->count++]=elapsed;
    return s->count==MP_FRAME_STATS_COUNT;
}
static inline void mp_frame_stats_sort(MPFrameStats*s){
    for(unsigned i=1;i<s->count;++i){
        uint32_t value=s->ticks[i];unsigned j=i;
        while(j&&s->ticks[j-1]>value){s->ticks[j]=s->ticks[j-1];--j;}
        s->ticks[j]=value;
    }
}
#endif
