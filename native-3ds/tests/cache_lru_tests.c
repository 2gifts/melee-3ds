#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../port/3ds/cache_lru.h"
static MPCacheLru lru;
static unsigned order[MP_CACHE_LRU_SLOTS],count;
static unsigned seed=0x711ac9;
static unsigned random_word(void){seed=seed*1664525+1013904223;return seed;}
static void remove_reference(unsigned index){
    for(unsigned i=0;i<count;++i)if(order[i]==index){memmove(order+i,order+i+1,(count-i-1)*sizeof(*order));--count;break;}
}
static void verify(void){
    unsigned seen[MP_CACHE_LRU_SLOTS+1]={0},p=lru.head,before=0;
    assert(count<=MP_CACHE_LRU_SLOTS);
    for(unsigned i=0;i<count;++i){assert(p==order[i]&&p>0&&p<=MP_CACHE_LRU_SLOTS&&!seen[p]);seen[p]=1;
        assert(lru.previous[p]==before);before=p;p=lru.next[p];}
    assert(p==0&&lru.tail==before);
    p=lru.tail;unsigned after=0;
    for(unsigned i=count;i;--i){assert(p==order[i-1]&&lru.next[p]==after);after=p;p=lru.previous[p];}
    assert(p==0&&lru.head==after);
    for(unsigned i=1;i<=MP_CACHE_LRU_SLOTS;++i)if(!seen[i])assert(!lru.previous[i]&&!lru.next[i]);
}
int main(void){
    verify();mp_cache_lru_remove(&lru,7);verify();
    for(unsigned i=1;i<=MP_CACHE_LRU_SLOTS;++i){mp_cache_lru_touch(&lru,i);order[count++]=i;}verify();
    unsigned touched[MP_CACHE_LRU_SLOTS+1]={0};
    for(unsigned step=0;step<100000;++step){
        /* Use upper bits: taking every other LCG output's low bits would
         * exercise only half the slots in this two-draw loop. */
        unsigned index=1+(random_word()>>8)%MP_CACHE_LRU_SLOTS,action=(random_word()>>24)%5;
        ++touched[index];
        if(action<3){remove_reference(index);order[count++]=index;mp_cache_lru_touch(&lru,index);}
        else{remove_reference(index);mp_cache_lru_remove(&lru,index);}
        verify();
    }
    for(unsigned i=1;i<=MP_CACHE_LRU_SLOTS;++i)assert(touched[i]);
    while(count){unsigned oldest=order[0];remove_reference(oldest);mp_cache_lru_remove(&lru,oldest);verify();}
    puts("LRU: 100,000 operations across 2,048 slots match an independent ordered-array reference; both directions and removed entries verified");
    return 0;
}
