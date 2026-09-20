#ifndef MP_SOURCE_SNAPSHOT_CACHE_H
#define MP_SOURCE_SNAPSHOT_CACHE_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "texture_visibility.h"

typedef const void *(*MPSnapshotResolve)(uint32_t, size_t, void*);
#define MP_SOURCE_CACHE_ENTRIES 512
#define MP_SOURCE_CACHE_WAYS 4
typedef struct {
    uint32_t address,bytes;
    void *data;
    uint64_t stamp;
    unsigned valid,pinned;
} MPSourceCacheEntry;
typedef struct {
    MPSourceCacheEntry entry[MP_SOURCE_CACHE_ENTRIES];
    size_t bytes,budget;
    uint64_t clock,copied_bytes,hit_bytes;
    unsigned hits,misses,evictions,checks,mismatches,validate;
} MPSourceSnapshotCache;

/* Only the producer accesses metadata. Consumers borrow immutable data from
 * pinned entries; an acknowledged CPU fence releases pins. No native GPU
 * texture pointer or moving renderer-cache slot is shared between threads. */
static inline void mp_source_cache_free(MPSourceSnapshotCache *c,MPSourceCacheEntry *e) {
    if(e->data) { c->bytes-=e->bytes; free(e->data); }
    memset(e,0,sizeof(*e));
}
static inline void mp_source_cache_release(MPSourceSnapshotCache *c) {
    for(unsigned i=0;i<MP_SOURCE_CACHE_ENTRIES;++i) {
        MPSourceCacheEntry *e=&c->entry[i]; e->pinned=0;
        if(e->data && !e->valid) mp_source_cache_free(c,e);
    }
}
static inline void mp_source_cache_dirty(MPSourceSnapshotCache *c,uint32_t address,unsigned bytes,int all) {
    for(unsigned i=0;i<MP_SOURCE_CACHE_ENTRIES;++i) {
        MPSourceCacheEntry *e=&c->entry[i];
        if(e->data && (all || mp_texture_range_overlap(e->address,e->bytes,address,bytes))) {
            e->valid=0;
            if(!e->pinned) mp_source_cache_free(c,e);
        }
    }
}
static inline void mp_source_cache_close(MPSourceSnapshotCache *c) {
    /* Caller must have joined all readers before closing. */
    for(unsigned i=0;i<MP_SOURCE_CACHE_ENTRIES;++i) mp_source_cache_free(c,&c->entry[i]);
}
static inline const void *mp_source_cache_get(MPSourceSnapshotCache *c,uint32_t address,unsigned bytes,
                                            MPSnapshotResolve resolve,void *ctx) {
    unsigned bank=((address>>5)^(address>>17)^bytes)&(MP_SOURCE_CACHE_ENTRIES/MP_SOURCE_CACHE_WAYS-1);
    MPSourceCacheEntry *set=&c->entry[bank*MP_SOURCE_CACHE_WAYS],*slot=NULL;
    for(unsigned i=0;i<MP_SOURCE_CACHE_WAYS;++i) {
        MPSourceCacheEntry *e=&set[i];
        if(e->data && e->valid && e->address==address && e->bytes==bytes) {
            if(c->validate) {
                const void *source=resolve(address,bytes,ctx); ++c->checks;
                if(!source || memcmp(e->data,source,bytes)) { ++c->mismatches; return NULL; }
            }
            e->pinned=1; e->stamp=++c->clock; ++c->hits; c->hit_bytes+=bytes; return e->data;
        }
        if(!e->pinned && (!slot || !e->data || (slot->data && e->stamp<slot->stamp))) slot=e;
    }
    ++c->misses;
    if(!slot || bytes>c->budget) return NULL;
    const void *source=resolve(address,bytes,ctx); if(!source) return NULL;
    if(slot->data) { ++c->evictions; mp_source_cache_free(c,slot); }
    while(c->bytes>c->budget-bytes) {
        MPSourceCacheEntry *old=NULL;
        for(unsigned i=0;i<MP_SOURCE_CACHE_ENTRIES;++i) {
            MPSourceCacheEntry *e=&c->entry[i];
            if(e->data && !e->pinned && (!old || e->stamp<old->stamp)) old=e;
        }
        if(!old) return NULL;
        ++c->evictions; mp_source_cache_free(c,old);
    }
    void *copy=malloc(bytes); if(!copy) return NULL;
    memcpy(copy,source,bytes);
    *slot=(MPSourceCacheEntry){.address=address,.bytes=bytes,.data=copy,.stamp=++c->clock,.valid=1,.pinned=1};
    c->bytes+=bytes; c->copied_bytes+=bytes; return copy;
}
#endif
