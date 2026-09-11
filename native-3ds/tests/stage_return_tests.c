#include <assert.h>
#include <stdio.h>
typedef struct {float y;} HSD_JObj;
typedef struct {HSD_JObj *jobj;int result;} HSD_GObj;
typedef HSD_GObj Item_GObj;
struct grIceMt_GObj9_GObj10_UnderUpperIdPair {short under,upper;};
static HSD_JObj joints[2];
static HSD_GObj objects[2]={{&joints[0],0},{&joints[1],1}};
#define HSD_ASSERT(line,p) assert(p)
#define GET_JOBJ(p) ((p)->jobj)
static HSD_GObj *Ground_GetMapGObj(int i){assert(i>=0&&i<2);return &objects[i];}
static void HSD_JObjAddTranslationY(HSD_JObj*j,float y){j->y+=y;}
static int it_802751D8(Item_GObj*g){return g->result;}
typedef int bool;
#include "stage_return_functions.inc"
int main(void){
    unsigned cases=0;
    for(int mask=0;mask<4;++mask)for(int step=-8;step<=8;++step){
        struct grIceMt_GObj9_GObj10_UnderUpperIdPair ids={mask&1?0:-1,mask&2?1:-1};
        joints[0].y=31;joints[1].y=-14;
        float y=step*.25f;
        assert(grIceMt_801F96E0(&ids,y)==y);
        assert(joints[0].y==31+(mask&1?y:0));
        assert(joints[1].y==-14+(mask&2?y:0));++cases;
    }
    assert(itKyasarinegg_UnkMotion4_Anim(&objects[0])==0);
    assert(itKyasarinegg_UnkMotion4_Anim(&objects[1])==1);
    printf("Stage return checks: %u scrolling cases and both egg cleanup results passed\n",cases);
}
