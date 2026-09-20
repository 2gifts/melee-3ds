#ifndef MP_EFB_RGB5A3_H
#define MP_EFB_RGB5A3_H
#include <stdint.h>
#include <string.h>

typedef uint32_t MPEfb5Word __attribute__((may_alias));
/* RGB5A3 uses RGB555 for alpha >= 224, otherwise RGB444 and alpha3.
 * Cache distinct native source rows/pixels, then write four-pixel tile rows.
 * This retains the CPU-visible GX image exactly, including edge/padding bytes;
 * no GPU-only alias or lower-quality texture replaces a Results portrait. */
static int mp_efb_rgb5a3(void*destination,const uint32_t*source,
    unsigned width,unsigned height,unsigned left,unsigned top,
    unsigned source_width,unsigned source_height,unsigned screen_width)
{
    if(!width||!height||width>1024||height>1024||((uintptr_t)destination&3))return 0;
    unsigned columns[1024];uint16_t row[1024];
    unsigned pitch=(width+3)/4,previous_y=~0u;
    memset(destination,0,pitch*((height+3)/4)*32);
    for(unsigned x=0;x<width;++x){
        unsigned sx=(400-screen_width)/2+(left+x*source_width/width)*screen_width/640;
        if(sx>=400)sx=399;
        columns[x]=sx*240;
    }
    for(unsigned x=width;x<pitch*4;++x)row[x]=0;
    for(unsigned y=0;y<height;++y){
        unsigned sy=(top+y*source_height/height)*240/480;
        if(sy>=240)sy=239;
        if(sy!=previous_y){
            const uint32_t*line=source+239-sy;
            for(unsigned x=0;x<width;){
                unsigned column=columns[x],color=line[column];
                unsigned value=(color&255)>=224
                    ?0x8000|((color>>17)&0x7c00)|((color>>14)&0x3e0)|((color>>11)&0x1f)
                    :((color<<7)&0x7000)|((color>>20)&0xf00)|((color>>16)&0xf0)|((color>>12)&0xf);
                uint16_t encoded=(uint16_t)((value>>8)|(value<<8));
                do{row[x++]=encoded;}while(x<width&&columns[x]==column);
            }
            previous_y=sy;
        }
        MPEfb5Word*tile=(MPEfb5Word*)((uint8_t*)destination+(y/4)*pitch*32+(y&3)*8);
        for(unsigned x=0;x<pitch*4;x+=4,tile+=8){
            tile[0]=(uint32_t)row[x]|((uint32_t)row[x+1]<<16);
            tile[1]=(uint32_t)row[x+2]|((uint32_t)row[x+3]<<16);
        }
    }
    return 1;
}
#endif
