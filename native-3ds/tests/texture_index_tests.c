#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../port/3ds/texture_index.h"
static MPTextureIndex index;
static struct{unsigned key,bucket,retired;}entry[MP_TEXTURE_SLOTS];
static unsigned count,next_key;
static void insert(unsigned bucket){
    assert(count<MP_TEXTURE_SLOTS);entry[count].key=++next_key;entry[count].bucket=bucket;entry[count].retired=0;
    mp_texture_index_add(&index,bucket,count++);
}
static void remove_slot(unsigned slot){
    mp_texture_index_remove(&index,entry[slot].bucket,slot);
    if(slot!=--count){mp_texture_index_remove(&index,entry[count].bucket,count);entry[slot]=entry[count];mp_texture_index_add(&index,entry[slot].bucket,slot);}
}
static void check(void){
    unsigned seen[MP_TEXTURE_SLOTS]={0};
    for(unsigned bucket=0;bucket<MP_TEXTURE_BUCKETS;++bucket)for(unsigned link=index.head[bucket];link;link=index.next[link-1]){
        unsigned i=link-1;assert(i<count&&entry[i].bucket==bucket);assert(++seen[i]==1);
    }
    for(unsigned i=0;i<count;++i){
        assert(seen[i]==1);int found=-1;
        for(unsigned link=index.head[entry[i].bucket];link;link=index.next[link-1])
            if(!entry[link-1].retired&&entry[link-1].key==entry[i].key)found=link-1;
        assert(found==(entry[i].retired?-1:(int)i));
    }
}
int main(void){
    /* Worst-case collisions, deleting head/middle/tail, and moving the
     * final dense-array entry across buckets must preserve every link. */
    for(unsigned i=0;i<MP_TEXTURE_SLOTS;++i)insert(7);check();
    while(count){remove_slot(count/2);check();}
    unsigned random=0x54455849;
    for(unsigned step=0;step<20000;++step){
        random=random*1664525+1013904223;
        if(!count||(count<MP_TEXTURE_SLOTS&&(random&3))){
            unsigned bucket=mp_texture_bucket(random&~31u,random>>16,random%15,32u<<(random%6),16u<<(random%7));insert(bucket);
        }else remove_slot((random>>8)%count);
        if(count)entry[random%count].retired=(random>>17)&1;
        check();
    }
    while(count)remove_slot(count-1);check();
    puts("Texture index: collisions, 20000 insert/retire/compact operations and complete removal passed");
}
