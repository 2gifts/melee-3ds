#ifndef MP_MATERIAL_PROGRAM_H
#define MP_MATERIAL_PROGRAM_H
#include <stdint.h>
#include <string.h>

/* The CPU color evaluators read fields 2..20 and 23..24. Texture maps,
 * coordinates, swap state and indirect configuration remain live elsewhere
 * in GX; none is an input to these compiled vertex-color plans. */
#define MP_MATERIAL_PROGRAMS 64
typedef struct {
    uint32_t serial,stamp,hash,stages;
    uint32_t config[16][21];
    uint32_t colors,konst,raster;
} MPMaterialProgram;
typedef struct {
    MPMaterialProgram entries[MP_MATERIAL_PROGRAMS];
    uint32_t serial,clock,resets,hits,misses;
} MPMaterialPrograms;

static void mp_material_dependencies(const uint32_t config[16][30],unsigned stages,
                                    uint32_t*colors,uint32_t*konst,uint32_t*raster){
    uint32_t written=0,read=0,kread=0,rread=0;
    if(!stages)rread=15;
    for(unsigned i=0;i<stages&&i<16;++i){const uint32_t*s=config[i];
        for(unsigned j=3;j<11;++j){unsigned arg=s[j],mask=0,sel=0,kmask=j<7?7:8,ras=0;
            if(j<7){
                if(arg<8)mask=((arg&1)?8:7)<<((arg/2)*4);
                else if(arg==14)sel=s[23];
                else if(arg==10||arg==11)ras=arg==11||s[2]==2||s[2]==3?8:7;
            }else{
                if(arg<4)mask=8<<(arg*4);else if(arg==6)sel=s[24];else if(arg==5)ras=8;
            }
            read|=mask&~written;
            if(sel>=12&&sel<16)kread|=kmask<<((sel-12)*4);
            else if(sel>=16&&sel<32)kread|=1u<<((sel&3)*4+(sel-16)/4);
            if(s[2]<6)rread|=ras<<((s[2]&1)*4);
        }
        /* Both color and alpha read the old registers before either write. */
        written|=(7u<<((s[15]&3)*4))|(8u<<((s[20]&3)*4));
    }
    if(stages)read|=15&~written;
    *colors=read;*konst=kread;*raster=rread;
}
static uint32_t mp_material_program_hash(const uint32_t config[16][30],unsigned stages){
    uint32_t hash=(2166136261u^stages)*16777619u;
    for(unsigned i=0;i<stages;++i){
        for(unsigned j=2;j<=20;++j)hash=(hash^config[i][j])*16777619u;
        hash=(hash^config[i][23])*16777619u;hash=(hash^config[i][24])*16777619u;
    }
    return hash;
}
static int mp_material_program_equal(const MPMaterialProgram*p,const uint32_t config[16][30],unsigned stages){
    if(p->stages!=stages)return 0;
    for(unsigned i=0;i<stages;++i)
        if(memcmp(p->config[i],config[i]+2,19*4)||memcmp(p->config[i]+19,config[i]+23,2*4))return 0;
    return 1;
}
static const MPMaterialProgram*mp_material_program_get(MPMaterialPrograms*cache,const uint32_t config[16][30],unsigned stages){
    if(stages>16)stages=16;
    /* Notify the binding cache before reusing any serial after wrap. */
    if(!++cache->clock||cache->serial==UINT32_MAX){
        memset(cache->entries,0,sizeof(cache->entries));cache->serial=0;cache->clock=1;++cache->resets;
    }
    uint32_t hash=mp_material_program_hash(config,stages);
    MPMaterialProgram*set=cache->entries+(hash&(MP_MATERIAL_PROGRAMS/4-1))*4,*slot=set;
    for(unsigned i=0;i<4;++i){MPMaterialProgram*p=set+i;
        if(p->serial&&p->hash==hash&&mp_material_program_equal(p,config,stages)){
            p->stamp=cache->clock;++cache->hits;return p;
        }
        if(p->stamp<slot->stamp)slot=p;
    }
    ++cache->misses;slot->serial=++cache->serial;slot->stamp=cache->clock;slot->hash=hash;slot->stages=stages;
    for(unsigned i=0;i<stages;++i){memcpy(slot->config[i],config[i]+2,19*4);memcpy(slot->config[i]+19,config[i]+23,2*4);}
    mp_material_dependencies(config,stages,&slot->colors,&slot->konst,&slot->raster);
    return slot;
}
static void mp_material_bind_constants(const MPMaterialProgram*p,int flat,
                                      unsigned char material[8],unsigned char colors[16],unsigned char konst[16],float alpha[2]){
    for(unsigned i=0;i<16;++i){if(!(p->colors&(1u<<i)))colors[i]=0;if(!(p->konst&(1u<<i)))konst[i]=0;}
    for(unsigned i=0;i<8;++i)if(!flat||!(p->raster&(1u<<i)))material[i]=0;
    /* Flat color evaluates the actual material directly. Variable plans
     * read constant raster alpha only when that channel's alpha is used. */
    for(unsigned i=0;i<2;++i)if(flat||!(p->raster&(8u<<(i*4))))alpha[i]=0;
}
#endif
