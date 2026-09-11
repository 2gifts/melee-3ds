#include <assert.h>
#include <stdio.h>
#include "../port/engine/timing.h"
int main(void){
    const int64_t period=675000;
    assert(mp_periodic_start(100,200,10)==200);
    assert(mp_periodic_start(200,200,10)==210);
    assert(mp_periodic_start(100000000000LL,period,period)%period==0);
    /* A 50 ms draw still produces three scheduled pad samples. */
    int64_t fire=period,now=3*period;int samples=0;
    while(fire<=now){fire=mp_periodic_advance(now,fire,period);++samples;}
    assert(samples==3&&fire==4*period);
    /* Jitter accumulates without changing the original 60 Hz phase. */
    fire=period;samples=0;
    for(now=123456;now<100*period;now+=123456)
        while(fire<=now){fire=mp_periodic_advance(now,fire,period);++samples;}
    assert(samples==99&&fire==100*period);
    /* Long I/O stalls have bounded recovery, including after 32-bit wrap. */
    now=(1LL<<33)+1234;fire=period;samples=0;
    while(fire<=now){fire=mp_periodic_advance(now,fire,period);++samples;assert(samples<=9);}
    assert(fire>now&&fire-now<=period&&fire%period==0);
    puts("Periodic sampling keeps phase and bounded recovery");
}
