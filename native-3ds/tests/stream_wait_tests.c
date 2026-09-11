#include <assert.h>
#include <stdio.h>
static int polling;
static volatile unsigned HSD_Synth_804D7778;
static unsigned dvd_pending,ar_pending,blocks,dvd_calls,ar_calls,nested_mixer;
/* Completion callbacks restore interrupt state and attempt the general poll.
 * The I/O-only wait must prevent a reentrant audio/VI callback. */
static void restore_interrupts(void){if(!polling)++nested_mixer;}
void mp_dvd_pump(void){++dvd_calls;if(dvd_pending){dvd_pending=0;ar_pending=1;restore_interrupts();}}
void mp_ar_pump(void){++ar_calls;if(ar_pending){ar_pending=0;if(--blocks)dvd_pending=1;else HSD_Synth_804D7778=0;restore_interrupts();}}
#include "stream_wait.inc"
int main(void){
    for(unsigned initial=0;initial<2;++initial)for(unsigned count=1;count<=1024;++count){
        polling=initial;HSD_Synth_804D7778=1;blocks=count;dvd_pending=1;ar_pending=0;dvd_calls=ar_calls=nested_mixer=0;
        stream_wait();assert(polling==(int)initial&&!HSD_Synth_804D7778&&!nested_mixer);
        assert(dvd_calls==count&&ar_calls==count&&!dvd_pending&&!ar_pending);
        stream_wait();assert(dvd_calls==count&&ar_calls==count);
    }
    puts("Stream wait: 2048 deferred DVD/ARAM chains completed; existing poll state preserved; no nested mixer; unlocked path does no I/O");
}
