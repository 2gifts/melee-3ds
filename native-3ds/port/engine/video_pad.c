#include <dolphin/vi.h>
#include <dolphin/pad.h>
#include "native.h"
static VIRetraceCallback pre,post;static u32 retraces,pad_spec=5;static GXRenderModeObj mode;static void*framebuffer;static BOOL black;
extern void mp_engine_poll(void);
static u32 last_retrace;static int vi_initialized;
void VIInit(void){last_retrace=mp_platform_ticks();vi_initialized=1;}
/* Retraces follow the top screen's VBlank (59.83 Hz; the GameCube's NTSC VI
 * ran at 59.94 Hz), so game steps and displayed frames share one clock and a
 * capped frame rate stays evenly paced. A free-running 60.00 Hz timer drifted
 * against the LCD by one frame every ~6 s. Before the renderer's VBlank count
 * starts, or if it stalls for 6 periods, the timer paces retraces instead. */
static u32 last_vblank;static int vblank_paced;
void mp_vi_poll_at(u32 now){
    if(!vi_initialized)return;
    u32 vblank=mp_platform_vblank_count();unsigned n=0;
    if(vblank!=last_vblank){
        n=vblank-last_vblank;
        if(last_vblank&&!vblank_paced){vblank_paced=1;n=1;}
        last_vblank=vblank;
        if(!vblank_paced)n=0;
    }
    if(vblank_paced){
        if(n)last_retrace=now;
        else if(now-last_retrace>=6*675000)vblank_paced=0;
    }
    if(!vblank_paced){
        if(now-last_retrace<675000)return;
        last_retrace+=((now-last_retrace)/675000)*675000;n=1;
    }
    /* A poll late by more than one VBlank still steps the game once per
     * VBlank, up to two, as the original pad queue would have. */
    if(n>2)n=2;
    while(n--){++retraces;if(pre)pre(retraces);if(post)post(retraces);}
}
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb){VIRetraceCallback old=pre;pre=cb;return old;}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb){VIRetraceCallback old=post;post=cb;return old;}
void VIWaitForRetrace(void){
    extern void mp_platform_idle(void);extern u32 mp_gx_fifo_state[];
    u32 before=retraces,site=mp_gx_fifo_state[9];if(!site)mp_gx_fifo_state[9]=4; /* stall report: retrace wait */
    while(retraces==before){mp_engine_poll();mp_platform_idle();}
    mp_gx_fifo_state[9]=site;
}
u32 VIGetRetraceCount(void){return retraces;}u32 VIGetTvFormat(void){return 0;}u32 VIGetDTVStatus(void){return 0;}
void VIConfigure(GXRenderModeObj*m){mode=*m;}void VISetBlack(BOOL b){black=b;}void VIFlush(void){}void VISetNextFrameBuffer(void*p){framebuffer=p;}
BOOL PADInit(void){return 1;}u32 PADRead(PADStatus*p){mp_platform_pad(p);return PAD_CHAN0_BIT;}
void PADSetSpec(u32 s){pad_spec=s;}unsigned long PADGetSpec(void){return pad_spec;}
void PADSetSamplingRate(unsigned long n){(void)n;}int PADReset(unsigned long mask){(void)mask;return 1;}BOOL PADRecalibrate(u32 mask){(void)mask;return 1;}
void PADControlMotor(s32 chan,u32 command){(void)chan;(void)command;}
