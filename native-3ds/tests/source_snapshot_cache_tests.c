#include <assert.h>
#include <stdio.h>
#include "../port/3ds/source_snapshot_cache.h"

static unsigned char input[256];
static unsigned resolves;
static const void *resolve(uint32_t address,size_t bytes,void *unused) {
    (void)unused; ++resolves;
    if(address==0xdead || bytes>sizeof(input)) return NULL;
    return input;
}
static unsigned bank(unsigned address,unsigned bytes) {
    return ((address>>5)^(address>>17)^bytes)&127;
}
int main(void) {
    MPSourceSnapshotCache c={.budget=256};
    memset(input,0x17,sizeof(input));
    const void *first=mp_source_cache_get(&c,4096,64,resolve,NULL);assert(first);
    mp_source_cache_release(&c);
    unsigned before=resolves;
    assert(mp_source_cache_get(&c,4096,64,resolve,NULL)==first && resolves==before);
    /* A partial write retires the old generation without changing queued data. */
    mp_source_cache_dirty(&c,4127,1,0);memset(input,0x28,sizeof(input));
    const void *second=mp_source_cache_get(&c,4096,64,resolve,NULL);
    assert(second && second!=first && *(unsigned char*)first==0x17 && *(unsigned char*)second==0x28);
    assert(c.bytes==128);mp_source_cache_release(&c);assert(c.bytes==64);
    c.validate=1;assert(mp_source_cache_get(&c,4096,64,resolve,NULL)==second);
    input[63]^=1;assert(!mp_source_cache_get(&c,4096,64,resolve,NULL));assert(c.checks==2 && c.mismatches==1);
    mp_source_cache_dirty(&c,0,0,1);assert(c.bytes==64);
    mp_source_cache_release(&c);assert(c.bytes==0);c.validate=0;
    assert(!mp_source_cache_get(&c,0xdead,32,resolve,NULL));
    assert(!mp_source_cache_get(&c,4096,257,resolve,NULL));
    /* Four pinned generations fill a hash set. The fifth must fall back. */
    unsigned keys[5],n=0;
    for(unsigned key=32;n<5;key+=32) if(bank(key,64)==0)keys[n++]=key;
    const void *held[4];
    for(unsigned i=0;i<4;++i) { input[0]=i;held[i]=mp_source_cache_get(&c,keys[i],64,resolve,NULL);assert(held[i]); }
    assert(!mp_source_cache_get(&c,keys[4],64,resolve,NULL));
    for(unsigned i=0;i<4;++i)assert(*(unsigned char*)held[i]==i);
    mp_source_cache_release(&c);assert(mp_source_cache_get(&c,keys[4],64,resolve,NULL));assert(c.bytes==256);
    mp_source_cache_close(&c);assert(c.bytes==0);
    /* Budget eviction may never free a pinned pointer, even across sets. */
    c.budget=64;
    first=mp_source_cache_get(&c,32,64,resolve,NULL);assert(first);
    assert(!mp_source_cache_get(&c,64,64,resolve,NULL));
    mp_source_cache_release(&c);assert(mp_source_cache_get(&c,64,64,resolve,NULL));
    mp_source_cache_close(&c);
    c.budget=256;
    for(unsigned generation=0;generation<4096;++generation) {
        memset(input,generation&255,sizeof(input));
        first=mp_source_cache_get(&c,4096,128,resolve,NULL);assert(first);
        mp_source_cache_dirty(&c,4096,128,0);
        memset(input,(generation+1)&255,sizeof(input));
        second=mp_source_cache_get(&c,4096,128,resolve,NULL);assert(second);
        assert(*(unsigned char*)first==(generation&255));assert(*(unsigned char*)second==((generation+1)&255));
        mp_source_cache_release(&c);assert(c.bytes==128);
        mp_source_cache_dirty(&c,0,0,1);assert(!c.bytes);
    }
    mp_source_cache_close(&c);
    puts("Passed persistent cache identity, write overlap, 4096 immutable generations, validation, pinned set/budget exhaustion, eviction and cleanup.");
}
