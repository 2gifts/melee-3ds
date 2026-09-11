#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../port/engine/vertex_decode.h"
int main(void){
    unsigned char data[32]={0};float out[5];
    for(unsigned offset=0;offset<4;++offset){
        unsigned char*p=data+offset;
        const unsigned char fixed[]={0xc0,0x00,0x40,0x00,0x7f,0xff};
        memcpy(p,fixed,sizeof fixed);out[0]=out[4]=1234;
        mp_vertex_components(out+1,p,3,3,1.f/16384);
        assert(out[0]==1234&&out[1]==-1&&out[2]==1&&out[3]==32767.f/16384&&out[4]==1234);
        mp_vertex_components(out+1,p,2,2,1.f/256);
        assert(out[1]==192&&out[2]==64&&out[3]==32767.f/16384);
        p[0]=0x80;p[1]=0x40;p[2]=0xff;
        mp_vertex_components(out+1,p,1,3,1.f/64);
        assert(out[1]==-2&&out[2]==1&&out[3]==-1.f/64);
        mp_vertex_components(out+1,p,0,3,1.f/16);
        assert(out[1]==8&&out[2]==4&&out[3]==255.f/16);
        const unsigned char fp[]={0x3f,0x80,0,0,0xc0,0x20,0,0,0x3d,0x80,0,0};
        memcpy(p,fp,sizeof fp);mp_vertex_components(out+1,p,4,3,999);
        assert(out[1]==1&&out[2]==-2.5f&&out[3]==.0625f);
        assert(out[0]==1234&&out[4]==1234);
    }
    puts("GX fixed-point and unaligned float vertex decoding passed");
}
