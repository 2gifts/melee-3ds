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
void HSD_Panic(char *file,u32 line,char *message){OSReport("PANIC %s:%u: %s\n",file,line,message);(void)panic_callback;mp_platform_panic(message);}
void __assert(char *file,u32 line,char *condition){HSD_Panic(file,line,condition);}
