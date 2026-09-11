#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../port/3ds/cpu_speed.c"
static int mode,requests,retries,hooks,unhooks;
static s64 test_clock,test_cache;
static char last_log[200];
void mp_native_log(const char*s){snprintf(last_log,sizeof(last_log),"%s",s);}
void osSetSpeedupEnable(bool value){assert(value);++requests;if(mode==0){test_clock=804;test_cache=1;}}
Result svcGetSystemInfo(s64*out,int type,int param){assert(type==0x10001);if(mode==2)return -1;*out=param==0?test_clock:param==1?804:test_cache;return 0;}
Result svcKernelSetState(int type,...){assert(type==10);va_list args;va_start(args,type);assert(va_arg(args,int)==3);va_end(args);++retries;if(mode==3)return -1;test_clock=804;test_cache=1;return 0;}
void aptHook(aptHookCookie*c,aptHookFn f,void*p){++hooks;c->callback=f;c->param=p;}
void aptUnhook(aptHookCookie*c){(void)c;++unhooks;}
int main(void){
    mp_native_cpu_init(0);assert(!requests&&!retries&&!hooks);mp_native_cpu_exit();assert(!unhooks);
    mp_native_cpu_init(1);assert(requests==1&&!retries&&hooks==1);assert(!strcmp(mp_cpu_status,"CPU: 804 MHz, L2 on"));
    mp_native_cpu_poll();assert(requests==1);
    cpu_hook.callback(APTHOOK_ONSLEEP,NULL);mp_native_cpu_poll();assert(requests==1);
    cpu_hook.callback(APTHOOK_ONWAKEUP,NULL);mp_native_cpu_poll();assert(requests==2);mp_native_cpu_exit();assert(unhooks==1);
    mode=1;test_clock=268;test_cache=0;mp_native_cpu_init(1);assert(retries==1&&test_clock==804&&test_cache==1);mp_native_cpu_exit();
    mode=2;mp_native_cpu_init(1);assert(retries==1&&strstr(mp_cpu_status,"unverified"));mp_native_cpu_exit();
    mode=3;test_clock=268;test_cache=0;mp_native_cpu_init(1);assert(retries==2);assert(!strcmp(mp_cpu_status,"CPU: 268 MHz, L2 off"));
    mp_native_cpu_poll();assert(retries==2);mp_native_cpu_exit();
    mode=1;test_clock=123;test_cache=9;mp_native_cpu_init(1);assert(retries==2&&strstr(mp_cpu_status,"unverified"));mp_native_cpu_exit();
    puts("CPU setup: old-model bypass, fast-state readback, bounded retry, unknown/error handling and restore hook passed");
}
