#ifndef MP_SHIELD_MATERIAL_H
#define MP_SHIELD_MATERIAL_H
#include "layered_material.h"
/* Match the GX algebra, independent of fighter, player slot and image address.
 * Shields interpolate between a player tint and a highlight using the texture.
 * Substituting white for that texture loses the tint. The startup effect also
 * crossfades a second image with independent UVs; the held effect has one map. */
static inline int mp_shield_material(const unsigned c[16][30],unsigned n){
    static const unsigned held[4][20]={
        {15,15,15,14,7,7,7,7,0,0,0,1,3,0,0,0,0,0,0,0},
        {6,14,8,15,7,6,4,7,0,0,0,1,3,0,0,0,1,0,0,0},
        {15,15,15,14,7,7,7,0,0,0,0,1,0,0,0,0,0,0,0,0},
        {0,6,14,15,1,0,6,7,0,0,0,1,0,0,0,0,1,0,0,0}};
    static const unsigned start2[20]={15,14,8,15,7,1,4,7,0,0,0,1,0,0,0,0,1,0,0,0};
    if(n!=4||c[1][0]>=8||c[1][1]>=8)return 0;
    int start=c[2][1]<8;
    if(start&&(c[2][0]>=8||c[2][1]==c[1][1]))return 0;
    for(unsigned i=0;i<4;++i){
        if(c[i][2]!=255)return 0;
        if((i==0||i==3||(!start&&i==2))&&(c[i][0]!=255||c[i][1]!=255))return 0;
        for(unsigned j=0;j<20;++j){
            unsigned expected=start&&i==2?start2[j]:held[i][j];
            if(start&&i==1&&j==17)expected=3;
            if(start&&i==3&&j==4)expected=0;
            if(start&&i==3&&j==5)expected=3;
            if(c[i][3+j]!=expected)return 0;
        }
        unsigned k=c[i][23];if(k>=32||(k>=8&&k<12))return 0;
    }
    /* The final crossfade weight is scalar and shared by RGB and alpha. */
    unsigned weight=c[3][23],alpha=c[1][24];
    if(weight!=c[3][24]||(weight>=8&&weight<16)||alpha>=32||(alpha>=8&&alpha<16))return 0;
    return start?MP_FRAGMENT_SHIELD_START:MP_FRAGMENT_TINT;
}
static inline unsigned mp_fragment_color(const float c[4]){
    unsigned packed=0;for(unsigned j=0;j<4;++j){float v=c[j];if(v<0)v=0;if(v>1)v=1;packed|=(unsigned)(v*255.f+.5f)<<(8*j);}return packed;
}
static inline void mp_shield_parameters(unsigned mode,const float tint[4],const float highlight[4],const float base[4],float weight,float alpha,float low[4],MPTextureLayer*layer){
    float high[4],background[4];layer->mode=mode;
    for(unsigned j=0;j<4;++j){
        float a=j==3?0:tint[j],b=j==3?alpha:highlight[j];
        background[j]=base[j]*(1-weight);
        low[j]=mode==MP_FRAGMENT_TINT?background[j]+a*weight:a;
        high[j]=mode==MP_FRAGMENT_TINT?background[j]+b*weight:b;
    }
    layer->tint=mp_fragment_color(high);layer->base=mp_fragment_color(background);
    layer->blend=(unsigned)(weight*255.f+.5f);
}
#endif
