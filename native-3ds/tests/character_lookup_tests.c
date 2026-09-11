#include <assert.h>
#include <stdio.h>
typedef float f32;
typedef int CharacterKind;
typedef unsigned u32;
enum { CKind_Zelda=18, CKind_Seak=19, CKind_MasterH=26, CKind_Boy=27,
       CKind_Girl=28, CKind_GKoops=29, CKind_CrezyH=30,
       ChKind_Sandbag=31, ChKind_Popo=32 };
#include "character_lookup_function.inc"
static int player_character,player_variant,player_costume;
static int Player_GetPlayerCharacter(int slot){assert(slot==2);return player_character;}
static unsigned Player_GetCostumeId(int slot){assert(slot==2);return player_costume;}
static int Player_80036394(int slot){assert(slot==2);return player_variant;}
#include "stock_lookup_wrapper.inc"
int main(void) {
    /* Index mapping from the original PPC routine, including r3's unchanged
       fall-through path for ordinary fighters and special costume handling. */
    const int base[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,
                      18,18,19,20,21,22,23,24,28,26,26,58,27,59,14};
    for(int fighter=0;fighter<33;++fighter)
        for(int variant=0;variant<2;++variant)
            for(int costume=0;costume<6;++costume) {
                int expected=base[fighter];
                if((fighter==18||fighter==19)&&variant)expected=25;
                if(fighter<26||fighter==32)expected+=30*costume;
                assert(gm_80168B34(fighter,variant?7:0,costume)==expected);
                player_character=fighter;player_variant=variant?7:0;player_costume=costume;
                assert(gm_80168BF8(2)==expected);
            }
    puts("396 original character/variant/costume mappings and stock-icon wrapper returns passed");
}
