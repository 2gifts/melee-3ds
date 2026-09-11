#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef int32_t s32;
typedef struct {int value;} HSD_GObj;
typedef struct {HSD_GObj*gobj;uint16_t gfx_id;u8 alpha,pad;} EF_ParamEntry;
static struct {EF_ParamEntry array[16];unsigned char guard[256];} queue_storage;
#define efLib_AnimQueue queue_storage.array
static EF_ParamEntry efLib_ParamTable[8];
/* These two functions are extracted from the actual generated overlay. */
#include "effect_params_functions.inc"
int main(void){
    HSD_GObj objects[9];memset(&queue_storage,0xa5,sizeof(queue_storage));
    for(int i=0;i<8;++i){efLib_SetParamAlpha(&objects[i],i+10);efLib_SetParamGfxId(&objects[i],0x417+i);}
    for(int i=0;i<8;++i){assert(efLib_ParamTable[i].gobj==&objects[i]);assert(efLib_ParamTable[i].alpha==i+10);assert(efLib_ParamTable[i].gfx_id==0x417+i);}
    efLib_SetParamAlpha(&objects[3],255);efLib_SetParamGfxId(&objects[3],0x419);
    assert(efLib_ParamTable[3].alpha==255&&efLib_ParamTable[3].gfx_id==0x419);
    EF_ParamEntry saved[8];memcpy(saved,efLib_ParamTable,sizeof(saved));
    efLib_SetParamAlpha(&objects[8],1);efLib_SetParamGfxId(&objects[8],2);assert(!memcmp(saved,efLib_ParamTable,sizeof(saved)));
    for(unsigned i=0;i<sizeof(queue_storage);++i)assert(((unsigned char*)&queue_storage)[i]==0xa5);
    puts("Effect parameter updates preserve the animation queue and neighboring storage");
}
