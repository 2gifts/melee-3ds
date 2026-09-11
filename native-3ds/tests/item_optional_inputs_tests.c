#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef float f32;
typedef struct{float x,y,z;} Vec3;
typedef struct{Vec3 pos;} Item;
typedef struct{Item*user_data;} Item_GObj;
#define GET_ITEM(g) ((g)->user_data)
enum{It_Kind_Freeze=17};
typedef struct{int kind;Vec3 prev_pos,pos,vel;float facing_dir;unsigned x3C_damage;void*x0_parent_gobj,*x4_parent_gobj2;struct{bool b0;}x44_flag;int x40;} SpawnItem;
static SpawnItem captured;static Item_GObj created;static int allocation_fail,spawned,configured;static Item_GObj*owner;
static Item_GObj*Item_80268B18(SpawnItem*s){captured=*s;return allocation_fail?NULL:&created;}
static void it_3F14_Logic17_Spawned(Item_GObj*g){assert(g==&created);++spawned;}
static void it_8028F434(Item_GObj*g,float d,Item_GObj*r){assert(g==&created&&d==.5f);owner=r;++configured;}
#include "freeze_spawn_function.inc"
typedef struct{int unused;} HSD_Joint;
typedef struct{int unused;} HSD_JObj;
static HSD_JObj loaded;static unsigned loads;
static HSD_JObj*HSD_JObjLoadJoint(HSD_Joint*j){assert(j);++loads;return &loaded;}
#include "arrow_joint_function.inc"
int main(void){
    Vec3 pos={1,2,3};Item parent_item={{4,5,6}};Item_GObj parent={&parent_item};
    for(unsigned fail=0;fail<2;++fail)for(unsigned has_parent=0;has_parent<2;++has_parent){
        allocation_fail=fail;spawned=configured=0;Item_GObj*ref=has_parent?&parent:NULL;
        assert(it_8028EB88(ref,&pos,-1,.5f)==(fail?NULL:&created));
        assert(!memcmp(&captured.prev_pos,&pos,sizeof(pos)));
        assert(!memcmp(&captured.pos,has_parent?&parent_item.pos:&pos,sizeof(pos)));
        assert(captured.kind==It_Kind_Freeze&&captured.facing_dir==-1&&captured.x44_flag.b0);
        assert(spawned==!fail&&configured==!fail);if(!fail)assert(owner==ref);
    }
    assert(itLinkArrow_802A850C_inline(NULL)==NULL&&loads==0);
    HSD_Joint joint;assert(itLinkArrow_802A850C_inline(&joint)==&loaded&&loads==1);
    puts("Original item helpers: parent/absent parent, allocation failure, joint/absent joint passed");
}
