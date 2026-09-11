#ifndef MP_TEXTURE_STORAGE_H
#define MP_TEXTURE_STORAGE_H
#include <stdint.h>
/* PICA texture format IDs (libctru GPU_TEXCOLOR). Preserve the full source
 * precision; formats without an exact smaller representation stay RGBA8. */
static unsigned mp_texture_format(unsigned gx,unsigned palette){
    if(gx==0)return 10; /* L4; GX intensity also supplies alpha in the TEV. */
    if(gx==1)return 7;  /* L8 */
    if(gx==2)return 9;  /* LA4 */
    if(gx==3)return 5;  /* LA8 */
    if(gx==4)return 3;  /* RGB565 */
    if(gx>=8&&gx<=10){if(palette==1)return 3;if(palette==0)return 5;}
    return 0;
}
static unsigned mp_texture_bits(unsigned format){
    return format==10?4:format==7||format==9?8:format==3||format==5?16:32;
}
static void mp_texture_store(void*buffer,unsigned index,unsigned format,uint32_t rgba){
    uint8_t*bytes=buffer;unsigned r=rgba>>24,g=(rgba>>16)&255,b=(rgba>>8)&255,a=rgba&255;
    if(format==10){unsigned shift=(index&1)*4;bytes[index/2]=(bytes[index/2]&~(15u<<shift))|((r>>4)<<shift);}
    else if(format==7)bytes[index]=r;
    else if(format==9)bytes[index]=(r&0xf0)|(a>>4);
    else if(format==5)((uint16_t*)buffer)[index]=(r<<8)|a;
    else if(format==3)((uint16_t*)buffer)[index]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
    else ((uint32_t*)buffer)[index]=rgba;
}
#endif
