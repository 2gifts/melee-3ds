#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef unsigned u32;typedef unsigned char u8;
typedef struct{u8 r,g,b,a;}GXColor;
#include "../port/engine/gx_shade.h"
#include "../port/engine/shield_material.h"
#include "shield_programs.inc"
static unsigned num_stages=4,tev_configuration[16][30];
static GXColor tev_color[4],konst_color[4];
static float reference_textures[8][4],*reference_texture;
#include "shield_reference.inc"
static unsigned seed=127671;
static unsigned random_byte(void){seed=seed*1664525+1013904223;return seed>>24;}
int main(void){
    unsigned comparisons=0,rejected=0;float worst=0;
    for(unsigned mode=0;mode<2;++mode){
        memcpy(tev_configuration,captured[mode],sizeof(tev_configuration));
        int kind=mode?MP_FRAGMENT_SHIELD_START:MP_FRAGMENT_TINT;
        assert(mp_shield_material(tev_configuration,4)==kind);
        assert(!mp_shield_material(tev_configuration,3));assert(!mp_shield_material(tev_configuration,5));
        for(unsigned stage=0;stage<4;++stage)for(unsigned field=2;field<=22;++field){
            unsigned saved=tev_configuration[stage][field];tev_configuration[stage][field]^=1;
            assert(!mp_shield_material(tev_configuration,4));++rejected;tev_configuration[stage][field]=saved;
        }
        for(unsigned i=0;i<100000;++i){
            for(unsigned j=0;j<16;++j){((u8*)konst_color)[j]=random_byte();((u8*)tev_color)[j]=random_byte();}
            /* Permute legal konst selectors, map slots and final weights. */
            unsigned scalar[]={0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
            for(unsigned j=0;j<3;++j){unsigned k=random_byte()%28;tev_configuration[j][23]=k<8?k:k+4;}
            tev_configuration[1][24]=scalar[random_byte()%24];
            tev_configuration[3][23]=tev_configuration[3][24]=scalar[random_byte()%24];
            unsigned t1=i%8,t0=(t1+1)%8;tev_configuration[1][1]=t1;
            if(mode)tev_configuration[2][1]=t0;
            assert(mp_shield_material(tev_configuration,4)==kind);
            for(unsigned j=0;j<4;++j){reference_textures[t0][j]=random_byte()/255.f;reference_textures[t1][j]=random_byte()/255.f;}
            float tint[4],highlight[4],base[4],low[4],actual[4],raster[2][4]={{0}};
            for(unsigned j=0;j<4;++j){tint[j]=konst(tev_configuration[0][23],j);highlight[j]=konst(tev_configuration[1][23],j);base[j]=konst(tev_configuration[2][23],j);}
            base[3]=tev_color[1].a/255.f;float weight=konst(tev_configuration[3][23],0),alpha=konst(tev_configuration[1][24],3);
            MPTextureLayer layer={0};mp_shield_parameters(kind,tint,highlight,base,weight,alpha,low,&layer);
            shade(actual,raster);
            for(unsigned j=0;j<4;++j){
                float t=reference_textures[t1][j],high=((layer.tint>>(j*8))&255)/255.f;
                float result=low[j]*(1-t)+high*t;
                if(mode)result=result*(layer.blend/255.f)+reference_textures[t0][j]*(((layer.base>>(j*8))&255)/255.f);
                float error=fabsf(result-actual[j]);if(error>worst)worst=error;
                assert(error<=2.f/255.f);++comparisons;
            }
        }
    }
    printf("Shield materials: %u GX component comparisons, %u altered programs rejected; max 8-bit parameter error %.6f\n",comparisons,rejected,worst);
    return 0;
}
