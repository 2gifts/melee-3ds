#include <dolphin/vi.h>
#include <dolphin/pad.h>
#include "native.h"
static VIRetraceCallback pre,post;static u32 retraces,pad_spec=5;static GXRenderModeObj mode;static void*framebuffer;static BOOL black;
extern void mp_engine_poll(void);
static u32 last_retrace;static int vi_initialized;
void VIInit(void){last_retrace=mp_platform_ticks();vi_initialized=1;}
void mp_vi_poll_at(u32 now){if(!vi_initialized||(now-last_retrace)<675000)return;last_retrace+=((now-last_retrace)/675000)*675000;++retraces;if(pre)pre(retraces);if(post)post(retraces);}
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb){VIRetraceCallback old=pre;pre=cb;return old;}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb){VIRetraceCallback old=post;post=cb;return old;}
void VIWaitForRetrace(void){extern void mp_platform_idle(void);u32 before=retraces;while(retraces==before){mp_engine_poll();mp_platform_idle();}}
u32 VIGetRetraceCount(void){return retraces;}u32 VIGetTvFormat(void){return 0;}u32 VIGetDTVStatus(void){return 0;}
void VIConfigure(GXRenderModeObj*m){mode=*m;}void VISetBlack(BOOL b){black=b;}void VIFlush(void){}void VISetNextFrameBuffer(void*p){framebuffer=p;}
BOOL PADInit(void){return 1;}u32 PADRead(PADStatus*p){mp_platform_pad(p);return PAD_CHAN0_BIT;}
void PADSetSpec(u32 s){pad_spec=s;}unsigned long PADGetSpec(void){return pad_spec;}
void PADSetSamplingRate(unsigned long n){(void)n;}int PADReset(unsigned long mask){(void)mask;return 1;}BOOL PADRecalibrate(u32 mask){(void)mask;return 1;}
void PADControlMotor(s32 chan,u32 command){(void)chan;(void)command;}
