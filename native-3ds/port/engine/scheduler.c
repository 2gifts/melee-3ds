#include <dolphin/os.h>
#include <dolphin/os/OSThread.h>
#include <dolphin/os/OSMessage.h>
#include <dolphin/os/OSAlarm.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include "native.h"
#include "timing.h"
typedef struct{u32 core[10];u64 vfp[8];}ArmContext;
typedef struct{OSThread*thread;void*(*entry)(void*);void*arg;ArmContext saved;}Task;
static Task tasks[16];static OSThread main_thread;static Task*running;
static int scheduler_disabled;
static int polling;
void mp_engine_io_poll(void){
    extern void mp_ar_pump(void);
    /* Completion callbacks restore interrupt state. Suppress nested VI/AX
     * polling while finishing I/O so a music wait cannot reenter its mixer. */
    int previous=polling;polling=1;
    mp_dvd_pump();mp_ar_pump();polling=previous;
}
extern void mp_be_context_swap(ArmContext*,ArmContext*),mp_be_thread_trampoline(void);
static void init(void){if(!running){main_thread.state=OS_THREAD_STATE_RUNNING;main_thread.priority=16;tasks[0].thread=&main_thread;running=&tasks[0];}}
static Task*find(OSThread*t){init();for(int i=0;i<16;++i)if(tasks[i].thread==t)return &tasks[i];return NULL;}
static void schedule(void){init();if(scheduler_disabled)return;Task*next=NULL;for(int i=0;i<16;++i){OSThread*t=tasks[i].thread;if(t&&!t->suspend&&(t->state==1||t->state==2)&&(!next||t->priority<next->thread->priority))next=&tasks[i];}
    while(!next){
        mp_dvd_pump();for(int i=0;i<16;++i)if(tasks[i].thread&&!tasks[i].thread->suspend&&tasks[i].thread->state==1){next=&tasks[i];break;}
        if(next||!mp_dvd_pending())break;
        /* Every engine task may sleep waiting for an asynchronous disc
         * completion. Yield to the native I/O worker until it can wake one. */
        extern void mp_platform_idle(void);mp_platform_idle();
    }
    if(!next)HSD_Panic(__FILE__,__LINE__,"No runnable engine thread");if(next!=running){Task*old=running;if(old->thread->state==2)old->thread->state=1;running=next;next->thread->state=2;mp_be_context_swap(&old->saved,&next->saved);}}
void mp_engine_thread_start(Task*t){t->thread->val=t->entry(t->arg);t->thread->state=8;schedule();HSD_Panic(__FILE__,__LINE__,"Exited engine thread resumed");}
int OSCreateThread(OSThread*t,void*(*entry)(void*),void*arg,void*stack,unsigned long size,long priority,unsigned short attr){init();for(int i=1;i<16;++i)if(!tasks[i].thread||tasks[i].thread->state==8){Task*x=&tasks[i];memset(t,0,sizeof(*t));memset(x,0,sizeof(*x));x->thread=t;x->entry=entry;x->arg=arg;t->state=1;t->suspend=1;t->priority=t->base=priority;t->attr=attr;t->stackBase=stack;t->stackEnd=(u32*)((u8*)stack-size);*t->stackEnd=OS_THREAD_STACK_MAGIC;x->saved.core[0]=(u32)x;x->saved.core[8]=(u32)stack&~7u;x->saved.core[9]=(u32)mp_be_thread_trampoline;return 1;}return 0;}
OSThread*OSGetCurrentThread(void){init();return running->thread;}
s32 OSResumeThread(OSThread*t){Task*x=find(t);HSD_ASSERT(1,x);int old=t->suspend;if(t->suspend>0)--t->suspend;schedule();return old;}
s32 OSSuspendThread(OSThread*t){int old=t->suspend++;schedule();return old;}
void OSCancelThread(OSThread*t){t->state=8;if(t==OSGetCurrentThread())schedule();}
s32 OSDisableScheduler(void){return scheduler_disabled++;}s32 OSEnableScheduler(void){int old=scheduler_disabled;if(scheduler_disabled)--scheduler_disabled;return old;}
void OSInitThreadQueue(OSThreadQueue*q){q->head=q->tail=NULL;}
void OSSleepThread(OSThreadQueue*q){OSThread*t=OSGetCurrentThread();t->state=4;t->queue=q;t->link.next=NULL;t->link.prev=q->tail;if(q->tail)q->tail->link.next=t;else q->head=t;q->tail=t;schedule();}
void OSWakeupThread(OSThreadQueue*q){OSThread*t=q->head;q->head=q->tail=NULL;while(t){OSThread*next=t->link.next;t->state=1;t->queue=NULL;t=next;}}
long OSCheckActiveThreads(void){init();long n=0;for(int i=0;i<16;++i)if(tasks[i].thread&&tasks[i].thread->state!=8)++n;return n;}
void OSInitMessageQueue(struct OSMessageQueue*q,void*array,long count){memset(q,0,sizeof(*q));q->msgArray=array;q->msgCount=count;}
int OSSendMessage(struct OSMessageQueue*q,void*msg,long flags){while(q->usedCount==q->msgCount){if(!(flags&1))return 0;OSSleepThread(&q->queueSend);}((void**)q->msgArray)[(q->firstIndex+q->usedCount)%q->msgCount]=msg;++q->usedCount;OSWakeupThread(&q->queueReceive);return 1;}
int OSReceiveMessage(struct OSMessageQueue*q,void*msg,long flags){while(!q->usedCount){mp_dvd_pump();if(q->usedCount)break;if(!(flags&1))return 0;OSSleepThread(&q->queueReceive);}if(msg)*(void**)msg=((void**)q->msgArray)[q->firstIndex];q->firstIndex=(q->firstIndex+1)%q->msgCount;--q->usedCount;OSWakeupThread(&q->queueSend);return 1;}
static OSAlarm*alarms[64];static unsigned alarm_count;
void OSInitAlarm(void){}void OSCreateAlarm(OSAlarm*a){memset(a,0,sizeof(*a));}
void OSCancelAlarm(OSAlarm*a){a->handler=NULL;for(unsigned i=0;i<alarm_count;++i)if(alarms[i]==a)alarms[i]=NULL;while(alarm_count&&!alarms[alarm_count-1])--alarm_count;}
static void alarm_insert(OSAlarm*a){for(unsigned i=0;i<alarm_count;++i)if(alarms[i]==a)return;for(unsigned i=0;i<64;++i)if(!alarms[i]){alarms[i]=a;if(alarm_count<i+1)alarm_count=i+1;return;}HSD_Panic(__FILE__,__LINE__,"Alarm capacity exceeded");}
void OSSetAlarm(OSAlarm*a,OSTime delay,OSAlarmHandler cb){a->handler=cb;a->fire=OSGetTime()+delay;a->period=0;alarm_insert(a);}
void OSSetPeriodicAlarm(OSAlarm*a,OSTime start,OSTime period,OSAlarmHandler cb){HSD_ASSERT(1,period>0);a->handler=cb;a->fire=mp_periodic_start(OSGetTime(),start,period);a->period=period;a->start=start;alarm_insert(a);}
void mp_engine_poll(void){extern int mp_interrupts_enabled(void);extern void mp_ar_pump(void),mp_vi_poll_at(u32),mp_audio_poll_at(u32);if(polling||!mp_interrupts_enabled())return;polling=1;OSTime now=OSGetTime();for(unsigned i=0;i<alarm_count;++i){OSAlarm*a=alarms[i];if(a&&a->handler&&a->fire<=now){OSAlarmHandler cb=a->handler;if(a->period>0)a->fire=mp_periodic_advance(now,a->fire,a->period);else alarms[i]=NULL;cb(a,OSGetCurrentContext());}}mp_dvd_pump();mp_ar_pump();mp_vi_poll_at((u32)now);mp_audio_poll_at((u32)now);polling=0;}
