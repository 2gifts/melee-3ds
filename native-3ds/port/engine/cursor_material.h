#ifndef MP_CURSOR_MATERIAL_H
#define MP_CURSOR_MATERIAL_H
#include "layered_material.h"
/* A constant-coloured base texture with a second texture alpha-blended over
 * it (HSD colormap/alphamap ALPHA_MASK on the second TObj):
 *   P   = C * T0              (RGB and alpha; C is a register or konst)
 *   out = mix(P, T1, T1.a)    (alpha: mix(P.a, T1.a, T1.a))
 * No raster colour is read. The stage select cursor draws its ring as T0 and
 * its animated port label ("P1".."P4") as T1; with one texture per material
 * the label was never drawn. Match the algebra, not an image or menu. */
static inline int mp_cursor_constant_inputs(const unsigned c[16][30]){
    unsigned rgb=c[0][4],alpha=c[0][8],ksel=c[0][23],asel=c[0][24];
    /* C0..C2 or a konst colour (scalar 0..7, K0..K3 12..15, components 16..31). */
    if(rgb!=2&&rgb!=4&&rgb!=6&&!(rgb==14&&ksel<32&&(ksel<8||ksel>=12)))return 0;
    /* A0..A2 or a konst alpha (scalar 0..7, components 16..31). */
    if(alpha!=1&&alpha!=2&&alpha!=3&&!(alpha==6&&asel<32&&(asel<8||asel>=16)))return 0;
    return 1;
}
static inline int mp_cursor_material(const unsigned c[16][30],unsigned n){
    /* Fields 3..22; 99 marks stage 0's constant inputs, checked above. */
    static const unsigned expected[2][20]={
        {15,99,8,15,7,99,4,7,0,0,0,1,0,0,0,0,1,0,0,0},
        {0,8,9,15,0,4,4,7,0,0,0,1,0,0,0,0,1,0,0,0}};
    if(n!=2||c[0][1]>=8||c[1][1]>=8||c[0][1]==c[1][1]||c[0][2]!=255||c[1][2]!=255)return 0;
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<20;++j)
        if(expected[i][j]!=99&&c[i][3+j]!=expected[i][j])return 0;
    return mp_cursor_constant_inputs(c);
}
/* GX konst selector value (component 0..3) from the four konst colours. */
static inline float mp_cursor_konst(unsigned sel,unsigned component,const unsigned char konst[4][4]){
    if(sel<8)return(8-sel)/8.f;
    if(sel>=12&&sel<16)return konst[sel-12][component]/255.f;
    if(sel>=16&&sel<32)return konst[sel&3][(sel-16)/4]/255.f;
    return 0;
}
/* Stage 0's constant C as packed RGBA8 (red in the low byte). colors[] is
 * indexed by GXTevRegID (PREV, REG0, REG1, REG2); stage 0 runs first, so
 * every register still holds its initial value. */
static inline unsigned mp_cursor_constant(const unsigned c[16][30],const unsigned char colors[4][4],const unsigned char konst[4][4]){
    float value[4];unsigned packed=0;
    for(unsigned j=0;j<3;++j)value[j]=c[0][4]<8?colors[c[0][4]/2][j]/255.f:mp_cursor_konst(c[0][23],j,konst);
    value[3]=c[0][8]<4?colors[c[0][8]][3]/255.f:mp_cursor_konst(c[0][24],3,konst);
    for(unsigned j=0;j<4;++j)packed|=(unsigned)(value[j]*255.f+.5f)<<(8*j);
    return packed;
}
#endif
