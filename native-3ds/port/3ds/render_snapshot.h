#ifndef MP_RENDER_SNAPSHOT_H
#define MP_RENDER_SNAPSHOT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "texture_visibility.h"
#include "source_snapshot_cache.h"
#include "../engine/gpu_vertex.h"
#include "../engine/layered_material.h"

enum { MP_DRAW_WORDS=34, MP_DRAW_GPU=20, MP_DRAW_INDICES=21,
       MP_DRAW_INDEX_COUNT=22, MP_DRAW_GEOMETRY=23, MP_DRAW_LAYER=32 };
typedef struct { uint32_t address, bytes; const void *data; } MPSourceSnapshot;
typedef struct {
    uint32_t draw[MP_DRAW_WORDS];
    unsigned count;
    const void *vertices, *indices, *uniforms, *layer, *uv;
    MPSourceSnapshot source[4];
    unsigned sources;
    unsigned borrowed_geometry;
} MPDrawSnapshot;
#define MP_SNAPSHOT_SOURCES 512
typedef struct {
    unsigned char *data;
    size_t capacity, used;
    MPSourceSnapshot source[MP_SNAPSHOT_SOURCES];
    unsigned sources;
    uint64_t copied_bytes, reused_bytes;
    MPSourceSnapshotCache *persistent;
    unsigned borrow_geometry;
} MPSnapshotArena;
static inline uint32_t mp_snapshot_word(const void *p) {
    const unsigned char *b=p;
    return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
}
static inline void *mp_snapshot_allocate(MPSnapshotArena *a,size_t n) {
    if(a->used>SIZE_MAX-7) return NULL;
    size_t start=(a->used+7)&~(size_t)7;
    if(start>a->capacity || n>a->capacity-start) return NULL;
    void *result=a->data+start; a->used=start+n; return result;
}
static inline const void *mp_snapshot_copy(MPSnapshotArena *a,const void *source,size_t n) {
    if(!source) return NULL;
    void *copy=mp_snapshot_allocate(a,n); if(!copy) return NULL;
    memcpy(copy,source,n); a->copied_bytes+=n; return copy;
}
static inline int mp_snapshot_texture(MPSnapshotArena *a,MPDrawSnapshot *draw,
                                     uint32_t address,unsigned bytes,MPSnapshotResolve resolve,void *ctx) {
    if(!address || !bytes) return 1;
    if(draw->sources==4) return 0;
    for(unsigned i=0;i<a->sources;++i) {
        MPSourceSnapshot *s=&a->source[i];
        if(s->address==address && s->bytes==bytes) {
            if(a->persistent && a->persistent->validate) {
                const void *source=resolve(address,bytes,ctx);++a->persistent->checks;
                if(!source || memcmp(s->data,source,bytes)) { ++a->persistent->mismatches;return 0; }
            }
            draw->source[draw->sources++]=*s; a->reused_bytes+=bytes; return 1;
        }
    }
    const void *copy=a->persistent?mp_source_cache_get(a->persistent,address,bytes,resolve,ctx):NULL;
    if(!copy) copy=mp_snapshot_copy(a,resolve(address,bytes,ctx),bytes);
    if(!copy) return 0;
    MPSourceSnapshot s={address,bytes,copy};
    draw->source[draw->sources++]=s;
    if(a->sources<MP_SNAPSHOT_SOURCES) a->source[a->sources++]=s;
    return 1;
}
static inline int mp_snapshot_image(MPSnapshotArena *a,MPDrawSnapshot *draw,
                                   const uint32_t *be,MPSnapshotResolve resolve,void *ctx) {
    unsigned image=mp_snapshot_word(be),w=mp_snapshot_word(be+1),h=mp_snapshot_word(be+2);
    if(!image || !w || !h) return 1;
    unsigned fmt=mp_snapshot_word(be+3),palette=mp_snapshot_word(be+4),entries=mp_snapshot_word(be+6);
    /* Invalid or unusually large inputs take the original synchronous path,
     * which retains its existing diagnostics. Avoid overflow before copying. */
    if(w>1024 || h>1024 || entries>16384) return 0;
    return mp_snapshot_texture(a,draw,image,mp_texture_source_bytes(fmt,w,h),resolve,ctx) &&
           mp_snapshot_texture(a,draw,palette,entries*2,resolve,ctx);
}
/* All inputs are copied before publication. Texture identity remains the
 * original address, while decode bytes are resolved separately by the worker.
 * Cache entries are reusable only within the current source generation. */
static inline MPDrawSnapshot *mp_snapshot_draw(MPSnapshotArena *a,const void *vertices,
                    unsigned count,const void *state,MPSnapshotResolve resolve,void *ctx) {
    size_t before=a->used; unsigned sources_before=a->sources;
    MPDrawSnapshot *s=mp_snapshot_allocate(a,sizeof(*s));
    if(!s) return NULL;
    memset(s,0,sizeof(*s)); memcpy(s->draw,state,sizeof(s->draw)); s->count=count;
    if(count>65536) goto fail;
    s->borrowed_geometry=a->borrow_geometry && mp_snapshot_word(s->draw+MP_DRAW_GEOMETRY)!=0;
    s->vertices=s->borrowed_geometry?vertices:mp_snapshot_copy(a,vertices,(size_t)count*sizeof(MPGPUVertex));
    if(!s->vertices) goto fail;
    unsigned address=mp_snapshot_word(s->draw+MP_DRAW_INDICES);
    unsigned indices=mp_snapshot_word(s->draw+MP_DRAW_INDEX_COUNT);
    if(indices>196608) goto fail;
    if(address && indices) {
        const void *input=resolve(address,indices*2,ctx);
        s->indices=s->borrowed_geometry?input:mp_snapshot_copy(a,input,indices*2);
        if(!s->indices) goto fail;
    }
    address=mp_snapshot_word(s->draw+MP_DRAW_GPU);
    if(address) {
        s->uniforms=mp_snapshot_copy(a,resolve(address,sizeof(MPGPUUniforms),ctx),sizeof(MPGPUUniforms));
        if(!s->uniforms) goto fail;
    }
    if(!mp_snapshot_image(a,s,s->draw,resolve,ctx)) goto fail;
    address=mp_snapshot_word(s->draw+MP_DRAW_LAYER);
    if(address) {
        s->layer=mp_snapshot_copy(a,resolve(address,sizeof(MPTextureLayer),ctx),sizeof(MPTextureLayer));
        if(!s->layer) goto fail;
        const uint32_t *layer=s->layer;
        if(mp_snapshot_word(layer+13)!=MP_FRAGMENT_TINT) {
            if(!mp_snapshot_image(a,s,layer,resolve,ctx)) goto fail;
            address=mp_snapshot_word(layer+9);
            s->uv=mp_snapshot_copy(a,resolve(address,(size_t)count*8,ctx),(size_t)count*8);
            if(!s->uv) goto fail;
        }
    }
    return s;
fail:
    a->used=before; a->sources=sources_before;
    return NULL;
}
static inline const void *mp_snapshot_source(const MPDrawSnapshot *s,unsigned address,unsigned bytes) {
    for(unsigned i=0;i<s->sources;++i) {
        const MPSourceSnapshot *p=&s->source[i];
        if(p->address==address && bytes<=p->bytes) return p->data;
    }
    return NULL;
}
#endif
