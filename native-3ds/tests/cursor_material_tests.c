#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef unsigned u32;typedef unsigned char u8;
typedef struct{u8 r,g,b,a;}GXColor;
#include "../port/engine/gx_shade.h"
#include "../port/engine/shield_material.h"
#include "../port/engine/cursor_material.h"
#include "cursor_program.inc"
#include "cursor_shield_programs.inc"
static unsigned num_stages=2,tev_configuration[16][30];
static GXColor tev_color[4],konst_color[4];
static float reference_textures[8][4],*reference_texture;
#include "cursor_reference.inc"
static unsigned seed=20261126;
static unsigned random_byte(void){seed=seed*1664525+1013904223;return seed>>24;}
static unsigned constant(void){return mp_cursor_constant(tev_configuration,(const u8(*)[4])tev_color,(const u8(*)[4])konst_color);}
int main(void){
    unsigned rejected=0,comparisons=0;float worst=0;
    memcpy(tev_configuration,captured,sizeof(captured));
    assert(mp_cursor_material(tev_configuration,2));
    assert(!mp_cursor_material(tev_configuration,1));assert(!mp_cursor_material(tev_configuration,3));
    /* The menu data's constant: diffuse 99b3b3, alpha 0.999 (254). */
    memcpy(tev_color,captured_colors,sizeof(tev_color));memcpy(konst_color,captured_konst,sizeof(konst_color));
    assert(constant()==0xfeb3b399u);
    /* Any one changed map, channel, input, op, bias, scale, clamp, output or swap. */
    for(unsigned stage=0;stage<2;++stage)for(unsigned field=1;field<=22;++field){
        unsigned saved=tev_configuration[stage][field];tev_configuration[stage][field]^=1;
        assert(!mp_cursor_material(tev_configuration,2));++rejected;tev_configuration[stage][field]=saved;
    }
    unsigned saved=tev_configuration[1][1];tev_configuration[1][1]=tev_configuration[0][1];
    assert(!mp_cursor_material(tev_configuration,2));++rejected;tev_configuration[1][1]=saved;
    /* Konst selectors that select nothing. */
    for(unsigned sel=8;sel<12;++sel){tev_configuration[0][23]=sel;assert(!mp_cursor_material(tev_configuration,2));++rejected;}
    tev_configuration[0][23]=captured[0][23];
    /* The other two-texture matchers and this one never accept each other's programs. */
    assert(!mp_layered_material(tev_configuration,2)&&!mp_shield_material(tev_configuration,2));
    for(unsigned i=0;i<2;++i){assert(!mp_cursor_material(captured_shield[i],4));assert(!mp_cursor_material(captured_shield[i],2));}
    /* The fragment algebra against gx.c's own TEV evaluator, over register
     * and konst constants, every legal selector and both texture maps. */
    static const unsigned rgb_inputs[]={2,4,6,14},alpha_inputs[]={1,2,3,6};
    static const unsigned color_selectors[]={0,1,2,3,4,5,6,7,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    static const unsigned alpha_selectors[]={0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    for(unsigned i=0;i<200000;++i){
        for(unsigned j=0;j<16;++j){((u8*)konst_color)[j]=random_byte();((u8*)tev_color)[j]=random_byte();}
        tev_configuration[0][4]=rgb_inputs[i%4];tev_configuration[0][8]=alpha_inputs[(i/4)%4];
        tev_configuration[0][23]=color_selectors[random_byte()%28];tev_configuration[0][24]=alpha_selectors[random_byte()%24];
        unsigned t0=i%8,t1=(t0+1+random_byte()%7)%8;tev_configuration[0][1]=t0;tev_configuration[1][1]=t1;
        assert(mp_cursor_material(tev_configuration,2));
        for(unsigned j=0;j<4;++j){reference_textures[t0][j]=random_byte()/255.f;reference_textures[t1][j]=random_byte()/255.f;}
        float raster[2][4]={{0}},actual[4];shade(actual,raster);
        unsigned c=constant();float label=reference_textures[t1][3];
        for(unsigned j=0;j<4;++j){
            float base=reference_textures[t0][j]*(((c>>(8*j))&255)/255.f);
            float top=j==3?label:reference_textures[t1][j];
            float expected=top*label+base*(1-label),error=fabsf(expected-actual[j]);
            if(error>worst)worst=error;
            assert(error<=1.f/255.f);++comparisons;
        }
    }
    printf("Cursor material: %u component comparisons against the original TEV evaluator, %u altered programs rejected; max 8-bit constant error %.6f\n",comparisons,rejected,worst);
    return 0;
}
