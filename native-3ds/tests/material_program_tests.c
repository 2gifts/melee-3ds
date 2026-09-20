#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"material program line %d: %s\n",__LINE__,#x);exit(86);}}while(0)
typedef uint8_t u8;typedef uint32_t u32;
typedef struct {u8 r,g,b,a;} GXColor;
static unsigned num_stages,tev_configuration[16][30];
static GXColor tev_color[4],konst_color[4];
#include "../port/engine/gx_shade.h"
#include "../port/engine/clamped_shade.h"
#include "../port/engine/material_program.h"
#include "material-reference.inc"
typedef struct{unsigned stages,config[16][30];u8 colors[16],konst[16];float alpha[2];} Fixture;
#include "material-fixtures.inc"
static MPMaterialPrograms cache;
static unsigned seed=0x45cf02a1,program_checks,binding_checks,flat_checks;
static unsigned rng(void){seed=seed*1664525u+1013904223u;return seed;}
static float unit(void){return (rng()>>24)/255.f;}
static void compile(MPShadePlan*p,MPClampedPlan*q,const u8*color,const u8*konst,const float alpha[2]){
    float c[4][4],k[4][4];for(unsigned i=0;i<16;++i){((float*)c)[i]=color[i]/255.f;((float*)k)[i]=konst[i]/255.f;}
    memset(p,0,sizeof(*p));memset(q,0,sizeof(*q));
    mp_shade_compile_channels(p,tev_configuration,num_stages,c,k,alpha);
    mp_clamped_compile(q,tev_configuration,num_stages,c,k,alpha);
}
static void compare_plan(const MPShadePlan*a,const MPShadePlan*b,const MPClampedPlan*c,const MPClampedPlan*d){
    CHECK(!memcmp(a,b,sizeof(*a)));CHECK(c->valid==d->valid);
    if(c->valid)CHECK(!memcmp(c,d,sizeof(*c)));
}
static void bindings(const float alpha[2]){
    const MPMaterialProgram*p=mp_material_program_get(&cache,tev_configuration,num_stages);
    u8 color[16],konst[16],mat[8];float masked_alpha[2]={alpha[0],alpha[1]};
    memcpy(color,tev_color,16);memcpy(konst,konst_color,16);for(unsigned i=0;i<8;++i)mat[i]=rng()>>24;
    MPShadePlan a,b;MPClampedPlan c,d;
    compile(&a,&c,color,konst,alpha);
    mp_material_bind_constants(p,0,mat,color,konst,masked_alpha);
    compile(&b,&d,color,konst,masked_alpha);compare_plan(&a,&b,&c,&d);++binding_checks;
    /* Flat evaluation reads the original scalar evaluator, with material
     * bytes as unlit register/vertex colors for both raster channels. */
    GXColor save_color[4],save_konst[4];memcpy(save_color,tev_color,16);memcpy(save_konst,konst_color,16);
    for(unsigned i=0;i<8;++i)mat[i]=rng()>>24;
    float ras[2][4],actual[4],expected[4];for(unsigned i=0;i<8;++i)((float*)ras)[i]=mat[i]/255.f;
    shade(expected,ras);
    memcpy(color,tev_color,16);memcpy(konst,konst_color,16);
    mp_material_bind_constants(p,1,mat,color,konst,masked_alpha);
    memcpy(tev_color,color,16);memcpy(konst_color,konst,16);for(unsigned i=0;i<8;++i)((float*)ras)[i]=mat[i]/255.f;
    shade(actual,ras);CHECK(!memcmp(expected,actual,sizeof(actual)));++flat_checks;
    memcpy(tev_color,save_color,16);memcpy(konst_color,save_konst,16);
}
static void program(const float alpha[2]){
    MPShadePlan a,b;MPClampedPlan c,d;
    compile(&a,&c,(u8*)tev_color,(u8*)konst_color,alpha);
    const MPMaterialProgram*p=mp_material_program_get(&cache,tev_configuration,num_stages);unsigned id=p->serial;
    unsigned saved[16][30];memcpy(saved,tev_configuration,sizeof(saved));
    for(unsigned i=0;i<num_stages;++i){
        for(unsigned j=0;j<30;++j)if(j<2||j==21||j==22||j>24)tev_configuration[i][j]=rng();
    }
    p=mp_material_program_get(&cache,tev_configuration,num_stages);CHECK(p->serial==id);
    compile(&b,&d,(u8*)tev_color,(u8*)konst_color,alpha);compare_plan(&a,&b,&c,&d);++program_checks;
    bindings(alpha);memcpy(tev_configuration,saved,sizeof(saved));
}
static void lifecycle(void){
    memset(&cache,0,sizeof(cache));memset(tev_configuration,0,sizeof(tev_configuration));num_stages=1;
    const MPMaterialProgram*p=mp_material_program_get(&cache,tev_configuration,1);unsigned first=p->serial,slot=p-cache.entries;
    /* Force a full-hash candidate with different semantic bytes. Equality
     * must reject it, even before considering an ordinary set collision. */
    cache.entries[slot].config[0][1]=1;
    p=mp_material_program_get(&cache,tev_configuration,1);CHECK(p->serial!=first);
    unsigned last=p->serial;
    for(unsigned i=0;i<8192;++i){tev_configuration[0][23]=i;mp_material_program_get(&cache,tev_configuration,1);}
    tev_configuration[0][23]=0;p=mp_material_program_get(&cache,tev_configuration,1);CHECK(p->serial!=last);
    unsigned resets=cache.resets;cache.serial=UINT32_MAX;
    p=mp_material_program_get(&cache,tev_configuration,1);CHECK(p->serial==1&&cache.resets==resets+1);
    cache.clock=UINT32_MAX;p=mp_material_program_get(&cache,tev_configuration,1);
    CHECK(p->serial==1&&cache.resets==resets+2&&cache.clock==1);
    CHECK(mp_material_program_get(&cache,tev_configuration,UINT32_MAX)->stages==16);
}
int main(void){
    lifecycle();
    for(unsigned f=0;f<sizeof(fixtures)/sizeof(*fixtures);++f){
        num_stages=fixtures[f].stages;memcpy(tev_configuration,fixtures[f].config,sizeof(tev_configuration));
        memcpy(tev_color,fixtures[f].colors,16);memcpy(konst_color,fixtures[f].konst,16);program(fixtures[f].alpha);
        for(unsigned j=0;j<256;++j){for(unsigned k=0;k<16;++k){((u8*)tev_color)[k]=rng()>>24;((u8*)konst_color)[k]=rng()>>24;}
            float alpha[2]={j%3?-1:unit(),j%5?-1:unit()};program(alpha);}
    }
    for(unsigned count=0;count<20000;++count){
        memset(tev_configuration,0,sizeof(tev_configuration));num_stages=(rng()>>24)%17;
        for(unsigned i=0;i<16;++i){((u8*)tev_color)[i]=rng()>>24;((u8*)konst_color)[i]=rng()>>24;}
        for(unsigned i=0;i<num_stages;++i){unsigned*s=tev_configuration[i];s[2]=(rng()>>24)%8;
            for(unsigned j=3;j<7;++j)s[j]=(rng()>>24)%16;
            for(unsigned j=7;j<11;++j)s[j]=(rng()>>24)%8;
            s[11]=(rng()>>24)%2;s[16]=(rng()>>24)%2;
            if(!(count%13)){s[11]=8+count%8;s[16]=8+count%8;}
            s[12]=(rng()>>24)%3;s[17]=(rng()>>24)%3;s[13]=(rng()>>24)%4;s[18]=(rng()>>24)%4;
            s[14]=(rng()>>24)%2;s[19]=(rng()>>24)%2;s[15]=(rng()>>24)%4;s[20]=(rng()>>24)%4;
            s[23]=(rng()>>24)%32;s[24]=(rng()>>24)%32;
        }
        float alpha[2]={count%3?-1:unit(),count%5?-1:unit()};program(alpha);
    }
    CHECK(program_checks==20771&&binding_checks==20771&&flat_checks==20771);
    printf("Passed %u program identities, %u exact compiled bindings and %u exact flat evaluations; all 0..16 stage counts, captured materials, animated constants, collision rejection, eviction and serial/clock wrap.\n",program_checks,binding_checks,flat_checks);
}
