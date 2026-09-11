#include <assert.h>
#include <stdio.h>
#include "../port/3ds/texture_visibility.h"
static uint32_t seed=0x11228833;
static uint32_t random_word(void){seed=seed*1664525+1013904223;return seed;}
static int reference(uint32_t a,uint32_t n,uint32_t b,uint32_t m){
    if(!n||!m)return 0;
    uint64_t first=a,last=(uint64_t)a+n-1,other_first=b,other_last=(uint64_t)b+m-1;
    return !(last<other_first||other_last<first);
}
int main(void){
    for(unsigned i=0;i<100000;++i){
        uint32_t a=random_word(),b=(i&1)?a+(random_word()%512)-256:random_word();
        uint32_t n=random_word()%512,m=random_word()%512;
        assert(mp_texture_range_overlap(a,n,b,m)==reference(a,n,b,m));
    }
    assert(!mp_texture_range_overlap(100,32,132,20));
    assert(mp_texture_range_overlap(100,32,131,20));
    assert(mp_texture_range_overlap(0xfffffff0u,32,0xffffffffu,1));
    assert(!mp_texture_range_overlap(0xfffffff0u,32,0,16));
    assert(mp_texture_source_overlap(100,32,500,16,516,1));
    assert(!mp_texture_source_overlap(100,32,500,16,532,1));
    assert(!mp_texture_source_overlap(100,32,0,16,0,8));
    assert(mp_texture_source_overlap(100,32,0,0,99,2));
    unsigned formats[]={0,1,2,3,4,5,6,8,9,10,14,0x20,0x22,0x23,0x27,0x28,0x29,0x2a,0x2b,0x2c};
    unsigned widths[]={8,8,8,4,4,4,4,8,8,4,8,8,8,4,8,8,8,8,4,4};
    unsigned heights[]={8,4,4,4,4,4,4,8,4,4,8,8,4,4,4,4,4,4,4,4};
    unsigned sizes=0;
    for(unsigned f=0;f<sizeof(formats)/sizeof(*formats);++f)for(unsigned w=0;w<=65;++w)for(unsigned h=0;h<=65;++h){
        unsigned n=0;for(unsigned y=0;y<h;y+=heights[f])for(unsigned x=0;x<w;x+=widths[f])n+=formats[f]==6?64:32;
        assert(mp_texture_source_bytes(formats[f],w,h)==n);++sizes;
    }
    printf("Texture visibility: 100,000 overlap cases, %u independently counted tile sizes, boundary and palette aliases passed\n",sizes);
}
