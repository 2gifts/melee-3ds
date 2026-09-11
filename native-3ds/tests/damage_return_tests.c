#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef int enum_t,HitElement;
enum{HitElement_Fire=1,HitElement_Electric=2,HitElement_Ice=5,HitElement_Dark=13};
typedef struct{struct{float x1838_percentTemp;}dmg;}Fighter;
typedef struct{Fighter *user_data;}Fighter_GObj;
static unsigned calls;static int effect,answer;static Fighter *received;
static bool ftCo_800BFFD0(Fighter *fp,int id,int zero){assert(!zero);received=fp;effect=id;++calls;return answer;}
#include "damage_return_function.inc"
int main(void){
    unsigned checks=0;float amounts[]={0.f,-0.f,1.f,42.f,-1.f};Fighter f;Fighter_GObj g={&f};
    for(unsigned amount=0;amount<5;++amount)for(int element=0;element<16;++element)
        for(int level=0;level<8;++level)for(answer=0;answer<2;++answer){
            f.dmg.x1838_percentTemp=amounts[amount];calls=0;
            bool result=ftCo_8008DA4C(&g,element,level);
            if(amount<2){assert(result&&calls==0);}
            else{
                assert(result==answer&&calls==1&&received==&f);
                int expected=element==1?level+11:element==2?level+15:element==5?level+31:element==13?level+35:4;
                assert(effect==expected);
            }
            ++checks;
        }
    printf("Original damage-effect boolean return and dispatch: %u cases passed\n",checks);
}
