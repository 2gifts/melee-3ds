#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <melee/cm/camera.h>

/* The original camera remains unchanged: zoom/tracking and game rules keep
 * their 4:3 behavior. Only the world projection and visibility frustum expand.
 * Full-screen menu model cameras expand too. Flat overlays and small
 * offscreen cameras keep their original scale and centered placement.
 * Adapted from the camera-aspect approach in Dolphin's GALE01r2.ini proper
 * widescreen code (Dan Salvato, mirrorbender), using 5:3 instead of 16:9. */
unsigned mp_display_expanded;
unsigned mp_display_stereo;
void mp_display_results_capture(HSD_GObj*gobj,int pass){
    extern unsigned mp_gx_left_capture(unsigned);
    /* These two original offscreen cameras are copied from the left EFB,
     * then erased in BOTH eyes. Visible results cameras remain stereo. */
    unsigned previous=mp_gx_left_capture(1);
    Camera_800313E0(gobj,pass);
    mp_gx_left_capture(previous);
}
static HSD_CObj* world_camera;
/* Layout cameras whose models are 2D artwork staged at arbitrary depths
 * (titles, logos, captions). Their true relief contradicts the drawing
 * order, so they render at the screen plane. Cleared at scene changes. */
static HSD_CObj* flat_cameras[4];
void mp_display_flat_camera(HSD_CObj* cobj){
    for(unsigned i=0;i<4;++i)if(!flat_cameras[i]||flat_cameras[i]==cobj){flat_cameras[i]=cobj;return;}
}
/* Stage map parts with their own camera (ground.c; Rainbow Cruise's sky and
 * sea, among others) are the far part of the world view. They widen with the
 * world camera and sit at the far limit of the stereo range. */
static HSD_CObj* background_cameras[4];
void mp_display_background_camera(HSD_CObj* cobj){
    for(unsigned i=0;i<4;++i)if(!background_cameras[i]||background_cameras[i]==cobj){background_cameras[i]=cobj;return;}
}
static int background_camera(const HSD_CObj* cobj){
    for(unsigned i=0;i<4;++i)if(background_cameras[i]==cobj)return 1;
    return 0;
}
/* Full-screen overlay cameras (hit flashes, fades and wipes: lbbgflash.c)
 * draw 640x480 GX quads that must cover the whole display, including the
 * widescreen margins, at the screen plane. */
static HSD_CObj* overlay_cameras[4];
HSD_CObj* mp_display_overlay_camera(HSD_CObj* cobj){
    for(unsigned i=0;i<4;++i)if(!overlay_cameras[i]||overlay_cameras[i]==cobj){overlay_cameras[i]=cobj;break;}
    return cobj;
}
static int overlay_camera(const HSD_CObj* cobj){
    for(unsigned i=0;i<4;++i)if(overlay_cameras[i]==cobj)return 1;
    return 0;
}
/* A camera whose SIS text is written on a panel nearer than the text itself
 * (the notice window: panel at z = 35, text at z = 0, eye at z = 64). */
static HSD_CObj* text_camera;
static float text_panel_z;
static void text_camera_reset(void){text_camera=NULL;}
void mp_display_set_world(HSD_CObj* cobj){
    if(!cobj){extern void mp_gx_invalidate_sources(void);mp_gx_invalidate_sources();
        for(unsigned i=0;i<4;++i)flat_cameras[i]=background_cameras[i]=overlay_cameras[i]=NULL;
        text_camera_reset();}
    world_camera=cobj;
}
void mp_display_text_plane(HSD_CObj* cobj,float panel_z){text_camera=cobj;text_panel_z=panel_z;}
void mp_display_set_mode(unsigned expanded){mp_display_expanded=!!expanded;}
void mp_display_set_stereo(unsigned depth){
    extern void mp_gx_set_stereo(unsigned);
    mp_display_stereo=depth>1000?1000:depth;mp_gx_set_stereo(mp_display_stereo);
}
unsigned mp_display_stereo_scene(void){return 1;}
float mp_display_camera_convergence(HSD_CObj*cobj){
    if(!mp_display_stereo||!cobj||cobj->projection_type!=PROJ_PERSPECTIVE||overlay_camera(cobj))return 0;
    if(world_camera)return cobj==world_camera?HSD_CObjGetEyeDistance(cobj):background_camera(cobj)?1e-3f:0;
    for(unsigned i=0;i<4;++i)if(flat_cameras[i]==cobj)return 0;
    /* Original menu model cameras have real perspective depth. SIS text,
     * orthographic overlays and small offscreen targets remain flat. */
    return cobj->viewport.xmax-cobj->viewport.xmin>=600 &&
           cobj->viewport.ymax-cobj->viewport.ymin>=400?HSD_CObjGetEyeDistance(cobj):0;
}
void mp_display_ribbon_callback(HSD_GObj*gobj,int pass){
    extern void mp_gx_camera_convergence(float);
    float distance=mp_display_camera_convergence(HSD_CObjGetCurrent());
    /* CSS uses immediate opaque/edge/translucent GObj passes. Increase only
     * this ribbon's convergence to place it in front of the LCD, without
     * changing its scale, screen position, animation, or cursor hit areas. */
    mp_gx_camera_convergence(distance*1.5f);
    HSD_GObj_JObjCallback(gobj,pass);
    mp_gx_camera_convergence(distance);
}
/* Text on the convergence plane (depth D) has no parallax; the panel at
 * depth D - panel_z has 1 - D/(D - panel_z). Convergence D*D/(D - panel_z)
 * gives the text that parallax (and the same pop-out limit). */
void mp_display_text_begin(void){
    extern void mp_gx_camera_convergence(float);
    HSD_CObj*cobj=HSD_CObjGetCurrent();
    if(!text_camera||cobj!=text_camera)return;
    float distance=mp_display_camera_convergence(cobj),panel=distance-text_panel_z;
    if(distance>0&&panel>1)mp_gx_camera_convergence(distance*distance/panel);
}
void mp_display_text_end(void){
    extern void mp_gx_camera_convergence(float);
    HSD_CObj*cobj=HSD_CObjGetCurrent();
    if(text_camera&&cobj==text_camera)mp_gx_camera_convergence(mp_display_camera_convergence(cobj));
}
/* A model drawn at the screen plane inside a camera that otherwise keeps its
 * depth (the Classic intro's road map shares the fighters' camera). */
void mp_display_flat_callback(HSD_GObj*gobj,int pass){
    extern void mp_gx_camera_convergence(float);
    float distance=mp_display_camera_convergence(HSD_CObjGetCurrent());
    mp_gx_camera_convergence(0);
    HSD_GObj_JObjCallback(gobj,pass);
    mp_gx_camera_convergence(distance);
}
/* The world and stage-background cameras sample textures about 2x minified
 * on the 240-line screen; their textures get generated mip chains. */
unsigned mp_display_camera_mips(HSD_CObj* cobj){
    return cobj&&world_camera&&(cobj==world_camera||background_camera(cobj));
}
unsigned mp_display_camera_width(HSD_CObj* cobj){
    if(mp_display_expanded&&cobj&&overlay_camera(cobj))return 400;
    if(!mp_display_expanded||!cobj||cobj->projection_type!=PROJ_PERSPECTIVE)return 320;
    if(world_camera)return cobj==world_camera||background_camera(cobj)?400:320;
    return cobj->viewport.xmax-cobj->viewport.xmin>=600 &&
           cobj->viewport.ymax-cobj->viewport.ymin>=400?400:320;
}
float mp_display_camera_aspect(HSD_CObj* cobj){
    /* The union's first field is the orthographic bottom for other types. */
    if(cobj->projection_type!=PROJ_PERSPECTIVE)return cobj->projection_param.perspective.aspect;
    /* Overlay quads stretch across the widened viewport; a widened
     * projection would keep them at their 4:3 size in the middle. */
    if(overlay_camera(cobj))return cobj->projection_param.perspective.aspect;
    unsigned width=mp_display_camera_width(cobj);float margin=1.f;
    /* Distant scenery shifts by up to MP_STEREO_PIXELS_PER_SLIDER/2 pixels
     * per slider step in each eye, exposing that strip at the viewport edge.
     * Widen the world view by the shift (plus half a pixel) so both eyes
     * stay covered; 3% at the full slider. */
    if(mp_display_stereo&&world_camera&&(cobj==world_camera||background_camera(cobj)))
        margin=1.f+(mp_display_stereo*.012f+1.f)/width;
    return cobj->projection_param.perspective.aspect*(width==400?1.25f:1.f)*margin;
}
