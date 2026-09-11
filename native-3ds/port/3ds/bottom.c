#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include "bottom.h"

extern void mp_game_bottom_snapshot(MPBottomState*);
extern const unsigned char* mp_game_bottom_font(void);
extern void mp_native_toggle_display(void),mp_native_log(const char*);
static uint16_t pixels[320*240] __attribute__((aligned(32)));
const uint16_t* mp_native_bottom_pixels=pixels;
static MPBottomState previous,match;
static unsigned last_vblank=~0u,last_fps=~0u,last_view=~0u,show_fps,guide,dirty=1,ready;
unsigned mp_bottom_redraws,mp_bottom_draw_ticks;
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
    if(!mp_bottom_art_init(mp_game_bottom_font(),"sdmc:/3ds/melee/files/MnSlChr.usd","sdmc:/3ds/melee/files/IfAll.usd"))
        mp_native_log("Bottom screen: portrait archive unavailable; text fallback active\n");
    else mp_native_log("Bottom screen: 118 original portraits cached (1546272 bytes), no selection-time reads\n");
    /* The first image stays visible during the one-time archive decode. */
    mp_bottom_loading(pixels,"PREPARING MENUS");
    if(last_vblank!=C3D_FrameCounter(1))present();ready=1;
}
void mp_native_bottom_frame(unsigned fps,unsigned expanded,unsigned touch,unsigned x,unsigned y){
    if(!ready)return;
#ifdef MP_SMOKE_TEST
    if(mp_test_bottom_disabled)return;
    if(mp_test_bottom_touch){x=mp_test_bottom_touch&65535;y=mp_test_bottom_touch>>16;touch=1;mp_test_bottom_touch=0;}
#endif
    if(touch){switch(mp_bottom_hit(x,y)){
        case MP_BOTTOM_FPS:show_fps^=1;dirty=1;break;
        case MP_BOTTOM_VIEW:mp_native_toggle_display();break;
        case MP_BOTTOM_GUIDE:guide^=1;dirty=1;break;
    }}
    MPBottomState state;mp_game_bottom_snapshot(&state);
    /* The bridge changes ARM endianness, not the bytes of pointed-to data. */
    unsigned* words=(unsigned*)&state;for(unsigned i=0;i<sizeof(state)/4;++i)words[i]=__builtin_bswap32(words[i]);
    if(state.scene==2||state.scene==3||state.scene==4||state.scene==44)match=state;
    else if(state.scene==5){unsigned scene=state.scene;state=match;state.scene=scene;}
    /* Close the guide at scene changes so it never masks a new match. */
    if(state.scene!=previous.scene)guide=0;
#ifdef MP_SMOKE_TEST
    mp_bottom_observed=state;mp_bottom_fps_visible=show_fps;mp_bottom_guide_visible=guide;
#endif
    unsigned shown=show_fps?fps:0;
    if(shown!=last_fps||expanded!=last_view||
        (guide?state.scene!=previous.scene:memcmp(&state,&previous,sizeof(state))))dirty=1;
    /* C3D does not own a bottom render target. Present at most once per
     * bottom VBlank, without waiting or changing top-screen GPU commands. */
    if(!dirty||last_vblank==C3D_FrameCounter(1))return;
    u64 tick=svcGetSystemTick();
    mp_bottom_draw(pixels,&state,fps,show_fps,expanded,guide);present();
    mp_bottom_draw_ticks+=(unsigned)((svcGetSystemTick()-tick)*40500000/SYSCLOCK_ARM11);
    ++mp_bottom_redraws;previous=state;last_fps=shown;last_view=expanded;dirty=0;
}
void mp_native_bottom_exit(void){mp_bottom_art_exit();ready=0;}
