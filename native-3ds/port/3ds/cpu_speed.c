#include <3ds.h>
#include <stdio.h>
#include <string.h>

extern void mp_native_log(const char*);
static aptHookCookie cpu_hook;
static unsigned pending,enabled;
char mp_cpu_status[40]="CPU status unavailable";

static int cpu_state(s64*clock,s64*maximum,s64*cache){
    *clock=*maximum=*cache=-1;
    Result a=svcGetSystemInfo(clock,0x10001,0);
    Result b=svcGetSystemInfo(maximum,0x10001,1);
    Result c=svcGetSystemInfo(cache,0x10001,2);
    return R_SUCCEEDED(a)&&R_SUCCEEDED(b)&&R_SUCCEEDED(c)&&
        (*clock==268||*clock==536||*clock==804)&&
        (*maximum==536||*maximum==804)&&(*cache==0||*cache==1);
}
static void configure(void){
    /* Retain libctru's requested setting for its own suspend/restore path. */
    osSetSpeedupEnable(true);
    s64 clock,maximum,cache;int known=cpu_state(&clock,&maximum,&cache);
    if(known&&(clock!=maximum||!cache)){
        /* Same documented bitmask used by Rosalina's New 3DS menu. Only
         * attempt it after valid Luma CPU queries demonstrate a slow state. */
        Result result=svcKernelSetState(10,3);
        char text[100];snprintf(text,sizeof(text),"New 3DS CPU request required kernel retry: result=%08lx\n",(unsigned long)result);mp_native_log(text);
        known=cpu_state(&clock,&maximum,&cache);
    }
    if(known)snprintf(mp_cpu_status,sizeof(mp_cpu_status),"CPU: %ld MHz, L2 %s",(long)clock,cache?"on":"off");
    else strcpy(mp_cpu_status,"CPU: fast mode requested (unverified)");
    char text[112];snprintf(text,sizeof(text),"%s%s\n",mp_cpu_status,known?" (read back from Luma)":"; status query unavailable");mp_native_log(text);
}
static void on_apt(APT_HookType hook,void*unused){
    (void)unused;
    if(hook==APTHOOK_ONRESTORE||hook==APTHOOK_ONWAKEUP)__atomic_store_n(&pending,1,__ATOMIC_RELEASE);
}
void mp_native_cpu_init(int is_new){
    enabled=is_new!=0;
    if(!enabled){strcpy(mp_cpu_status,"CPU: original 3DS");return;}
    configure();aptHook(&cpu_hook,on_apt,NULL);
}
void mp_native_cpu_poll(void){if(enabled&&__atomic_exchange_n(&pending,0,__ATOMIC_ACQUIRE))configure();}
void mp_native_cpu_exit(void){if(enabled)aptUnhook(&cpu_hook);enabled=0;}
