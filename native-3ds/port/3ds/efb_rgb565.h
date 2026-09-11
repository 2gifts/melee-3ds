#ifndef MP_EFB_RGB565_H
#define MP_EFB_RGB565_H
#include <stdint.h>
#include <string.h>

typedef uint32_t MPEfbWord __attribute__((may_alias));
/* Native little-endian ARM output, with GameCube big-endian RGB565 bytes.
 * GX copies frequently upscale a 320x240 native view to a 640-wide image.
 * Convert each distinct source pixel/row once, then write whole tile rows.
 * All crop rounding, edge clamping and padded texels match the CPU copier. */
static int mp_efb_rgb565(void*destination,const uint32_t*source,
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
                unsigned value=((color>>16)&0xf800)|((color>>13)&0x7e0)|((color>>11)&0x1f);
                uint16_t encoded=(uint16_t)((value>>8)|(value<<8));
                do{row[x++]=encoded;}while(x<width&&columns[x]==column);
            }
            previous_y=sy;
        }
        MPEfbWord*tile=(MPEfbWord*)((uint8_t*)destination+(y/4)*pitch*32+(y&3)*8);
        for(unsigned x=0;x<pitch*4;x+=4,tile+=8){
            tile[0]=(uint32_t)row[x]|((uint32_t)row[x+1]<<16);
            tile[1]=(uint32_t)row[x+2]|((uint32_t)row[x+3]<<16);
        }
    }
    return 1;
}
#endif
