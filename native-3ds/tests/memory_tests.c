#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../port/engine/byte_memory.h"
int main(void){
    unsigned char a[512],b[512],source[512];for(unsigned i=0;i<512;++i)source[i]=(i*79+i/7)&255;
    for(unsigned src=0;src<16;++src)for(unsigned dst=0;dst<16;++dst)for(unsigned n=0;n<257;++n){
        memset(a,0xac,512);memset(b,0xac,512);memcpy(a+dst,source+src,n);assert(mp_memory_copy(b+dst,source+src,n)==b+dst);assert(!memcmp(a,b,512));
        memcpy(a,source,512);memcpy(b,source,512);memmove(a+dst,a+src,n);assert(mp_memory_move(b+dst,b+src,n)==b+dst);assert(!memcmp(a,b,512));
    }
    for(unsigned dst=0;dst<16;++dst)for(unsigned n=0;n<257;++n)for(int c=-1;c<257;c+=17){
        memcpy(a,source,512);memcpy(b,source,512);memset(a+dst,c,n);assert(mp_memory_set(b+dst,c,n)==b+dst);assert(!memcmp(a,b,512));
    }
    for(unsigned src=0;src<8;++src)for(unsigned dst=0;dst<8;++dst)for(unsigned n=0;n<257;++n){
        memcpy(a+src,source,n);memcpy(b+dst,source,n);assert(!mp_memory_compare(a+src,b+dst,n));
        for(unsigned i=0;i<n;++i){b[dst+i]^=0x80;int expected=memcmp(a+src,b+dst,n),actual=mp_memory_compare(a+src,b+dst,n);
            assert((expected<0)==(actual<0)&&(expected>0)==(actual>0));b[dst+i]^=0x80;}
    }
    puts("Byte memory: alignment, overlap, fill values, lengths, and guard bytes verified");
}
