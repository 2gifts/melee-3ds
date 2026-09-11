#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "alpha_test.h"
#include "texture_storage.h"
#include "texture_decode.h"
#include "texture_repack.h"
#include "texture_index.h"
#include "texture_visibility.h"
#include "cache_lru.h"
#include "efb_rgb565.h"
#include "scissor.h"
#include "blend_state.h"
#include "audio_trace.h"
#include "log_progress.h"
#include "citro3d_fix.h"
#define C3D_TexBind mp_native_tex_bind
#include "../engine/gpu_vertex.h"
#include "../engine/layered_material.h"
_Static_assert(GPU_L4==10&&GPU_L8==7&&GPU_LA4==9&&GPU_LA8==5&&GPU_RGB565==3&&GPU_RGBA8==0,"Native texture storage format IDs");
_Static_assert(GPU_BLEND_REVERSE_SUBTRACT==2&&GPU_DST_COLOR==4&&GPU_SRC_ALPHA==6&&GPU_DST_ALPHA==8,"Native blend constants");
_Static_assert(GPU_LOGICOP_SET==4&&GPU_LOGICOP_AND_INVERTED==13&&GPU_LOGICOP_OR_INVERTED==15,"Native logic constants");

typedef struct{float p[4],c[4],t[2],n[4];} Vertex;
_Static_assert(sizeof(Vertex)==sizeof(MPGPUVertex),"GPU vertex wire layout");
typedef struct{u32 image,w,h,format,palette,palfmt,palcount,depth,write,func,blend,src,dst,cull,alpha,color_mask,texture_rgb,texture_alpha,wrap_s,wrap_t,gpu,indices,index_count,geometry_id,scissor[4],points,point_size,point_offset,screen_width,layer;float convergence;} Draw;
_Static_assert(sizeof(Draw)==34*4,"GX draw bridge layout");
typedef struct{u32 image,palette,format,palfmt,palcount;C3D_Tex tex;unsigned w,h,hash,generation,frame,source_bytes;int retired;} Texture;
extern const unsigned char mp_shader[];extern const unsigned mp_shader_size;
extern const unsigned char mp_dual_shader[];extern const unsigned mp_dual_shader_size;
extern void mp_native_panic(const char*);extern void mp_native_log(const char*);
static DVLB_s*dvlb,*dual_dvlb;static shaderProgram_s program,point_program,dual_program;static C3D_RenderTarget*target;
extern const unsigned char mp_stereo_shader[],mp_dual_stereo_shader[];
extern const unsigned mp_stereo_shader_size,mp_dual_stereo_shader_size;
static DVLB_s*stereo_dvlb,*stereo_dual_dvlb;
static shaderProgram_s stereo_program,stereo_point_program,stereo_dual_program;
static C3D_RenderTarget*stereo_target,*eye_output[2];
static C3D_RenderTarget*mono_target;
unsigned mp_native_stereo_depth;
static unsigned stereo_active;
static int stereo_failed;
#define OUTPUT_FLAGS (GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8))
static int point_program_active;
static void bind_program(int points){
    if(points!=point_program_active){if(point_program_active==2)C3D_TexBind(1,NULL);
        C3D_BindProgram(stereo_active?(points==2?&stereo_dual_program:points==1?&stereo_point_program:&stereo_program):(points==2?&dual_program:points==1?&point_program:&program));point_program_active=points;}
}
static int stereo_init(void){
    if(stereo_target)return 1;if(stereo_failed)return 0;
    /* Adjacent 240x400 tile regions share a 240x800 render target. Moving
     * the viewport between eyes avoids a framebuffer flush on every draw.
     * Output views borrow VRAM; only stereo_target owns those allocations. */
    stereo_target=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    if(!stereo_target){stereo_failed=1;return 0;}
    for(unsigned eye=0;eye<2;++eye){
        C3D_Tex view={0};view.data=(u8*)stereo_target->frameBuf.colorBuf+eye*240*400*4;
        view.width=240;view.height=400;view.fmt=GPU_RGBA8;view.size=240*400*4;
        eye_output[eye]=C3D_RenderTargetCreateFromTex(&view,GPU_TEXFACE_2D,0,(C3D_DEPTHTYPE){.__i=-1});
        if(!eye_output[eye])mp_native_panic("Stereo output allocation failed");
    }
    mp_native_log("Stereo framebuffer ready: two 400x240 views, shared geometry\n");return 1;
}
static Vertex*vertices;static unsigned vertex_count,draw_count,render_vertex_count;
static float(*layer_uv)[2];static int layer_attributes;
#ifdef MP_SMOKE_TEST
unsigned layered_draws;
unsigned shield_draws[2];
#endif
static u16*indices;static unsigned index_count;
static unsigned flushed_vertices,flushed_indices;
#define COMMAND_BUFFER_BYTES (2*1024*1024)
#define COMMAND_HEADROOM (64*1024)
static unsigned command_barriers,command_peak_bytes;
/* Retain a small command trail in ordinary RAM. The watchdog can report it
 * if the GPU queue stalls, without reading live engine/allocator pointers. */
static unsigned gpu_trail[32][8],gpu_trail_head,gpu_last_command_bytes;
static void trace_draw(unsigned frame,const void*vertex,const void*index,unsigned count,const void*tex0,const void*tex1,unsigned mode){
    unsigned head=__atomic_load_n(&gpu_trail_head,__ATOMIC_RELAXED),row=head&31;
    unsigned values[8]={head,frame,(unsigned)vertex,(unsigned)index,count,(unsigned)tex0,(unsigned)tex1,mode};
    for(unsigned i=0;i<8;++i)__atomic_store_n(&gpu_trail[row][i],values[i],__ATOMIC_RELAXED);
    __atomic_store_n(&gpu_trail_head,head+1,__ATOMIC_RELEASE);
}
void mp_renderer_stall_report(FILE*file){
    unsigned end=__atomic_load_n(&gpu_trail_head,__ATOMIC_ACQUIRE),start=end>32?end-32:0;
    fprintf(file,"Last GPU command bytes=%u; recent draws (sequence frame vertex index count tex0 tex1 mode):\n",__atomic_load_n(&gpu_last_command_bytes,__ATOMIC_RELAXED));
    for(unsigned n=start;n<end;++n){unsigned v[8];for(unsigned i=0;i<8;++i)v[i]=__atomic_load_n(&gpu_trail[n&31][i],__ATOMIC_RELAXED);
        fprintf(file,"%u %u %08x %08x %u %08x %08x %u\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);}
}
#ifdef MP_SMOKE_TEST
static volatile unsigned command_byte_budget=COMMAND_BUFFER_BYTES;
#endif
static unsigned command_usage(void){
    u32 size,offset;GPUCMD_GetBuffer(NULL,&size,&offset);
    if(offset>size||size>COMMAND_BUFFER_BYTES/4)mp_native_panic("Invalid GPU command buffer position");
    unsigned used=COMMAND_BUFFER_BYTES-(size-offset)*4;
    __atomic_store_n(&gpu_last_command_bytes,used,__ATOMIC_RELAXED);
    if(used>command_peak_bytes)command_peak_bytes=used;
    return used;
}
/* Every CPU-written GPU resource is flushed at its write site. Keep the old
 * whole-heap flush selectable only for automated comparison. */
#ifdef MP_SMOKE_TEST
static volatile unsigned gpu_full_heap_flush;
static volatile unsigned gpu_vblank_wait;
static void end_command_frame(u8 flags){command_usage();unsigned previous=mp_log_phase(MP_LOG_GPU_END);C3D_FrameEnd(gpu_full_heap_flush?flags:flags|GX_CMDLIST_FLUSH);mp_log_phase(previous);}
#else
static void end_command_frame(u8 flags){command_usage();unsigned previous=mp_log_phase(MP_LOG_GPU_END);C3D_FrameEnd(flags|GX_CMDLIST_FLUSH);mp_log_phase(previous);}
#endif
static bool begin_command_frame(u8 flags){unsigned previous=mp_log_phase(MP_LOG_GPU_BEGIN);bool result=C3D_FrameBegin(flags);mp_log_phase(previous);return result;}
#define C3D_FrameEnd(flags) end_command_frame(flags)
#define C3D_FrameBegin(flags) begin_command_frame(flags)
static void reserve_commands(void);
static void checked_draw_elements(GPU_Primitive_t primitive,int count,int type,const void*data){reserve_commands();C3D_DrawElements(primitive,count,type,data);}
static void checked_draw_arrays(GPU_Primitive_t primitive,int first,int count){reserve_commands();C3D_DrawArrays(primitive,first,count);}
/* Guard each physical draw, including the multiple passes used to emulate
 * complex alpha comparisons. One GX submission may emit many such draws. */
#define C3D_DrawElements(primitive,count,type,data) checked_draw_elements(primitive,count,type,data)
#define C3D_DrawArrays(primitive,first,count) checked_draw_arrays(primitive,first,count)
static const u16*draw_indices;
typedef struct{unsigned id,frame,bytes,count,index_count;float us,vs;int textured;Vertex*vertices;u16*indices;} NativeGeometry;
#define NATIVE_GEOMETRY_ENTRIES 2048
#define NATIVE_GEOMETRY_BUDGET (4*1024*1024)
static NativeGeometry native_geometry[NATIVE_GEOMETRY_ENTRIES];
_Static_assert(NATIVE_GEOMETRY_ENTRIES==MP_CACHE_LRU_SLOTS,"Native geometry LRU size");
static MPCacheLru native_geometry_lru;
static unsigned native_geometry_bytes,native_geometry_hits;
#ifdef MP_SMOKE_TEST
unsigned native_geometry_work[8];
static volatile unsigned native_geometry_linear_scan,native_geometry_lru_validate;
unsigned native_geometry_lru_checks;
static volatile unsigned native_geometry_validate;
unsigned native_geometry_checks;
#define NATIVE_WORK(i,n) (native_geometry_work[i]+=(n))
#else
#define NATIVE_WORK(i,n) ((void)0)
#endif
static int frame_active;
static unsigned render_width=320;
#include "../engine/stereo_config.h"
static unsigned stereo_eye;
#ifdef MP_SMOKE_TEST
static volatile unsigned stereo_order_reference;
unsigned stereo_eye_switches,stereo_pairs,stereo_attribute_writes;
#endif
static float stereo_scale_cache,stereo_bias_cache;
static unsigned stereo_shift_valid;
static void stereo_shift(float scale,float bias){
    if(!stereo_shift_valid||scale!=stereo_scale_cache||bias!=stereo_bias_cache
#ifdef MP_SMOKE_TEST
       ||stereo_order_reference
#endif
    ){
        C3D_FixedAttribSet(6,scale,bias,0,0);
        stereo_scale_cache=scale;stereo_bias_cache=bias;stereo_shift_valid=1;
#ifdef MP_SMOKE_TEST
        ++stereo_attribute_writes;
#endif
    }
}
static void screen_viewport(unsigned width){render_width=width;stereo_eye=0;C3D_SetViewport(0,(stereo_active?400:0)+(400-width)/2,240,width);}
static Texture textures[MP_TEXTURE_SLOTS];static unsigned texture_count;
static unsigned texture_uploads,texture_evictions;
#ifdef MP_SMOKE_TEST
static volatile unsigned texture_repack_disable,texture_repack_validate;
unsigned texture_repack_checks;
unsigned texture_repack_format_checks[16],texture_repack_fast_ticks[16],texture_repack_reference_ticks[16];
#endif
static MPTextureIndex texture_index;
#ifdef MP_SMOKE_TEST
static volatile unsigned texture_lookup_disable,texture_lookup_validate;
unsigned texture_lookup_checks;
#endif
static unsigned texture_bytes,texture_barriers;
#ifdef MP_SMOKE_TEST
static volatile unsigned texture_budget=16*1024*1024;
static volatile unsigned texture_slot_budget=MP_TEXTURE_SLOTS;
#define TEXTURE_BUDGET texture_budget
#define TEXTURE_SLOT_BUDGET texture_slot_budget
#else
#define TEXTURE_BUDGET (16*1024*1024)
#define TEXTURE_SLOT_BUDGET MP_TEXTURE_SLOTS
#endif
static unsigned texture_generation=1,frame_number;
typedef struct {Texture texture;C3D_RenderTarget*target;unsigned copied_frame;} EfbTexture;
static EfbTexture efb_textures[8];static C3D_Tex efb_capture;
static unsigned efb_gpu_copies;
static volatile unsigned efb_gpu_disable;
static int gpu_efb_copy(const u32*);
#ifdef MP_SMOKE_TEST
static volatile unsigned efb_verify;
static volatile unsigned efb_rgb565_disable,efb_rgb565_validate;
static volatile unsigned framebuffer_range_disable,texture_content_validate;
unsigned texture_content_checks,texture_hash_checks,texture_dirty_calls,texture_dirty_invalidated,framebuffer_cpu_calls;
unsigned texture_upload_ticks,texture_upload_max_ticks;
unsigned long long texture_hash_bytes,framebuffer_cpu_pixels;
unsigned efb_rgb565_checks,efb_rgb565_pixels,efb_rgb565_ticks[2];
#endif
static const GPU_TESTFUNC depth_comparisons[]={GPU_NEVER,GPU_GREATER,GPU_EQUAL,GPU_GEQUAL,GPU_LESS,GPU_NOTEQUAL,GPU_LEQUAL,GPU_ALWAYS};
static const GPU_TESTFUNC alpha_comparisons[]={GPU_NEVER,GPU_LESS,GPU_EQUAL,GPU_LEQUAL,GPU_GREATER,GPU_NOTEQUAL,GPU_GEQUAL,GPU_ALWAYS};
static u32*efb_pixels;
static u32 uniform_cache[MP_GPU_UNIFORMS][4];
static u8 uniform_valid[MP_GPU_UNIFORMS];
static u32 material0_cache[4];static int material0_valid;
/* Citro3D marks the complete effect block dirty on each setter, even if its
 * value is unchanged. Retain normal-draw state across adjacent batches.
 * Copies and frame-target changes invalidate it before the next draw. */
enum {STATE_CULL=1,STATE_SCISSOR=2,STATE_TEXENV=4,STATE_DEPTH=8,STATE_BLEND=16,STATE_ALPHA=32};
static struct {unsigned valid,cull,clip[4],texenv,depth,blend,src,dst,alpha;} raster_state;
#ifdef MP_SMOKE_TEST
static volatile unsigned raster_state_disable;
unsigned raster_state_hits,raster_state_updates;
#endif
static void raster_state_invalidate(void){raster_state.valid=0;}
static int state_needed(unsigned bit,int changed){
    int needed=!(raster_state.valid&bit)||changed;raster_state.valid|=bit;
#ifdef MP_SMOKE_TEST
    if(raster_state_disable)needed=1;
    if(needed)++raster_state_updates;else ++raster_state_hits;
#endif
    return needed;
}
static void raster_alpha(unsigned func,unsigned ref){
    unsigned key=func|(ref<<3);
    if(state_needed(STATE_ALPHA,key!=raster_state.alpha))C3D_AlphaTest(func!=7,alpha_comparisons[func],ref);
    raster_state.alpha=key;
}
#ifdef MP_SMOKE_TEST
extern unsigned mp_gx_profile;
unsigned mp_native_detail_ticks[4],mp_native_detail_count[4];
static unsigned native_detail_sequence[4];
extern unsigned mp_native_ticks(void);
static unsigned native_detail_begin(unsigned phase){return mp_gx_profile&&!(native_detail_sequence[phase]++&15)?mp_native_ticks():0;}
static void native_detail_end(unsigned phase,unsigned start){if(start){mp_native_detail_ticks[phase]+=mp_native_ticks()-start;++mp_native_detail_count[phase];}}
#else
static unsigned native_detail_begin(unsigned phase){(void)phase;return 0;}
static void native_detail_end(unsigned phase,unsigned start){(void)phase;(void)start;}
#endif
#define MAX_VERTICES 16384
#define MAX_INDICES (MAX_VERTICES*3)
static unsigned stream_barriers;
#ifdef MP_SMOKE_TEST
static volatile unsigned stream_vertex_budget=MAX_VERTICES;
#define STREAM_VERTEX_BUDGET stream_vertex_budget
#else
#define STREAM_VERTEX_BUDGET MAX_VERTICES
#endif
static void vertex_attributes(int dual){
    if(layer_attributes==dual)return;layer_attributes=dual;
    C3D_AttrInfo*a=C3D_GetAttrInfo();AttrInfo_Init(a);AttrInfo_AddLoader(a,0,GPU_FLOAT,4);AttrInfo_AddLoader(a,1,GPU_FLOAT,4);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);AttrInfo_AddLoader(a,3,GPU_FLOAT,4);AttrInfo_AddFixed(a,4);
    if(dual)AttrInfo_AddLoader(a,5,GPU_FLOAT,2);
    if(stereo_active){if(!dual)AttrInfo_AddFixed(a,5);AttrInfo_AddFixed(a,6);}
}
static void vertex_pointer(const Vertex*first){vertex_attributes(0);C3D_BufInfo*b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,first,sizeof(Vertex),4,0x3210);}
static void layered_vertex_pointer(const Vertex*first,const float*uv){vertex_attributes(1);C3D_BufInfo*b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,first,sizeof(Vertex),4,0x3210);BufInfo_Add(b,uv,8,1,5);}
static void vertex_base(unsigned first){vertex_pointer(vertices+first);}
static void indexed_draw(unsigned first,unsigned count){C3D_DrawElements(point_program_active==1?GPU_GEOMETRY_PRIM:GPU_TRIANGLES,count,C3D_UNSIGNED_SHORT,draw_indices+first);}
static int raster_cull(unsigned mode){
    /* Map the GX API modes to this renderer's native viewport convention.
     * Visible CSS/menu polygons arrive clockwise in native clip space.
     * Passing identically named PICA modes culled those visible faces. The
     * synthetic culling fixture must retain this observed menu winding. */
    if(mode==3)return 0;
    C3D_CullFace(mode==1?GPU_CULL_BACK_CCW:mode==2?GPU_CULL_FRONT_CCW:GPU_CULL_NONE);return 1;
}
static void raster_blend(unsigned packed,unsigned src,unsigned dst){
    MPBlendState b=mp_blend_state(packed,src,dst);
    if(b.logical)C3D_ColorLogicOp((GPU_LOGICOP)b.logic);
    else C3D_AlphaBlend((GPU_BLENDEQUATION)b.equation,(GPU_BLENDEQUATION)b.equation,
        (GPU_BLENDFACTOR)b.src,(GPU_BLENDFACTOR)b.dst,(GPU_BLENDFACTOR)b.src_alpha,(GPU_BLENDFACTOR)b.dst_alpha);
}
static void flush_dynamic(void){
    if(vertex_count>flushed_vertices){GSPGPU_FlushDataCache(vertices+flushed_vertices,(vertex_count-flushed_vertices)*sizeof(Vertex));flushed_vertices=vertex_count;}
    if(index_count>flushed_indices){GSPGPU_FlushDataCache(indices+flushed_indices,(index_count-flushed_indices)*sizeof(u16));flushed_indices=index_count;}
}
static void reserve_commands(void){
    unsigned limit=COMMAND_BUFFER_BYTES;
#ifdef MP_SMOKE_TEST
    if(command_byte_budget>=2*COMMAND_HEADROOM&&command_byte_budget<limit)limit=command_byte_budget;
#endif
    if(command_usage()+COMMAND_HEADROOM<=limit)return;
    /* Shader/context uploads and list finalization need spare command words.
     * GPUCMD_AddRawCommands does not bounds-check its memcpy. FrameSplit alone
     * advances within the same allocation; finish the queue to recycle it.
     * Keep the partial framebuffer and all vertex/index allocations intact. */
    flush_dynamic();bool used=target->used;target->used=false;C3D_FrameEnd(0);
    MP_AUDIO_TRACE(2,C3D_FrameBegin(0));target->used=used;++command_barriers;
}
static void reserve_dynamic(unsigned needed_vertices,unsigned needed_indices){
    unsigned limit=STREAM_VERTEX_BUDGET;
    if(!limit||limit>MAX_VERTICES)limit=MAX_VERTICES;
    if(needed_vertices>limit||needed_indices>limit*3)mp_native_panic("Single draw exceeds streaming buffer");
    if(vertex_count+needed_vertices<=limit&&index_count+needed_indices<=limit*3)return;
    /* Complete queued draws before recycling their linear-memory ranges.
     * The render target, material state and cache allocations stay live.
     * This saves about 10.7 MiB versus reserving an entire worst-case frame. */
    flush_dynamic();bool used=target->used;target->used=false;C3D_FrameEnd(0);MP_AUDIO_TRACE(2,C3D_FrameBegin(0));target->used=used;
    vertex_count=index_count=flushed_vertices=flushed_indices=0;++stream_barriers;
}
typedef u32 WireWord __attribute__((may_alias));
/* Every 32-bit bridge input is an aligned struct/float word. Make that
 * contract explicit so ARM11 uses LDR+REV rather than four byte loads. */
static u32 read32(const void*p){return __builtin_bswap32(*(const WireWord*)p);}
static u16 read16(const void*p){const u8*b=p;return(b[0]<<8)|b[1];}
static void convert_vertices(Vertex*dest,const Vertex*be,unsigned count,int textured,float us,float vs){
    for(unsigned i=0;i<count;++i){Vertex*v=&dest[i];for(unsigned j=0;j<14;++j)((u32*)v)[j]=read32((const u32*)&be[i]+j);
        if(v->n[3]<0){float x=v->p[0];v->p[0]=v->p[1];v->p[1]=-x;}
        if(textured){v->t[0]*=us;v->t[1]=1.f-v->t[1]*vs;}
    }
}
static void native_geometry_free(NativeGeometry*e){
    if(e->id)mp_cache_lru_remove(&native_geometry_lru,(unsigned)(e-native_geometry)+1);
    if(e->vertices){linearFree(e->vertices);native_geometry_bytes-=e->bytes;}memset(e,0,sizeof(*e));
}
static NativeGeometry*native_geometry_oldest(void){
    NativeGeometry*old=native_geometry_lru.head?&native_geometry[native_geometry_lru.head-1]:NULL;
    /* Touches are ordered by first use in a frame. If the oldest allocation
     * is still referenced by this frame, none may be released yet. */
    if(old&&old->frame>=frame_number)old=NULL;
#ifdef MP_SMOKE_TEST
    if(native_geometry_linear_scan||native_geometry_lru_validate){NativeGeometry*linear=NULL;
        for(unsigned i=0;i<NATIVE_GEOMETRY_ENTRIES;++i){NativeGeometry*e=&native_geometry[i];if(e->id&&e->frame<frame_number&&(!linear||e->frame<linear->frame))linear=e;}
        if(native_geometry_lru_validate){
            if((old==NULL)!=(linear==NULL)||(old&&old->frame!=linear->frame))mp_native_panic("Geometry LRU differs from oldest-frame scan");
            ++native_geometry_lru_checks;
        }
        if(native_geometry_linear_scan){NATIVE_WORK(6,NATIVE_GEOMETRY_ENTRIES);return linear;}
    }
#endif
    NATIVE_WORK(6,1);return old;
}
#ifdef MP_SMOKE_TEST
static void native_geometry_check_lru(void){
    unsigned entries=0,bytes=0,index=native_geometry_lru.head,previous=0,frame=0;
    while(index){
        if(index>NATIVE_GEOMETRY_ENTRIES||++entries>NATIVE_GEOMETRY_ENTRIES)mp_native_panic("Invalid geometry LRU chain");
        NativeGeometry*e=&native_geometry[index-1];
        if(!e->id||!e->vertices||native_geometry_lru.previous[index]!=previous||e->frame<frame)mp_native_panic("Invalid geometry LRU ordering");
        previous=index;frame=e->frame;bytes+=e->bytes;index=native_geometry_lru.next[index];
    }
    unsigned live=0;for(unsigned i=0;i<NATIVE_GEOMETRY_ENTRIES;++i){
        if(native_geometry[i].id)++live;
        else if(native_geometry_lru.previous[i+1]||native_geometry_lru.next[i+1])mp_native_panic("Freed geometry remains linked");
    }
    if(live!=entries||bytes!=native_geometry_bytes||native_geometry_lru.tail!=previous)mp_native_panic("Geometry LRU accounting mismatch");
    ++native_geometry_lru_checks;
}
static void native_geometry_check(const NativeGeometry*e,const Vertex*be,const Draw*d){
    /* Check reuse against the current bridge inputs, including texture scale
     * and endian conversion. This catches a stale/wrong cache allocation even
     * when the decoded BE geometry and the list's links are both correct. */
    for(unsigned i=0;i<e->count;++i){Vertex fresh;
        convert_vertices(&fresh,be+i,1,e->textured,e->us,e->vs);
        if(memcmp(&fresh,e->vertices+i,sizeof(fresh)))mp_native_panic("Native cached vertex differs from fresh conversion");
    }
    for(unsigned i=0;i<e->index_count;++i)
        if(e->indices[i]!=read16((const u8*)d->indices+2*i))mp_native_panic("Native cached index differs from bridge input");
    ++native_geometry_checks;
}
#endif
static NativeGeometry*native_geometry_get(const Vertex*be,unsigned count,const Draw*d,int textured,float us,float vs){
    NATIVE_WORK(0,1);
    if(!d->geometry_id){NATIVE_WORK(1,count);return NULL;}
    NativeGeometry*set=native_geometry+((d->geometry_id^(d->geometry_id>>8))&255)*8,*slot=NULL;
    for(unsigned i=0;i<8;++i){NativeGeometry*e=&set[i];
        if(e->id==d->geometry_id&&e->count==count&&e->index_count==d->index_count&&e->textured==textured&&e->us==us&&e->vs==vs){
#ifdef MP_SMOKE_TEST
            if(native_geometry_validate)native_geometry_check(e,be,d);
#endif
            if(e->frame!=frame_number){mp_cache_lru_touch(&native_geometry_lru,(unsigned)(e-native_geometry)+1);e->frame=frame_number;}
            ++native_geometry_hits;return e;
        }
        if(!e->id||e->frame<frame_number){if(!slot||!e->id||(slot->id&&e->frame<slot->frame))slot=e;}
    }
    NATIVE_WORK(2,1);
    if(!slot){NATIVE_WORK(4,1);return NULL;}native_geometry_free(slot);
    unsigned bytes=count*sizeof(Vertex)+d->index_count*sizeof(u16);
    if(bytes>NATIVE_GEOMETRY_BUDGET)return NULL;
    while(native_geometry_bytes+bytes>NATIVE_GEOMETRY_BUDGET){NativeGeometry*old=native_geometry_oldest();
        if(!old){NATIVE_WORK(4,1);return NULL;}NATIVE_WORK(5,1);native_geometry_free(old);
    }
    Vertex*v=linearAlloc(bytes);if(!v)return NULL;
    u16*ib=(void*)(v+count);const u16*src=(void*)d->indices;
    convert_vertices(v,be,count,textured,us,vs);for(unsigned i=0;i<d->index_count;++i)ib[i]=__builtin_bswap16(src[i]);
    NATIVE_WORK(3,count);
    GSPGPU_FlushDataCache(v,bytes);
    *slot=(NativeGeometry){d->geometry_id,frame_number,bytes,count,d->index_count,us,vs,textured,v,ib};native_geometry_bytes+=bytes;
    mp_cache_lru_touch(&native_geometry_lru,(unsigned)(slot-native_geometry)+1);return slot;
}
static u32 rgba(unsigned r,unsigned g,unsigned b,unsigned a){return(r<<24)|(g<<16)|(b<<8)|a;}
static u32 rgb565(unsigned v){return mp_rgb565(v);}
static u32 rgb5a3(unsigned v){return mp_rgb5a3(v);}
static unsigned morton(unsigned x,unsigned y){return(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);}
static u32 palette_color(const Draw*d,unsigned i){if(!d->palette||i>=d->palcount)return 0xffffffff;unsigned v=read16((void*)(d->palette+2*i));return d->palfmt==1?rgb565(v):d->palfmt==2?rgb5a3(v):rgba(v&255,v&255,v&255,v>>8);}
static u32 decode(const Draw*d,unsigned x,unsigned y)
{
    unsigned fmt=d->format;const u8*src=(void*)d->image;unsigned bw=4,bh=4,bytes=32;
    if(fmt==0||fmt==8){bw=bh=8;}else if(fmt==1||fmt==2||fmt==9){bw=8;bh=4;}else if(fmt==6){bytes=64;}else if(fmt==14){bw=bh=8;}
    unsigned pitch=(d->w+bw-1)/bw;const u8*p=src+((y/bh)*pitch+x/bw)*bytes;
    unsigned ix=x%bw,iy=y%bh,k=iy*bw+ix,v;
    switch(fmt){case 0:v=(p[k/2]>>((k&1)?0:4))&15;v*=17;return rgba(v,v,v,v);
    case 1:v=p[k];return rgba(v,v,v,v);
    case 2:v=p[k];return rgba((v&15)*17,(v&15)*17,(v&15)*17,(v>>4)*17);
    case 3:v=read16(p+2*k);return rgba(v&255,v&255,v&255,v>>8);
    case 4:return rgb565(read16(p+2*k));case 5:return rgb5a3(read16(p+2*k));
    case 6:return rgba(p[2*k+1],p[32+2*k],p[32+2*k+1],p[2*k]);
    case 8:return palette_color(d,(p[k/2]>>((k&1)?0:4))&15);
    case 9:return palette_color(d,p[k]);case 10:return palette_color(d,read16(p+2*k)&0x3fff);
    case 14:{p+=((iy/4)*2+ix/4)*8;u32 c[4];mp_cmpr_palette(p,c);return c[(p[4+iy%4]>>(6-2*(ix%4)))&3];}
    default:{char text[160];snprintf(text,sizeof(text),"Unsupported GX texture format %u at %08x, size %ux%u\n",d->format,d->image,d->w,d->h);mp_native_panic(text);return 0;}}
}
static unsigned texture_bucket(const Texture*t){return mp_texture_bucket(t->image,t->palette,t->format,t->w,t->h);}
static int texture_matches(const Texture*t,const Draw*d){return !t->retired&&t->image==d->image&&t->palette==d->palette&&t->format==d->format&&t->palfmt==d->palfmt&&t->palcount==d->palcount&&t->w==d->w&&t->h==d->h;}
#ifdef MP_SMOKE_TEST
static Texture*texture_lookup_linear(const Draw*d){for(unsigned i=0;i<texture_count;++i)if(texture_matches(&textures[i],d))return &textures[i];return NULL;}
#endif
static Texture*texture_lookup(const Draw*d){
#ifdef MP_SMOKE_TEST
    if(texture_lookup_disable&&!texture_lookup_validate)return texture_lookup_linear(d);
#endif
    unsigned bucket=mp_texture_bucket(d->image,d->palette,d->format,d->w,d->h);Texture*found=NULL;
    for(unsigned link=texture_index.head[bucket];link;link=texture_index.next[link-1]){
        Texture*t=&textures[link-1];if(texture_matches(t,d)){found=t;break;}}
#ifdef MP_SMOKE_TEST
    if(texture_lookup_disable||texture_lookup_validate){
        Texture*linear=texture_lookup_linear(d);
        if(texture_lookup_validate){if(linear!=found)mp_native_panic("Texture index differs from linear lookup");++texture_lookup_checks;}
        if(texture_lookup_disable)return linear;
    }
#endif
    return found;
}
static void texture_remove(unsigned i){
    mp_texture_index_remove(&texture_index,texture_bucket(&textures[i]),i);
    texture_bytes-=textures[i].tex.size;C3D_TexDelete(&textures[i].tex);
    if(i!=--texture_count){
        mp_texture_index_remove(&texture_index,texture_bucket(&textures[texture_count]),texture_count);
        textures[i]=textures[texture_count];mp_texture_index_add(&texture_index,texture_bucket(&textures[i]),i);
    }
}
static const Draw*texture_pin;static unsigned texture_pin_bytes;
static int texture_evict(void){
    unsigned oldest=texture_count;
    for(unsigned i=0;i<texture_count;++i)if(textures[i].frame<frame_number&&(!texture_pin||!texture_matches(&textures[i],texture_pin))&&
        (oldest==texture_count||textures[i].frame<textures[oldest].frame))oldest=i;
    if(oldest==texture_count)return 0;texture_remove(oldest);++texture_evictions;return 1;
}
static void texture_barrier(void){
    /* Large scenes may reference more textures than fit at once. Finish the
     * queued draws before evicting their textures, retaining the color buffer
     * and rendering the rest of this game frame into it. */
    flush_dynamic();bool used=target->used;target->used=false;C3D_FrameEnd(0);MP_AUDIO_TRACE(2,C3D_FrameBegin(0));target->used=used;
    for(unsigned i=0;i<texture_count;++i)textures[i].frame=0;++texture_barriers;
}
static u32 texture_hash(const Draw*d){
    unsigned size=mp_texture_source_bytes(d->format,d->w,d->h);
#ifdef MP_SMOKE_TEST
    ++texture_hash_checks;texture_hash_bytes+=size+(d->palette?d->palcount*2:0);
#endif
    u32 hash=2166136261u;const u32*words=(void*)d->image;
    for(unsigned i=0;i<size/4;++i)hash=(hash^words[i])*16777619u;
    if(d->palette){const u16*p=(void*)d->palette;for(unsigned i=0;i<d->palcount;++i)hash=(hash^p[i])*16777619u;}
    return hash;
}
void mp_native_texture_dirty(unsigned changed,unsigned bytes,unsigned cpu_written){
    if(!bytes)return;
#ifdef MP_SMOKE_TEST
    if(framebuffer_range_disable)return;
    ++texture_dirty_calls;
#endif
    for(unsigned i=0;i<texture_count;++i){Texture*t=&textures[i];
        if(!t->retired&&t->generation==texture_generation&&mp_texture_source_overlap(t->image,t->source_bytes,t->palette,t->palcount,changed,bytes)){
            t->generation=texture_generation-1;
#ifdef MP_SMOKE_TEST
            ++texture_dirty_invalidated;
#endif
        }
    }
    /* CPU writes can replace a GPU-only image during its frame of use.
     * CPU invalidation is not a write and must retain the completed copy. */
    if(cpu_written)for(unsigned i=0;i<8;++i){EfbTexture*e=&efb_textures[i];Texture*t=&e->texture;
        if(e->copied_frame&&mp_texture_source_overlap(t->image,t->source_bytes,0,0,changed,bytes))e->copied_frame=0;
    }
}
static void framebuffer_texture_visibility(const u32*q){
#ifdef MP_SMOKE_TEST
    if(framebuffer_range_disable){++texture_generation;return;}
#endif
    mp_native_texture_dirty(q[0],mp_texture_source_bytes(q[7],q[5],q[6]),0);
}
static Texture*texture(const Draw*d)
{
    if(!d->image||!d->w||!d->h)return NULL;
    for(unsigned i=0;i<8;++i){EfbTexture*e=&efb_textures[i];Texture*t=&e->texture;
        if(e->copied_frame==frame_number&&t->image==d->image&&t->w==d->w&&t->h==d->h&&t->format==d->format){t->frame=frame_number;return t;}}
    Texture*cached=texture_lookup(d);if(cached&&cached->generation==texture_generation){
#ifdef MP_SMOKE_TEST
        if(texture_content_validate){++texture_content_checks;if(texture_hash(d)!=cached->hash)mp_native_panic("Texture reused after an untracked source change");}
#endif
        cached->frame=frame_number;return cached;
    }
    u32 hash=texture_hash(d);
    if(cached&&cached->hash==hash){cached->generation=texture_generation;cached->frame=frame_number;return cached;}if(cached)cached->retired=1;
#ifdef MP_SMOKE_TEST
    unsigned upload_start=mp_native_ticks();
#endif
    unsigned w=8,h=8;while(w<d->w)w*=2;while(h<d->h)h*=2;
    if(w>1024||h>1024)mp_native_panic("Texture exceeds PICA dimensions");
    unsigned format=mp_texture_format(d->format,d->palfmt),needed=w*h*mp_texture_bits(format)/8;
    /* A debug pressure limit can be smaller than one texture. Permit that
     * one allocation so the pressure test cannot deadlock the eviction loop. */
    unsigned budget=TEXTURE_BUDGET;if(budget<needed+texture_pin_bytes)budget=needed+texture_pin_bytes;
    /* Menus fill the old 256-entry limit at only 2-3 MiB. Retain more small
     * labels/portraits under the same 16 MiB byte cap to avoid redecoding. */
    unsigned slots=TEXTURE_SLOT_BUDGET;if(texture_pin&&slots<2)slots=2;
    while(texture_count>=slots||texture_bytes+needed>budget){if(!texture_evict())texture_barrier();}
    Texture*t=&textures[texture_count];
    while(!C3D_TexInit(&t->tex,w,h,(GPU_TEXCOLOR)format)){
        if(!texture_count)mp_native_panic("Native texture memory exhausted");
        if(!texture_evict())texture_barrier();t=&textures[texture_count];
    }
    ++texture_count;++texture_uploads;texture_bytes+=t->tex.size;memset(t->tex.data,0,t->tex.size);
    t->image=d->image;t->palette=d->palette;t->format=d->format;t->palfmt=d->palfmt;t->palcount=d->palcount;t->w=d->w;t->h=d->h;t->hash=hash;t->source_bytes=mp_texture_source_bytes(d->format,d->w,d->h);t->generation=texture_generation;t->frame=frame_number;t->retired=0;
    mp_texture_index_add(&texture_index,texture_bucket(t),texture_count-1);
    if(d->format==14)mp_cmpr_to_native(t->tex.data,w,h,(const void*)d->image,d->w,d->h);
    else {
        MPTextureSource source={(const void*)d->image,(const void*)d->palette,d->w,d->h,d->format,d->palfmt,d->palcount};
        int repacked=0;
#ifdef MP_SMOKE_TEST
        unsigned repack_start=texture_repack_validate?mp_native_ticks():0;
        if(!texture_repack_disable)
#endif
        repacked=mp_texture_repack(t->tex.data,w,h,&source);
#ifdef MP_SMOKE_TEST
        unsigned repack_ticks=texture_repack_validate?mp_native_ticks()-repack_start:0;
#endif
        if(!repacked)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned tx=x<d->w?x:d->w-1,ty=y<d->h?y:d->h-1;mp_texture_store(t->tex.data,((y/8)*(w/8)+x/8)*64+morton(x,y),format,decode(d,tx,ty));}
#ifdef MP_SMOKE_TEST
        if(repacked&&texture_repack_validate){
            void*reference=calloc(1,t->tex.size);if(!reference)mp_native_panic("Texture verification allocation failed");
            unsigned reference_start=mp_native_ticks();
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned tx=x<d->w?x:d->w-1,ty=y<d->h?y:d->h-1;mp_texture_store(reference,((y/8)*(w/8)+x/8)*64+morton(x,y),format,decode(d,tx,ty));}
            unsigned reference_ticks=mp_native_ticks()-reference_start;
            if(memcmp(reference,t->tex.data,t->tex.size))mp_native_panic("Native texture repack differs from original conversion");
            free(reference);++texture_repack_checks;
            ++texture_repack_format_checks[d->format];texture_repack_fast_ticks[d->format]+=repack_ticks;texture_repack_reference_ticks[d->format]+=reference_ticks;
        }
#endif
    }
    C3D_TexSetFilter(&t->tex,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&t->tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);C3D_TexFlush(&t->tex);
#ifdef MP_SMOKE_TEST
    unsigned upload_ticks=mp_native_ticks()-upload_start;texture_upload_ticks+=upload_ticks;
    if(upload_ticks>texture_upload_max_ticks)texture_upload_max_ticks=upload_ticks;
#endif
    return t;
}
int mp_renderer_init(void)
{
    if(!C3D_Init(COMMAND_BUFFER_BYTES))return 0;
    target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});if(!target)return 0;
    mono_target=target;
    C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
    dvlb=DVLB_ParseFile((u32*)mp_shader,mp_shader_size);if(!dvlb)return 0;
    shaderProgramInit(&program);shaderProgramSetVsh(&program,&dvlb->DVLE[0]);
    shaderProgramInit(&point_program);shaderProgramSetVsh(&point_program,&dvlb->DVLE[0]);shaderProgramSetGsh(&point_program,&dvlb->DVLE[1],3);
    dual_dvlb=DVLB_ParseFile((u32*)mp_dual_shader,mp_dual_shader_size);if(!dual_dvlb)return 0;
    shaderProgramInit(&dual_program);shaderProgramSetVsh(&dual_program,&dual_dvlb->DVLE[0]);
    stereo_dvlb=DVLB_ParseFile((u32*)mp_stereo_shader,mp_stereo_shader_size);
    stereo_dual_dvlb=DVLB_ParseFile((u32*)mp_dual_stereo_shader,mp_dual_stereo_shader_size);
    if(!stereo_dvlb||!stereo_dual_dvlb)return 0;
    shaderProgramInit(&stereo_program);shaderProgramSetVsh(&stereo_program,&stereo_dvlb->DVLE[0]);
    shaderProgramInit(&stereo_point_program);shaderProgramSetVsh(&stereo_point_program,&stereo_dvlb->DVLE[0]);shaderProgramSetGsh(&stereo_point_program,&stereo_dvlb->DVLE[1],3);
    shaderProgramInit(&stereo_dual_program);shaderProgramSetVsh(&stereo_dual_program,&stereo_dual_dvlb->DVLE[0]);
    vertices=linearAlloc(MAX_VERTICES*sizeof(Vertex));indices=linearAlloc(MAX_INDICES*sizeof(u16));layer_uv=linearAlloc(MAX_VERTICES*8);return vertices!=NULL&&indices!=NULL&&layer_uv!=NULL;
}
void mp_renderer_begin(void)
{
    /* Melee's pad/VI queue already paces simulation at 60 Hz. Waiting for a
     * second VBlank here quantizes a late frame to 30/20/15 FPS. Still wait
     * for the previous GPU queue before recycling any streaming storage. */
    if(frame_active)return;
#ifdef MP_SMOKE_TEST
    MP_AUDIO_TRACE(2,C3D_FrameBegin(gpu_vblank_wait?C3D_FRAME_SYNCDRAW:0));
#else
    MP_AUDIO_TRACE(2,C3D_FrameBegin(0));
#endif
    frame_active=1;++frame_number;
    unsigned requested=mp_native_stereo_depth&&stereo_init();
    if(requested!=stereo_active){
        stereo_active=requested;gfxSet3D(stereo_active!=0);
        if(stereo_active){C3D_RenderTargetSetOutput(eye_output[0],GFX_TOP,GFX_LEFT,OUTPUT_FLAGS);C3D_RenderTargetSetOutput(eye_output[1],GFX_TOP,GFX_RIGHT,OUTPUT_FLAGS);}
        else C3D_RenderTargetSetOutput(mono_target,GFX_TOP,GFX_LEFT,OUTPUT_FLAGS);
    }
    target=stereo_active?stereo_target:mono_target;
    if(stereo_active){stereo_shift_valid=0;stereo_shift(0,0);}
    /* Context bindings outlive a GPU frame. Drop them before compacting or
     * freeing cache objects, including when the next draw is untextured. */
    C3D_TexBind(0,NULL);C3D_TexBind(1,NULL);C3D_TexBind(2,NULL);
    /* Retain unchanged textures across menu visits. Capacity and allocation
     * pressure already evict the least recently used entries; expiring them
     * after 120 frames forced needless decoding when backing out of menus.
     * Reused source addresses are still hash-checked after GX invalidation. */
    for(unsigned i=0;i<texture_count;){if(textures[i].retired)texture_remove(i);else ++i;}
    C3D_RenderTargetClear(target,C3D_CLEAR_ALL,0x000000ff,0);C3D_FrameDrawOn(target);screen_viewport(320);
    raster_state_invalidate();
    point_program_active=-1;bind_program(0);layer_attributes=-1;vertex_attributes(0);
    vertex_base(0);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_CullFace(GPU_CULL_NONE);vertex_count=index_count=draw_count=render_vertex_count=flushed_vertices=flushed_indices=0;
}
static unsigned draw_with_alpha(const Draw*d,unsigned first,unsigned count){
    static struct{unsigned key,func,ref;int simple,used;}cache[64];
    unsigned slot=(d->alpha^(d->alpha>>8)^(d->alpha>>16))&63;
    if(!cache[slot].used||cache[slot].key!=d->alpha){cache[slot].key=d->alpha;cache[slot].used=1;cache[slot].simple=mp_alpha_reduce(d->alpha,&cache[slot].func,&cache[slot].ref);}
    if(cache[slot].simple){
        raster_alpha(cache[slot].func,cache[slot].ref);
        indexed_draw(first,count);return 1;
    }
    unsigned primitive_size=d->points?1:3;
    raster_state.valid&=~STATE_ALPHA;
    if(count>primitive_size){unsigned n=0;for(unsigned i=0;i<count;i+=primitive_size)n+=draw_with_alpha(d,first+i,primitive_size);return n;}
    /* A complex two-comparison predicate uses the otherwise-unused stencil
     * plane. Build A's coverage, then render the B predicate for A=0 and A=1.
     * Preliminary passes never change color or depth. */
    unsigned a=d->alpha&7,ra=(d->alpha>>3)&255,op=(d->alpha>>11)&3,b=(d->alpha>>13)&7,rb=(d->alpha>>16)&255,n=2;
    C3D_DepthTest(false,GPU_ALWAYS,0);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_StencilTest(true,GPU_ALWAYS,0,1,1);C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
    indexed_draw(first,count);
    C3D_StencilTest(true,GPU_ALWAYS,1,1,1);C3D_AlphaTest(a!=7,alpha_comparisons[a],ra);indexed_draw(first,count);
    C3D_DepthTest(d->depth,depth_comparisons[d->func&7],d->color_mask|(d->write?GPU_WRITE_DEPTH:0));
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    for(unsigned value=0;value<2;++value){
        unsigned f=op==0?(value?b:0):op==1?(value?7:b):op==2?(value?7-b:b):(value?b:7-b);
        if(!f)continue;C3D_StencilTest(true,GPU_EQUAL,value,1,0);C3D_AlphaTest(f!=7,alpha_comparisons[f],rb);
        indexed_draw(first,count);++n;
    }
    C3D_StencilTest(false,GPU_ALWAYS,0,255,255);return n;
}
void mp_native_submit(const Vertex*be,unsigned count,const Draw*state)
{
    Draw d;for(unsigned i=0;i<sizeof(d)/4;++i)((u32*)&d)[i]=read32((const u32*)state+i);
    if(d.points&&!d.point_size)return;
    if(d.cull==3)return;
    unsigned width=d.screen_width==320?320:400;
    if(render_width!=width)screen_viewport(width);
    unsigned clip[4];if(!mp_scissor_rect_width(d.scissor[0],d.scissor[1],d.scissor[2],d.scissor[3],width,clip))return;
    /* PICA's viewport origin is at the bottom; the first VRAM tile rows
     * become the upper half of the 800-pixel framebuffer (left LCD view). */
    if(stereo_active&&!stereo_eye){clip[1]+=400;clip[3]+=400;}
    if(state_needed(STATE_CULL,d.cull!=raster_state.cull))raster_cull(d.cull);
    raster_state.cull=d.cull;
    unsigned measured=native_detail_begin(0);MPTextureLayer layer={0};Texture*t1=NULL;Draw second={0};
    if(d.layer)for(unsigned i=0;i<sizeof(layer)/4;++i)((u32*)&layer)[i]=read32((const u32*)d.layer+i);
    int two_uv=d.layer&&layer.mode!=MP_FRAGMENT_TINT;
    if(two_uv){
        memcpy(&second,&layer,7*4);second.wrap_s=layer.wrap_s;second.wrap_t=layer.wrap_t;
        t1=texture(&second);if(!t1)mp_native_panic("Layered material has no second texture");
        /* Eviction compacts the slot array. Pin by key and look up again after
         * the first texture loads, so a cached object pointer cannot go stale. */
        texture_pin=&second;texture_pin_bytes=t1->tex.size;
    }
    Texture*t=texture(&d);
    if(two_uv){t1=texture(&second);texture_pin=NULL;texture_pin_bytes=0;if(!t)mp_native_panic("Layered material has no base texture");}
    native_detail_end(0,measured);
    bind_program(two_uv?2:d.points!=0);
    if(state_needed(STATE_SCISSOR,memcmp(clip,raster_state.clip,sizeof(clip))!=0))C3D_SetScissor(GPU_SCISSOR_NORMAL,clip[0],clip[1],clip[2],clip[3]);
    memcpy(raster_state.clip,clip,sizeof(clip));
    measured=native_detail_begin(1);
    if(d.gpu){const u32(*u)[4]=(void*)d.gpu;
        const u32*material=(const void*)((const MPGPUUniforms*)d.gpu)->material0;
        if(!material0_valid||memcmp(material0_cache,material,16)){
            union{u32 u[4];float f[4];}m;for(unsigned j=0;j<4;++j)m.u[j]=read32(material+j);
            C3D_FixedAttribSet(4,m.f[0],m.f[1],m.f[2],m.f[3]);memcpy(material0_cache,material,16);material0_valid=1;
        }
        unsigned rows=read32(&((const MPGPUUniforms*)d.gpu)->matrix_rows);int lighting=u[MP_GPU_CONFIG][0]!=0||u[MP_GPU_CONFIG+1][0]!=0;
        for(unsigned i=0;i<MP_GPU_UNIFORMS;++i){
        if(i<30&&i>=rows)continue;
        if(i>=30&&i<60&&(!lighting||i-30>=rows))continue;
        if(i>=MP_GPU_LIGHT_POS&&i<MP_GPU_SHADE){
            if(i<MP_GPU_LIGHT_COLOR||i>=MP_GPU_LIGHT_COLOR+4){if(!lighting||!u[MP_GPU_LIGHT_COLOR+(i-MP_GPU_LIGHT_POS)%4][3])continue;}
        }
        if(i>=MP_GPU_AMBIENT&&i<MP_GPU_AMBIENT+2&&!u[MP_GPU_CONFIG+i-MP_GPU_AMBIENT][0])continue;
        if(!uniform_valid[i]||u[i][0]!=uniform_cache[i][0]||u[i][1]!=uniform_cache[i][1]||u[i][2]!=uniform_cache[i][2]||u[i][3]!=uniform_cache[i][3]){
            union{u32 u[4];float f[4];}v;for(int j=0;j<4;++j)v.u[j]=read32(&u[i][j]);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,i,v.f[0],v.f[1],v.f[2],v.f[3]);memcpy(uniform_cache[i],u[i],16);uniform_valid[i]=1;
        }}}
    native_detail_end(1,measured);
    float us=t?(float)t->w/t->tex.width:1,vs=t?(float)t->h/t->tex.height:1;
    if(d.points){
        static const float offsets[]={0,1.f/16,1.f/8,1.f/4,1.f/2,1};
        float uv=d.point_offset<6?offsets[d.point_offset]:0;
        C3D_FVUnifSet(GPU_GEOMETRY_SHADER,0,d.point_size/2880.f,d.point_size/(12.f*width),uv*us,uv*vs);
    }
    measured=native_detail_begin(2);
    NativeGeometry*geometry=two_uv?NULL:native_geometry_get(be,count,&d,t!=NULL,us,vs);const Vertex*submitted;float*submitted_uv=NULL;
    if(geometry){submitted=geometry->vertices;draw_indices=geometry->indices;}
    else{
        NATIVE_WORK(7,count);
        reserve_dynamic(count,d.index_count);
        const u16*src=(void*)d.indices;for(unsigned i=0;i<d.index_count;++i)indices[index_count+i]=__builtin_bswap16(src[i]);
        convert_vertices(vertices+vertex_count,be,count,t!=NULL,us,vs);submitted=vertices+vertex_count;draw_indices=indices+index_count;
        if(two_uv){const u32*source=(const void*)layer.uv;float su=(float)t1->w/t1->tex.width,sv=(float)t1->h/t1->tex.height;
            submitted_uv=layer_uv[vertex_count];for(unsigned i=0;i<count;++i){union{u32 u[2];float f[2];}uv={{read32(source+2*i),read32(source+2*i+1)}};
                submitted_uv[2*i]=uv.f[0]*su;submitted_uv[2*i+1]=1.f-uv.f[1]*sv;}
            GSPGPU_FlushDataCache(submitted_uv,count*8);
        }
        vertex_count+=count;index_count+=d.index_count;
    }
    native_detail_end(2,measured);measured=native_detail_begin(3);
    if(two_uv)layered_vertex_pointer(submitted,submitted_uv);else vertex_pointer(submitted);
    static unsigned sampled;
    if(sampled++<5){char text[200];const Vertex*v=submitted;snprintf(text,sizeof(text),"GX draw texture=%08lx fmt=%lu %lux%lu rgba=%.2f %.2f %.2f %.2f uv=%.2f %.2f\n",(unsigned long)d.image,(unsigned long)d.format,(unsigned long)d.w,(unsigned long)d.h,v->c[0],v->c[1],v->c[2],v->c[3],v->t[0],v->t[1]);mp_native_log(text);}
    unsigned env_key=d.layer?8:t?!!d.texture_rgb|(!!d.texture_alpha<<1)|((!!d.texture_alpha&&(d.format==0||d.format==1))<<2):0;
    if(d.layer&&layer.mode){
        for(unsigned i=0;i<3;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv*env=C3D_GetTexEnv(0);
        GPU_TEVSRC texture_source=two_uv?GPU_TEXTURE1:GPU_TEXTURE0;
        unsigned fmt=two_uv?layer.format:d.format;
        GPU_TEVOP_A alpha_source=fmt==0||fmt==1?GPU_TEVOP_A_SRC_R:GPU_TEVOP_A_SRC_ALPHA;
        C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT,GPU_PRIMARY_COLOR,texture_source);C3D_TexEnvFunc(env,C3D_Both,GPU_INTERPOLATE);
        C3D_TexEnvOpAlpha(env,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA,alpha_source);C3D_TexEnvColor(env,layer.tint);
        if(two_uv){
            env=C3D_GetTexEnv(1);C3D_TexEnvSrc(env,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT,0);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
            C3D_TexEnvColor(env,layer.blend*0x01010101u);
            env=C3D_GetTexEnv(2);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT,GPU_PREVIOUS);C3D_TexEnvFunc(env,C3D_Both,GPU_MULTIPLY_ADD);
            alpha_source=d.format==0||d.format==1?GPU_TEVOP_A_SRC_R:GPU_TEVOP_A_SRC_ALPHA;
            C3D_TexEnvOpAlpha(env,alpha_source,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvColor(env,layer.base);
        }
        raster_state.valid|=STATE_TEXENV;
#ifdef MP_SMOKE_TEST
        ++shield_draws[layer.mode==MP_FRAGMENT_TINT];
#endif
    }else if(d.layer){
        for(unsigned i=0;i<3;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv*env=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvColor(env,layer.tint|(layer.blend<<24));
        env=C3D_GetTexEnv(1);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1,0);C3D_TexEnvFunc(env,C3D_RGB,GPU_SUBTRACT);
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT,GPU_TEXTURE1);C3D_TexEnvFunc(env,C3D_Alpha,GPU_INTERPOLATE);
        GPU_TEVOP_A alpha_source=layer.format==0||layer.format==1?GPU_TEVOP_A_SRC_R:GPU_TEVOP_A_SRC_ALPHA;
        C3D_TexEnvOpAlpha(env,alpha_source,GPU_TEVOP_A_SRC_ALPHA,alpha_source);C3D_TexEnvColor(env,layer.alpha<<24);
        env=C3D_GetTexEnv(2);C3D_TexEnvSrc(env,C3D_Both,GPU_PREVIOUS,GPU_PRIMARY_COLOR,0);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
        raster_state.valid|=STATE_TEXENV;
#ifdef MP_SMOKE_TEST
        ++layered_draws;
#endif
    }else if(state_needed(STATE_TEXENV,env_key!=raster_state.texenv)){
        if(raster_state.texenv&8){C3D_TexEnvInit(C3D_GetTexEnv(1));C3D_TexEnvInit(C3D_GetTexEnv(2));}
        C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
        if(env_key&1){C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,0);C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);}
        if(env_key&2){C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,0);C3D_TexEnvFunc(env,C3D_Alpha,GPU_MODULATE);}
        if(env_key&4)C3D_TexEnvOpAlpha(env,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA,0);
    }
    raster_state.texenv=env_key;
    /* Texture slots move during eviction; always bind the current object. */
    if(t1){
        /* Two maps may share image storage but have different wrap modes.
         * Citro3D retains texture-object pointers until draw submission. */
        static C3D_Tex bindings[2];static const GPU_TEXTURE_WRAP_PARAM wraps[]={GPU_CLAMP_TO_EDGE,GPU_REPEAT,GPU_MIRRORED_REPEAT};
        bindings[0]=t->tex;bindings[1]=t1->tex;
        C3D_TexSetWrap(&bindings[0],wraps[d.wrap_s%3],wraps[d.wrap_t%3]);C3D_TexSetWrap(&bindings[1],wraps[layer.wrap_s%3],wraps[layer.wrap_t%3]);
        C3D_TexBind(0,&bindings[0]);C3D_TexBind(1,&bindings[1]);
    }else {
        C3D_TexBind(1,NULL);
        if(t){static const GPU_TEXTURE_WRAP_PARAM wraps[]={GPU_CLAMP_TO_EDGE,GPU_REPEAT,GPU_MIRRORED_REPEAT};C3D_TexSetWrap(&t->tex,wraps[d.wrap_s%3],wraps[d.wrap_t%3]);C3D_TexBind(0,&t->tex);}
        else C3D_TexBind(0,NULL);
    }
    unsigned depth=!!d.depth|((d.func&7)<<1)|((d.color_mask|(d.write?GPU_WRITE_DEPTH:0))<<4);
    if(state_needed(STATE_DEPTH,depth!=raster_state.depth))C3D_DepthTest(d.depth,depth_comparisons[d.func&7],d.color_mask|(d.write?GPU_WRITE_DEPTH:0));
    raster_state.depth=depth;
    if(state_needed(STATE_BLEND,d.blend!=raster_state.blend||d.src!=raster_state.src||d.dst!=raster_state.dst))raster_blend(d.blend,d.src,d.dst);
    raster_state.blend=d.blend;raster_state.src=d.src;raster_state.dst=d.dst;
    trace_draw(frame_number,submitted,draw_indices,d.index_count,t?t->tex.data:NULL,t1?t1->tex.data:NULL,(d.points?1:0)|(d.layer?2:0)|(stereo_active?4:0));
    if(stereo_active){
        float scale=d.convergence>0?mp_native_stereo_depth*MP_STEREO_PIXELS_PER_SLIDER/width:0;
        if(stereo_eye)scale=-scale;
        stereo_shift(scale,-scale*d.convergence);
        draw_count+=draw_with_alpha(&d,0,d.index_count);
        /* Preserve per-eye draw order, but start each material in the eye
         * where the previous one finished. Only one viewport/scissor switch
         * is needed for a pair. Frame setup, copies and clears reset the eye. */
        stereo_eye^=1;
        int offset=stereo_eye?-400:400;clip[1]+=offset;clip[3]+=offset;
        C3D_SetViewport(0,(stereo_eye?0:400)+(400-width)/2,240,width);
        C3D_SetScissor(GPU_SCISSOR_NORMAL,clip[0],clip[1],clip[2],clip[3]);
        memcpy(raster_state.clip,clip,sizeof(clip));
        stereo_shift(-scale,scale*d.convergence);
        draw_count+=draw_with_alpha(&d,0,d.index_count);
#ifdef MP_SMOKE_TEST
        ++stereo_pairs;++stereo_eye_switches;
        if(stereo_order_reference){
            if(stereo_eye){clip[1]+=400;clip[3]+=400;}
            screen_viewport(width);C3D_SetScissor(GPU_SCISSOR_NORMAL,clip[0],clip[1],clip[2],clip[3]);
            memcpy(raster_state.clip,clip,sizeof(clip));stereo_shift(0,0);++stereo_eye_switches;
        }
#endif
        render_vertex_count+=count;
    }else draw_count+=draw_with_alpha(&d,0,d.index_count);
    render_vertex_count+=count;native_detail_end(3,measured);
}
#ifdef MP_SMOKE_TEST
#include "point_verify.h"
#include "cull_verify.h"
#include "blend_verify.h"
#include "raster_state_verify.h"
#include "layered_verify.h"
#include "shield_verify.h"
#include "stereo_verify.h"
#endif
void mp_renderer_end(void){if(!frame_active)return;
#ifdef MP_SMOKE_TEST
    if(stereo_verify)verify_stereo();
    if(native_geometry_lru_validate)native_geometry_check_lru();
    if(layered_verify)verify_layered_material();
    if(shield_verify)verify_shield_material();
    if(raster_state_verify)verify_raster_state();
    if(point_verify)verify_points();
    if(cull_verify)verify_culling();
    if(blend_verify)verify_blending();
    if(efb_verify&2){
        /* Test RGB565 against the completed color frame even when no fighter
         * currently uses refraction. The synthetic texture is never sampled
         * by the game and no game state or source framebuffer is changed. */
        const u32 q[14]={(u32)&efb_verify,0,0,640,480,320,240,4,0,0,0,0,0,1};
        gpu_efb_copy(q);
    }
#endif
    if(stereo_active){eye_output[0]->used=true;eye_output[1]->used=true;}
    flush_dynamic();C3D_FrameEnd(0);frame_active=0;
    if(frame_number%60==0){char text[150];snprintf(text,sizeof(text),"GPU geometry hits=%u bytes=%u dynamic vertices=%u EFB copies=%u\n",native_geometry_hits,native_geometry_bytes,vertex_count,efb_gpu_copies);mp_native_log(text);}
    if(frame_number%60==0){char text[140];snprintf(text,sizeof(text),"Textures=%u bytes=%u barriers=%u stream barriers=%u linear free=%u\n",texture_count,texture_bytes,texture_barriers,stream_barriers,(unsigned)linearSpaceFree());mp_native_log(text);}
    if(frame_number%300==0){char text[100];snprintf(text,sizeof(text),"Texture uploads=%u capacity evictions=%u\n",texture_uploads,texture_evictions);mp_native_log(text);}
    if(frame_number%60==0){char text[100];snprintf(text,sizeof(text),"GPU command peak=%u bytes, capacity=%u, barriers=%u\n",command_peak_bytes,COMMAND_BUFFER_BYTES,command_barriers);mp_native_log(text);}
}
void mp_native_texture_invalidate(void){++texture_generation;}
void mp_native_frame_texture_visibility(void){
#ifdef MP_SMOKE_TEST
    if(framebuffer_range_disable)++texture_generation;
#endif
}
/* The shadow and refraction captures are consumed by the GPU in this frame.
 * Copy the native tile rows with DMA, then crop/quantize on the GPU. Other
 * callers retain the CPU-visible GameCube byte layout below. */
#ifdef MP_SMOKE_TEST
/* Capture the exact source and result of one copy, before either can change.
 * This is deliberately absent from the hardware build. */
static void verify_efb_copy(const u32*q,EfbTexture*slot){
    unsigned bit=q[7]==4?2:1;if(!(efb_verify&bit))return;
    unsigned size=slot->texture.tex.width*slot->texture.tex.height*4;
    u8*source=linearAlloc(240*400*4),*result=linearAlloc(size);
    if(!source||!result)mp_native_panic("EFB verification allocation failed");
    flush_dynamic();
    C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32*)source,GX_BUFFER_DIM(240,400),0);
    unsigned dims=GX_BUFFER_DIM(slot->texture.tex.width,slot->texture.tex.height);
    C3D_SyncDisplayTransfer(slot->target->frameBuf.colorBuf,dims,(u32*)result,dims,
        GX_TRANSFER_IN_FORMAT(q[7]==4?GX_TRANSFER_FMT_RGB565:GX_TRANSFER_FMT_RGBA4));
    bool used=target->used;target->used=false;C3D_FrameEnd(0);MP_AUDIO_TRACE(2,C3D_FrameBegin(0));target->used=used;
    GSPGPU_InvalidateDataCache(source,240*400*4);GSPGPU_InvalidateDataCache(result,size);
    u8*copy=malloc(size>240*400*4?size:240*400*4);
    if(!copy)mp_native_panic("EFB verification CPU allocation failed");
    for(unsigned part=0;part<3;++part){
        char path[100];snprintf(path,sizeof(path),"sdmc:/3ds/melee/efb-%x-%u.bin",q[7],part);
        unsigned n=part==0?14*4:part==1?240*400*4:size;
        const volatile u8*p=part==0?(const u8*)q:part==1?source:result;
        for(unsigned i=0;i<n;++i)copy[i]=p[i];
        FILE*f=fopen(path,"wb");if(!f)mp_native_panic("EFB verification output failed");
        fwrite(copy,1,n,f);fclose(f);
    }
    {char path[100];snprintf(path,sizeof(path),"sdmc:/3ds/melee/efb-%x-width.txt",q[7]);FILE*f=fopen(path,"w");if(f){fprintf(f,"%u\n",render_width);fclose(f);}}
    free(copy);linearFree(source);linearFree(result);efb_verify&=~bit;
}
#endif
static int gpu_efb_copy(const u32*q){
    if(stereo_active)stereo_shift(0,0);
    if(efb_gpu_disable||(q[7]!=0x20&&q[7]!=4)||q[1]+q[3]>640||q[2]+q[4]>480)return 0;
    unsigned w=q[5],h=q[6],pw=8,ph=8;while(pw<w)pw*=2;while(ph<h)ph*=2;
    GPU_TEXCOLOR format=q[7]==4?GPU_RGB565:GPU_RGBA4;EfbTexture*slot=NULL;
    for(unsigned i=0;i<8;++i){EfbTexture*e=&efb_textures[i];
        if(e->texture.frame>=frame_number)continue;
        if(!slot||e->texture.image==q[0])slot=e;
        if(e->texture.image==q[0])break;
    }
    if(!slot)return 0;
    if(!efb_capture.data){if(!C3D_TexInit(&efb_capture,256,512,GPU_RGBA8))return 0;
        memset(efb_capture.data,0,efb_capture.size);C3D_TexFlush(&efb_capture);
        C3D_TexSetFilter(&efb_capture,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&efb_capture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);}
    Texture*t=&slot->texture;
    if(!t->tex.data||t->tex.width!=pw||t->tex.height!=ph||t->tex.fmt!=format){
        if(t->tex.data)C3D_TexDelete(&t->tex);memset(&t->tex,0,sizeof(t->tex));t->image=0;slot->copied_frame=0;
        if(!C3D_TexInitVRAM(&t->tex,pw,ph,format))return 0;
        if(slot->target)C3D_FrameBufTex(&slot->target->frameBuf,&t->tex,GPU_TEXFACE_2D,0);
        else slot->target=C3D_RenderTargetCreateFromTex(&t->tex,GPU_TEXFACE_2D,0,(C3D_DEPTHTYPE){.__i=-1});
        if(!slot->target){C3D_TexDelete(&t->tex);memset(&t->tex,0,sizeof(t->tex));return 0;}
    }
    reserve_dynamic(24,0);
    flush_dynamic();
    /* A framebuffer tile row is 240*8 RGBA bytes; add two texture tiles of
     * padding per row to obtain a valid 256-wide PICA texture. Units are 16 B. */
    C3D_SyncTextureCopy(target->frameBuf.colorBuf,GX_BUFFER_DIM(240*8*4/16,0),efb_capture.data,
        GX_BUFFER_DIM(240*8*4/16,16*8*4/16),240*400*4,8);
    raster_state_invalidate();C3D_FrameDrawOn(slot->target);bind_program(0);C3D_CullFace(GPU_CULL_NONE);C3D_TexBind(0,&efb_capture);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    if(q[7]==0x20){C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_R,0,0);C3D_TexEnvOpAlpha(env,GPU_TEVOP_A_SRC_R,0,0);}
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    float rects[4][4]={{0,0,w,h},{w,0,pw,h},{0,h,w,ph},{w,h,pw,ph}};unsigned corner[]={0,1,2,0,2,3},count=0;
    for(unsigned r=0;r<4;++r){float*b=rects[r];if(b[0]>=b[2]||b[1]>=b[3])continue;
        float xy[4][2]={{b[0],b[1]},{b[0],b[3]},{b[2],b[3]},{b[2],b[1]}};
        for(unsigned i=0;i<6;++i){Vertex*v=&vertices[vertex_count+count++];memset(v,0,sizeof(*v));
            float x=xy[corner[i]][0],y=xy[corner[i]][1];v->p[0]=2*x/pw-1;v->p[1]=1-2*y/ph;v->p[2]=-.5f;v->p[3]=1;v->n[3]=-1;
            for(int j=0;j<4;++j)v->c[j]=1;
            if(b[0]>=w)x=w-.5f;if(b[1]>=h)y=h-.5f;
            v->t[0]=(240-(q[2]+(y-.5f)*q[4]/h)*.5f-.0001f)/256.f;
            v->t[1]=1-((400-render_width)*.5f+(q[1]+(x-.5f)*q[3]/w)*(render_width/640.f))/512.f-.000001f;
        }
    }
    vertex_base(0);C3D_DrawArrays(GPU_TRIANGLES,vertex_count,count);vertex_count+=count;render_vertex_count+=count;++draw_count;
    C3D_TexSetFilter(&t->tex,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&t->tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    t->image=q[0];t->w=w;t->h=h;t->format=q[7]==4?4:0;t->source_bytes=mp_texture_source_bytes(t->format,w,h);t->frame=frame_number;slot->copied_frame=frame_number;
#ifdef MP_SMOKE_TEST
    verify_efb_copy(q,slot);
#endif
    C3D_FrameDrawOn(target);screen_viewport(render_width);for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    ++efb_gpu_copies;return 1;
}
static void efb_copy_cpu_reference(const u32*q,const u32*source,unsigned screen_width){
    unsigned w=q[5],h=q[6],fmt=q[7],bw=4,bh=4,bytes=32;
    if(fmt==0||fmt==0x20){bw=bh=8;}
    else if(fmt==1||fmt==2||fmt==0x22||fmt==0x27||fmt==0x28||fmt==0x29||fmt==0x2a)bw=8;
    else if(fmt==6)bytes=64;
    unsigned pitch=(w+bw-1)/bw;u8*dst=(void*)q[0];
    memset(dst,0,pitch*((h+bh-1)/bh)*bytes);
    unsigned source_x[1024],tile_x[1024];
    for(unsigned x=0;x<w;++x){unsigned sx=(400-screen_width)/2+(q[1]+x*q[3]/w)*screen_width/640;if(sx>=400)sx=399;source_x[x]=sx*240;tile_x[x]=(x/bw)*bytes;}
    for(unsigned y=0;y<h;++y){
        unsigned sy=(q[2]+y*q[4]/h)*240/480;if(sy>=240)sy=239;
        const u32*row=source+239-sy;u8*tile_row=dst+(y/bh)*pitch*bytes;unsigned row_index=(y&(bh-1))*bw;
        for(unsigned x=0;x<w;++x){
        u32 color=row[source_x[x]];unsigned r=color>>24,g=(color>>16)&255,b=(color>>8)&255,a=color&255;
        unsigned k=row_index+(x&(bw-1));u8*p=tile_row+tile_x[x];
        unsigned intensity=(77*r+150*g+29*b)>>8,v=0;
        switch(fmt){
        case 0:case 0x20:v=(fmt==0?intensity:r)>>4;p[k/2]|=v<<((k&1)?0:4);continue;
        case 1:p[k]=intensity;continue;
        case 2:case 0x22:p[k]=(a&0xf0)|((fmt==2?intensity:r)>>4);continue;
        case 3:case 0x23:v=(a<<8)|(fmt==3?intensity:r);break;
        case 4:v=((r>>3)<<11)|((g>>2)<<5)|(b>>3);break;
        case 5:v=a>=224?0x8000|((r>>3)<<10)|((g>>3)<<5)|(b>>3):((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4);break;
        case 6:p[2*k]=a;p[2*k+1]=r;p[32+2*k]=g;p[32+2*k+1]=b;continue;
        case 0x27:p[k]=a;continue;case 0x28:p[k]=r;continue;case 0x29:p[k]=g;continue;case 0x2a:p[k]=b;continue;
        case 0x2b:v=(g<<8)|r;break;case 0x2c:v=(b<<8)|g;break;
        }p[2*k]=v>>8;p[2*k+1]=v;
    }}
}
/* Copy the completed PICA render target back into the tiled, big-endian
 * texture layout expected by HSD shadow and refraction passes. */
void mp_native_efb_copy(const u32*request)
{
    raster_state_invalidate();
    u32 q[14];for(unsigned i=0;i<14;++i)q[i]=read32(request+i);
    unsigned w=q[5],h=q[6],fmt=q[7];
    if(!q[0]||!w||!h||w>1024||h>1024)mp_native_panic("Invalid GX framebuffer copy extent");
    switch(fmt){case 0:case 1:case 2:case 3:case 4:case 5:case 6:case 0x20:
        case 0x22:case 0x23:case 0x27:case 0x28:case 0x29:case 0x2a:case 0x2b:case 0x2c:break;
        default:mp_native_panic("Unsupported GX framebuffer copy format");}
    for(unsigned i=0;i<8;++i)if(mp_texture_source_overlap(efb_textures[i].texture.image,efb_textures[i].texture.source_bytes,0,0,q[0],mp_texture_source_bytes(fmt,w,h)))efb_textures[i].copied_frame=0;
    if(q[13]&&gpu_efb_copy(q)){framebuffer_texture_visibility(q);goto clear;}
#ifdef MP_SMOKE_TEST
    ++framebuffer_cpu_calls;framebuffer_cpu_pixels+=(unsigned long long)w*h;
#endif
    if(!efb_pixels)efb_pixels=linearAlloc(400*240*4);
    if(!efb_pixels)mp_native_panic("Framebuffer readback allocation failed");
    flush_dynamic();
    C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),efb_pixels,GX_BUFFER_DIM(240,400),0);
    /* Submit without presenting: this is a render pass inside a game frame.
     * FrameBegin waits for both the draw and transfer before CPU conversion. */
    bool used=target->used;target->used=false;C3D_FrameEnd(0);MP_AUDIO_TRACE(2,C3D_FrameBegin(0));target->used=used;
    GSPGPU_InvalidateDataCache(efb_pixels,400*240*4);
    unsigned converted=0;
#ifdef MP_SMOKE_TEST
    unsigned verify=efb_rgb565_validate&&fmt==4,start=verify?mp_native_ticks():0;
#endif
    if(fmt==4){
#ifdef MP_SMOKE_TEST
        if(!efb_rgb565_disable)
#endif
        converted=mp_efb_rgb565((void*)q[0],efb_pixels,w,h,q[1],q[2],q[3],q[4],render_width);
    }
    if(!converted)efb_copy_cpu_reference(q,efb_pixels,render_width);
#ifdef MP_SMOKE_TEST
    if(verify){
        unsigned primary_ticks=mp_native_ticks()-start,size=((w+3)/4)*((h+3)/4)*32;
        void*other=memalign(32,size);if(!other)mp_native_panic("Framebuffer comparison allocation failed");
        u32 otherq[14];memcpy(otherq,q,sizeof(otherq));otherq[0]=(u32)other;
        start=mp_native_ticks();
        if(converted)efb_copy_cpu_reference(otherq,efb_pixels,render_width);
        else if(!mp_efb_rgb565(other,efb_pixels,w,h,q[1],q[2],q[3],q[4],render_width))mp_native_panic("Framebuffer comparison extent rejected");
        unsigned other_ticks=mp_native_ticks()-start;
        if(memcmp((void*)q[0],other,size))mp_native_panic("RGB565 framebuffer copy differs from original conversion");
        efb_rgb565_ticks[converted?0:1]+=primary_ticks;efb_rgb565_ticks[converted?1:0]+=other_ticks;
        ++efb_rgb565_checks;efb_rgb565_pixels+=w*h;free(other);
    }
#endif
    framebuffer_texture_visibility(q);
clear:
    if(q[8]&&(q[11]||q[12])){
        if(stereo_active){stereo_shift(0,0);screen_viewport(render_width);}
        /* Copy-clear uses its own source rectangle, independently of GX's
         * raster scissor. The next normal draw reapplies that scissor. */
        C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
        bind_program(0);C3D_CullFace(GPU_CULL_NONE);
        reserve_dynamic(6,0);
        float x0=q[1]/320.f-1,y0=1-q[2]/240.f,x1=(q[1]+q[3])/320.f-1,y1=1-(q[2]+q[4])/240.f;
        float z=q[10]/16777215.f-1;unsigned corner[]={0,1,2,0,2,3};
        float xy[4][2]={{x0,y0},{x0,y1},{x1,y1},{x1,y0}};
        for(int i=0;i<6;++i){Vertex*v=&vertices[vertex_count+i];memset(v,0,sizeof(*v));v->n[3]=-1;v->p[0]=xy[corner[i]][1];v->p[1]=-xy[corner[i]][0];v->p[2]=z;v->p[3]=1;for(int j=0;j<4;++j)v->c[j]=((q[9]>>(24-8*j))&255)/255.f;}
        for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
        C3D_DepthTest(true,GPU_ALWAYS,q[11]|(q[12]?GPU_WRITE_DEPTH:0));C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        vertex_base(0);C3D_DrawArrays(GPU_TRIANGLES,vertex_count,6);
        if(stereo_active){C3D_SetViewport(0,(400-render_width)/2,240,render_width);C3D_DrawArrays(GPU_TRIANGLES,vertex_count,6);screen_viewport(render_width);++draw_count;render_vertex_count+=6;}
        vertex_count+=6;render_vertex_count+=6;++draw_count;
    }
}
void mp_renderer_counts(unsigned*v,unsigned*d){*v=render_vertex_count;*d=draw_count;}
void mp_renderer_exit(void){C3D_Fini();for(unsigned i=0;i<8;++i)if(efb_textures[i].texture.tex.data)C3D_TexDelete(&efb_textures[i].texture.tex);if(efb_capture.data)C3D_TexDelete(&efb_capture);for(unsigned i=0;i<NATIVE_GEOMETRY_ENTRIES;++i)native_geometry_free(&native_geometry[i]);for(unsigned i=0;i<texture_count;++i)C3D_TexDelete(&textures[i].tex);linearFree(efb_pixels);linearFree(indices);linearFree(vertices);linearFree(layer_uv);shaderProgramFree(&point_program);shaderProgramFree(&program);shaderProgramFree(&dual_program);shaderProgramFree(&stereo_program);shaderProgramFree(&stereo_point_program);shaderProgramFree(&stereo_dual_program);DVLB_Free(dvlb);DVLB_Free(dual_dvlb);DVLB_Free(stereo_dvlb);DVLB_Free(stereo_dual_dvlb);}
