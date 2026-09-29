#ifndef MP_GPU_PROBE_H
#define MP_GPU_PROBE_H
/* Hardware builds: holding the bottom-screen FPS button for two seconds runs
 * this once. Modes rotate every frame (60 frames each, interleaved, so
 * every mode samples the same moments of a live scene) with early GPU
 * submission off, so C3D_GetDrawingTime covers the whole frame. Differences
 * between modes split GPU time into per-draw overhead, vertex work, fill and
 * texture fetch:
 *   full          normal rendering
 *   no-fragments  every draw scissored away: vertex + setup + per-draw cost
 *   one-triangle  every draw limited to its first triangle: per-draw cost
 *   flat-texture  every texture replaced by one 8x8 texture: texture fetch
 *   one-eye       right-eye draws omitted: per-eye share
 *   keep-texture-cache  Citro3D's texture-cache clear on every texture
 *                 change suppressed (textures may briefly show stale texels)
 * The top screen visibly glitches for about six seconds; nothing else
 * changes. */
enum{PROBE_FULL,PROBE_NO_FRAGMENTS,PROBE_ONE_TRIANGLE,PROBE_FLAT_TEXTURE,PROBE_ONE_EYE,PROBE_KEEP_CACHE,PROBE_MODES};
static const char*const probe_names[PROBE_MODES]={"full","no-fragments","one-triangle","flat-texture","one-eye","keep-texture-cache"};
extern volatile unsigned mp_texture_cache_keep;
static unsigned probe_request;
void mp_native_gpu_probe_request(void){__atomic_store_n(&probe_request,1,__ATOMIC_RELEASE);}
#define PROBE_WARMUP 8u
#define PROBE_FRAMES 60u
static unsigned probe_mode=PROBE_MODES,probe_frame,probe_measured_mode=PROBE_MODES,probe_running;
static double probe_ms[PROBE_MODES];
static unsigned probe_samples[PROBE_MODES],probe_draws[PROBE_MODES],probe_vertices[PROBE_MODES],probe_stereo,probe_width;
static C3D_Tex probe_texture;

/* Returns nonzero when the draw is omitted. */
static int gpu_probe_draw(unsigned*count){
    if(probe_mode==PROBE_ONE_EYE&&stereo_active&&stereo_eye)return 1;
    if(probe_mode==PROBE_ONE_TRIANGLE&&*count>3)*count=3;
    if(probe_mode==PROBE_NO_FRAGMENTS)C3D_SetScissor(GPU_SCISSOR_INVERT,0,0,240,800);
    return 0;
}
static void gpu_probe_after_draw(void){
    if(probe_mode==PROBE_NO_FRAGMENTS)C3D_SetScissor(GPU_SCISSOR_NORMAL,raster_state.clip[0],raster_state.clip[1],raster_state.clip[2],raster_state.clip[3]);
}
static void gpu_probe_bind(int unit,C3D_Tex*texture){
    if(texture&&probe_mode==PROBE_FLAT_TEXTURE&&probe_texture.data)texture=&probe_texture;
    mp_native_tex_bind(unit,texture);
}
static void gpu_probe_init(void){
    if(!C3D_TexInit(&probe_texture,8,8,GPU_RGBA8))return;
    memset(probe_texture.data,0xff,probe_texture.size);C3D_TexFlush(&probe_texture);
    C3D_TexSetFilter(&probe_texture,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&probe_texture,GPU_REPEAT,GPU_REPEAT);
}
/* A completed frame's GPU time; mode is the one it was rendered in. */
static void gpu_probe_account(double ms){
    unsigned mode=probe_measured_mode;probe_measured_mode=PROBE_MODES;
    if(mode<PROBE_MODES){probe_ms[mode]+=ms;++probe_samples[mode];}
}
static void gpu_probe_report(void){
    double m[PROBE_MODES];char text[400];
    for(unsigned i=0;i<PROBE_MODES;++i)m[i]=probe_samples[i]?probe_ms[i]/probe_samples[i]:0;
    snprintf(text,sizeof(text),"GPU probe stereo=%u width=%u draws=%u vertices=%u: %s=%.2f %s=%.2f %s=%.2f %s=%.2f %s=%.2f %s=%.2f ms\n",
        probe_stereo,probe_width,probe_samples[0]?probe_draws[0]/probe_samples[0]:0,probe_samples[0]?probe_vertices[0]/probe_samples[0]:0,
        probe_names[0],m[0],probe_names[1],m[1],probe_names[2],m[2],probe_names[3],m[3],probe_names[4],m[4],probe_names[5],m[5]);
    mp_native_log(text);
    snprintf(text,sizeof(text),"GPU probe split: per-draw=%.2f vertex=%.2f fill=%.2f texture=%.2f right-eye=%.2f texture-cache-clears=%.2f ms\n",
        m[2],m[1]-m[2],m[0]-m[1],m[0]-m[3],m[0]-m[4],m[0]-m[5]);
    mp_native_log(text);
    memset(probe_ms,0,sizeof(probe_ms));memset(probe_samples,0,sizeof(probe_samples));
    memset(probe_draws,0,sizeof(probe_draws));memset(probe_vertices,0,sizeof(probe_vertices));
}
/* After FrameBegin. Early submission stays off for the whole probe. */
static void gpu_probe_frame_begin(void){
    if(!probe_running){
        if(!__atomic_exchange_n(&probe_request,0,__ATOMIC_ACQUIRE))return;
        mp_native_log("GPU probe started\n");
        probe_running=1;probe_frame=0;
    }else if(++probe_frame==PROBE_WARMUP+PROBE_MODES*PROBE_FRAMES){
        gpu_probe_report();probe_running=0;probe_mode=PROBE_MODES;gpu_probe_active=0;mp_texture_cache_keep=0;mp_native_tex_bind_invalidate();return;
    }
    probe_mode=probe_frame<PROBE_WARMUP?PROBE_FULL:(probe_frame-PROBE_WARMUP)%PROBE_MODES;
    mp_texture_cache_keep=probe_mode==PROBE_KEEP_CACHE;
    gpu_probe_active=1;mp_native_tex_bind_invalidate();
}
/* Before FrameEnd: this frame's statistics and the mode its time belongs to. */
static void gpu_probe_frame_end(unsigned draws,unsigned vertices){
    if(!probe_running||probe_frame<PROBE_WARMUP)return;
    probe_measured_mode=probe_mode;probe_draws[probe_mode]+=draws;probe_vertices[probe_mode]+=vertices;
    probe_stereo=stereo_active;probe_width=render_width;
}
#endif
