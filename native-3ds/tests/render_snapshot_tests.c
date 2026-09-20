#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../port/3ds/render_snapshot.h"

typedef struct { uint32_t key; unsigned size; unsigned char data[sizeof(MPGPUUniforms)]; } Source;
static Source source[8];
static unsigned char vertices[4*sizeof(MPGPUVertex)],saved_vertices[sizeof(vertices)];
static unsigned char saved_image[128];
static unsigned char saved_sources[8][sizeof(MPGPUUniforms)];
static uint32_t descriptor[34];
static void put(void *destination,unsigned word,unsigned value) {
    unsigned char *p=(unsigned char*)destination+word*4;
    p[0]=value>>24; p[1]=value>>16; p[2]=value>>8; p[3]=value;
}
static const void *resolve(uint32_t key,size_t bytes,void *ctx) {
    (void)ctx;
    for(unsigned i=0;i<8;++i) if(source[i].key==key) {
        assert(bytes<=source[i].size); return source[i].data;
    }
    return NULL;
}
static void initialize(void) {
    const unsigned sizes[]={sizeof(MPGPUUniforms),12,128,32,sizeof(MPTextureLayer),64,16,32};
    memset(descriptor,0,sizeof(descriptor));
    for(unsigned i=0;i<8;++i) {
        source[i].key=(i+1)*4096; source[i].size=sizes[i];
        for(unsigned j=0;j<sizes[i];++j) source[i].data[j]=(unsigned char)(i*27+j*13);
    }
    for(unsigned j=0;j<sizeof(vertices);++j) vertices[j]=(unsigned char)(j*19);
    put(descriptor,0,12288); put(descriptor,1,8); put(descriptor,2,8); put(descriptor,3,5);
    put(descriptor,4,16384); put(descriptor,5,2); put(descriptor,6,16);
    put(descriptor,MP_DRAW_GPU,4096); put(descriptor,MP_DRAW_INDICES,8192);
    put(descriptor,MP_DRAW_INDEX_COUNT,6); put(descriptor,MP_DRAW_GEOMETRY,0x89abcdef);
    put(descriptor,MP_DRAW_LAYER,20480);
    void *layer=source[4].data; memset(layer,0,sizeof(MPTextureLayer));
    put(layer,0,24576); put(layer,1,8); put(layer,2,8); put(layer,3,9);
    put(layer,4,28672); put(layer,5,1); put(layer,6,8);
    put(layer,9,32768); put(layer,13,MP_FRAGMENT_SUBTRACT);
}
int main(void) {
    unsigned char *data=malloc(65536); assert(data);
    MPSnapshotArena a={.data=data,.capacity=65536};
    initialize(); memcpy(saved_vertices,vertices,sizeof(vertices)); memcpy(saved_image,source[2].data,128);
    MPDrawSnapshot *first=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL); assert(first);
    assert(first->sources==4 && first->count==4 && a.sources==4);
    assert(mp_snapshot_word(first->draw+MP_DRAW_GEOMETRY)==0x89abcdef);
    assert(!memcmp(first->uniforms,source[0].data,sizeof(MPGPUUniforms)));
    assert(!memcmp(first->indices,source[1].data,12) && !memcmp(first->uv,source[7].data,32));
    size_t used=a.used;
    MPDrawSnapshot *second=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL); assert(second);
    assert(first->source[0].data==second->source[0].data && a.sources==4);
    assert(a.reused_bytes==128+32+64+16);
    assert(a.used-used<used);
    for(unsigned i=0;i<8;++i) {
        memcpy(saved_sources[i],source[i].data,source[i].size);
        memset(source[i].data,0x80+i,source[i].size);
    }
    assert(!memcmp(first->indices,saved_sources[1],12));
    assert(!memcmp(first->layer,saved_sources[4],sizeof(MPTextureLayer)));
    assert(!memcmp(first->uv,saved_sources[7],32));
    assert(!memcmp(mp_snapshot_source(first,16384,32),saved_sources[3],32));
    assert(!memcmp(mp_snapshot_source(first,24576,64),saved_sources[5],64));
    assert(!memcmp(mp_snapshot_source(first,28672,16),saved_sources[6],16));
    for(unsigned i=0;i<8;++i) memcpy(source[i].data,saved_sources[i],source[i].size);
    memset(vertices,0xa5,sizeof(vertices)); memset(source[2].data,0x5a,128);
    memset(source[0].data,0xcc,sizeof(MPGPUUniforms));
    assert(!memcmp(first->vertices,saved_vertices,sizeof(vertices)));
    assert(!memcmp(mp_snapshot_source(first,12288,128),saved_image,128));
    assert(mp_snapshot_source(first,12288,129)==NULL);
    /* A dirty event clears only future source reuse. Earlier packets retain
     * their own generation's data until the CPU-consumption barrier. */
    a.sources=0;
    MPDrawSnapshot *third=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL); assert(third);
    assert(memcmp(third->source[0].data,first->source[0].data,128));
    assert(!memcmp(first->source[0].data,saved_image,128));
    assert(!memcmp(third->uniforms,source[0].data,sizeof(MPGPUUniforms)));
    /* Failure must not invalidate earlier packets or advance the arena. */
    size_t before=a.used; unsigned count=a.sources;
    a.capacity=before+32;
    assert(!mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL));
    assert(a.used==before && a.sources==count);
    assert(a.failure==MP_SNAPSHOT_MEMORY);
    assert(!memcmp(first->source[0].data,saved_image,128));
    unsigned capacities=0,fit=0;
    for(size_t capacity=0;capacity<4096;++capacity) {
        initialize(); memset(data,0xdd,65536);
        a.used=a.sources=0; a.capacity=capacity;
        MPDrawSnapshot *s=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);
        if(s) { assert(a.used<=capacity); ++fit; }
        else assert(a.used==0 && a.sources==0);
        for(size_t j=capacity;j<capacity+16;++j) assert(data[j]==0xdd);
        ++capacities;
    }
    assert(fit && fit<capacities);
    initialize(); a.used=a.sources=0; a.capacity=65536;
    put(descriptor,1,0xffffffff); assert(!mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL));
    assert(a.used==0 && a.sources==0);
    assert(a.failure==MP_SNAPSHOT_LAYOUT&&a.failure_address==12288);
    initialize(); put(descriptor,MP_DRAW_INDEX_COUNT,0xffffffff);
    assert(!mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL));
    initialize(); put(descriptor,MP_DRAW_GPU,0xdeadbeef);
    assert(!mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL));
    assert(a.failure==MP_SNAPSHOT_SOURCE&&a.failure_address==0xdeadbeef&&a.failure_bytes==sizeof(MPGPUUniforms));
    initialize(); put(source[4].data,13,MP_FRAGMENT_TINT);
    first=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);
    assert(first && first->sources==2 && !first->uv);
    assert(a.failure==MP_SNAPSHOT_OK);
    /* Frame reuse retains only immutable texture copies. Every draw's
     * geometry, uniforms and UV data still belong to its current packet. */
    MPSourceSnapshotCache cache={.budget=4096,.validate=1};
    initialize();a.used=a.sources=0;a.persistent=&cache;
    first=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);assert(first);
    const void *cached=first->source[0].data;
    size_t frame_bytes=a.used;
    a.used=a.sources=0;mp_source_cache_release(&cache);
    second=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);assert(second);
    assert(second->source[0].data==cached && cache.hits==4 && cache.checks==4 && a.used==frame_bytes);
    source[2].data[0]^=1;
    assert(!mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL) && cache.mismatches==1);
    assert(a.failure==MP_SNAPSHOT_GENERATION&&a.failure_address==12288);
    cache.mismatches=0;a.sources=0;mp_source_cache_dirty(&cache,12288,128,0);
    third=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);assert(third);
    assert(third->source[0].data!=cached && *(unsigned char*)cached!=source[2].data[0]);
    mp_source_cache_close(&cache);
    initialize();a.used=a.sources=0;a.persistent=NULL;a.borrow_geometry=1;
    first=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);assert(first);
    assert(first->borrowed_geometry && first->vertices==vertices && first->indices==source[1].data);
    assert(first->uniforms!=source[0].data && !memcmp(first->uniforms,source[0].data,sizeof(MPGPUUniforms)));
    /* Transient geometry (ID zero) must still be copied in borrow mode. */
    put(descriptor,MP_DRAW_GEOMETRY,0);
    second=mp_snapshot_draw(&a,vertices,4,descriptor,resolve,NULL);assert(second);
    assert(!second->borrowed_geometry && second->vertices!=vertices && second->indices!=source[1].data);
    assert(!memcmp(second->vertices,vertices,sizeof(vertices)));
    free(data);
    printf("Passed owned vertices/uniforms/indices/layer/UV/textures, immutable generations, texture identity/reuse, missing-source fallback, tint path and %u guarded arena capacities.\n",capacities);
}
