#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdio.h>
#include "bottom.h"

extern void mp_game_bottom_snapshot(MPBottomState*);
extern const unsigned char* mp_game_bottom_font(void);
extern void mp_native_toggle_display(void),mp_native_cycle_rate(void),mp_native_log(const char*);
static uint16_t pixels[320*240] __attribute__((aligned(32)));
const uint16_t* mp_native_bottom_pixels=pixels;
static MPBottomState previous,match;
/* Camera Mode wants the camera controller in port 4. While the game is in
 * Camera Mode, port 4 is present and the fourth bottom button switches the
 * 3DS controls between the fighter (port 1) and the camera (services.c). */
volatile unsigned mp_native_camera_mode,mp_native_camera_control,mp_native_camera_match;
static unsigned last_vblank=~0u,last_fps=~0u,last_view=~0u,last_rate=~0u,show_fps,guide,dirty=1,ready;
unsigned mp_bottom_redraws,mp_bottom_draw_ticks;
unsigned mp_native_context_serial;
#ifdef MP_SMOKE_TEST
volatile unsigned mp_test_bottom_touch; /* packed x | y<<16, consumed once */
volatile unsigned mp_test_bottom_disabled;
MPBottomState mp_bottom_observed;
unsigned mp_bottom_fps_visible,mp_bottom_guide_visible;
#endif
static void present(void){
    uint16_t* fb=(uint16_t*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    memcpy(fb,pixels,sizeof(pixels));GSPGPU_FlushDataCache(fb,sizeof(pixels));
    gfxScreenSwapBuffers(GFX_BOTTOM,false);last_vblank=C3D_FrameCounter(1);
}
void mp_native_bottom_init(void){
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
    mp_bottom_font_init(mp_game_bottom_font());
    mp_bottom_loading(pixels,"PREPARING FIGHTERS");present();
}
/* After the disc index (file_io.c), which knows the files' SD folders. */
void mp_native_bottom_art(void){
    extern int mp_native_file_path(const char*,char*,size_t);
    char css[256],hud[256];
    if(!mp_native_file_path("MnSlChr.usd",css,sizeof(css))||!mp_native_file_path("IfAll.usd",hud,sizeof(hud))||
       !mp_bottom_art_init(mp_game_bottom_font(),css,hud))
        mp_native_log("Bottom screen: portrait archive unavailable; text fallback active\n");
    else mp_native_log("Bottom screen: 118 original portraits cached (1546272 bytes), no selection-time reads\n");
    /* The first image stays visible during the one-time archive decode. */
    mp_bottom_loading(pixels,"PREPARING MENUS");
    if(last_vblank!=C3D_FrameCounter(1))present();ready=1;
}
void mp_native_bottom_notice(const char* title,const char* lead,const char* line1,const char* line2,const char* line3,const char* hint){
    mp_bottom_notice(pixels,title,lead,line1,line2,line3,hint);present();
}
void mp_native_bottom_frame(unsigned fps,unsigned expanded,unsigned rate,unsigned touch,unsigned x,unsigned y){
    if(!ready)return;
#ifdef MP_SMOKE_TEST
    if(mp_test_bottom_disabled)return;
    if(mp_test_bottom_touch){x=mp_test_bottom_touch&65535;y=mp_test_bottom_touch>>16;touch=1;mp_test_bottom_touch=0;}
#endif
    /* The open CONTROLS page: tabs, button customization, tap jump. */
    if(touch&&guide&&mp_bottom_guide_touch(x,y)){dirty=1;touch=0;}
    if(touch){switch(mp_bottom_hit(x,y)){
        case MP_BOTTOM_FPS:show_fps^=1;dirty=1;break;
        case MP_BOTTOM_VIEW:mp_native_toggle_display();break;
        case MP_BOTTOM_RATE:mp_native_cycle_rate();break;
        case MP_BOTTOM_GUIDE:if(mp_native_camera_mode)mp_native_camera_control^=1;else{guide^=1;mp_bottom_guide_reset();}dirty=1;break;
    }}
    MPBottomState state;mp_game_bottom_snapshot(&state);
    /* The bridge changes ARM endianness, not the bytes of pointed-to data. */
    unsigned* words=(unsigned*)&state;for(unsigned i=0;i<sizeof(state)/4;++i)words[i]=__builtin_bswap32(words[i]);
    /* Reuse the HUD snapshot instead of traversing fighter objects again.
     * Record scene/roster changes so automatic performance logs can identify
     * the actual workload without asking the player to transcribe it. */
    static unsigned context[12],context_ready;
    unsigned current[12]={state.scene,state.mode,state.stage,state.items};
    for(unsigned i=0;i<4;++i){current[4+i*2]=state.players[i].kind;current[5+i*2]=state.players[i].character;}
    if(!context_ready||memcmp(context,current,sizeof(context))){
        char text[224];
        snprintf(text,sizeof(text),"Render context: scene=%u mode=%u stage=%u items=%u players=%u:%u,%u:%u,%u:%u,%u:%u\n",
            current[0],current[1],current[2],current[3],current[4],current[5],current[6],current[7],current[8],current[9],current[10],current[11]);
        mp_native_log(text);memcpy(context,current,sizeof(context));context_ready=1;++mp_native_context_serial;
    }
    {unsigned camera=state.mode==10;if(camera!=mp_native_camera_mode){mp_native_camera_mode=camera;mp_native_camera_control=0;dirty=1;}
        /* The camera takes the controls only during the photo match. */
        mp_native_camera_match=camera&&state.scene==2;}
    if(state.scene==2||state.scene==3||state.scene==4||state.scene==44)match=state;
    else if(state.scene==5){unsigned scene=state.scene;state=match;state.scene=scene;}
    /* Close the guide at scene changes so it never masks a new match. */
    if(state.scene!=previous.scene&&guide){guide=0;mp_bottom_guide_reset();}
#ifdef MP_SMOKE_TEST
    mp_bottom_observed=state;mp_bottom_fps_visible=show_fps;mp_bottom_guide_visible=guide;
#endif
    unsigned shown=show_fps?fps:0;
    if(shown!=last_fps||expanded!=last_view||rate!=last_rate||
        (guide?state.scene!=previous.scene:memcmp(&state,&previous,sizeof(state))))dirty=1;
    /* C3D does not own a bottom render target. Present at most once per
     * bottom VBlank, without waiting or changing top-screen GPU commands. */
    if(!dirty||last_vblank==C3D_FrameCounter(1))return;
    u64 tick=svcGetSystemTick();
    mp_bottom_draw(pixels,&state,fps,show_fps,expanded,rate,guide);present();
    mp_bottom_draw_ticks+=(unsigned)((svcGetSystemTick()-tick)*40500000/SYSCLOCK_ARM11);
    ++mp_bottom_redraws;previous=state;last_fps=shown;last_view=expanded;last_rate=rate;dirty=0;
}
void mp_native_bottom_exit(void){mp_bottom_art_exit();ready=0;}
