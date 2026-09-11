#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../references/experiments/source_dirty.h"
int main(void){
    MPDirtyLog log={0};
    assert(!mp_dirty_intersects(&log,0,(void*)1000,100));
    assert(mp_dirty_record(&log,(void*)1100,10));
    assert(!mp_dirty_intersects(&log,0,(void*)1000,100)); /* Adjacent is disjoint. */
    assert(mp_dirty_intersects(&log,0,(void*)1099,2));
    assert(mp_dirty_record(&log,(void*)999,2));
    assert(mp_dirty_intersects(&log,1,(void*)1000,100));
    unsigned seen=log.sequence;
    for(unsigned i=0;i<MP_DIRTY_RANGES;++i)assert(mp_dirty_record(&log,(void*)(uintptr_t)(2000+i*4),4));
    assert(!mp_dirty_intersects(&log,seen,(void*)1000,100));
    assert(mp_dirty_record(&log,(void*)4000,4));
    assert(mp_dirty_intersects(&log,seen,(void*)1000,100)); /* Overwritten history forces comparison. */
    assert(!mp_dirty_intersects(&log,log.sequence,(void*)1000,100));
    assert(mp_dirty_record(&log,(void*)1000,0));assert(log.sequence==seen+MP_DIRTY_RANGES+1);
    assert(!mp_dirty_record(&log,(void*)(UINTPTR_MAX-3),8));
    assert(mp_dirty_intersects(&log,log.sequence,(void*)(UINTPTR_MAX-3),8));
    log.sequence=UINT_MAX;assert(!mp_dirty_record(&log,(void*)2000,4));
    assert(log.sequence==0&&mp_dirty_intersects(&log,UINT_MAX,(void*)2000,4));
    /* Compare a randomized ring against an independent unbounded history. */
    log=(MPDirtyLog){0};MPDirtyRange history[1000];unsigned random=0x53524345;
    for(unsigned i=0;i<1000;++i){random=random*1664525+1013904223;unsigned begin=random%4000;
        random=random*1664525+1013904223;unsigned size=1+random%500;
        history[i]=(MPDirtyRange){begin,begin+size};assert(mp_dirty_record(&log,(void*)(uintptr_t)begin,size));
        for(unsigned j=0;j<20;++j){random=random*1664525+1013904223;unsigned ago=random%90;
            if(ago>i+1)ago=i+1;unsigned start=i+1-ago;
            random=random*1664525+1013904223;unsigned base=random%4500,bytes=1+(random>>16)%400;
            int expected=ago>MP_DIRTY_RANGES;
            for(unsigned k=start;k<=i&&!expected;++k)expected=base<history[k].end&&history[k].begin<base+bytes;
            assert(mp_dirty_intersects(&log,start,(void*)(uintptr_t)base,bytes)==expected);
        }
    }
    puts("Dirty source history: boundaries, overflow, sequence wrap and 20,000 independent overlap comparisons passed");
}
