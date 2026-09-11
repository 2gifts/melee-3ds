#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int8_t s8;
typedef int32_t s32;
typedef int CharacterKind;
typedef int Gm_PKind;
enum { Gm_PKind_Human=0, Gm_PKind_Cpu=1 };
typedef struct {s8 internal_id,extra_internal_id,has_transformation;} ftMapping;
typedef struct {int dummy;} HSD_Archive;
typedef struct {
    int player_character,slot_type,transformed[2],falls[2];
    void*player_entity[2];
    struct {bool b2;} flags;
} StaticPlayer;
static ftMapping ftMapping_list[33];
static StaticPlayer player_slots[1];
static void Player_CheckSlot(int slot){assert(slot==0);}
static int demo_kind[2],demo_count,demo_argument;
static HSD_Archive*demo_archive;
static void ftDemo_SetArchiveData(int kind,HSD_Archive*archive,int argument){
    assert(demo_count<2);demo_kind[demo_count++]=kind;
    demo_archive=archive;demo_argument=argument;
}
static void*clapped[2];static int clap_count,status_count;
static void ftCo_800D4FF4(void*entity){assert(clap_count<2);clapped[clap_count++]=entity;}
static int ftLib_8008701C(void*entity){return entity!=NULL;}
static int ftLib_800873CC(void*entity){return entity!=NULL;}
static void ifStatus_802F6E1C(int slot){assert(slot==0);++status_count;}
#include "player_mapping_functions.inc"
#include "player_mapping_reference.inc"
int main(void){
    _Static_assert(sizeof(ftMapping_list)==sizeof(original_mapping),"Mapping size");
    memcpy(ftMapping_list,original_mapping,sizeof(original_mapping));
    HSD_Archive archive;int entities[2];unsigned cases=0;
    for(unsigned character=0;character<33;++character){
        const unsigned char*row=original_mapping+character*3;
        StaticPlayer*p=&player_slots[0];p->player_character=character;
        demo_count=0;Player_80036E20(character,&archive,47);
        assert(demo_kind[0]==row[0]&&demo_archive==&archive&&demo_argument==47);
        bool secondary=row[1]!=255&&row[2]==0;
        assert(demo_count==(secondary?2:1));if(secondary)assert(demo_kind[1]==row[1]);
        assert(Player_80032610(0,0)==row[0]);
        assert(Player_80032610(0,1)==(s8)row[1]);
        for(int transformed=0;transformed<2;++transformed){
            p->transformed[0]=transformed;p->transformed[1]=1-transformed;
            p->falls[0]=3;p->falls[1]=7;
            assert(Player_GetFalls(0)==(row[1]!=255&&row[2]?10:p->falls[transformed]));
            for(int slot_type=0;slot_type<4;++slot_type){
                p->slot_type=slot_type;
                assert(Player_8003248C(0,0)==slot_type);
                assert(Player_8003248C(0,1)==(!row[2]&&slot_type<2?1:slot_type));
            }
            for(unsigned has_secondary=0;has_secondary<2;++has_secondary){
                p->flags.b2=has_secondary;
                p->player_entity[0]=&entities[0];p->player_entity[1]=&entities[1];
                clap_count=status_count=0;Player_80032070(0,false);
                assert(status_count==1&&clapped[0]==p->player_entity[transformed]);
                assert(clap_count==1+(has_secondary&&row[2]==0));
                if(clap_count==2)assert(clapped[1]==p->player_entity[1-transformed]);
                ++cases;
            }
        }
    }
    printf("Player mapping: all 33 original DOL entries, demo archives, secondary entities, slot types and falls passed (%u variants)\n",cases);
}
