#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../port/engine/gx_shade.h"
static void near(float a,float b){assert(fabsf(a-b)<.00001f);}
int main(void){
    unsigned config[16][30]={{0}};float colors[4][4]={{0}},konst[4][4]={{.2f,.1f,.3f,.4f}};
    MPShadePlan p;
    mp_shade_compile(&p,config,0,colors,konst);assert(p.valid);
    float ras[4]={.25f,.5f,.75f,.4f},out[4];mp_shade_apply(&p,out,ras);
    for(unsigned j=0;j<4;++j)near(out[j],ras[j]);
    unsigned*s=config[0];s[3]=15;s[4]=8;s[5]=10;s[6]=15;s[7]=7;s[8]=4;s[9]=5;s[10]=7;s[14]=s[19]=1;
    mp_shade_compile(&p,config,1,colors,konst);assert(p.valid);
    for(unsigned i=0;i<256;++i){for(unsigned j=0;j<4;++j)ras[j]=((i*(j*12+13)+j*3)%256)/255.f;mp_shade_apply(&p,out,ras);for(unsigned j=0;j<4;++j)near(out[j],ras[j]);}
    s=config[1];s[3]=15;s[4]=0;s[5]=13;s[6]=14;s[23]=12;s[7]=s[8]=s[9]=7;s[10]=0;s[14]=s[19]=1;
    mp_shade_compile(&p,config,2,colors,konst);assert(p.valid);
    for(unsigned i=0;i<256;++i){for(unsigned j=0;j<4;++j)ras[j]=((i*(j*12+13)+j*3)%256)/255.f;mp_shade_apply(&p,out,ras);for(unsigned j=0;j<3;++j)near(out[j],ras[j]*.5f+konst[0][j]);near(out[3],ras[3]);}
    config[0][4]=10;config[0][5]=10;mp_shade_compile(&p,config,1,colors,konst);assert(!p.valid);
    config[0][4]=8;config[0][5]=10;config[0][12]=1;mp_shade_compile(&p,config,2,colors,konst);assert(!p.valid);
    mp_shade_compile(&p,config,1,colors,konst);assert(p.valid&&p.clamp[0]);
    ras[0]=.9f;ras[1]=.1f;ras[2]=.6f;mp_shade_apply(&p,out,ras);near(out[0],1);near(out[1],.6f);near(out[2],1);
    config[0][4]=10;config[0][5]=11;config[0][12]=0;
    mp_shade_compile(&p,config,1,colors,konst);assert(!p.valid);
    mp_shade_compile_alpha(&p,config,1,colors,konst,.5f);assert(p.valid);
    for(unsigned i=0;i<256;++i){ras[0]=i/255.f;ras[1]=1-ras[0];ras[2]=.3f;ras[3]=.5f;
        mp_shade_apply(&p,out,ras);for(unsigned j=0;j<3;++j)near(out[j],ras[j]*.5f);near(out[3],.5f);}
    config[0][2]=4;config[0][4]=8;config[0][5]=10;
    s=config[1];s[2]=5;s[3]=15;s[4]=10;s[5]=14;s[6]=0;s[23]=12;
    s[7]=s[8]=s[9]=7;s[10]=0;s[14]=s[19]=1;
    mp_shade_compile(&p,config,2,colors,konst);assert(p.valid&&p.second);
    float both[2][4];
    for(unsigned i=0;i<256;++i){for(unsigned ch=0;ch<2;++ch)for(unsigned j=0;j<4;++j)both[ch][j]=((i*(j*12+13)+j*3+ch*53)%256)/255.f;
        mp_shade_apply_channels(&p,out,both);for(unsigned j=0;j<3;++j)near(out[j],fminf(1,both[0][j]+konst[0][j]*both[1][j]));near(out[3],both[0][3]);}
    s[2]=3;s[3]=s[4]=s[5]=15;s[6]=10;s[10]=5;mp_shade_compile(&p,config,2,colors,konst);assert(p.valid&&p.second);
    mp_shade_apply_channels(&p,out,both);for(unsigned j=0;j<4;++j)near(out[j],both[1][3]);
    s[2]=6;mp_shade_compile(&p,config,2,colors,konst);mp_shade_apply_channels(&p,out,both);for(unsigned j=0;j<4;++j)near(out[j],0);
    s[2]=5;s[3]=15;s[4]=0;s[5]=10;s[6]=15;mp_shade_compile(&p,config,2,colors,konst);assert(!p.valid);
    mp_raster_select(out,both,4);for(unsigned j=0;j<4;++j)near(out[j],both[0][j]);
    mp_raster_select(out,both,5);for(unsigned j=0;j<4;++j)near(out[j],both[1][j]);
    mp_raster_select(out,both,3);for(unsigned j=0;j<4;++j)near(out[j],both[1][3]);
    /* Hardware-match fallback: two lights add past 1, followed by a texture
     * identity stage. Its clamp can move to the final vertex shader output. */
    memset(config,0,sizeof(config));
    s=config[0];s[2]=4;s[3]=15;s[4]=14;s[5]=10;s[6]=15;s[23]=13;s[14]=1;
    s[7]=s[8]=s[9]=s[10]=7;
    s=config[1];s[2]=5;s[3]=15;s[4]=14;s[5]=10;s[6]=0;s[23]=12;s[14]=1;
    s[7]=s[8]=s[9]=7;s[10]=0;
    s=config[2];s[2]=4;s[3]=15;s[4]=0;s[5]=8;s[6]=15;s[14]=1;
    s[7]=7;s[8]=1;s[9]=5;s[10]=7;s[19]=1;
    for(unsigned j=0;j<3;++j){konst[0][j]=1;konst[1][j]=179.f/255;}
    colors[1][3]=1;
    float alphas[2]={1,1};mp_shade_compile_channels(&p,config,3,colors,konst,alphas);
    assert(p.valid&&p.second&&p.clamp[0]);
    for(unsigned a=0;a<257;++a)for(unsigned b=0;b<257;++b){
        for(unsigned j=0;j<4;++j){both[0][j]=a/256.f;both[1][j]=b/256.f;}
        both[0][3]=both[1][3]=1;mp_shade_apply_channels(&p,out,both);
        for(unsigned j=0;j<3;++j)near(out[j],fminf(1,179.f/255*both[0][j]+both[1][j]));
    }
    /* Moving the clamp through a non-identity scale is not allowed. */
    config[2][5]=13;mp_shade_compile_channels(&p,config,3,colors,konst,alphas);assert(!p.valid);
    config[2][5]=8;config[2][12]=1;mp_shade_compile_channels(&p,config,3,colors,konst,alphas);assert(!p.valid);
    puts("Affine shading and 66,049 deferred-clamp combinations verified");
}
