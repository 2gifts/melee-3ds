#include <sysdolphin/baselib/debug.h>
#include <stdarg.h>
#include <stdio.h>
#include "native.h"
static ReportCallback report_callback;
static PanicCallback panic_callback;
void HSD_LogInit(void) {}
void HSD_SetReportCallback(ReportCallback cb){report_callback=cb;}
void HSD_SetPanicCallback(PanicCallback cb){panic_callback=cb;}
void OSReport(char *fmt,...){char text[1024];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);mp_platform_log(text);if(report_callback)report_callback((unsigned char*)text,strlen(text));}
/* Code addresses found on the stack after a panic: the caller chain, for
 * symbolizing with the ELF (ARM code keeps no frame pointers). */
static void report_stack(const void*caller){
    char text[400];int n=snprintf(text,sizeof(text),"PANIC caller=%p stack:",caller);
    u32*sp=(u32*)__builtin_frame_address(0);unsigned found=0;
    for(unsigned i=0;i<512&&found<16&&n<(int)sizeof(text)-12;++i){u32 v=sp[i];
        if(v>=0x00100000u&&v<0x00800000u&&(v&3)==0){n+=snprintf(text+n,sizeof(text)-n," %x",(unsigned)v);++found;}}
    snprintf(text+n,sizeof(text)-n,"\n");mp_platform_log(text);
}
static void panic_at(char*file,u32 line,char*message,const void*caller){
    OSReport("PANIC %s:%u: %s\n",file,line,message);report_stack(caller);(void)panic_callback;mp_platform_panic(message);
}
void HSD_Panic(char *file,u32 line,char *message){panic_at(file,line,message,__builtin_return_address(0));}
void __assert(char *file,u32 line,char *condition){panic_at(file,line,condition,__builtin_return_address(0));}
