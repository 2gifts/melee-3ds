#ifndef MP_BYTE_MEMORY_H
#define MP_BYTE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t MPWord __attribute__((__may_alias__));
static int mp_memory_compare(const void*left,const void*right,size_t n){
    const unsigned char*a=left,*b=right;
    if((((uintptr_t)a^(uintptr_t)b)&3)==0){
        while(n&&((uintptr_t)a&3)){if(*a!=*b)return *a-*b;++a;++b;--n;}
        while(n>=16){const MPWord*x=(const MPWord*)a,*y=(const MPWord*)b;
            if((x[0]^y[0])|(x[1]^y[1])|(x[2]^y[2])|(x[3]^y[3]))break;
            a+=16;b+=16;n-=16;
        }
        while(n>=4&&*(const MPWord*)a==*(const MPWord*)b){a+=4;b+=4;n-=4;}
    }
    while(n--){if(*a!=*b)return *a-*b;++a;++b;}return 0;
}
/* Same-endian word loads/stores preserve bytes in both BE8 and native mode. */
static void*mp_memory_copy(void*dst,const void*src,size_t n){
    unsigned char*a=dst;const unsigned char*b=src;
    if((((uintptr_t)a^(uintptr_t)b)&3)==0){
        while(n&&((uintptr_t)a&3)){*a++=*b++;--n;}
        MPWord*d=(MPWord*)a;const MPWord*s=(const MPWord*)b;
        while(n>=32){d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];d[4]=s[4];d[5]=s[5];d[6]=s[6];d[7]=s[7];d+=8;s+=8;n-=32;}
        while(n>=4){*d++=*s++;n-=4;}a=(unsigned char*)d;b=(const unsigned char*)s;
    }
    while(n--)*a++=*b++;return dst;
}
static void*mp_memory_set(void*dst,int value,size_t n){
    unsigned char*a=dst;unsigned char byte=value;
    while(n&&((uintptr_t)a&3)){*a++=byte;--n;}
    MPWord*d=(MPWord*)a;MPWord word=(MPWord)byte*0x01010101u;
    while(n>=32){d[0]=word;d[1]=word;d[2]=word;d[3]=word;d[4]=word;d[5]=word;d[6]=word;d[7]=word;d+=8;n-=32;}
    while(n>=4){*d++=word;n-=4;}a=(unsigned char*)d;while(n--)*a++=byte;return dst;
}
static void*mp_memory_move(void*dst,const void*src,size_t n){
    unsigned char*a=dst;const unsigned char*b=src;
    if((uintptr_t)a<=(uintptr_t)b)return mp_memory_copy(dst,src,n);
    a+=n;b+=n;
    if((((uintptr_t)a^(uintptr_t)b)&3)==0){
        while(n&&((uintptr_t)a&3)){*--a=*--b;--n;}
        MPWord*d=(MPWord*)a;const MPWord*s=(const MPWord*)b;
        while(n>=32){d[-1]=s[-1];d[-2]=s[-2];d[-3]=s[-3];d[-4]=s[-4];d[-5]=s[-5];d[-6]=s[-6];d[-7]=s[-7];d[-8]=s[-8];d-=8;s-=8;n-=32;}
        while(n>=4){*--d=*--s;n-=4;}a=(unsigned char*)d;b=(const unsigned char*)s;
    }
    while(n--)*--a=*--b;return dst;
}
#endif
