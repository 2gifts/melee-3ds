#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../port/3ds/texture_storage.h"
static uint32_t rgba(unsigned r,unsigned g,unsigned b,unsigned a){return(r<<24)|(g<<16)|(b<<8)|a;}
int main(void){
    union{uint32_t words[4];uint8_t bytes[16];uint16_t halves[8];}buf;
    for(unsigned i=0;i<16;++i)for(unsigned a=0;a<16;++a){
        memset(&buf,0xA5,sizeof(buf));
        mp_texture_store(&buf,2,10,rgba(i*17,i*17,i*17,i*17));
        assert(buf.bytes[1]==(0xA0|i)&&buf.bytes[0]==0xA5&&buf.bytes[2]==0xA5);
        mp_texture_store(&buf,3,10,rgba(a*17,a*17,a*17,a*17));
        assert(buf.bytes[1]==((a<<4)|i));
        mp_texture_store(&buf,4,9,rgba(i*17,i*17,i*17,a*17));
        assert((buf.bytes[4]>>4)*17==i*17&&(buf.bytes[4]&15)*17==a*17);
    }
    for(unsigned i=0;i<256;++i)for(unsigned a=0;a<256;++a){
        mp_texture_store(&buf,1,5,rgba(i,i,i,a));
        assert(buf.bytes[2]==a&&buf.bytes[3]==i);
        mp_texture_store(&buf,7,7,rgba(i,i,i,i));assert(buf.bytes[7]==i);
    }
    for(unsigned packed=0;packed<65536;++packed){
        unsigned r=packed>>11,g=(packed>>5)&63,b=packed&31;
        mp_texture_store(&buf,2,3,rgba((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2),255));
        assert(buf.halves[2]==packed);
    }
    assert(mp_texture_format(0,0)==10&&mp_texture_bits(10)==4);
    assert(mp_texture_format(1,0)==7&&mp_texture_bits(7)==8);
    assert(mp_texture_format(2,0)==9&&mp_texture_bits(9)==8);
    assert(mp_texture_format(3,0)==5&&mp_texture_bits(5)==16);
    assert(mp_texture_format(4,0)==3&&mp_texture_bits(3)==16);
    for(unsigned fmt=8;fmt<=10;++fmt){assert(mp_texture_format(fmt,0)==5);assert(mp_texture_format(fmt,1)==3);assert(mp_texture_format(fmt,2)==0);}
    for(unsigned fmt=5;fmt<=6;++fmt)assert(mp_texture_format(fmt,0)==0);
    assert(mp_texture_format(14,0)==0&&mp_texture_bits(0)==32);
    puts("Native luminance, alpha and RGB565 storage preserves source precision");
}
