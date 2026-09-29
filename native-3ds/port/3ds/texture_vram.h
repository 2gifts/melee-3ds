#ifndef MP_TEXTURE_VRAM_H
#define MP_TEXTURE_VRAM_H
/* Hot textures move to VRAM: PICA samples VRAM faster than FCRAM, which the
 * engine, GX translator and renderer cores also load. Textures are still
 * decoded into FCRAM. One used in consecutive frames is marked, and at the
 * next frame start (after FrameBegin has joined the previous frame's GPU work
 * and cleared the command queue, before any draw) it is copied by the GX
 * transfer engine, queued ahead of that frame's command lists. No transfer is
 * appended to a queue that may be running. The FCRAM copy is freed at the
 * following frame start.
 *
 * VRAM also holds the render targets and EFB copy textures. Both render
 * targets are created at startup, before any promotion; promoted textures
 * fragment VRAM, so a later 1.5 MiB target could fail even with enough total
 * space. EFB copy textures release promoted textures until they fit.
 * The GX queue has 32 entries (svcBreak when full); early submission keeps 8
 * spare, so promotions stop well before that. */
#define VRAM_TEXTURE_RESERVE (1024u*1024u)
#define VRAM_PROMOTIONS_PER_FRAME 2u
#define VRAM_COLD_FRAMES 120u
#define VRAM_PENDING 16u
static void*vram_pending_free[VRAM_PENDING];
static unsigned vram_pending_count,vram_frame,vram_frame_promotions,vram_wanted;
/* Marked this frame: slot and FCRAM data (a slot can move on eviction). */
static struct{unsigned slot;void*data;}vram_marked[VRAM_PROMOTIONS_PER_FRAME];static unsigned vram_marked_count;
unsigned mp_vram_promotions,mp_vram_releases,mp_vram_texture_bytes;
#ifdef MP_SMOKE_TEST
volatile unsigned texture_vram_disable; /* Development comparison. */
#else
/* Off on hardware: update 26 hung the GPU on a queued transfer after ~1700
 * promotions/releases, with textures corrupted across stages. */
#define texture_vram_disable 1
#endif
static int vram_address(const void*p){return (u32)p>=0x1F000000u&&(u32)p<0x1F600000u;}
static void texture_vram_forget(const Texture*t){
    if(!vram_address(t->tex.data))return;
    mp_vram_texture_bytes-=texture_total_bytes(&t->tex);
}
/* Evict the least recently used promoted texture unused for cold frames.
 * Only earlier frames' textures qualify; their GPU work has completed. This
 * compacts textures[]: never call it while holding a Texture pointer. */
static int texture_vram_release_one(unsigned cold){
    unsigned oldest=texture_count;
    for(unsigned i=0;i<texture_count;++i)if(vram_address(textures[i].tex.data)&&textures[i].frame+cold<=frame_number&&
        (oldest==texture_count||textures[i].frame<textures[oldest].frame))oldest=i;
    if(oldest==texture_count)return 0;
    texture_remove(oldest);++mp_vram_releases;return 1;
}
static int texture_vram_release(unsigned needed,unsigned cold){
    while(vramSpaceFree()<needed)if(!texture_vram_release_one(cold))return 0;
    return 1;
}
/* Mark a texture used in consecutive frames; promoted at the next frame start. */
static void texture_vram_promote(Texture*t){
    if(texture_vram_disable||vram_address(t->tex.data))return;
    if(vram_frame!=frame_number){vram_frame=frame_number;vram_frame_promotions=0;}
    if(vram_frame_promotions>=VRAM_PROMOTIONS_PER_FRAME||vram_marked_count>=VRAM_PROMOTIONS_PER_FRAME)return;
    unsigned size=texture_total_bytes(&t->tex);
    if(vramSpaceFree()<size+VRAM_TEXTURE_RESERVE){if(size>vram_wanted)vram_wanted=size;return;}
    vram_marked[vram_marked_count].slot=(unsigned)(t-textures);vram_marked[vram_marked_count++].data=t->tex.data;
    ++vram_frame_promotions;
}
/* After FrameBegin (previous frame's GPU work joined, queue cleared) and
 * before this frame's first draw; texture bindings are already dropped.
 * Free last frame's FCRAM copies, make room for a hot texture that found
 * VRAM full, then copy the marked textures. */
static void texture_vram_frame_begin(void){
    for(unsigned i=0;i<vram_pending_count;++i)linearFree(vram_pending_free[i]);
    vram_pending_count=0;
    if(vram_wanted){texture_vram_release(vram_wanted+VRAM_TEXTURE_RESERVE,VRAM_COLD_FRAMES);vram_wanted=0;}
    gxCmdQueue_s*q=&C3Di_GetContext()->gxQueue;
    for(unsigned i=0;i<vram_marked_count;++i){
        unsigned slot=vram_marked[i].slot;
        /* Removal compacts textures[]: promote only the same, still-present image. */
        if(slot>=texture_count||textures[slot].tex.data!=vram_marked[i].data)continue;
        Texture*t=&textures[slot];unsigned size=texture_total_bytes(&t->tex);
        if(vram_pending_count>=VRAM_PENDING||q->numEntries+12>=q->maxEntries||vramSpaceFree()<size+VRAM_TEXTURE_RESERVE)continue;
        void*v=vramAlloc(size);if(!v)continue;
        unsigned site=mp_translator_site;mp_translator_site=6;
        GX_TextureCopy((u32*)t->tex.data,0,(u32*)v,0,size,8);
        mp_translator_site=site;
        vram_pending_free[vram_pending_count++]=t->tex.data;
        t->tex.data=v;++mp_vram_promotions;mp_vram_texture_bytes+=size;
    }
    vram_marked_count=0;
    mp_native_tex_bind_invalidate();
}
#endif
