#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef unsigned u32;typedef unsigned char u8;
typedef struct{u8 r,g,b,a;}GXColor;
#include "../port/engine/gx_shade.h"
#include "../port/engine/layered_material.h"
#include "turnip_material.inc"
static unsigned num_stages=4,tev_configuration[16][30];
static GXColor tev_color[4],konst_color[4];
static float reference_textures[8][4],*reference_texture;
#include "layered_reference.inc"
static unsigned seed=123456789;
static unsigned random_byte(void){seed=seed*1664525+1013904223;return seed>>24;}
int main(void){
    memcpy(tev_configuration,captured,sizeof(captured));assert(mp_layered_material(tev_configuration,4));
    assert(!mp_layered_material(tev_configuration,3));assert(!mp_layered_material(tev_configuration,5));
    unsigned rejected=0;
    for(unsigned stage=0;stage<4;++stage)for(unsigned field=3;field<=22;++field){
        unsigned original=tev_configuration[stage][field];tev_configuration[stage][field]^=1;
        assert(!mp_layered_material(tev_configuration,4));++rejected;tev_configuration[stage][field]=original;
    }
    float worst=0;unsigned checks=0;
    for(unsigned i=0;i<200000;++i){
        for(unsigned j=0;j<16;++j){((u8*)konst_color)[j]=random_byte();((u8*)tev_color)[j]=random_byte();}
        /* Cover all scalar interpolation selectors and all base color sources. */
        unsigned selectors[]={0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
        tev_configuration[0][23]=i%32;tev_configuration[1][23]=selectors[i%24];
        unsigned t0=(i%7),t1=(t0+1)%8;tev_configuration[1][1]=t0;tev_configuration[2][1]=t1;
        assert(mp_layered_material(tev_configuration,4));
        float raster[2][4],actual[4];
        for(unsigned j=0;j<4;++j){raster[0][j]=random_byte()/255.f;raster[1][j]=random_byte()/255.f;
            reference_textures[t0][j]=random_byte()/255.f;reference_textures[t1][j]=random_byte()/255.f;}
        shade(actual,raster);
        float weight=konst(tev_configuration[1][23],0),alpha=reference_textures[t1][3];
        for(unsigned j=0;j<4;++j){float expected;
            if(j<3)expected=fmaxf(0,konst(tev_configuration[0][23],j)*(1-weight)+reference_textures[t0][j]*weight-reference_textures[t1][j])*raster[0][j];
            else expected=(tev_color[1].a/255.f*(1-alpha)+alpha*alpha)*raster[0][3];
            float error=fabsf(actual[j]-expected);if(error>worst)worst=error;assert(error<.000001f);++checks;
        }
    }
    printf("Layered material: %u component comparisons against original TEV evaluator; %u altered programs rejected; maximum error %.9f\n",checks,rejected,worst);
    return 0;
}
