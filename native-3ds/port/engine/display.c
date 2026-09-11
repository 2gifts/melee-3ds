#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>

/* The original camera remains unchanged: zoom/tracking and game rules keep
 * their 4:3 behavior. Only the world projection and visibility frustum expand.
 * Full-screen menu model cameras expand too. Flat overlays and small
 * offscreen cameras keep their original scale and centered placement.
 * Adapted from the camera-aspect approach in Dolphin's GALE01r2.ini proper
 * widescreen code (Dan Salvato, mirrorbender), using 5:3 instead of 16:9. */
unsigned mp_display_expanded;
unsigned mp_display_stereo;
static HSD_CObj* world_camera;
void mp_display_set_world(HSD_CObj* cobj){
    if(!cobj){extern void mp_gx_invalidate_sources(void);mp_gx_invalidate_sources();}
    world_camera=cobj;
}
void mp_display_set_mode(unsigned expanded){mp_display_expanded=!!expanded;}
void mp_display_set_stereo(unsigned depth){mp_display_stereo=depth>1000?1000:depth;}
unsigned mp_display_stereo_scene(void){return 1;}
float mp_display_camera_convergence(HSD_CObj*cobj){
    if(!mp_display_stereo||!cobj||cobj->projection_type!=PROJ_PERSPECTIVE)return 0;
    if(world_camera)return cobj==world_camera?HSD_CObjGetEyeDistance(cobj):0;
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
unsigned mp_display_camera_width(HSD_CObj* cobj){
    if(!mp_display_expanded||!cobj||cobj->projection_type!=PROJ_PERSPECTIVE)return 320;
    if(world_camera)return cobj==world_camera?400:320;
    return cobj->viewport.xmax-cobj->viewport.xmin>=600 &&
           cobj->viewport.ymax-cobj->viewport.ymin>=400?400:320;
}
float mp_display_camera_aspect(HSD_CObj* cobj){
    return cobj->projection_param.perspective.aspect*(mp_display_camera_width(cobj)==400?1.25f:1.f);
}
