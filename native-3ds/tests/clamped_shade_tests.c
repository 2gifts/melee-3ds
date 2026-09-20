#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"clamped shade assertion %d: %s\n",__LINE__,#x);exit(86);}}while(0)
typedef uint8_t u8;typedef unsigned u32;
typedef struct{u8 r,g,b,a;} GXColor;
static unsigned num_stages,tev_configuration[16][30];
static GXColor tev_color[4],konst_color[4];
#include "../port/engine/gx_shade.h"
#include "../port/engine/clamped_shade.h"
#include "clamped-reference.inc"
typedef struct{unsigned stages,config[16][30];u8 colors[16],konst[16];float alpha[2];} Fixture;
#include "clamped-fixtures.inc"
static unsigned seed=0xc15a029f,accepted,rejected,checks,captured;
static float maximum_error,maximum_visible_error;
static unsigned random32(void){seed=seed*1664525u+1013904223u;return seed;}
static float unit(void){return (random32()&65535)/65535.f;}
static unsigned key_checks,key_bits_removed;
static void check_key(void){
    unsigned color_mask,konst_mask,saved_seed=seed;
    shade_constant_mask(tev_configuration,num_stages,&color_mask,&konst_mask);
    GXColor saved_color[4],saved_konst[4];memcpy(saved_color,tev_color,16);memcpy(saved_konst,konst_color,16);
    float raster[8][2][4],reference[8][4],actual[4];
    for(unsigned i=0;i<8;++i){for(unsigned j=0;j<8;++j)((float*)raster[i])[j]=unit();shade(reference[i],raster[i]);}
    for(unsigned i=0;i<16;++i){
        if(!(color_mask&(1u<<i))){((u8*)tev_color)[i]^=0xff;++key_bits_removed;}
        if(!(konst_mask&(1u<<i))){((u8*)konst_color)[i]^=0xff;++key_bits_removed;}
    }
    for(unsigned i=0;i<8;++i){shade(actual,raster[i]);CHECK(!memcmp(reference[i],actual,sizeof(actual)));++key_checks;}
    memcpy(tev_color,saved_color,16);memcpy(konst_color,saved_konst,16);seed=saved_seed;
}
static void evaluate(const float alpha[2],unsigned samples,int must_accept){
    check_key();
    float colors[4][4],konst[4][4];
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){colors[i][j]=((u8*)tev_color)[i*4+j]/255.f;konst[i][j]=((u8*)konst_color)[i*4+j]/255.f;}
    MPClampedPlan p;mp_clamped_compile(&p,tev_configuration,num_stages,colors,konst,alpha);
    if(must_accept)CHECK(p.valid);
    if(!p.valid){++rejected;return;}++accepted;
    for(unsigned sample=0;sample<samples;++sample){float raster[2][4],expected[4],actual[4];
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)raster[i][j]=unit();
        if(sample<16)for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)
            raster[i][j]=(sample&(1u<<(i+2*(j==3))))?1.f:0.f;
        for(unsigned i=0;i<2;++i)if(alpha[i]>=0)raster[i][3]=alpha[i];
        shade(expected,raster);mp_clamped_apply(&p,actual,raster);
        for(unsigned j=0;j<4;++j){float error=fabsf(expected[j]-actual[j]);
            if(error>maximum_error)maximum_error=error;
            float visible=fabsf(fminf(1,fmaxf(0,expected[j]))-fminf(1,fmaxf(0,actual[j])));
            if(visible>maximum_visible_error)maximum_visible_error=visible;
            /* Affine reassociation has float32 rounding; large unclamped
             * outputs can differ by a few ULPs before color saturation. */
            if(!(error<=.00002f+.000002f*fabsf(expected[j])&&visible<=.00002f)){fprintf(stderr,"error %g visible %g expected %g actual %g at material %u stages %u sample %u component %u\n",error,visible,expected[j],actual[j],accepted,num_stages,sample,j);exit(86);}
            ++checks;}
    }
}
int main(void){
    for(unsigned i=0;i<sizeof(fixtures)/sizeof(*fixtures);++i){const Fixture*f=fixtures+i;
        num_stages=f->stages;memcpy(tev_configuration,f->config,sizeof(tev_configuration));
        memcpy(tev_color,f->colors,16);memcpy(konst_color,f->konst,16);
        evaluate(f->alpha,65536,1);++captured;
        /* The original programs must remain correct under animated colors,
         * konst selectors' real byte values, and opacity changes. */
        for(unsigned variant=0;variant<256;++variant){
            for(unsigned j=0;j<16;++j){((u8*)tev_color)[j]=random32()>>24;((u8*)konst_color)[j]=random32()>>24;}
            evaluate(f->alpha,512,0);
        }
    }
    for(unsigned program=0;program<20000;++program){
        memset(tev_configuration,0,sizeof(tev_configuration));num_stages=1+(random32()>>24)%6;
        for(unsigned j=0;j<16;++j){((u8*)tev_color)[j]=random32()>>24;((u8*)konst_color)[j]=random32()>>24;}
        for(unsigned i=0;i<num_stages;++i){unsigned*s=tev_configuration[i];s[2]=(random32()>>24)%8;
            for(unsigned j=3;j<=6;++j)s[j]=(random32()>>24)%16;
            for(unsigned j=7;j<=10;++j)s[j]=(random32()>>24)%8;
            s[11]=(random32()>>24)%2;s[16]=(random32()>>24)%2;
            if(!(program%13)){s[11]=8+(program%8);s[16]=8+(program%8);}
            s[12]=(random32()>>24)%3;s[17]=(random32()>>24)%3;
            s[13]=(random32()>>24)%4;s[18]=(random32()>>24)%4;
            s[14]=(random32()>>24)%2;s[19]=(random32()>>24)%2;
            s[15]=(random32()>>24)%4;s[20]=(random32()>>24)%4;
            s[23]=(random32()>>24)%32;s[24]=(random32()>>24)%32;
        }
        float alpha[2]={program%3? -1:unit(),program%5? -1:unit()};evaluate(alpha,32,0);
    }
    CHECK(accepted>5000&&rejected>1000&&captured==3);
    printf("PASS %u captured materials, %u accepted programs, %u rejected programs, %u component comparisons; max raw error %.9g, saturated error %.9g\n",captured,accepted,rejected,checks,maximum_error,maximum_visible_error);
    printf("PASS %u exact color-key perturbation checks, %u unused input bytes changed\n",key_checks,key_bits_removed);
}
