#ifndef MP_BLEND_STATE_H
#define MP_BLEND_STATE_H
/* Native PICA blend constants; verified against libctru in renderer.c. */
typedef struct {unsigned equation,src,dst,src_alpha,dst_alpha,logic;int logical;} MPBlendState;
static MPBlendState mp_blend_state(unsigned packed,unsigned source,unsigned destination){
    const unsigned src[]={0,1,4,5,6,7,8,9},dst[]={0,1,2,3,6,7,8,9};
    const unsigned srca[]={0,1,8,9,6,7,8,9},dsta[]={0,1,6,7,6,7,8,9};
    const unsigned logic[]={0,1,2,3,13,6,11,9,10,12,7,14,5,15,8,4};
    MPBlendState b={.src=1,.src_alpha=1};unsigned mode=packed&255;
    if(mode==2){b.logical=1;b.logic=logic[(packed>>8)&15];return b;}
    if(mode==3){b.equation=2;b.dst=b.dst_alpha=1;return b;}
    if(mode!=1)return b;
    b.src=src[source&7];b.dst=dst[destination&7];b.src_alpha=srca[source&7];b.dst_alpha=dsta[destination&7];
    /* GX RGB targets read a destination alpha of one. This includes the
     * source-factor aliases DST_COLOR when evaluating the alpha channel. */
    if(!(packed&0x10000)){
        unsigned*f[]={&b.src,&b.dst,&b.src_alpha,&b.dst_alpha};
        for(unsigned i=0;i<4;++i){if(*f[i]==8)*f[i]=1;else if(*f[i]==9)*f[i]=0;}
    }
    return b;
}
#endif
