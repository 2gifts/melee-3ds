#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint64_t u64;typedef int32_t s32;typedef float f32;typedef int ItemKind;
enum{It_Kind_BombHei=6,It_Kind_L_Gun_Ray=35};
typedef struct{u8 size;u8*x4;u16 x8;u16*xC;}ItemPickTable;
typedef struct{s32 x0;ItemPickTable x4;s32 x14;u64 x18;}RandomItemSpawner;
static struct{RandomItemSpawner value;uint8_t guard[128];}spawner_storage;
#define it_804A0E30 spawner_storage.value
static ItemPickTable it_804A0E50;
static void*HSD_MemAlloc(unsigned n){void*p=malloc(n?n:1);assert(p);return p;}
#include "item_table_functions.inc"
int main(void){
    memset(&spawner_storage,0xA5,sizeof(spawner_storage));
    s32 counts[35]={0};counts[6]=3;counts[8]=7;counts[20]=5;
    uint64_t mask=(1ULL<<0)|(1ULL<<2)|(1ULL<<14);
    it_8026CA4C(&it_804A0E50,counts,mask,6,.5f);
    it_8026CD50(counts,mask,.5f);
    assert(it_804A0E50.size==3&&it_804A0E50.x8==9);
    assert(it_804A0E50.x4[0]==6&&it_804A0E50.x4[1]==8&&it_804A0E50.x4[2]==20);
    assert(it_804A0E50.xC[0]==0&&it_804A0E50.xC[1]==2&&it_804A0E50.xC[2]==6);
    for(unsigned i=0;i<sizeof(spawner_storage);++i)assert(((uint8_t*)&spawner_storage)[i]==0xA5);
    free(it_804A0E50.x4);free(it_804A0E50.xC);
    puts("Container-drop table uses its own storage and correct cumulative weights");
}
