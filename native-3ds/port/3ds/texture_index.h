#ifndef MP_TEXTURE_INDEX_H
#define MP_TEXTURE_INDEX_H
#include <stdint.h>
enum {MP_TEXTURE_BUCKETS=1024,MP_TEXTURE_SLOTS=1024};
typedef struct {uint16_t head[MP_TEXTURE_BUCKETS],next[MP_TEXTURE_SLOTS];}MPTextureIndex;
static unsigned mp_texture_bucket(uint32_t image,uint32_t palette,unsigned format,unsigned width,unsigned height){
    unsigned key=(image>>5)^(image>>16)^(palette>>5)^(palette>>17)^(format*0x9e37u)^width^(height<<5);
    return(key^(key>>9))&(MP_TEXTURE_BUCKETS-1);
}
static void mp_texture_index_add(MPTextureIndex *index,unsigned bucket,unsigned slot){
    index->next[slot]=index->head[bucket];index->head[bucket]=slot+1;
}
static void mp_texture_index_remove(MPTextureIndex *index,unsigned bucket,unsigned slot){
    uint16_t *link=&index->head[bucket];
    while(*link){unsigned i=*link-1;if(i==slot){*link=index->next[i];index->next[i]=0;return;}link=&index->next[i];}
}
#endif
