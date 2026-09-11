#ifndef MP_CACHE_LRU_H
#define MP_CACHE_LRU_H
#include <stdint.h>
/* Index zero is the sentinel. Real cache slots are indexed from one. The
 * renderer alone decides which entries are old enough to release safely. */
#define MP_CACHE_LRU_SLOTS 2048
typedef struct {
    uint16_t previous[MP_CACHE_LRU_SLOTS+1],next[MP_CACHE_LRU_SLOTS+1];
    uint16_t head,tail;
} MPCacheLru;
static inline void mp_cache_lru_remove(MPCacheLru*l,unsigned index){
    unsigned before=l->previous[index],after=l->next[index];
    if(before)l->next[before]=after;else if(l->head==index)l->head=after;
    if(after)l->previous[after]=before;else if(l->tail==index)l->tail=before;
    l->previous[index]=l->next[index]=0;
}
static inline void mp_cache_lru_touch(MPCacheLru*l,unsigned index){
    if(l->tail==index)return;
    mp_cache_lru_remove(l,index);
    l->previous[index]=l->tail;
    if(l->tail)l->next[l->tail]=index;else l->head=index;
    l->tail=index;
}
#endif
