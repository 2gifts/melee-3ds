#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../port/engine/ucf_math.h"
typedef struct {float x,y;} Vec2;
typedef struct Fighter Fighter;
typedef struct {Fighter*user_data;} Fighter_GObj;
typedef struct {struct {signed char x,y;} lstick;float facing_dir;} Nana;
struct Fighter {
    unsigned x618_player_id,player_id,kind,motion_id,x221F_b4,is_cpu;
    unsigned x670_timer_lstick_tilt_x,x671_timer_lstick_tilt_y,x673,x674;
    float facing_dir,cur_anim_frame;
    struct {Vec2 lstick[3],cstick[3];}input;
    struct {struct {struct {bool has_turned,just_turned;}turn;}co;}mv;
    struct {Nana*x444;}cpu;
    int coll_data;
};
enum {Ft_Kind_Zelda=19};
static struct {float dash_smash_stick_threshold,sdi_min_stick_mag,x210,x314;int x320;float x94;} common={.8f,.7f,.8f,-.7f,4,.65f};
static typeof(common)*p_ftCommonData=&common;
typedef struct {signed char stickX,stickY,substickX,substickY;} PADStatus;
static struct {PADStatus stat[4];} raw_queue[5];
static struct {unsigned qread,qnum;typeof(raw_queue[0])*queue;} HSD_PadLibData={1,5,raw_queue};
static Fighter_GObj*secondary;
static int ftCo_800A2040(Fighter*f){return f->is_cpu;}
static Fighter_GObj*Player_GetEntityAtIndex(int slot,int index){assert(slot<6&&index==1);return secondary;}
static int mpColl_IsOnPlatform(int*c){return *c;}
#include "ucf_body.inc"
static Fighter make(void){Fighter f={0};f.facing_dir=1;return f;}
static void speed(int x,int y){memset(pads,0,sizeof(pads));mp_ucf_push(&pads[0],0,0);mp_ucf_push(&pads[0],0,0);mp_ucf_push(&pads[0],x,y);}
int main(void){
    unsigned cases=0;
    for(int x=-128;x<128;++x)for(int y=-128;y<128;++y){
        float a=.37f,b=-.23f;mp_ucf_cardinal(x,y,&a,&b);
        if(abs(x)>=80&&abs(y)<=6){assert(a==(x<0?-1.f:1.f)&&b==0);}
        else if(abs(y)>=80&&abs(x)<=6){assert(a==0&&b==(y<0?-1.f:1.f));}
        else assert(a==.37f&&b==-.23f);
        float fx=x/80.f,fy=y/80.f;
        int ix=(int)(float)(fabs((double)fx)*80.0-(double).0001f)+2;
        int iy=(int)(float)(fabs((double)fy)*80.0-(double).0001f)+2;
        assert(mp_ucf_rim(fx,fy)==(ix*ix+iy*iy>6400));++cases;
    }
    Fighter f=make();raw_queue[0].stat[0]=(PADStatus){80,6,-4,-81};
    mp_ucf_reset();mp_ucf_input(&f);assert(f.input.lstick[0].x==1&&f.input.lstick[0].y==0&&f.input.cstick[0].y==-1);
    f.kind=Ft_Kind_Zelda;f.motion_id=349;f.input.lstick[0]=(Vec2){.95f,.075f};mp_ucf_input(&f);assert(f.input.lstick[0].x==.95f);
    unsigned index=pads[0].index;f.is_cpu=1;mp_ucf_input(&f);assert(pads[0].index==index);
    f=make();f.cur_anim_frame=2;f.input.lstick[0].x=.9f;speed(75,0);mp_ucf_dashback(&f);assert(!f.mv.co.turn.has_turned);
    speed(76,0);mp_ucf_dashback(&f);assert(f.mv.co.turn.has_turned&&f.mv.co.turn.just_turned);
    f.mv.co.turn.has_turned=false;f.x670_timer_lstick_tilt_x=2;mp_ucf_dashback(&f);assert(!f.mv.co.turn.has_turned);
    f.x670_timer_lstick_tilt_x=0;f.x221F_b4=1;mp_ucf_dashback(&f);assert(!f.mv.co.turn.has_turned);
    Fighter n=make();Nana nr={0};n.cpu.x444=&nr;Fighter_GObj ng={&n};secondary=&ng;
    f.x221F_b4=0;f.facing_dir=-1;f.input.lstick[0].x=-.9f;mp_ucf_dashback(&f);assert(nr.facing_dir==-1&&nr.lstick.x==-128);secondary=NULL;
    f=make();speed(62,0);assert(!mp_ucf_sdi(&f)&&!mp_ucf_shield_sdi(&f));
    speed(63,0);assert(mp_ucf_sdi(&f)&&mp_ucf_shield_sdi(&f));
    f.x673=f.x674=2;assert(!mp_ucf_sdi(&f)&&!mp_ucf_shield_sdi(&f));
    f.x673=f.x674=0;f.input.lstick[1].x=.8f;assert(!mp_ucf_sdi(&f)&&!mp_ucf_shield_sdi(&f));
    f.input.lstick[1].x=0;speed(76,0);f.x670_timer_lstick_tilt_x=1;assert(mp_ucf_tumble(&f));
    f.x670_timer_lstick_tilt_x=2;assert(!mp_ucf_tumble(&f));
    f.x670_timer_lstick_tilt_x=1;f.input.lstick[1].x=-.8f;assert(!mp_ucf_tumble(&f));
    f=make();f.coll_data=1;f.input.lstick[0]=(Vec2){.8f,-.65f};f.x670_timer_lstick_tilt_x=4;
    assert(mp_ucf_suppress_spotdodge(&f));f.coll_data=0;assert(!mp_ucf_suppress_spotdodge(&f));f.coll_data=1;
    f.input.cstick[0].y=-.8f;assert(!mp_ucf_suppress_spotdodge(&f));f.input.cstick[0].y=0;
    f.input.lstick[0].y=-.8f;assert(!mp_ucf_suppress_spotdodge(&f));
    f.input.lstick[0]=(Vec2){.8f,-.65f};f.x670_timer_lstick_tilt_x=0;assert(mp_ucf_squat_threshold(&f)==.59f);
    f.x670_timer_lstick_tilt_x=1;assert(mp_ucf_squat_threshold(&f)==common.x94);
    speed(64,-50);mp_ucf_drop_history(&pads[0],.8f,-.625f,0);assert(!mp_ucf_drop_extended(&f));
    mp_ucf_push(&pads[0],64,-50);mp_ucf_drop_history(&pads[0],.8f,-.625f,1);assert(mp_ucf_drop_extended(&f));
    mp_ucf_drop_history(&pads[0],0,0,0);assert(!mp_ucf_drop_extended(&f));
    f.x618_player_id=255;mp_ucf_input(&f);assert(!mp_ucf_sdi(&f));
    printf("UCF: %u raw stick pairs; cardinal, rim, history, dashback/Nana, SDI, shield SDI, tumble, shield-drop and crouch gates passed\n",cases);
}
