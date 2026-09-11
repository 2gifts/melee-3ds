#ifndef MP_SCISSOR_H
#define MP_SCISSOR_H
/* GX uses a 640x480 top-left rectangle. PICA renders the rotated 240x400
 * target from its bottom-left. Preserve the GX half-open pixel-center test. */
static inline int mp_scissor_rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned out[4]) {
    if(x>640)x=640;if(y>480)y=480;
    if(w>640-x)w=640-x;if(h>480-y)h=480-y;
    out[0]=(481-(y+h))/2;out[1]=(3204-5*(x+w))/8;
    out[2]=(481-y)/2;out[3]=(3204-5*x)/8;
    return out[0]<out[2]&&out[1]<out[3];
}
static inline int mp_scissor_rect_width(unsigned x,unsigned y,unsigned w,unsigned h,unsigned width,unsigned out[4]) {
    if(width!=320)return mp_scissor_rect(x,y,w,h,out);
    if(x>640)x=640;if(y>480)y=480;
    if(w>640-x)w=640-x;if(h>480-y)h=480-y;
    out[0]=(481-(y+h))/2;out[1]=40+(641-(x+w))/2;
    out[2]=(481-y)/2;out[3]=40+(641-x)/2;
    return out[0]<out[2]&&out[1]<out[3];
}
#endif
