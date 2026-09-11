#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "log_progress.h"

/* The game produces text on one OS thread (its BE8 threads are cooperative).
 * A sleeping native thread owns the SD file. It can preempt a stalled game
 * to preserve buffered logs; file IPC yields the CPU back to the game. */
#define LOG_CAPACITY 32768
static FILE* file;
static Thread worker;
static LightEvent wake;
static char queue[LOG_CAPACITY],stdio_buffer[8192];
static unsigned read_cursor,write_cursor,flush_request,flush_done,stopping;
static unsigned progress;
unsigned log_flushes,log_flush_ticks,log_dropped,log_errors;
extern unsigned mp_native_ticks(void);
unsigned mp_log_phase(unsigned phase){
    unsigned before=__atomic_load_n(&progress,__ATOMIC_RELAXED);
    __atomic_store_n(&progress,(before&~7u)|(phase&7),__ATOMIC_RELAXED);return before&7;
}
void mp_log_frame(unsigned frame){__atomic_store_n(&progress,(frame<<3)|MP_LOG_ENGINE,__ATOMIC_RELAXED);}

static void flush_file(void){
    unsigned start=mp_native_ticks();
    if(fflush(file))__atomic_fetch_add(&log_errors,1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&log_flush_ticks,mp_native_ticks()-start,__ATOMIC_RELAXED);
    __atomic_fetch_add(&log_flushes,1,__ATOMIC_RELAXED);
}
static void log_worker(void* unused){
    (void)unused;
    u64 last_progress=svcGetSystemTick(),last_flush=last_progress;
    unsigned observed=0;int stalled=0,dirty=0;
    for(;;){
        LightEvent_WaitTimeout(&wake,500000000);
        unsigned request=__atomic_load_n(&flush_request,__ATOMIC_ACQUIRE);
        /* Acquire the request before draining: every byte published before
         * that request is included before we acknowledge its flush. */
        for(;;){
            unsigned end=__atomic_load_n(&write_cursor,__ATOMIC_ACQUIRE);
            unsigned cursor=__atomic_load_n(&read_cursor,__ATOMIC_RELAXED);
            unsigned count=end-cursor;if(!count)break;
            unsigned offset=cursor%LOG_CAPACITY;
            if(count>LOG_CAPACITY-offset)count=LOG_CAPACITY-offset;
            if(fwrite(queue+offset,1,count,file)!=count)__atomic_fetch_add(&log_errors,1,__ATOMIC_RELAXED);
            dirty=1;
            __atomic_store_n(&read_cursor,cursor+count,__ATOMIC_RELEASE);
        }
        u64 now=svcGetSystemTick();unsigned current=__atomic_load_n(&progress,__ATOMIC_RELAXED);
        if(current!=observed){observed=current;last_progress=now;stalled=0;}
        else if((current>>3)&&!stalled&&now-last_progress>10ULL*SYSCLOCK_ARM11){
            fprintf(file,"No frame progress for 10 seconds: frame=%u phase=%u (1 engine, 2 GPU wait, 3 GPU submit, 4 disc read)\n",current>>3,current&7);
            extern void mp_renderer_stall_report(FILE*);mp_renderer_stall_report(file);
            stalled=1;dirty=1;last_flush=0;
        }
        if(request!=__atomic_load_n(&flush_done,__ATOMIC_RELAXED)||(dirty&&now-last_flush>=2ULL*SYSCLOCK_ARM11)){
            flush_file();dirty=0;last_flush=now;__atomic_store_n(&flush_done,request,__ATOMIC_RELEASE);
        }
        if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))break;
    }
}
void mp_log_init(void){
    file=fopen("sdmc:/3ds/melee/game.log","w");if(!file)return;
    setvbuf(file,stdio_buffer,_IOFBF,sizeof(stdio_buffer));
    mp_native_ticks(); /* Initialize the shared clock origin before spawning. */
    LightEvent_Init(&wake,RESET_ONESHOT);
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    worker=threadCreate(log_worker,NULL,16384,priority>0x18?priority-1:priority,-2,false);
}
void mp_log_append(const char* text){
    if(!file)return;
    if(!worker){fputs(text,file);return;}
    unsigned size=strlen(text),cursor=__atomic_load_n(&write_cursor,__ATOMIC_RELAXED);
    unsigned used=cursor-__atomic_load_n(&read_cursor,__ATOMIC_ACQUIRE);
    if(size>LOG_CAPACITY-used){++log_dropped;return;}
    unsigned offset=cursor%LOG_CAPACITY,first=LOG_CAPACITY-offset;if(first>size)first=size;
    memcpy(queue+offset,text,first);memcpy(queue,text+first,size-first);
    __atomic_store_n(&write_cursor,cursor+size,__ATOMIC_RELEASE);LightEvent_Signal(&wake);
}
void mp_log_flush(int wait){
    if(!file)return;
    if(!worker){flush_file();return;}
    unsigned request=__atomic_add_fetch(&flush_request,1,__ATOMIC_RELEASE);
    LightEvent_Signal(&wake);
    if(wait)while(__atomic_load_n(&flush_done,__ATOMIC_ACQUIRE)!=request)svcSleepThread(1000000);
}
void mp_log_close(void){
    if(!file)return;
    mp_log_flush(1);
    if(worker){__atomic_store_n(&stopping,1,__ATOMIC_RELEASE);LightEvent_Signal(&wake);threadJoin(worker,U64_MAX);threadFree(worker);worker=NULL;}
    fclose(file);file=NULL;
}
