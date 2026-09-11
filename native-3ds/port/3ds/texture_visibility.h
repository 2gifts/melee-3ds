#ifndef MP_TEXTURE_VISIBILITY_H
#define MP_TEXTURE_VISIBILITY_H
#include <stdint.h>
static inline unsigned mp_texture_source_bytes(unsigned format,unsigned width,unsigned height){
    unsigned bw=4,bh=4,bytes=32;
    if(format==0||format==8||format==14||format==0x20)bw=bh=8;
    else if(format==1||format==2||format==9||format==0x22||(format>=0x27&&format<=0x2a))bw=8;
    else if(format==6)bytes=64;
    return ((width+bw-1)/bw)*((height+bh-1)/bh)*bytes;
}
static inline int mp_texture_range_overlap(uint32_t first,uint32_t first_size,uint32_t second,uint32_t second_size){
    return first_size&&second_size&&(uint64_t)first<(uint64_t)second+second_size&&(uint64_t)second<(uint64_t)first+first_size;
}
static inline int mp_texture_source_overlap(uint32_t image,uint32_t bytes,uint32_t palette,uint32_t palette_entries,uint32_t changed,uint32_t changed_bytes){
    return (image&&mp_texture_range_overlap(image,bytes,changed,changed_bytes))||
        (palette&&mp_texture_range_overlap(palette,palette_entries*2,changed,changed_bytes));
}
#endif
