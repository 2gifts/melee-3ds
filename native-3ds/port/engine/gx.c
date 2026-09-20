#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include <math.h>
#include "native.h"
volatile unsigned shade_legacy_clamp;
#define MP_SHADE_DEFERRED_CLAMPS (!shade_legacy_clamp)
#include "gx_shade.h"
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
#include "clamped_shade.h"
#endif
#include "gpu_vertex.h"
extern int mp_be_memcmp_block(const void*,const void*,size_t);
#define MP_BOUNDS_KEY_COMPARE mp_be_memcmp_block
#include "geometry_bounds.h"
#include "vertex_decode.h"
#include "primitive_expand.h"
#include "layered_material.h"
#include "shield_material.h"
#include "stereo_config.h"
#include "draw_flags.h"
/* Constant-size matrix/color copies can be inlined safely in BE8. Any
 * remaining library call is redirected by the object converter. */
#undef memcpy
#undef memset
#define memcpy(d,s,n) __builtin_memcpy(d,s,n)
#define memset(d,c,n) __builtin_memset(d,c,n)
#ifdef MP_MATERIAL_PROGRAM_TEST
#include "material_program.h"
#endif

typedef MPGPUVertex RenderVertex;
typedef struct {u32 image,width,height,format,palette,palette_format,palette_count;
    u32 depth_test,depth_write,depth_func,blend,src,dst,cull,alpha_ref,color_mask,texture_rgb,texture_alpha,wrap_s,wrap_t,gpu,indices,index_count,geometry_id,scissor[4],points,point_size,point_offset,screen_width,layer;float convergence;} DrawState;
_Static_assert(sizeof(DrawState)==34*4,"GX draw bridge layout");
_Static_assert(__builtin_offsetof(DrawState,gpu)==20*4 && __builtin_offsetof(DrawState,indices)==21*4 &&
    __builtin_offsetof(DrawState,index_count)==22*4 && __builtin_offsetof(DrawState,geometry_id)==23*4 &&
    __builtin_offsetof(DrawState,layer)==32*4,"Owned draw pointer offsets");
extern void mp_platform_submit(const RenderVertex*,unsigned,const DrawState*);
typedef struct {GXCompCnt count;GXCompType type;u8 frac;} Format;
static GXAttrType descriptors[26];
static Format formats[8][26];
static const u8 *arrays[26];
static u8 strides[26];
static Mtx position_mtx[64],normal_mtx[64],texture_mtx[64];
static Mtx44 projection;
static GXProjectionType projection_type;
static unsigned current_mtx;
static GXColor material[2]={{255,255,255,255},{255,255,255,255}},ambient[2];
static GXColor tev_color[4],konst_color[4];
static GXTexObj textures[8];
static GXTlutObj palettes[32];
static GXLightObj lights[8];
static DrawState draw={.depth_test=1,.depth_write=1,.depth_func=GX_LEQUAL,.alpha_ref=GX_ALWAYS|(GX_ALWAYS<<13),.scissor={0,0,640,480},.screen_width=320};
static unsigned active_texture=0,active_coord,texgens,num_chans,num_stages;
static unsigned left_capture;
static u32 tev_configuration[16][30];
static u32 texgen_configuration[8][5];
static u32 channel_configuration[6][6],alpha_configuration[5];
static u32 color_update=1,alpha_update=1,dither,dst_alpha,zcomp_location;
static float viewport[6];static u32 scissor[4];
static RenderVertex batch[384];static unsigned batch_count,batch_points,batch_lines;
static u16 batch_indices[1152];static unsigned batch_index_count;
static unsigned first,previous[2],primitive_count;
static GXPrimitive primitive;static GXVtxFmt vertex_format;
static unsigned remaining,vertex_bytes,fifo_size;static u8 fifo[512];
typedef struct {unsigned kind,offset,mode,type,count,stride;float scale;const u8*array;} VertexField;
#define VERTEX_FIELDS 6
static VertexField fields[VERTEX_FIELDS];
static unsigned field_count;
static unsigned layer_active,tint_active,layer_coord,layer_uv_attr,primary_uv_attr;
static VertexField layer_field;
static MPTextureLayer texture_layer;
static float batch_layer_uv[384][2];
static int reuse_list_state;
static unsigned texture_used_rgb,texture_used_alpha;
static unsigned raster_channels;
static int flat_shading;static float flat_color[4];
static MPShadePlan shade_plan;
typedef struct {u32 stages,flat;GXColor material[2],colors[4],konst[4];float alpha[2];} ShadeKey;
typedef struct {u32 stamp,hash;ShadeKey key;u32 config[16][30];float flat_color[4];MPShadePlan plan;
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
    MPClampedPlan clamped;
#endif
} ShadeCache;
static ShadeCache shade_cache[256];
#ifdef MP_MATERIAL_PROGRAM_TEST
typedef struct {u32 program;ShadeKey key;} ShadeBindingKey;
typedef struct {u32 stamp,hash;ShadeBindingKey key;float flat_color[4];MPShadePlan plan;MPClampedPlan clamped;} ShadeBinding;
_Static_assert(sizeof(ShadeBindingKey)==60,"Packed material binding key");
static MPMaterialPrograms material_programs;
static ShadeBinding material_bindings[1024];
static unsigned material_bindings_reset;
volatile unsigned shade_program_disable=1;
unsigned shade_program_hits,shade_program_misses,shade_binding_hits,shade_binding_misses,shade_program_checks;
#endif
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
static MPClampedPlan clamped_plan;
#ifdef MP_RENDER_REWORK_TEST
static MPGeometryPlaneCache geometry_planes;
volatile unsigned geometry_planes_disable,geometry_planes_validate;
unsigned geometry_planes_checks;
static int geometry_outside(const MPGeometryBounds*b,const MPGPUUniforms*u,float strength,float convergence){
    if(geometry_planes_disable)return mp_bounds_outside_stereo(b,u,strength,convergence);
    int result=mp_bounds_outside_cached(&geometry_planes,b,u,strength,convergence);
#ifdef MP_RENDER_REWORK_TEST
    if(geometry_planes_validate){++geometry_planes_checks;
        if(result!=mp_bounds_outside_stereo(b,u,strength,convergence))HSD_Panic(__FILE__,__LINE__,"Cached clip planes disagree");}
#endif
    return result;
}
#else
/* Exact-key clipping reuse lost to the ordinary calculation in ARM tests.
 * Keep it available only for reproducing that development comparison. */
#define geometry_outside mp_bounds_outside_stereo
#endif

#ifdef MP_CLAMPED_SHADE_TEST
volatile unsigned shade_clamped_disable=1,shade_clamped_validate;
unsigned shade_clamped_checks;
#else
#define shade_clamped_disable 0
#endif
unsigned shade_clamped_hits,shade_clamped_vertices;
#endif
static unsigned shade_clock,shade_hits,shade_misses,shade_checks;
static volatile unsigned shade_validate;
static unsigned primitive_vertices[8];
static unsigned point_callers[16][2];
static volatile unsigned point_list[5];
static RenderVertex line_previous;
static unsigned expanded_points,expanded_lines;
static MPGPUUniforms gpu_uniforms;
static int gpu_shading;
/* Largest fallback list, retained for reading through the development
 * debugger. It identifies unsupported material algebra without logging in
 * the per-vertex loop. */
unsigned mp_fallback_vertex_count,mp_fallback_reason,mp_fallback_stages;
unsigned mp_fallback_tev[16][30];
unsigned char mp_fallback_colors[16],mp_fallback_konst[16];
float mp_fallback_alpha[2];
static unsigned gpu_reject_reason;
static volatile unsigned gpu_disable;
volatile unsigned mp_gx_profile;
unsigned mp_gx_detail_ticks[3],mp_gx_detail_count[3];
static unsigned detail_sequence[3];
static u32 detail_begin(unsigned phase){return mp_gx_profile&&!(detail_sequence[phase]++&15)?mp_platform_ticks():0;}
static void detail_end(unsigned phase,u32 start){if(start){mp_gx_detail_ticks[phase]+=mp_platform_ticks()-start;++mp_gx_detail_count[phase];}}
static unsigned gpu_vertices;
/* Keep decoded GPU input across frames. The original list and every indexed
 * source byte range are compared before reuse, so morphing or reused heaps
 * cannot make this cache return stale geometry. Matrix palettes and lighting
 * remain live uniforms and are deliberately not baked into these vertices. */
typedef struct {
    VertexField fields[VERTEX_FIELDS];unsigned count,stride,flat,material_source,alpha_source;
    GXColor material;float flat_color[4];u32 texgen[5];Mtx texmatrix[2];
} GeometryKey;
typedef struct GeometryChunk {struct GeometryChunk*next;unsigned count,indices,bytes,id,points;MPGeometryBounds bounds;} GeometryChunk;
volatile unsigned mp_geometry_cull=1;
unsigned mp_geometry_cull_checks,mp_geometry_cull_draws,mp_geometry_cull_vertices;
typedef struct GeometrySource {struct GeometrySource*next;const u8*source;u8*copy;unsigned size,refs,epoch,changed;const void*base;} GeometrySource;
typedef struct {
    const void*list;unsigned bytes,stamp,early_hint;GeometryKey key;
    GeometrySource*source[VERTEX_FIELDS+1];GeometryChunk*first,*last;
} GeometryCache;
#define GEOMETRY_SETS 256
#define GEOMETRY_WAYS 8
#define GEOMETRY_ENTRIES (GEOMETRY_SETS*GEOMETRY_WAYS)
static GeometryCache geometry_cache[GEOMETRY_ENTRIES],*geometry_record;
static GeometryCache*geometry_set(const void*list){unsigned p=(u32)list;return geometry_cache+(((p>>5)^(p>>14))&(GEOMETRY_SETS-1))*GEOMETRY_WAYS;}
static unsigned geometry_bytes,geometry_clock,geometry_hits,geometry_misses;
static GeometrySource*geometry_sources[1024];
static unsigned geometry_epoch=1;
unsigned geometry_source_compares,geometry_source_reuses,geometry_source_bytes;
/* Multiple meshes index overlapping parts of the same vertex array. Reuse
 * an already captured containing range, still comparing every source byte
 * after invalidation. A changed shared range invalidates all its users. */
volatile unsigned geometry_range_share=1;
static unsigned geometry_range_previous=1;
unsigned geometry_range_shares;
volatile unsigned geometry_key_block=1;
volatile unsigned geometry_source_compare_all;
static volatile unsigned geometry_block_compare=1,geometry_compare_validate;
unsigned geometry_compare_checks;
extern int mp_be_memcmp_block(const void*,const void*,size_t);
static int geometry_table_compare(const void*a,const void*b,unsigned n){
    return geometry_key_block?mp_be_memcmp_block(a,b,n):memcmp(a,b,n);
}
static unsigned source_bucket(const void*p){unsigned a=(u32)p;return((a>>4)^(a>>14))&1023;}
void mp_gx_invalidate_sources(void){
    if(!++geometry_epoch){geometry_epoch=1;for(unsigned i=0;i<1024;++i)for(GeometrySource*s=geometry_sources[i];s;s=s->next)s->epoch=0;}
}
static void source_release(GeometrySource*s){
    if(!s||--s->refs)return;
    GeometrySource**link=&geometry_sources[source_bucket(s->base)];
    while(*link!=s)link=&(*link)->next;*link=s->next;
    geometry_bytes-=sizeof(*s)+s->size+3;
    mp_platform_free(s->copy-((u32)s->source&3));mp_platform_free(s);
}
volatile unsigned geometry_source_grow=1;
unsigned geometry_source_growths;
static unsigned geometry_next_id;
/* Nonzero geometry IDs refer to immutable GeometryChunk payloads. Native
 * readers may borrow them until the retirement barrier below. Keep this
 * versioned contract so an old engine archive cannot enable zero-copy. */
const unsigned mp_geometry_borrow_contract=1;
extern void mp_platform_geometry_retire(void);
static volatile unsigned geometry_validate;
static unsigned geometry_checks;
static GeometryChunk*geometry_check;
static int geometry_checking;
static unsigned geometry_min[VERTEX_FIELDS],geometry_max[VERTEX_FIELDS];
static volatile unsigned geometry_budget=6*1024*1024;
#define GEOMETRY_BUDGET geometry_budget
/* Configured before the engine boots; lower-memory launch environments keep
 * the original cache ceiling. The cache remains bounded and allocation
 * failure retains the ordinary uncached rendering path. */
void mp_gx_configure_cache(unsigned bytes){geometry_budget=bytes>=12*1024*1024?12*1024*1024:6*1024*1024;}
static void geometry_clear(GeometryCache*e){
    for(unsigned i=0;i<VERTEX_FIELDS+1;++i)source_release(e->source[i]);
    if(e->first)mp_platform_geometry_retire();
    GeometryChunk*p=e->first;while(p){GeometryChunk*next=p->next;geometry_bytes-=p->bytes;mp_platform_free(p);p=next;}
    memset(e,0,sizeof(*e));
}
static void*geometry_alloc(unsigned bytes){
    if(bytes>GEOMETRY_BUDGET)return NULL;
    while(geometry_bytes+bytes>GEOMETRY_BUDGET){GeometryCache*old=NULL;
        for(unsigned i=0;i<GEOMETRY_ENTRIES;++i){GeometryCache*e=&geometry_cache[i];if(e!=geometry_record&&e->list&&(!old||e->stamp<old->stamp))old=e;}
        if(!old)return NULL;geometry_clear(old);
    }
    void*p=mp_platform_alloc(bytes);if(p)geometry_bytes+=bytes;return p;
}
static int source_valid(GeometrySource*s){
    if(!s||s->changed)return !s;
    if(s->epoch!=geometry_epoch||geometry_source_compare_all){
        s->changed=(geometry_block_compare?mp_be_memcmp_block(s->copy,s->source,s->size):memcmp(s->copy,s->source,s->size))!=0;s->epoch=geometry_epoch;
        if(geometry_compare_validate){
            int reference=memcmp(s->copy,s->source,s->size)!=0;
            int block=mp_be_memcmp_block(s->copy,s->source,s->size)!=0;
            if(reference!=block)HSD_Panic(__FILE__,__LINE__,"Block comparison differs from original byte comparison");
            ++geometry_compare_checks;
        }
        ++geometry_source_compares;geometry_source_bytes+=s->size;
    }else ++geometry_source_reuses;
    return !s->changed;
}
static GeometrySource*source_acquire(const void*base,const void*data,unsigned size){
    unsigned bucket=source_bucket(base);
    for(GeometrySource*s=geometry_sources[bucket];s;s=s->next)
        if(s->base==base&&(geometry_range_share?
           (u32)data>=(u32)s->source&&(u32)data-(u32)s->source<=s->size&&size<=s->size-((u32)data-(u32)s->source):
           s->source==data&&s->size==size)&&source_valid(s)){
            if(s->source!=data||s->size!=size)++geometry_range_shares;
            ++s->refs;return s;
        }
    /* Meshes often visit successively larger prefixes of the same array.
     * Grow a stable shared descriptor instead of retaining/checking every
     * smaller overlapping snapshot on every frame. Existing owners are safe
     * only after their old prefix has passed the usual content comparison.
     * Pin before allocation: cache-pressure eviction may release all owners. */
    if(geometry_range_share&&geometry_source_grow){
        GeometrySource*grow=NULL;
        for(GeometrySource*s=geometry_sources[bucket];s;s=s->next)
            if(s->base==base&&s->source==data&&s->size<size&&
               (!grow||s->size>grow->size)&&source_valid(s))grow=s;
        if(grow){
            ++grow->refs;
            u8*allocation=geometry_alloc(size+3);
            if(!allocation){source_release(grow);return NULL;}
            u8*copy=allocation+((u32)data&3);memcpy(copy,data,size);
            geometry_bytes-=grow->size+3;mp_platform_free(grow->copy-((u32)grow->source&3));
            grow->copy=copy;grow->size=size;grow->epoch=geometry_epoch;
            ++geometry_source_growths;++geometry_range_shares;return grow;
        }
    }
    GeometrySource*s=geometry_alloc(sizeof(*s));if(!s)return NULL;
    u8*allocation=geometry_alloc(size+3);
    if(!allocation){geometry_bytes-=sizeof(*s);mp_platform_free(s);return NULL;}
    s->source=data;s->base=base;s->size=size;s->refs=1;s->epoch=geometry_epoch;s->changed=0;
    s->copy=allocation+((u32)data&3);memcpy(s->copy,data,size);
    s->next=geometry_sources[bucket];geometry_sources[bucket]=s;return s;
}
static void geometry_cancel(void){if(geometry_record){GeometryCache*e=geometry_record;geometry_record=NULL;geometry_clear(e);}}
static void geometry_capture(const RenderVertex*v,unsigned count,const u16*indices,unsigned n){
    if(geometry_checking){GeometryChunk*c=geometry_check;
        if(!c||count!=c->count||n!=c->indices||batch_points!=c->points||memcmp(v,c+1,count*sizeof(*v))||memcmp(indices,(u8*)(c+1)+count*sizeof(*v),n*sizeof(*indices)))
            HSD_Panic(__FILE__,__LINE__,"Cached geometry differs from fresh GX decoding");
        geometry_check=c->next;++geometry_checks;
    }
    if(!geometry_record)return;
    unsigned bytes=sizeof(GeometryChunk)+count*sizeof(*v)+n*sizeof(*indices);
    GeometryChunk*p=geometry_alloc(bytes);if(!p){geometry_cancel();return;}
    p->next=NULL;p->count=count;p->indices=n;p->bytes=bytes;p->points=batch_points;p->id=++geometry_next_id;if(!p->id)p->id=++geometry_next_id;
    mp_bounds_build(&p->bounds,v,count);
    memcpy(p+1,v,count*sizeof(*v));memcpy((u8*)(p+1)+count*sizeof(*v),indices,n*sizeof(*indices));
    if(geometry_record->last)geometry_record->last->next=p;else geometry_record->first=p;geometry_record->last=p;
}
static void geometry_key(GeometryKey*k){
    memset(k,0,sizeof(*k));memcpy(k->fields,fields,field_count*sizeof(*fields));k->count=field_count;k->stride=vertex_bytes;
    /* Vertex colors come from the source array. Register material colors and
     * source selection are live GPU constants, including animated alpha. */
    u32*g=texgen_configuration[active_coord&7];memcpy(k->texgen,g,20);
    if(g[2]!=GX_IDENTITY&&g[2]/3<64)memcpy(k->texmatrix[0],g[2]<30?position_mtx[g[2]/3]:texture_mtx[g[2]/3],g[0]==GX_TG_MTX2x4?32:sizeof(Mtx));
    if(g[4]!=GX_PTIDENTITY&&g[4]/3<64)memcpy(k->texmatrix[1],texture_mtx[g[4]/3],sizeof(Mtx));
}
static GeometryCache*geometry_find(const void*list,unsigned bytes,const GeometryKey*k){
    ++geometry_clock;
    GeometryCache*set=geometry_set(list);
    for(unsigned i=0;i<GEOMETRY_WAYS;++i){GeometryCache*e=&set[i];
        if(e->list!=list||e->bytes!=bytes||geometry_table_compare(&e->key,k,sizeof(*k)))continue;
        int valid=1;for(unsigned j=0;j<VERTEX_FIELDS+1;++j)if(!source_valid(e->source[j])){valid=0;break;}
        if(valid){e->stamp=geometry_clock;++geometry_hits;return e;}geometry_clear(e);
    }
    ++geometry_misses;return NULL;
}
static void geometry_start(const void*list,unsigned bytes,const GeometryKey*k){
    GeometryCache*e=NULL,*set=geometry_set(list);
    for(unsigned i=0;i<GEOMETRY_WAYS;++i)if(!set[i].list){e=&set[i];break;}
    if(!e){e=set;for(unsigned i=1;i<GEOMETRY_WAYS;++i)if(set[i].stamp<e->stamp)e=&set[i];geometry_clear(e);}
    e->list=list;e->bytes=bytes;e->stamp=geometry_clock;e->key=*k;
    geometry_record=e;for(unsigned i=0;i<VERTEX_FIELDS;++i){geometry_min[i]=~0u;geometry_max[i]=0;}
}
static void geometry_finish(void){
    GeometryCache*e=geometry_record;if(!e)return;
    if(!e->first){geometry_cancel();return;}
    for(unsigned i=0;i<=field_count;++i){
        if(i&&geometry_min[i-1]==~0u)continue;
        /* Capture array prefixes so different mesh index windows can share
         * a containing snapshot. Every byte is inside the source array up
         * to an actually referenced element; no allocation boundary is
         * rounded outward. Unused changing bytes only cause safe eviction. */
        unsigned begin=i&&!geometry_range_share?geometry_min[i-1]:0;
        const void*data=i?fields[i-1].array+begin:e->list;
        unsigned n=i?geometry_max[i-1]-begin:e->bytes;
        if(!n)continue;e->source[i]=source_acquire(i?fields[i-1].array:e->list,data,n);
        if(!e->source[i]){geometry_cancel();return;}
    }
    geometry_record=NULL;
}
static unsigned flat_vertices,affine_vertices,slow_vertices;
static u32 profile_lists,profile_submit,profile_copy;
extern u32 mp_profile_audio;
static GXFifoObj fifo_object;static GXDrawDoneCallback done_callback;
static u32 misc[8],line_width=6,point_size=6,line_offset,point_offset,tex_offsets[8][2],indirect[16][9],ind_order[4][2],ind_scale[4][2],ind_count;
static float ind_matrix[12][2][3];static s8 ind_exponent[12];static u32 swap_table[4][4],clamp_mode[4];
static GXColorS10 color_s10[4];static u32 pixel_format,depth_format,field_mode,half_aspect,ztexture[3];
static struct{GXFogType type;float start,end,near,far;GXColor color;GXBool range;u16 center;GXFogAdjTable table;}fog;
static struct{u16 src[4],dst[2];float scale;GXColor color;u32 depth,clamp,gamma;u8 samples[12][2],filter[7];}copy;
static struct{u16 src[4],width,height;GXTexFmt format;GXBool mip;}tex_copy;
#define MODE(tv,filter) {tv,640,480,480,40,0,640,480,1,0,0,{{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}},filter}
GXRenderModeObj GXNtsc480IntDf={0,640,480,480,40,0,640,480,1,0,0,{{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}},{8,8,10,12,10,8,8}};
GXRenderModeObj GXNtsc480Int={0,640,480,480,40,0,640,480,1,0,0,{{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}},{0,0,21,22,21,0,0}};
GXRenderModeObj GXNtsc480Prog={2,640,480,480,40,0,640,480,0,0,0,{{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}},{0,0,21,22,21,0,0}};

static unsigned short_be(const u8*p){return (p[0]<<8)|p[1];}
GXFifoObj*GXInit(void*base,u32 size){memset(&fifo_object,0,sizeof(fifo_object));for(unsigned i=0;i<64;++i){memset(position_mtx[i],0,sizeof(Mtx));position_mtx[i][0][0]=position_mtx[i][1][1]=position_mtx[i][2][2]=1;}copy.src[2]=640;copy.src[3]=480;copy.scale=1;return &fifo_object;}
void GXSetMisc(GXMiscToken t,u32 value){if(t<8)misc[t]=value;}
void GXInvalidateVtxCache(void){
    /* Switch only at the frame's original GX invalidation point, for a
     * controlled same-encounter comparison through the development debugger. */
    if(geometry_range_previous!=!!geometry_range_share){
        geometry_cancel();for(unsigned i=0;i<GEOMETRY_ENTRIES;++i)geometry_clear(&geometry_cache[i]);
        geometry_range_previous=!!geometry_range_share;
    }
    mp_gx_invalidate_sources();
}
void GXInvalidateTexAll(void){extern void mp_platform_texture_invalidate(void);mp_platform_texture_invalidate();}
/* Frame setup itself does not change image data. CPU cache-visibility calls
 * and framebuffer copies track the writes before textures are reused. */
void mp_gx_frame_texture_visibility(void){extern void mp_platform_frame_texture_visibility(void);mp_platform_frame_texture_visibility();}
void GXSetTevClampMode(int a,int b){if((unsigned)a<4)clamp_mode[a]=b;}
void GXSetLineWidth(u8 w,GXTexOffset x){line_width=w;line_offset=x;}
void GXSetPointSize(u8 w,GXTexOffset x){point_size=w;point_offset=x;}
void GXEnableTexOffsets(GXTexCoordID id,u8 a,u8 b){if(id<8){tex_offsets[id][0]=a;tex_offsets[id][1]=b;}}
u32 GXGetTexBufferSize(u16 w,u16 h,u32 fmt,u8 mip,u8 lod){unsigned bw=4,bh=4,bytes=32,total=0;if(fmt==0||fmt==8||fmt==14){bw=bh=8;}else if(fmt==1||fmt==2||fmt==9)bw=8;else if(fmt==6||fmt==0x16)bytes=64;do{total+=((w+bw-1)/bw)*((h+bh-1)/bh)*bytes;if(!mip||!lod--||(w==1&&h==1))break;w=w>1?w/2:1;h=h>1?h/2:1;}while(1);return total;}
u16 GXGetTexObjWidth(const GXTexObj*o){return o->dummy[1]>>16;}u16 GXGetTexObjHeight(const GXTexObj*o){return o->dummy[1];}GXTexFmt GXGetTexObjFmt(const GXTexObj*o){return o->dummy[2];}
void GXGetViewportv(float*out){memcpy(out,viewport,sizeof(viewport));}
void GXProject(float x,float y,float z,float m[3][4],float*p,float*v,float*sx,float*sy,float*sz){float ex=m[0][0]*x+m[0][1]*y+m[0][2]*z+m[0][3],ey=m[1][0]*x+m[1][1]*y+m[1][2]*z+m[1][3],ez=m[2][0]*x+m[2][1]*y+m[2][2]*z+m[2][3];float w=p[0]==GX_PERSPECTIVE?-ez:1;float px=p[1]*ex+p[2]*(p[0]==GX_PERSPECTIVE?ez:1),py=p[3]*ey+p[4]*(p[0]==GX_PERSPECTIVE?ez:1);*sx=v[0]+v[2]*(px/w+1)*.5f;*sy=v[1]+v[3]*(1-py/w)*.5f;*sz=v[5]+(p[5]*ez+p[6])/w*(v[5]-v[4]);}
void GXSetTevColorS10(GXTevRegID id,GXColorS10 c){color_s10[id&3]=c;tev_color[id&3]=(GXColor){c.r,c.g,c.b,c.a};}
void GXSetTevSwapModeTable(GXTevSwapSel id,GXTevColorChan r,GXTevColorChan g,GXTevColorChan b,GXTevColorChan a){u32*t=swap_table[id&3];t[0]=r;t[1]=g;t[2]=b;t[3]=a;}
void GXSetNumIndStages(u8 n){ind_count=n;}void GXSetTevDirect(GXTevStageID s){memset(indirect[s&15],0,sizeof(indirect[0]));}
void GXSetIndTexOrder(GXIndTexStageID id,GXTexCoordID coord,GXTexMapID map){ind_order[id&3][0]=coord;ind_order[id&3][1]=map;}
void GXSetIndTexCoordScale(GXIndTexStageID id,GXIndTexScale s,GXIndTexScale t){ind_scale[id&3][0]=s;ind_scale[id&3][1]=t;}
void GXSetIndTexMtx(GXIndTexMtxID id,float m[2][3],s8 e){if(id<12){memcpy(ind_matrix[id],m,24);ind_exponent[id]=e;}}
void GXSetTevIndirect(GXTevStageID s,GXIndTexStageID id,GXIndTexFormat f,GXIndTexBiasSel b,GXIndTexMtxID m,GXIndTexWrap ws,GXIndTexWrap wt,GXBool add,GXBool lod,GXIndTexAlphaSel a){u32*v=indirect[s&15];v[0]=id;v[1]=f;v[2]=b;v[3]=m;v[4]=ws;v[5]=wt;v[6]=add;v[7]=lod;v[8]=a;}
void GXSetFog(GXFogType t,float s,float e,float n,float f,GXColor c){fog.type=t;fog.start=s;fog.end=e;fog.near=n;fog.far=f;fog.color=c;}
void GXInitFogAdjTable(GXFogAdjTable*t,u16 w,float p[4][4]){float side=p[3][3]==0?1/p[0][0]:0;for(int i=0;i<10;++i){float x=(i+1)*32.f/(w*.5f);t->r[i]=256*sqrtf(1+x*x*side*side);}}
void GXSetFogRangeAdj(GXBool b,u16 c,GXFogAdjTable*t){fog.range=b;fog.center=c;if(t)fog.table=*t;}
void GXSetPixelFmt(GXPixelFmt p,GXZFmt16 z){pixel_format=p;depth_format=z;}
void GXSetFieldMode(GXBool f,GXBool a){field_mode=f;half_aspect=a;}
void GXSetZTexture(GXZTexOp o,GXTexFmt f,u32 b){ztexture[0]=o;ztexture[1]=f;ztexture[2]=b;}
void GXSetDispCopySrc(u16 x,u16 y,u16 w,u16 h){copy.src[0]=x;copy.src[1]=y;copy.src[2]=w;copy.src[3]=h;}
void GXSetDispCopyDst(u16 w,u16 h){copy.dst[0]=w;copy.dst[1]=h;}
u32 GXSetDispCopyYScale(float s){copy.scale=s;return(unsigned)((copy.src[3]-1)*s)+1;}
void GXSetCopyClear(GXColor c,u32 z){copy.color=c;copy.depth=z;}
void GXSetCopyClamp(GXFBClamp c){copy.clamp=c;}void GXSetDispCopyGamma(GXGamma g){copy.gamma=g;}
void GXSetCopyFilter(GXBool aa,const u8 p[12][2],GXBool vf,const u8 f[7]){if(p)memcpy(copy.samples,p,24);if(f)memcpy(copy.filter,f,7);}
void GXSetTexCopySrc(u16 x,u16 y,u16 w,u16 h){tex_copy.src[0]=x;tex_copy.src[1]=y;tex_copy.src[2]=w;tex_copy.src[3]=h;}
void GXSetTexCopyDst(u16 w,u16 h,GXTexFmt fmt,GXBool mip){tex_copy.width=w;tex_copy.height=h;tex_copy.format=fmt;tex_copy.mip=mip;}
static void flush(void);
void GXCopyDisp(void*dest,GXBool clear){flush();/* Native render target is presented by the retrace adapter. */}
static void copy_texture(void*dest,GXBool clear,unsigned texture_only){
    extern void mp_platform_efb_copy(const u32*);
    u32 profile_start=mp_platform_ticks();flush();
    u32 request[]={(u32)dest,tex_copy.src[0],tex_copy.src[1],tex_copy.src[2],tex_copy.src[3],tex_copy.width,tex_copy.height,tex_copy.format,clear,
        (copy.color.r<<24)|(copy.color.g<<16)|(copy.color.b<<8)|copy.color.a,copy.depth,(color_update?7:0)|(alpha_update?8:0),draw.depth_write,texture_only};
    mp_platform_efb_copy(request);profile_copy+=mp_platform_ticks()-profile_start;
}
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb){GXDrawDoneCallback old=done_callback;done_callback=cb;return old;}
void GXWaitDrawDone(void){flush();}
void GXSetDrawDone(void){
    static unsigned frames;flush();
    if(++frames%60==0){
        OSReport("Vertex shading GPU=%u flat=%u affine=%u fallback=%u\n",gpu_vertices,flat_vertices,affine_vertices,slow_vertices);
        OSReport("Geometry cache hits=%u misses=%u checks=%u bytes=%u\n",geometry_hits,geometry_misses,geometry_checks,geometry_bytes);
        OSReport("Geometry sources compares=%u reuses=%u bytes=%u shared ranges=%u\n",geometry_source_compares,geometry_source_reuses,geometry_source_bytes,geometry_range_shares);
        OSReport("Shared geometry snapshot growths=%u\n",geometry_source_growths);
        OSReport("Off-screen geometry checks=%u skipped draws=%u vertices=%u\n",mp_geometry_cull_checks,mp_geometry_cull_draws,mp_geometry_cull_vertices);
        extern unsigned geometry_early_attempts,geometry_early_rejected,gpu_clamped_draws,gpu_clamped_vertices;
        OSReport("Early visibility attempts=%u rejected=%u\n",geometry_early_attempts,geometry_early_rejected);
#ifdef MP_RENDER_REWORK_TEST
        OSReport("Development clip cache hits=%u misses=%u\n",geometry_planes.hits,geometry_planes.misses);
#endif
        OSReport("GPU clamped materials draws=%u decoded-vertices=%u\n",gpu_clamped_draws,gpu_clamped_vertices);
        OSReport("Profile/60 frames list=%u submit=%u copy=%u audio=%u ticks\n",profile_lists,profile_submit,profile_copy,mp_profile_audio);
        OSReport("Material cache hits=%u misses=%u checks=%u\n",shade_hits,shade_misses,shade_checks);
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
        OSReport("Compiled materials selections=%u vertices=%u\n",shade_clamped_hits,shade_clamped_vertices);
#endif
        profile_lists=profile_submit=profile_copy=mp_profile_audio=0;
    }
    gpu_vertices=flat_vertices=affine_vertices=slow_vertices=0;if(done_callback)done_callback();mp_platform_frame();
}
void GXCopyTex(void*dest,GXBool clear){copy_texture(dest,clear,0);}
void mp_gx_copy_texture_only(void*dest,int clear){copy_texture(dest,clear,1);}
void mp_gx_copy_clear_only(void*dest,int clear){copy_texture(dest,clear,2);}
void GXDrawDone(void){GXSetDrawDone();}
static unsigned word_be(const u8*p){return(p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}
static unsigned component_size(GXCompType t){return(t==GX_U8||t==GX_S8)?1:(t==GX_U16||t==GX_S16)?2:4;}
static unsigned component_count(unsigned a,Format f){if(a==GX_VA_POS)return f.count==GX_POS_XY?2:3;if(a==GX_VA_NRM)return f.count==GX_NRM_XYZ?3:9;return f.count==GX_TEX_S?1:2;}
static unsigned direct_size(unsigned a,Format f){if(a<GX_VA_POS)return 1;if(a==GX_VA_CLR0||a==GX_VA_CLR1){unsigned sz[]={2,3,4,2,3,4};return f.type<6?sz[f.type]:4;}return component_count(a,f)*component_size(f.type);}
static float scalar(const u8*p,GXCompType t,unsigned frac){switch(t){case GX_U8:return p[0]/(float)(1u<<frac);case GX_S8:return(s8)p[0]/(float)(1u<<frac);case GX_U16:return short_be(p)/(float)(1u<<frac);case GX_S16:return(s16)short_be(p)/(float)(1u<<frac);default:{union{u32 u;float f;}v={word_be(p)};return v.f;}}}
static GXColor rgba(const u8*p,GXCompType t){unsigned x;switch(t){case GX_RGB565:x=short_be(p);return(GXColor){((x>>11)&31)*255/31,((x>>5)&63)*255/63,(x&31)*255/31,255};case GX_RGB8:case GX_RGBX8:return(GXColor){p[0],p[1],p[2],255};case GX_RGBA4:x=short_be(p);return(GXColor){((x>>12)&15)*17,((x>>8)&15)*17,((x>>4)&15)*17,(x&15)*17};case GX_RGBA6:x=(p[0]<<16)|(p[1]<<8)|p[2];return(GXColor){((x>>18)&63)*255/63,((x>>12)&63)*255/63,((x>>6)&63)*255/63,(x&63)*255/63};default:return(GXColor){p[0],p[1],p[2],p[3]};}}
static void submit_geometry(const RenderVertex*v,unsigned count,const u16*indices,unsigned n,unsigned id,unsigned points){
    GXTexObj*t=&textures[active_texture&7];draw.image=texgens&&(texture_used_rgb||texture_used_alpha)?t->dummy[0]:0;draw.width=t->dummy[1]>>16;draw.height=t->dummy[1]&65535;draw.format=t->dummy[2];
    GXTlutObj*p=&palettes[t->dummy[4]&31];draw.palette=p->dummy[0];draw.palette_format=p->dummy[1];draw.palette_count=p->dummy[2];draw.color_mask=(color_update?7:0)|(alpha_update&&pixel_format==GX_PF_RGBA6_Z24?8:0);
    /* TLUT changes cannot affect a direct-color texture. Including an
     * unrelated palette in its cache key creates redundant conversions. */
    if(draw.format<GX_TF_C4||draw.format>GX_TF_C14X2)draw.palette=draw.palette_format=draw.palette_count=0;
    draw.texture_rgb=texture_used_rgb;draw.texture_alpha=texture_used_alpha;draw.wrap_s=t->dummy[3]>>16;draw.wrap_t=(t->dummy[3]>>8)&255;draw.indices=(u32)indices;draw.index_count=n;draw.geometry_id=id;
    draw.layer=layer_active||tint_active?(u32)&texture_layer:0;
    draw.points=points;draw.point_size=point_size;draw.point_offset=tex_offsets[active_coord&7][1]?point_offset:0;
    /* GX face culling applies to polygon primitives only. CPU-expanded
     * lines and geometry-shader points have no original front/back face. */
    unsigned cull=draw.cull,blend=draw.blend;if(points||batch_lines)draw.cull=GX_CULL_NONE;
    draw.blend=blend|(pixel_format==GX_PF_RGBA6_Z24?MP_DRAW_RGBA6_Z24:0)|(left_capture?MP_DRAW_LEFT_CAPTURE:0);
    u32 start=mp_platform_ticks();mp_platform_submit(v,count,&draw);profile_submit+=mp_platform_ticks()-start;draw.cull=cull;draw.blend=blend;
}
static void flush(void){if(batch_index_count){geometry_capture(batch,batch_count,batch_indices,batch_index_count);submit_geometry(batch,batch_count,batch_indices,batch_index_count,0,batch_points);}batch_count=0;batch_index_count=0;}
void mp_gx_display_width(unsigned width){flush();draw.screen_width=width;}
void mp_gx_camera_convergence(float convergence){flush();draw.convergence=convergence;}
unsigned mp_gx_left_capture(unsigned enabled){flush();unsigned old=left_capture;left_capture=!!enabled;return old;}
static float konst(unsigned sel,unsigned component){if(sel<8)return(8-sel)/8.f;if(sel>=12&&sel<16)return((u8*)&konst_color[sel-12])[component]/255.f;if(sel>=16&&sel<32)return((u8*)&konst_color[sel&3])[(sel-16)/4]/255.f;return 0;}
static void raster_color(float*,GXColor,const float*,const float*,unsigned);
static void prepare_layered_material(void){
    u32*s=tev_configuration[2];GXTexObj*t=&textures[s[1]];GXTlutObj*p=&palettes[t->dummy[4]&31];
    texture_layer=(MPTextureLayer){t->dummy[0],t->dummy[1]>>16,t->dummy[1]&65535,t->dummy[2],
        p->dummy[0],p->dummy[1],p->dummy[2],t->dummy[3]>>16,(t->dummy[3]>>8)&255,(u32)batch_layer_uv,0,0,tev_color[1].a};
    if(texture_layer.format<GX_TF_C4||texture_layer.format>GX_TF_C14X2)texture_layer.palette=texture_layer.palette_format=texture_layer.palette_count=0;
    for(unsigned j=0;j<3;++j)texture_layer.tint|=(u32)(konst(tev_configuration[0][23],j)*255.f+.5f)<<(8*j);
    texture_layer.blend=(u32)(konst(tev_configuration[1][23],0)*255.f+.5f);
    /* Preserve the ordinary lit raster color; textures and clamps are now
     * evaluated in the fragment combiner with their independent coordinates. */
    if(flat_shading){float zero[3]={0};raster_color(flat_color,material[0],zero,zero,0);}
    else {memset(&shade_plan,0,sizeof(shade_plan));shade_plan.valid=1;
        for(unsigned j=0;j<4;++j){shade_plan.out[j].valid=1;if(j==3)shade_plan.out[j].a=1;else shade_plan.out[j].x=1;}}
}
static void prepare_shield_material(unsigned mode){
    memset(&texture_layer,0,sizeof(texture_layer));
    active_texture=tev_configuration[1][1];active_coord=tev_configuration[1][0];
    if(mode==MP_FRAGMENT_SHIELD_START){
        GXTexObj*t=&textures[active_texture];GXTlutObj*p=&palettes[t->dummy[4]&31];
        texture_layer=(MPTextureLayer){t->dummy[0],t->dummy[1]>>16,t->dummy[1]&65535,t->dummy[2],
            p->dummy[0],p->dummy[1],p->dummy[2],t->dummy[3]>>16,(t->dummy[3]>>8)&255,(u32)batch_layer_uv};
        if(texture_layer.format<GX_TF_C4||texture_layer.format>GX_TF_C14X2)texture_layer.palette=texture_layer.palette_format=texture_layer.palette_count=0;
        layer_coord=active_coord;active_texture=tev_configuration[2][1];active_coord=tev_configuration[2][0];
    }
    float tint[4],highlight[4],base[4];
    for(unsigned j=0;j<4;++j){tint[j]=konst(tev_configuration[0][23],j);highlight[j]=konst(tev_configuration[1][23],j);base[j]=konst(tev_configuration[2][23],j);}
    base[3]=tev_color[1].a/255.f;
    /* No raster or lighting inputs occur in this matched program. Keeping
     * the low endpoint in the ordinary primary-color shader retains cached
     * geometry for held shields and adds no per-vertex texture work. */
    flat_shading=1;
    mp_shield_parameters(mode,tint,highlight,base,konst(tev_configuration[3][23],0),konst(tev_configuration[1][24],3),flat_color,&texture_layer);
}
static float color_arg(unsigned a,unsigned k,float reg[4][4],const float*ras,unsigned sel){if(a<8)return reg[a/2][(a&1)?3:k];if(a==8||a==9)return 1;if(a==10)return ras[k];if(a==11)return ras[3];if(a==12)return 1;if(a==13)return .5f;if(a==14)return konst(sel,k);return 0;}
static float alpha_arg(unsigned a,float reg[4][4],const float*ras,unsigned sel){if(a<4)return reg[a][3];if(a==4)return 1;if(a==5)return ras[3];if(a==6)return konst(sel,3);return 0;}
static float tev_op(float a,float b,float c,float d,unsigned op,unsigned bias,unsigned scale,unsigned clamp){float value;if(op>=8)value=d+(((op&1)?a==b:a>b)?c:0);else value=d+(op==1?-1:1)*(a*(1-c)+b*c)+(bias==1?.5f:bias==2?-.5f:0);static const float scales[]={1,2,4,.5f};value*=scales[scale&3];if(clamp){if(value<0)value=0;if(value>1)value=1;}return value;}
static void shade(float*out,const float raster[2][4]){if(!num_stages){memcpy(out,raster[0],16);return;}float reg[4][4];for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)reg[i][k]=((u8*)&tev_color[i])[k]/255.f;for(unsigned i=0;i<num_stages&&i<16;++i){u32*s=tev_configuration[i];float value[4],ras[4];mp_raster_select(ras,raster,s[2]);for(unsigned k=0;k<3;++k)value[k]=tev_op(color_arg(s[3],k,reg,ras,s[23]),color_arg(s[4],k,reg,ras,s[23]),color_arg(s[5],k,reg,ras,s[23]),color_arg(s[6],k,reg,ras,s[23]),s[11],s[12],s[13],s[14]);value[3]=tev_op(alpha_arg(s[7],reg,ras,s[24]),alpha_arg(s[8],reg,ras,s[24]),alpha_arg(s[9],reg,ras,s[24]),alpha_arg(s[10],reg,ras,s[24]),s[16],s[17],s[18],s[19]);for(unsigned k=0;k<3;++k)reg[s[15]&3][k]=value[k];reg[s[20]&3][3]=value[3];}memcpy(out,reg[0],16);}
static void triangle(unsigned a,unsigned b,unsigned c){batch_indices[batch_index_count++]=a;batch_indices[batch_index_count++]=b;batch_indices[batch_index_count++]=c;}
static float dot3(const float*a,const float*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void raster_color(float*out,GXColor vertex_color,const float*eye,const float*normal,unsigned channel){
    const u8*vc=(const u8*)&vertex_color;
    for(unsigned part=0;part<2;++part){
        u32*config=channel_configuration[(part?GX_ALPHA0:GX_COLOR0)+channel];
        const u8*mat=config[2]==GX_SRC_VTX?vc:(const u8*)&material[channel];
        const u8*amb=config[1]==GX_SRC_VTX?vc:(const u8*)&ambient[channel];
        float illumination[4];for(unsigned k=0;k<4;++k)illumination[k]=config[0]?amb[k]/255.f:1;
        if(config[0])for(unsigned i=0;i<8;++i)if(config[3]&(1u<<i)){
            const GXLightObj*l=&lights[i];const float*lp=(const float*)&l->dummy[10],*dir=(const float*)&l->dummy[13];
            float delta[3]={lp[0]-eye[0],lp[1]-eye[1],lp[2]-eye[2]},distance=sqrtf(dot3(delta,delta));
            if(distance>0)for(int j=0;j<3;++j)delta[j]/=distance;else memcpy(delta,normal,12);
            float diffuse=config[4]==GX_DF_NONE?1:dot3(normal,delta);if(config[4]==GX_DF_CLAMP&&diffuse<0)diffuse=0;
            float attenuation=1;
            if(config[5]==GX_AF_SPOT){
                const float*a=(const float*)&l->dummy[4],*k=(const float*)&l->dummy[7];float cosine=dot3(delta,dir);if(cosine<0)cosine=0;
                float num=a[0]+a[1]*cosine+a[2]*cosine*cosine,den=k[0]+k[1]*distance+k[2]*distance*distance;
                attenuation=den>0&&num>0?num/den:0;
            }else if(config[5]==GX_AF_SPEC){
                const float*a=(const float*)&l->dummy[4],*k=(const float*)&l->dummy[7];float cosine=dot3(normal,dir);if(cosine<0||dot3(normal,delta)<0)cosine=0;
                float num=a[0]+a[1]*cosine+a[2]*cosine*cosine,den=k[0]+k[1]*cosine+k[2]*cosine*cosine;
                attenuation=den>0&&num>0?num/den:0;
            }
            const u8*color=(const u8*)&l->dummy[3];for(unsigned k=0;k<4;++k)illumination[k]+=diffuse*attenuation*color[k]/255.f;
        }
        for(unsigned k=part?3:0;k<(part?4:3);++k){float light=illumination[k];if(light<0)light=0;if(light>1)light=1;out[k]=mat[k]/255.f*light;}
    }
}
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
/* Keep only constants that the color evaluator can read before overwrite.
 * This is conservative: even dead stage results retain their input keys.
 * RGB/alpha outputs are committed together after both sets of inputs read. */
static void shade_constant_mask(const u32 config[16][30],unsigned stages,u32*colors,u32*konst){
    u32 written=0,read=0,kread=0;
    for(unsigned i=0;i<stages&&i<16;++i){const u32*s=config[i];
        for(unsigned j=3;j<11;++j){unsigned arg=s[j],mask=0,sel=0,kmask=j<7?7:8;
            if(j<7){if(arg<8)mask=((arg&1)?8:7)<<((arg/2)*4);else if(arg==14)sel=s[23];}
            else {if(arg<4)mask=8<<(arg*4);else if(arg==6)sel=s[24];}
            read|=mask&~written;
            if(sel>=12&&sel<16)kread|=kmask<<((sel-12)*4);
            else if(sel>=16&&sel<32)kread|=1u<<((sel&3)*4+(sel-16)/4);
        }
        written|=(7u<<((s[15]&3)*4))|(8u<<((s[20]&3)*4));
    }
    if(stages)read|=15&~written; /* Final PREV may retain an initial component. */
    *colors=read;*konst=kread;
}
static void canonical_shade_key(ShadeKey*key){
    u32 colors,konst;shade_constant_mask(tev_configuration,key->stages,&colors,&konst);
    for(unsigned i=0;i<16;++i){if(!(colors&(1u<<i)))((u8*)key->colors)[i]=0;if(!(konst&(1u<<i)))((u8*)key->konst)[i]=0;}
    /* Non-flat compilation only consumes raster alpha from key.alpha; the
     * material RGB stays in the live lighting/uniform state, outside this key. */
    if(!flat_shading)memset(key->material,0,sizeof(key->material));
}
#endif
/* The compact cache shares one structural program across animated bindings.
 * Keep the legacy cache independently selectable in the development build. */
#ifdef MP_MATERIAL_PROGRAM_TEST
static void prepare_material_program(void){
    clamped_plan.valid=0;
    ShadeBindingKey binding={0};ShadeKey*key=&binding.key;
    key->stages=num_stages<16?num_stages:16;
    key->flat=flat_shading|(shade_legacy_clamp<<1)|((!shade_clamped_disable)<<2);
    memcpy(key->material,material,sizeof(material));memcpy(key->colors,tev_color,sizeof(tev_color));memcpy(key->konst,konst_color,sizeof(konst_color));
    float raw_alpha[2];
    for(unsigned ch=0;ch<2;++ch){u32*a=channel_configuration[GX_ALPHA0+ch];
        raw_alpha[ch]=key->alpha[ch]=!a[0]&&(a[2]==GX_SRC_REG||!descriptors[GX_VA_CLR0+ch])?material[ch].a/255.f:-1;}
    const MPMaterialProgram*p=mp_material_program_get(&material_programs,tev_configuration,key->stages);
    shade_program_hits=material_programs.hits;shade_program_misses=material_programs.misses;
    if(material_bindings_reset!=material_programs.resets){memset(material_bindings,0,sizeof(material_bindings));material_bindings_reset=material_programs.resets;}
    binding.program=p->serial;
    mp_material_bind_constants(p,flat_shading,(u8*)key->material,(u8*)key->colors,(u8*)key->konst,key->alpha);
    unsigned hash=2166136261u;
    for(unsigned i=0;i<sizeof(binding)/4;++i)hash=(hash^((u32*)&binding)[i])*16777619u;
    ShadeBinding*set=material_bindings+(hash&255)*4,*slot=set,*hit=NULL;
    for(unsigned i=0;i<4;++i){ShadeBinding*e=set+i;
        if(e->stamp&&e->hash==hash&&!geometry_table_compare(&binding,&e->key,sizeof(binding))){hit=e;break;}
        if(e->stamp<slot->stamp)slot=e;
    }
    if(hit){++shade_hits;++shade_binding_hits;hit->stamp=material_programs.clock;
        if(!shade_validate){if(flat_shading)memcpy(flat_color,hit->flat_color,16);else shade_plan=hit->plan;
            if(hit->clamped.valid){clamped_plan=hit->clamped;++shade_clamped_hits;}
            return;
        }
    }else{++shade_misses;++shade_binding_misses;}
    /* Compile with the ORIGINAL values, not masked cache-key constants.
     * Validation therefore detects an incorrectly declared dependency. */
    if(flat_shading){float zero[3]={0},ras[2][4]={{0}};raster_color(ras[0],material[0],zero,zero,0);
        if(raster_channels&2)raster_color(ras[1],material[1],zero,zero,1);shade(flat_color,ras);}
    else{float colors[4][4],konst[4][4];
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){colors[i][j]=((u8*)&tev_color[i])[j]/255.f;konst[i][j]=((u8*)&konst_color[i])[j]/255.f;}
        mp_shade_compile_channels(&shade_plan,tev_configuration,num_stages,colors,konst,raw_alpha);
        if(!shade_plan.valid&&!shade_clamped_disable){mp_clamped_compile(&clamped_plan,tev_configuration,num_stages,colors,konst,raw_alpha);shade_clamped_hits+=clamped_plan.valid;}
    }
    if(hit){
        if(flat_shading?memcmp(flat_color,hit->flat_color,16):memcmp(&shade_plan,&hit->plan,sizeof(shade_plan)))
            HSD_Panic(__FILE__,__LINE__,"Shared material program differs from original TEV evaluation");
        if(clamped_plan.valid!=hit->clamped.valid||(clamped_plan.valid&&memcmp(&clamped_plan,&hit->clamped,sizeof(clamped_plan))))
            HSD_Panic(__FILE__,__LINE__,"Shared clamped material differs from original compilation");
        ++shade_checks;++shade_program_checks;return;
    }
    slot->stamp=material_programs.clock;slot->hash=hash;slot->key=binding;
    if(flat_shading)memcpy(slot->flat_color,flat_color,16);else slot->plan=shade_plan;
    slot->clamped.valid=clamped_plan.valid;if(clamped_plan.valid)slot->clamped=clamped_plan;
}
#endif
static void prepare_material(void)
{
#ifdef MP_MATERIAL_PROGRAM_TEST
    if(!shade_program_disable){prepare_material_program();return;}
#endif
    ShadeKey key;key.stages=num_stages<16?num_stages:16;key.flat=flat_shading|(shade_legacy_clamp<<1);
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
    clamped_plan.valid=0;key.flat|=(!shade_clamped_disable)<<2;
#endif
    memcpy(key.material,material,sizeof(material));memcpy(key.colors,tev_color,sizeof(tev_color));memcpy(key.konst,konst_color,sizeof(konst_color));
    for(unsigned ch=0;ch<2;++ch){u32*a=channel_configuration[GX_ALPHA0+ch];
        key.alpha[ch]=!a[0]&&(a[2]==GX_SRC_REG||!descriptors[GX_VA_CLR0+ch])?material[ch].a/255.f:-1;}
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
    if(!shade_clamped_disable)canonical_shade_key(&key);
#endif
    unsigned bytes=key.stages*sizeof(tev_configuration[0]),hash=2166136261u;
    for(unsigned i=0;i<sizeof(key)/4;++i)hash=(hash^((u32*)&key)[i])*16777619u;
    for(unsigned i=0;i<bytes/4;++i)hash=(hash^((u32*)tev_configuration)[i])*16777619u;
    ShadeCache*set=shade_cache+(hash&63)*4,*slot=set,*hit=NULL;++shade_clock;
    for(unsigned i=0;i<4;++i){ShadeCache*e=set+i;
        if(e->stamp&&e->hash==hash&&!geometry_table_compare(&key,&e->key,sizeof(key))&&!geometry_table_compare(tev_configuration,e->config,bytes)){hit=e;break;}
        if(e->stamp<slot->stamp)slot=e;
    }
    if(hit){++shade_hits;hit->stamp=shade_clock;
        if(!shade_validate){if(flat_shading)memcpy(flat_color,hit->flat_color,16);else shade_plan=hit->plan;
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
            if(hit->clamped.valid){clamped_plan=hit->clamped;++shade_clamped_hits;}
#endif
            return;}
    }else ++shade_misses;
    if(flat_shading){float zero[3]={0},ras[2][4]={{0}};raster_color(ras[0],material[0],zero,zero,0);
        if(raster_channels&2)raster_color(ras[1],material[1],zero,zero,1);shade(flat_color,ras);}
    else {float colors[4][4],konst[4][4];for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){colors[i][j]=((u8*)&tev_color[i])[j]/255.f;konst[i][j]=((u8*)&konst_color[i])[j]/255.f;}
        mp_shade_compile_channels(&shade_plan,tev_configuration,num_stages,colors,konst,key.alpha);
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
        if(!shade_plan.valid&&!shade_clamped_disable){
            mp_clamped_compile(&clamped_plan,tev_configuration,num_stages,colors,konst,key.alpha);
            shade_clamped_hits+=clamped_plan.valid;
        }
#endif
    }
    if(hit){
        if(flat_shading?memcmp(flat_color,hit->flat_color,16):memcmp(&shade_plan,&hit->plan,sizeof(shade_plan)))
            HSD_Panic(__FILE__,__LINE__,"Cached material differs from fresh TEV evaluation");
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
        if(clamped_plan.valid!=hit->clamped.valid||(clamped_plan.valid&&memcmp(&clamped_plan,&hit->clamped,sizeof(clamped_plan))))
            HSD_Panic(__FILE__,__LINE__,"Cached clamped material differs from fresh compilation");
#endif
        ++shade_checks;return;
    }
    slot->stamp=shade_clock;slot->hash=hash;slot->key=key;memcpy(slot->config,tev_configuration,bytes);
    if(flat_shading)memcpy(slot->flat_color,flat_color,16);else slot->plan=shade_plan;
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
    slot->clamped.valid=clamped_plan.valid;if(clamped_plan.valid)slot->clamped=clamped_plan;
#endif
}
#ifdef MP_RENDER_REWORK_TEST
volatile unsigned gpu_clamped_disable;
#else
#define gpu_clamped_disable 0
#endif
unsigned gpu_clamped_draws,gpu_clamped_vertices;
static int prepare_gpu_shading(void)
{
    gpu_reject_reason=1;
    if(gpu_disable)return 0;
    gpu_reject_reason=2;
    unsigned post=0,second=shade_plan.second;
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
    post=!gpu_clamped_disable&&!shade_clamped_disable&&!flat_shading&&!shade_plan.valid&&clamped_plan.valid;
    if(post)second=clamped_plan.second;
#endif
    if(!flat_shading&&!shade_plan.valid&&!post)return 0;
    gpu_uniforms.post_transform=post;
    gpu_reject_reason=3;
    float(*u)[4]=gpu_uniforms.value;
    memset(u+MP_GPU_LIGHT_COLOR,0,4*16);unsigned slot=0;
    for(unsigned ch=0;ch<2;++ch){u32*rgb=channel_configuration[GX_COLOR0+ch];
        int needed=!flat_shading&&(!ch||second);
        if(needed&&(channel_configuration[GX_ALPHA0+ch][0]||(rgb[0]&&rgb[1]==GX_SRC_VTX)||
            (ch&&descriptors[GX_VA_CLR1]&&(rgb[2]==GX_SRC_VTX||channel_configuration[GX_ALPHA1][2]==GX_SRC_VTX))))return 0;
        u[MP_GPU_CONFIG+ch][0]=needed&&rgb[0];u[MP_GPU_CONFIG+ch][1]=rgb[4]==GX_DF_NONE;
        u[MP_GPU_CONFIG+ch][2]=rgb[4]==GX_DF_CLAMP;u[MP_GPU_CONFIG+ch][3]=rgb[5];
        for(int j=0;j<4;++j)u[MP_GPU_AMBIENT+ch][j]=((u8*)&ambient[ch])[j]/255.f;
        if(needed&&rgb[0])for(unsigned i=0;i<8;++i)if(rgb[3]&(1u<<i)){
            if(slot==4)return 0;
            const GXLightObj*l=&lights[i];float*attenuation=u[MP_GPU_ATTENUATION+slot],*cosine=u[MP_GPU_COS_ATTENUATION+slot];
            attenuation[0]=cosine[0]=1;attenuation[1]=attenuation[2]=attenuation[3]=cosine[1]=cosine[2]=cosine[3]=0;
            if(rgb[5]!=GX_AF_NONE){memcpy(attenuation,&l->dummy[7],12);memcpy(cosine,&l->dummy[4],12);}
            float*p=u[MP_GPU_LIGHT_POS+slot];memcpy(p,&l->dummy[10],12);p[3]=1;
            float length=sqrtf(dot3(p,p));
            /* Infinite GX lights exceed the PICA float24 exponent range. */
            if(length>1.e10f){if(rgb[5]==GX_AF_SPOT&&(attenuation[1]!=0||attenuation[2]!=0))return 0;for(int j=0;j<3;++j)p[j]/=length;p[3]=0;}
            memcpy(u[MP_GPU_LIGHT_DIR+slot],&l->dummy[13],12);u[MP_GPU_LIGHT_DIR+slot][3]=0;
            const u8*c=(const u8*)&l->dummy[3];for(int j=0;j<3;++j)u[MP_GPU_LIGHT_COLOR+slot][j]=c[j]/255.f;
            u[MP_GPU_LIGHT_COLOR+slot][3]=ch+1;++slot;
        }
    }
    gpu_uniforms.matrix_rows=descriptors[GX_VA_PNMTXIDX]?30:3;
    gpu_uniforms.constant_color=flat_shading!=0;
    if(descriptors[GX_VA_PNMTXIDX]){
        memcpy(u+MP_GPU_POS,position_mtx,30*16);memcpy(u+MP_GPU_NORMAL,normal_mtx,30*16);
    }else{
        memcpy(u+MP_GPU_POS,position_mtx[(current_mtx/3)&63],3*16);
        memcpy(u+MP_GPU_NORMAL,normal_mtx[(current_mtx/3)&63],3*16);
    }
    float sx=1,sy=1,ox=0,oy=0,sz=1,oz=0;
    if(viewport[2]>0&&viewport[3]>0){sx=viewport[2]/640.f;sy=viewport[3]/480.f;
        ox=(2*viewport[0]+viewport[2]-640.f)/640.f;oy=(480.f-2*viewport[1]-viewport[3])/480.f;
        sz=viewport[5]-viewport[4];oz=viewport[5]-1.f;}
    for(int j=0;j<4;++j){
        u[MP_GPU_PROJECTION][j]=projection[1][j]*sy+projection[3][j]*oy;
        u[MP_GPU_PROJECTION+1][j]=-projection[0][j]*sx-projection[3][j]*ox;
        u[MP_GPU_PROJECTION+2][j]=(ztexture[0]==GX_ZT_REPLACE&&ztexture[1]==GX_TF_Z8?0:projection[2][j]*sz)+projection[3][j]*oz;
        u[MP_GPU_PROJECTION+3][j]=projection[3][j];
        u[MP_GPU_SHADE][j]=flat_shading?flat_color[j]:shade_plan.out[j].c;
        u[MP_GPU_SHADE+1][j]=flat_shading?0:shade_plan.out[j].x;
        u[MP_GPU_SHADE+2][j]=flat_shading?0:shade_plan.out[j].a;
        u[MP_GPU_SHADE+3][j]=flat_shading?0:shade_plan.out[j].y;
        u[MP_GPU_SHADE+4][j]=flat_shading?0:shade_plan.out[j].b;
        u[MP_GPU_MATERIAL1][j]=((u8*)&material[1])[j]/255.f;
        unsigned source=channel_configuration[j==3?GX_ALPHA0:GX_COLOR0][2];
        gpu_uniforms.material0[j]=source==GX_SRC_VTX&&descriptors[GX_VA_CLR0]?-1:((u8*)&material[0])[j]/255.f;
        u[MP_GPU_CLAMP][j]=flat_shading?0:shade_plan.clamp[j];
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
        if(post){
            for(unsigned k=0;k<5;++k)u[MP_GPU_SHADE+k][j]=clamped_plan.coefficients[k][j];
            u[MP_GPU_CLAMP][j]=clamped_plan.clamp[j];
            gpu_uniforms.post_scale[j]=clamped_plan.scale[j];gpu_uniforms.post_bias[j]=clamped_plan.bias[j];
        }
#endif
    }
    gpu_reject_reason=0;gpu_clamped_draws+=post;return 1;
}
static void texture_coord(float out[2],const float uv[2],const float pos[3],const float normal[3],unsigned coord){
    u32*gen=texgen_configuration[coord&7];float tc[3]={uv[0],uv[1],1};
    if(gen[1]==GX_TG_POS)memcpy(tc,pos,12);else if(gen[1]==GX_TG_NRM)memcpy(tc,normal,12);
    if(gen[2]!=GX_IDENTITY&&gen[2]/3<64){MtxPtr t=gen[2]<30?position_mtx[gen[2]/3]:texture_mtx[gen[2]/3];float result[3]={0,0,1};for(int k=0;k<(gen[0]==GX_TG_MTX2x4?2:3);++k)result[k]=t[k][0]*tc[0]+t[k][1]*tc[1]+t[k][2]*tc[2]+t[k][3];memcpy(tc,result,12);}
    if(gen[0]==GX_TG_MTX2x4)tc[2]=1;
    if(gen[3]){float length=sqrtf(tc[0]*tc[0]+tc[1]*tc[1]+tc[2]*tc[2]);if(length>0)for(int k=0;k<3;++k)tc[k]/=length;}
    if(gen[4]!=GX_PTIDENTITY&&gen[4]/3<64){MtxPtr t=texture_mtx[gen[4]/3];float result[3];for(int k=0;k<3;++k)result[k]=t[k][0]*tc[0]+t[k][1]*tc[1]+t[k][2]*tc[2]+t[k][3];memcpy(tc,result,12);}
    if(gen[0]==GX_TG_MTX3x4&&tc[2]!=0){tc[0]/=tc[2];tc[1]/=tc[2];}out[0]=tc[0];out[1]=tc[1];
}
static void vertex(const u8*bytes)
{
    int expanded=primitive==GX_LINES||primitive==GX_LINESTRIP;
    float pos[3]={0},normal[3]={0,0,1},uv[2]={0};GXColor color[2]={material[0],material[1]};unsigned matrix=current_mtx;
    for(unsigned i=0;i<field_count;++i){
        const u8*p=bytes+fields[i].offset,*data=p;unsigned mode=fields[i].mode;
        if(mode!=GX_DIRECT){unsigned offset=(mode==GX_INDEX8?*p:short_be(p))*fields[i].stride;data=fields[i].array+offset;
            if(geometry_record){unsigned a=fields[i].kind;unsigned n=a==GX_VA_CLR0||a==GX_VA_CLR1?direct_size(a,formats[vertex_format][a]):a<GX_VA_POS?1:fields[i].count*component_size(fields[i].type);
                if(offset<geometry_min[i])geometry_min[i]=offset;if(offset+n>geometry_max[i])geometry_max[i]=offset+n;}
        }
        unsigned a=fields[i].kind;
        if(a==GX_VA_PNMTXIDX)matrix=*data;
        else if(a==GX_VA_CLR0||a==GX_VA_CLR1)color[a-GX_VA_CLR0]=rgba(data,fields[i].type);
        else mp_vertex_components(a==GX_VA_POS?pos:a==GX_VA_NRM?normal:uv,data,fields[i].type,fields[i].count,fields[i].scale);
    }
    RenderVertex v;
    if(!expanded&&gpu_shading&&(!descriptors[GX_VA_PNMTXIDX]||matrix<30)){
        ++gpu_vertices;gpu_clamped_vertices+=gpu_uniforms.post_transform;memcpy(v.pos,pos,12);v.pos[3]=1;memcpy(v.normal,normal,12);
        v.normal[3]=descriptors[GX_VA_PNMTXIDX]?(matrix/3)*3:0;
        for(unsigned j=0;j<4;++j)v.color[j]=descriptors[GX_VA_CLR0]?((u8*)&color[0])[j]/255.f:1;
    }else{
    geometry_cancel();
    v.normal[0]=v.normal[1]=v.normal[2]=0;v.normal[3]=-1;
    float eye[4]={0,0,0,1};MtxPtr m=position_mtx[(matrix/3)&63];
    for(int r=0;r<3;++r)eye[r]=m[r][0]*pos[0]+m[r][1]*pos[1]+m[r][2]*pos[2]+m[r][3];
    for(int r=0;r<4;++r)v.pos[r]=projection[r][0]*eye[0]+projection[r][1]*eye[1]+projection[r][2]*eye[2]+projection[r][3];
    if(flat_shading){++flat_vertices;memcpy(v.color,flat_color,sizeof(flat_color));}
    else {
        float eye_normal[3]={0,0,1};
        if(channel_configuration[GX_COLOR0][0]||channel_configuration[GX_ALPHA0][0]||((raster_channels&2)&&(channel_configuration[GX_COLOR1][0]||channel_configuration[GX_ALPHA1][0]))){
            MtxPtr nm=normal_mtx[(matrix/3)&63];
            for(int r=0;r<3;++r)eye_normal[r]=nm[r][0]*normal[0]+nm[r][1]*normal[1]+nm[r][2]*normal[2];
            float length=sqrtf(dot3(eye_normal,eye_normal));if(length>0)for(int r=0;r<3;++r)eye_normal[r]/=length;
        }
        float ras[2][4]={{0}};raster_color(ras[0],color[0],eye,eye_normal,0);
        if(raster_channels&2)raster_color(ras[1],color[1],eye,eye_normal,1);
        if(shade_plan.valid){++affine_vertices;mp_shade_apply_channels(&shade_plan,v.color,ras);}
#if defined(MP_CLAMPED_SHADE_TEST) || defined(MP_CLAMPED_SHADE)
        else if(!shade_clamped_disable&&clamped_plan.valid){
            ++shade_clamped_vertices;mp_clamped_apply(&clamped_plan,v.color,ras);
#ifdef MP_CLAMPED_SHADE_TEST
            if(shade_clamped_validate){float reference[4];shade(reference,ras);
                for(unsigned j=0;j<4;++j)if(!(fabsf(v.color[j]-reference[j])<=.00002f+.000002f*fabsf(reference[j])))
                    HSD_Panic(__FILE__,__LINE__,"Compiled clamped color differs from original TEV evaluation");
                ++shade_clamped_checks;}
#endif
        }
#endif
        else{++slow_vertices;shade(v.color,ras);}
    }
    if(ztexture[0]==GX_ZT_REPLACE&&ztexture[1]==GX_TF_Z8)v.pos[2]=0;
    /* GX cameras can draw into a sub-rectangle of the EFB. PICA uses one
     * screen-sized viewport, so apply the GX viewport in homogeneous space. */
    if(viewport[2]>0&&viewport[3]>0){
        v.pos[0]=v.pos[0]*(viewport[2]/640.f)+v.pos[3]*((2*viewport[0]+viewport[2]-640.f)/640.f);
        v.pos[1]=v.pos[1]*(viewport[3]/480.f)+v.pos[3]*((480.f-2*viewport[1]-viewport[3])/480.f);
        v.pos[2]=v.pos[2]*(viewport[5]-viewport[4])+v.pos[3]*(viewport[5]-1.f);
    }
    }
    texture_coord(v.uv,uv,pos,normal,active_coord);
    float second_uv[2];if(layer_active){float raw[2]={0};
        if(layer_uv_attr==primary_uv_attr)memcpy(raw,uv,sizeof(raw));
        else if(layer_field.kind){const VertexField*f=&layer_field;const u8*p=bytes+f->offset;
            if(f->mode!=GX_DIRECT)p=f->array+(f->mode==GX_INDEX8?*p:short_be(p))*f->stride;
            mp_vertex_components(raw,p,f->type,f->count,f->scale);}
        texture_coord(second_uv,raw,pos,normal,layer_coord);
    }
    unsigned n=primitive_count++;
    if(expanded){
        RenderVertex quad[4];int emit=0;
        if(n&&(primitive==GX_LINESTRIP||(n&1))){
            float offset=tex_offsets[active_coord&7][0]?mp_tex_offset(line_offset):0;
            /* Expand in the original pixel scale, then restore the wider
             * projection; line thickness must not grow in expanded mode. */
            float scale=draw.screen_width/320.f;RenderVertex a=line_previous,b=v;
            a.pos[0]*=scale;b.pos[0]*=scale;
            emit=mp_expand_line(&a,&b,line_width,offset,quad);
            if(emit)for(unsigned i=0;i<4;++i)quad[i].pos[0]/=scale;
            expanded_lines+=emit;
        }
        line_previous=v;
        if(emit){
            if(batch_count+4>384||batch_index_count+6>1152)flush();
            unsigned index=batch_count;memcpy(batch+index,quad,sizeof(quad));batch_count+=4;
            triangle(index,index+2,index+1);triangle(index+1,index+2,index+3);
        }
        return;
    }
    if(batch_count>=384||batch_index_count+6>1152){
        if(n&&primitive!=GX_POINTS){RenderVertex a=batch[first],b=batch[n>1?previous[0]:first],c=batch[previous[1]];
            float retained[3][2];if(layer_active){memcpy(retained[0],batch_layer_uv[first],8);memcpy(retained[1],batch_layer_uv[n>1?previous[0]:first],8);memcpy(retained[2],batch_layer_uv[previous[1]],8);}
            flush();batch[0]=a;batch[1]=b;batch[2]=c;batch_count=3;first=0;previous[0]=1;previous[1]=2;
            if(layer_active)memcpy(batch_layer_uv,retained,sizeof(retained));
        }else flush();
    }
    unsigned index=batch_count++;batch[index]=v;
    if(layer_active)memcpy(batch_layer_uv[index],second_uv,sizeof(second_uv));
    if(!n)first=index;
    switch(primitive){case GX_TRIANGLES:if(n%3==2)triangle(previous[0],previous[1],index);break;
    case GX_POINTS:batch_indices[batch_index_count++]=index;++expanded_points;break;
    case GX_TRIANGLESTRIP:if(n>=2){if(n&1)triangle(previous[1],previous[0],index);else triangle(previous[0],previous[1],index);}break;
    case GX_TRIANGLEFAN:if(n>=2)triangle(first,previous[1],index);break;
    case GX_QUADS:if(n%4==0)first=index;else if(n%4==2)triangle(first,previous[1],index);else if(n%4==3)triangle(first,previous[1],index);break;
    default:break;}
    previous[0]=previous[1];previous[1]=index;
}
void GXBegin(GXPrimitive type,GXVtxFmt fmt,u16 nverts)
{
    unsigned points=type==GX_POINTS,lines=type==GX_LINES||type==GX_LINESTRIP;
    if(points!=batch_points||lines!=batch_lines){flush();batch_points=points;batch_lines=lines;}
    primitive_vertices[(type>>3)&7]+=nverts;
    if(type==GX_POINTS){unsigned caller=(unsigned)__builtin_return_address(0);for(unsigned i=0;i<16;++i)if(!point_callers[i][0]||point_callers[i][0]==caller){point_callers[i][0]=caller;point_callers[i][1]+=nverts;break;}}
    /* A supported display list contains only primitive commands. Its state
     * and matrix palette stay fixed, so adjoining strips can share a batch. */
    if(reuse_list_state&&fmt==vertex_format){primitive=type;remaining=nverts;primitive_count=0;fifo_size=0;return;}
    flush();texture_used_rgb=texture_used_alpha=0;raster_channels=1;int selected=-1;
    for(unsigned i=0;i<num_stages&&i<16;++i){u32*s=tev_configuration[i];
        for(int k=3;k<=6;++k)if(s[k]==8||s[k]==9){texture_used_rgb=1;if(selected<2&&s[1]<8){int priority=s[k]==8?2:1;if(priority>selected){active_texture=s[1];active_coord=s[0];selected=priority;}}}
        for(int k=7;k<=10;++k)if(s[k]==4){texture_used_alpha=1;if(selected<0&&s[1]<8){active_texture=s[1];active_coord=s[0];selected=0;}}
        if(s[2]<6){for(int k=3;k<=6;++k)if(s[k]==10||s[k]==11)raster_channels|=1u<<(s[2]&1);
            for(int k=7;k<=10;++k)if(s[k]==5)raster_channels|=1u<<(s[2]&1);}
    }
    flat_shading=!channel_configuration[GX_COLOR0][0]&&!channel_configuration[GX_ALPHA0][0]&&
        (!descriptors[GX_VA_CLR0]||(channel_configuration[GX_COLOR0][2]==GX_SRC_REG&&channel_configuration[GX_ALPHA0][2]==GX_SRC_REG));
    if(raster_channels&2)flat_shading=flat_shading&&!channel_configuration[GX_COLOR1][0]&&!channel_configuration[GX_ALPHA1][0]&&
        (!descriptors[GX_VA_CLR1]||(channel_configuration[GX_COLOR1][2]==GX_SRC_REG&&channel_configuration[GX_ALPHA1][2]==GX_SRC_REG));
    layer_active=!points&&!lines&&!ind_count&&mp_layered_material(tev_configuration,num_stages)&&
        tev_configuration[1][0]<texgens&&tev_configuration[2][0]<texgens;
    if(layer_active){active_texture=tev_configuration[1][1];active_coord=tev_configuration[1][0];layer_coord=tev_configuration[2][0];}
    unsigned shield=!points&&!lines&&!ind_count?mp_shield_material(tev_configuration,num_stages):0;
    if(shield&&(tev_configuration[1][0]>=texgens||(shield==MP_FRAGMENT_SHIELD_START&&tev_configuration[2][0]>=texgens)))shield=0;
    tint_active=shield==MP_FRAGMENT_TINT;
    u32 measured=detail_begin(0);
    if(shield){layer_active=shield==MP_FRAGMENT_SHIELD_START;prepare_shield_material(shield);}
    else if(layer_active)prepare_layered_material();else prepare_material();detail_end(0,measured);
    measured=detail_begin(1);gpu_shading=prepare_gpu_shading();detail_end(1,measured);draw.gpu=gpu_shading?(u32)&gpu_uniforms:0;
    if(!gpu_shading&&nverts>mp_fallback_vertex_count){
        mp_fallback_vertex_count=nverts;mp_fallback_reason=gpu_reject_reason;mp_fallback_stages=num_stages;
        memcpy(mp_fallback_tev,tev_configuration,sizeof(mp_fallback_tev));
        memcpy(mp_fallback_colors,tev_color,16);memcpy(mp_fallback_konst,konst_color,16);
        for(unsigned ch=0;ch<2;++ch){u32*a=channel_configuration[GX_ALPHA0+ch];
            mp_fallback_alpha[ch]=!a[0]&&(a[2]==GX_SRC_REG||!descriptors[GX_VA_CLR0+ch])?material[ch].a/255.f:-1;}
    }
    primitive=type;vertex_format=fmt;remaining=nverts;primitive_count=0;fifo_size=0;vertex_bytes=0;field_count=0;
    unsigned src=texgen_configuration[active_coord&7][1];
    unsigned uv_attr=src>=GX_TG_TEX0&&src<=GX_TG_TEX7?GX_VA_TEX0+src-GX_TG_TEX0:GX_VA_TEX0;
    primary_uv_attr=uv_attr;layer_field.kind=0;
    if(layer_active){unsigned source=texgen_configuration[layer_coord][1];layer_uv_attr=source>=GX_TG_TEX0&&source<=GX_TG_TEX7?GX_VA_TEX0+source-GX_TG_TEX0:GX_VA_TEX0;}
    for(unsigned a=0;a<=GX_VA_TEX7;++a)if(descriptors[a]){
        Format f=formats[fmt][a];unsigned n=descriptors[a]==GX_DIRECT?direct_size(a,f):descriptors[a]==GX_INDEX8?1:2;
        if(a==GX_VA_NRM&&f.count==GX_NRM_NBT3&&descriptors[a]!=GX_DIRECT)n*=3;
        if(layer_active&&a==layer_uv_attr&&a!=uv_attr){
            if(descriptors[a]!=GX_DIRECT&&!arrays[a])HSD_Panic(__FILE__,__LINE__,"GX second texture attribute has no array");
            layer_field=(VertexField){a,vertex_bytes,descriptors[a],f.type,component_count(a,f),strides[a],1.f/(float)(1u<<f.frac),arrays[a]};
        }
        if(a==GX_VA_PNMTXIDX||a==GX_VA_POS||a==GX_VA_NRM||a==GX_VA_CLR0||a==GX_VA_CLR1||a==uv_attr){
            unsigned i=field_count++,frac=a==GX_VA_NRM?(f.type==GX_S8?6:f.type==GX_S16?14:f.frac):f.frac;
            if(descriptors[a]!=GX_DIRECT&&!arrays[a])HSD_Panic(__FILE__,__LINE__,"GX indexed attribute has no array");
            fields[i].kind=a;fields[i].offset=vertex_bytes;fields[i].mode=descriptors[a];fields[i].type=f.type;
            fields[i].count=a==GX_VA_NRM?3:component_count(a,f);fields[i].stride=strides[a];
            fields[i].scale=1.f/(float)(1u<<frac);fields[i].array=arrays[a];
        }
        vertex_bytes+=n;
    }
    if(vertex_bytes>sizeof(fifo)||!vertex_bytes)HSD_Panic(__FILE__,__LINE__,"Invalid GX vertex stride");
}
static void write_byte(u8 x){if(!remaining)HSD_Panic(__FILE__,__LINE__,"GX FIFO write outside primitive");fifo[fifo_size++]=x;if(fifo_size==vertex_bytes){vertex(fifo);fifo_size=0;if(!--remaining)flush();}}
void mp_gx_write_u8(u8 x){write_byte(x);}void mp_gx_write_s8(s8 x){write_byte(x);}
void mp_gx_write_u16(u16 x){write_byte(x>>8);write_byte(x);}void mp_gx_write_s16(s16 x){mp_gx_write_u16(x);}
void mp_gx_write_u32(u32 x){write_byte(x>>24);write_byte(x>>16);write_byte(x>>8);write_byte(x);}void mp_gx_write_s32(s32 x){mp_gx_write_u32(x);}
void mp_gx_write_f32(float x){union{float f;u32 u;}v={x};mp_gx_write_u32(v.u);}
void mp_gx_write_u64(u64 x){mp_gx_write_u32(x>>32);mp_gx_write_u32(x);}void mp_gx_write_s64(s64 x){mp_gx_write_u64(x);}
void mp_gx_write_f64(double x){union{double f;u64 u;}v={x};mp_gx_write_u64(v.u);}
#ifdef MP_RENDER_REWORK_TEST
volatile unsigned geometry_early_disable,geometry_early_validate;
unsigned geometry_early_checks;
#else
#define geometry_early_disable 0
#endif
unsigned geometry_early_attempts,geometry_early_rejected;
/* Bounds need position/index layout only. Other attributes still determine
 * byte offsets and stride; changes to either invalidate this fast rejection.
 * This does not reuse colors, materials, textures, or render state. */
static int geometry_position_layout(const GeometryCache*e,unsigned fmt){
    unsigned offset=0,matched=0;
    for(unsigned a=0;a<=GX_VA_TEX7;++a)if(descriptors[a]){
        Format f=formats[fmt][a];unsigned n=descriptors[a]==GX_DIRECT?direct_size(a,f):descriptors[a]==GX_INDEX8?1:2;
        if(a==GX_VA_NRM&&f.count==GX_NRM_NBT3&&descriptors[a]!=GX_DIRECT)n*=3;
        if(a==GX_VA_POS||a==GX_VA_PNMTXIDX){
            const VertexField*field=NULL;for(unsigned i=0;i<e->key.count;++i)if(e->key.fields[i].kind==a){field=&e->key.fields[i];break;}
            if(!field||field->offset!=offset||field->mode!=descriptors[a]||field->type!=(unsigned)f.type||
               field->count!=component_count(a,f)||field->stride!=strides[a]||field->array!=arrays[a]||
               field->scale!=1.f/(float)(1u<<f.frac))return 0;
            matched|=a==GX_VA_POS?1:2;
        }
        offset+=n;
    }
    if(!(matched&1)||offset!=e->key.stride)return 0;
    for(unsigned i=0;i<e->key.count;++i)if(e->key.fields[i].kind==GX_VA_PNMTXIDX&&!(matched&2))return 0;
    return 1;
}
static int geometry_early_outside(const void*list,unsigned bytes){
    if(geometry_early_disable||!mp_geometry_cull||gpu_disable||geometry_validate||bytes<3)return 0;
    const u8*p=list,*end=p+bytes;while(p<end&&!*p)++p;if(end-p<3||!(*p&0x80))return 0;
    GeometryCache*set=geometry_set(list),*e=NULL;
    for(unsigned i=0;i<GEOMETRY_WAYS;++i)if(set[i].list==list&&set[i].bytes==bytes){e=&set[i];break;}
    if(!e||!e->first||!e->early_hint)return 0;
    for(GeometryChunk*c=e->first;c;c=c->next)if(c->points||c->bounds.row<0)return 0;
    ++geometry_early_attempts;
    if(!geometry_position_layout(e,*p&7)||!source_valid(e->source[0]))return 0;
    for(unsigned i=0;i<e->key.count;++i)if((e->key.fields[i].kind==GX_VA_POS||e->key.fields[i].kind==GX_VA_PNMTXIDX)&&!source_valid(e->source[i+1]))return 0;
    /* Same arithmetic as prepare_gpu_shading, with only position/XYW rows.
     * Normal, lighting and depth rows cannot affect this side-plane test. */
    static MPGPUUniforms clip;float(*u)[4]=clip.value;
    if(descriptors[GX_VA_PNMTXIDX])memcpy(u+MP_GPU_POS,position_mtx,30*16);
    else memcpy(u+MP_GPU_POS,position_mtx[(current_mtx/3)&63],3*16);
    float sx=1,sy=1,ox=0,oy=0;
    if(viewport[2]>0&&viewport[3]>0){sx=viewport[2]/640.f;sy=viewport[3]/480.f;
        ox=(2*viewport[0]+viewport[2]-640.f)/640.f;oy=(480.f-2*viewport[1]-viewport[3])/480.f;}
    for(unsigned j=0;j<4;++j){u[MP_GPU_PROJECTION][j]=projection[1][j]*sy+projection[3][j]*oy;
        u[MP_GPU_PROJECTION+1][j]=-projection[0][j]*sx-projection[3][j]*ox;u[MP_GPU_PROJECTION+3][j]=projection[3][j];}
    extern unsigned mp_display_stereo;
    float strength=draw.convergence>0?mp_display_stereo*MP_STEREO_PIXELS_PER_SLIDER/draw.screen_width:0;
    for(GeometryChunk*c=e->first;c;c=c->next)if(!geometry_outside(&c->bounds,&clip,strength,draw.convergence))return 0;
    return 1;
}
void GXCallDisplayList(void*list,u32 nbytes){
    u32 profile_start=mp_platform_ticks();const u8*p=list,*end=p+nbytes;reuse_list_state=0;unsigned initial_format=~0u;
    /* GX work used to defer cooperative sound/pad interrupts until most of
     * the frame had been decoded. Service them between lists, at most once
     * per millisecond, so audio is not starved by an expensive stage draw. */
    static u32 last_poll;if((u32)(profile_start-last_poll)>=40500){extern void mp_engine_poll(void);last_poll=profile_start;mp_engine_poll();}
    unsigned early=geometry_early_outside(list,nbytes);
#ifdef MP_RENDER_REWORK_TEST
    if(early&&!geometry_early_validate)
#else
    if(early)
#endif
    {flush();remaining=0;++geometry_early_rejected;profile_lists+=mp_platform_ticks()-profile_start;return;}
    while(p<end){unsigned op=*p++;if(!op)continue;
        if((op&0x80)==0)HSD_Panic(__FILE__,__LINE__,"Unsupported GX display-list command");
        if(end-p<2)HSD_Panic(__FILE__,__LINE__,"Truncated GX primitive");unsigned n=short_be(p);p+=2;
        if(initial_format!=~0u&&(op&7)!=initial_format)geometry_cancel();
        GXBegin(op&0xf8,op&7,n);
        if(initial_format==~0u){initial_format=op&7;
            if(gpu_shading&&!layer_active){u32 measured=detail_begin(2);GeometryKey key;geometry_key(&key);GeometryCache*e=geometry_find(list,nbytes,&key);detail_end(2,measured);
                if(e){
#ifdef MP_RENDER_REWORK_TEST
                    if(early&&geometry_early_validate){
                        extern unsigned mp_display_stereo;float strength=draw.convergence>0?mp_display_stereo*MP_STEREO_PIXELS_PER_SLIDER/draw.screen_width:0;
                        for(GeometryChunk*c=e->first;c;c=c->next)if(c->points||c->bounds.row<0||!mp_bounds_outside_stereo(&c->bounds,&gpu_uniforms,strength,draw.convergence))
                            HSD_Panic(__FILE__,__LINE__,"Early visibility disagrees with prepared draw");
                        ++geometry_early_checks;
                    }
#endif
                    if(geometry_validate){geometry_check=e->first;geometry_checking=1;}
                    else {unsigned visible=0;for(GeometryChunk*c=e->first;c;c=c->next){
                            if(mp_geometry_cull&&!c->points&&c->bounds.row>=0){++mp_geometry_cull_checks;
                                extern unsigned mp_display_stereo;
                                float strength=draw.convergence>0?mp_display_stereo*MP_STEREO_PIXELS_PER_SLIDER/draw.screen_width:0;
                                if(geometry_outside(&c->bounds,&gpu_uniforms,strength,draw.convergence)){++mp_geometry_cull_draws;mp_geometry_cull_vertices+=c->count;continue;}}
                            ++visible;const RenderVertex*v=(const void*)(c+1);submit_geometry(v,c->count,(const void*)(v+c->count),c->indices,c->id,c->points);}
                        e->early_hint=!visible;remaining=0;reuse_list_state=0;profile_lists+=mp_platform_ticks()-profile_start;return;}
                }else geometry_start(list,nbytes,&key);
            }
        }
        if((op&0xf8)==GX_POINTS){point_list[0]=(u32)list;point_list[1]=nbytes;point_list[2]=p-(const u8*)list-3;point_list[3]=n;point_list[4]=vertex_bytes;}
        reuse_list_state=1;if((size_t)(end-p)<n*vertex_bytes)HSD_Panic(__FILE__,__LINE__,"Truncated GX vertex data");
        while(n--){vertex(p);p+=vertex_bytes;}remaining=0;
    }
    flush();if(geometry_check)HSD_Panic(__FILE__,__LINE__,"Cached geometry has extra batches");geometry_checking=0;
    geometry_finish();reuse_list_state=0;profile_lists+=mp_platform_ticks()-profile_start;
}
void GXClearVtxDesc(void){memset(descriptors,0,sizeof(descriptors));}
void GXSetVtxDesc(GXAttr a,GXAttrType t){if(a==GX_VA_NBT)a=GX_VA_NRM;if(a<26)descriptors[a]=t;}
void GXSetVtxAttrFmt(GXVtxFmt f,GXAttr a,GXCompCnt c,GXCompType t,u8 frac){if(a==GX_VA_NBT)a=GX_VA_NRM;if(a<26&&f<8)formats[f][a]=(Format){c,t,frac};}
void GXSetArray(GXAttr a,const void*p,u8 stride){if(a==GX_VA_NBT)a=GX_VA_NRM;if(a<26){arrays[a]=p;strides[a]=stride;}}
void GXSetCurrentMtx(u32 id){current_mtx=id;}
void GXLoadPosMtxImm(Mtx m,u32 id){if(id/3<64)memcpy(position_mtx[id/3],m,sizeof(Mtx));}
void GXLoadNrmMtxImm(Mtx m,u32 id){if(id/3<64)memcpy(normal_mtx[id/3],m,sizeof(Mtx));}
void GXLoadTexMtxImm(f32 m[][4],u32 id,GXTexMtxType type){if(id/3<64)memcpy(texture_mtx[id/3],m,type==GX_MTX2x4?32:48);}
void GXSetProjection(Mtx44 m,GXProjectionType type){memcpy(projection,m,sizeof(projection));projection_type=type;}
void GXGetProjectionv(float*p){p[0]=projection_type;p[1]=projection[0][0];p[2]=projection_type==GX_PERSPECTIVE?projection[0][2]:projection[0][3];p[3]=projection[1][1];p[4]=projection_type==GX_PERSPECTIVE?projection[1][2]:projection[1][3];p[5]=projection[2][2];p[6]=projection[2][3];}
void GXSetProjectionv(float*p){memset(projection,0,sizeof(projection));projection_type=p[0];projection[0][0]=p[1];projection[1][1]=p[3];projection[2][2]=p[5];projection[2][3]=p[6];if(projection_type==GX_PERSPECTIVE){projection[0][2]=p[2];projection[1][2]=p[4];projection[3][2]=-1;}else{projection[0][3]=p[2];projection[1][3]=p[4];projection[3][3]=1;}}
void GXSetViewport(float x,float y,float w,float h,float n,float f){viewport[0]=x;viewport[1]=y;viewport[2]=w;viewport[3]=h;viewport[4]=n;viewport[5]=f;}
void GXSetViewportJitter(float x,float y,float w,float h,float n,float f,u32 field){(void)field;GXSetViewport(x,y,w,h,n,f);}
void GXSetScissor(u32 x,u32 y,u32 w,u32 h){scissor[0]=x;scissor[1]=y;scissor[2]=w;scissor[3]=h;memcpy(draw.scissor,scissor,sizeof(scissor));}
void GXSetCullMode(GXCullMode mode){draw.cull=mode;}
void GXSetZMode(GXBool enable,GXCompare func,GXBool update){draw.depth_test=enable;draw.depth_func=func;draw.depth_write=update;}
void GXSetBlendMode(GXBlendMode type,GXBlendFactor src,GXBlendFactor dst,GXLogicOp op){draw.blend=type|(op<<8);draw.src=src;draw.dst=dst;}
static void channel_color(GXColor *table,GXChannelID chan,GXColor c){GXColor*dst=&table[chan&1];if(chan<2){dst->r=c.r;dst->g=c.g;dst->b=c.b;}else if(chan<4)dst->a=c.a;else *dst=c;}
void GXSetChanMatColor(GXChannelID chan,GXColor c){channel_color(material,chan,c);}
void GXSetChanAmbColor(GXChannelID chan,GXColor c){channel_color(ambient,chan,c);}
void GXSetChanCtrl(GXChannelID chan,GXBool en,GXColorSrc a,GXColorSrc m,u32 mask,GXDiffuseFn diff,GXAttnFn attn){if(chan<6){u32 value[]={en,a,m,mask,attn==GX_AF_SPEC?GX_DF_NONE:diff,attn};if(chan>=GX_COLOR0A0){memcpy(channel_configuration[chan&1],value,sizeof(value));memcpy(channel_configuration[2+(chan&1)],value,sizeof(value));}else memcpy(channel_configuration[chan],value,sizeof(value));}}
void GXSetNumChans(u8 n){num_chans=n;}
void GXSetNumTexGens(u8 n){texgens=n;}
void GXSetNumTevStages(u8 n){num_stages=n;}
void GXSetTexCoordGen2(GXTexCoordID id,GXTexGenType func,GXTexGenSrc src,u32 mtx,GXBool norm,u32 post){if(id<8){u32*v=texgen_configuration[id];v[0]=func;v[1]=src;v[2]=mtx;v[3]=norm;v[4]=post;}}
void GXSetTevOrder(GXTevStageID s,GXTexCoordID coord,GXTexMapID map,GXChannelID color){u32*v=tev_configuration[s&15];v[0]=coord;v[1]=map;v[2]=color;if(map<8)active_texture=map;}
void GXSetTevColorIn(GXTevStageID s,GXTevColorArg a,GXTevColorArg b,GXTevColorArg c,GXTevColorArg d){u32*v=tev_configuration[s&15];v[3]=a;v[4]=b;v[5]=c;v[6]=d;}
void GXSetTevAlphaIn(GXTevStageID s,GXTevAlphaArg a,GXTevAlphaArg b,GXTevAlphaArg c,GXTevAlphaArg d){u32*v=tev_configuration[s&15];v[7]=a;v[8]=b;v[9]=c;v[10]=d;}
void GXSetTevColorOp(GXTevStageID s,GXTevOp op,GXTevBias bias,GXTevScale scale,GXBool clamp,GXTevRegID out){u32*v=tev_configuration[s&15];v[11]=op;v[12]=bias;v[13]=scale;v[14]=clamp;v[15]=out;}
void GXSetTevAlphaOp(GXTevStageID s,GXTevOp op,GXTevBias bias,GXTevScale scale,GXBool clamp,GXTevRegID out){u32*v=tev_configuration[s&15];v[16]=op;v[17]=bias;v[18]=scale;v[19]=clamp;v[20]=out;}
void GXSetTevSwapMode(GXTevStageID s,GXTevSwapSel ras,GXTevSwapSel tex){tev_configuration[s&15][21]=ras;tev_configuration[s&15][22]=tex;}
void GXSetTevKColorSel(GXTevStageID s,GXTevKColorSel sel){tev_configuration[s&15][23]=sel;}
void GXSetTevKAlphaSel(GXTevStageID s,GXTevKAlphaSel sel){tev_configuration[s&15][24]=sel;}
void GXSetTevOp(GXTevStageID s,GXTevMode mode){GXTevColorArg c=s?GX_CC_CPREV:GX_CC_RASC;GXTevAlphaArg a=s?GX_CA_APREV:GX_CA_RASA;switch(mode){case GX_MODULATE:GXSetTevColorIn(s,GX_CC_ZERO,GX_CC_TEXC,c,GX_CC_ZERO);GXSetTevAlphaIn(s,GX_CA_ZERO,GX_CA_TEXA,a,GX_CA_ZERO);break;case GX_DECAL:GXSetTevColorIn(s,c,GX_CC_TEXC,GX_CC_TEXA,GX_CC_ZERO);GXSetTevAlphaIn(s,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,a);break;case GX_BLEND:GXSetTevColorIn(s,c,GX_CC_ONE,GX_CC_TEXC,GX_CC_ZERO);GXSetTevAlphaIn(s,GX_CA_ZERO,GX_CA_TEXA,a,GX_CA_ZERO);break;case GX_REPLACE:GXSetTevColorIn(s,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_TEXC);GXSetTevAlphaIn(s,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_TEXA);break;case GX_PASSCLR:GXSetTevColorIn(s,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,c);GXSetTevAlphaIn(s,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,a);break;}GXSetTevColorOp(s,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,1,GX_TEVPREV);GXSetTevAlphaOp(s,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,1,GX_TEVPREV);}
void GXSetTevColor(GXTevRegID id,GXColor c){tev_color[id&3]=c;}
void GXSetTevKColor(GXTevKColorID id,GXColor c){konst_color[id&3]=c;}
void GXSetColorUpdate(GXBool x){color_update=x;}void GXSetAlphaUpdate(GXBool x){alpha_update=x;}
void GXSetDstAlpha(GXBool en,u8 a){dst_alpha=(en<<8)|a;}void GXSetDither(GXBool x){dither=x;}
void GXSetZCompLoc(GXBool x){zcomp_location=x;}
void GXSetAlphaCompare(GXCompare a,u8 refa,GXAlphaOp op,GXCompare b,u8 refb){alpha_configuration[0]=a;alpha_configuration[1]=refa;alpha_configuration[2]=op;alpha_configuration[3]=b;alpha_configuration[4]=refb;draw.alpha_ref=a|(refa<<3)|(op<<11)|(b<<13)|(refb<<16);}
void GXPixModeSync(void){flush();}
void GXInitTexObj(GXTexObj*o,void*p,u16 w,u16 h,GXTexFmt fmt,GXTexWrapMode ws,GXTexWrapMode wt,u8 mip){memset(o,0,sizeof(*o));o->dummy[0]=(u32)p;o->dummy[1]=(w<<16)|h;o->dummy[2]=fmt;o->dummy[3]=(ws<<16)|(wt<<8)|mip;}
void GXInitTexObjCI(GXTexObj*o,void*p,u16 w,u16 h,GXTexFmt fmt,GXTexWrapMode ws,GXTexWrapMode wt,u8 mip,u32 tlut){GXInitTexObj(o,p,w,h,fmt,ws,wt,mip);o->dummy[4]=tlut;}
void GXInitTexObjLOD(GXTexObj*o,GXTexFilter min,GXTexFilter mag,f32 lo,f32 hi,f32 bias,GXBool clamp,GXBool edge,GXAnisotropy aniso){o->dummy[5]=(min<<16)|mag;memcpy(&o->dummy[6],&lo,4);memcpy(&o->dummy[7],&hi,4);(void)bias;(void)clamp;(void)edge;(void)aniso;}
void GXLoadTexObj(GXTexObj*o,GXTexMapID id){if(id<8)textures[id]=*o;}
void GXInitTlutObj(GXTlutObj*o,void*p,GXTlutFmt fmt,u16 n){o->dummy[0]=(u32)p;o->dummy[1]=fmt;o->dummy[2]=n;}
void GXLoadTlut(GXTlutObj*o,u32 id){palettes[id&31]=*o;}
void GXInitLightColor(GXLightObj*o,GXColor c){memcpy(&o->dummy[3],&c,4);}
void GXInitLightDir(GXLightObj*o,float x,float y,float z){float*v=(float*)&o->dummy[13];v[0]=-x;v[1]=-y;v[2]=-z;}
void GXInitLightAttn(GXLightObj*o,float a,float b,float c,float d,float e,float f){float*v=(float*)&o->dummy[4];v[0]=a;v[1]=b;v[2]=c;v[3]=d;v[4]=e;v[5]=f;}
void GXLoadLightObjImm(GXLightObj*o,GXLightID id){for(unsigned n=0;n<8;++n)if(id&(1u<<n))lights[n]=*o;}
void GXInitLightPos(GXLightObj*o,float x,float y,float z){float*v=(float*)&o->dummy[10];v[0]=x;v[1]=y;v[2]=z;}
void GXInitLightDistAttn(GXLightObj*o,float distance,float brightness,GXDistAttnFn kind){float*v=(float*)&o->dummy[7];v[0]=1;v[1]=v[2]=0;if(distance<=0||brightness<=0||brightness>=1)return;float k=(1-brightness)/brightness;if(kind==GX_DA_GENTLE)v[1]=k/distance;else if(kind==GX_DA_MEDIUM){v[1]=k/(2*distance);v[2]=k/(2*distance*distance);}else if(kind==GX_DA_STEEP)v[2]=k/(distance*distance);}
void GXInitLightSpot(GXLightObj*o,float cutoff,GXSpotFn kind){float*v=(float*)&o->dummy[4];float c=cosf(cutoff*3.1415927f/180.f),d=(1-c)*(1-c);v[0]=1;v[1]=v[2]=0;if(cutoff<=0||cutoff>90)return;switch(kind){case GX_SP_FLAT:v[0]=-1000*c;v[1]=1000;break;case GX_SP_COS:v[0]=-c/(1-c);v[1]=1/(1-c);break;case GX_SP_COS2:v[0]=0;v[1]=-c/(1-c);v[2]=1/(1-c);break;case GX_SP_SHARP:v[0]=c*(c-2)/d;v[1]=2/d;v[2]=-1/d;break;case GX_SP_RING1:v[0]=-4*c/d;v[1]=4*(1+c)/d;v[2]=-4/d;break;case GX_SP_RING2:v[0]=1-2*c*c/d;v[1]=4*c/d;v[2]=-2/d;break;default:break;}}
