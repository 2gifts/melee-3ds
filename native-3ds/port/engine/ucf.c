#include "ucf.h"
#include "ucf_math.h"
#include <string.h>
#include <melee/ft/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/pl/player.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

static MPUcfPad pads[4];
volatile unsigned mp_ucf_enabled=1;
unsigned mp_ucf_checks[8],mp_ucf_applied[8];
void mp_ucf_reset(void){memset(pads,0,sizeof(pads));}
static int active(Fighter*f){return mp_ucf_enabled&&f->x618_player_id<4;}
void mp_ucf_input(Fighter*f){
    if(!active(f)||ftCo_800A2040(f))return;
    ++mp_ucf_checks[0];
    int i=(int)HSD_PadLibData.qread-1;
    if(i<0)i+=HSD_PadLibData.qnum;
    if(!HSD_PadLibData.queue||i<0||i>=HSD_PadLibData.qnum)return;
    PADStatus*s=&HSD_PadLibData.queue[i].stat[f->x618_player_id];
    MPUcfPad*p=&pads[f->x618_player_id];mp_ucf_push(p,s->stickX,s->stickY);
    if(f->kind!=Ft_Kind_Zelda||f->motion_id!=349){
        mp_ucf_cardinal(s->stickX,s->stickY,&f->input.lstick[0].x,&f->input.lstick[0].y);
        mp_ucf_cardinal(s->substickX,s->substickY,&f->input.cstick[0].x,&f->input.cstick[0].y);
    }
    mp_ucf_drop_history(p,f->input.lstick[0].x,f->input.lstick[0].y,f->x671_timer_lstick_tilt_y);
}
static int xsmash(Fighter*f){return mp_ucf_delta2(&pads[f->x618_player_id],1)>75*75;}
void mp_ucf_dashback(Fighter*f){
    if(!active(f))return;++mp_ucf_checks[1];
    if(f->x221F_b4||f->cur_anim_frame!=2.f||
       f->input.lstick[0].x*f->facing_dir<p_ftCommonData->dash_smash_stick_threshold||
       f->x670_timer_lstick_tilt_x>=2||!xsmash(f))return;
    f->mv.co.turn.has_turned=true;f->mv.co.turn.just_turned=true;++mp_ucf_applied[1];
    Fighter_GObj*g=Player_GetEntityAtIndex(f->player_id,1);
    if(g){Fighter*n=g->user_data;if(n->cpu.x444){n->cpu.x444->facing_dir=f->facing_dir;n->cpu.x444->lstick.x=f->facing_dir<0?-128:127;}}
}
int mp_ucf_sdi(Fighter*f){
    if(!active(f))return 0;++mp_ucf_checks[2];
    float x=f->input.lstick[1].x,y=f->input.lstick[1].y,t=p_ftCommonData->sdi_min_stick_mag;
    int yes=(f->x673<2||f->x674<2)&&x*x+y*y<t*t&&mp_ucf_delta2(&pads[f->x618_player_id],3)>62*62;
    mp_ucf_applied[2]+=yes;return yes;
}
int mp_ucf_shield_sdi(Fighter*f){
    if(!active(f))return 0;++mp_ucf_checks[3];
    /* Preserve UCF 0.84's signed previous-X comparison. */
    int yes=f->x673<2&&f->input.lstick[1].x<p_ftCommonData->sdi_min_stick_mag&&mp_ucf_delta2(&pads[f->x618_player_id],1)>62*62;
    mp_ucf_applied[3]+=yes;return yes;
}
int mp_ucf_tumble(Fighter*f){
    if(!active(f))return 0;++mp_ucf_checks[4];
    float x=f->input.lstick[1].x;if(x<0)x=-x;
    int yes=f->x670_timer_lstick_tilt_x==1&&x<p_ftCommonData->x210&&xsmash(f);
    mp_ucf_applied[4]+=yes;return yes;
}
int mp_ucf_drop_extended(Fighter*f){
    if(!active(f))return 0;++mp_ucf_checks[5];
    int yes=pads[f->x618_player_id].drop_frames>=2;mp_ucf_applied[5]+=yes;return yes;
}
int mp_ucf_suppress_spotdodge(Fighter*f){
    if(!active(f))return 0;++mp_ucf_checks[6];
    int yes=f->input.cstick[0].y>p_ftCommonData->x314&&
        f->x670_timer_lstick_tilt_x>=p_ftCommonData->x320&&f->input.lstick[0].y>-.8f&&
        mpColl_IsOnPlatform(&f->coll_data)&&mp_ucf_rim(f->input.lstick[0].x,f->input.lstick[0].y);
    mp_ucf_applied[6]+=yes;return yes;
}
float mp_ucf_squat_threshold(Fighter*f){
    if(!active(f))return p_ftCommonData->x94;++mp_ucf_checks[7];
    if(f->x670_timer_lstick_tilt_x<1&&mp_ucf_rim(f->input.lstick[0].x,f->input.lstick[0].y)){++mp_ucf_applied[7];return .59f;}
    return p_ftCommonData->x94;
}
