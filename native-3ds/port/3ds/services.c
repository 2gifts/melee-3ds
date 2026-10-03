#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern void mp_native_panic(const char*),mp_native_log(const char*);
unsigned mp_native_idle_count;
u64 mp_native_idle_ticks; /* Engine thread waiting for retrace/alarms (thread load). */
/* Every engine wait (retrace, frame cap, alarms, disc, draw-done) idles
 * here. A failed GX translator never completes the frame being waited for,
 * so report its error instead of waiting forever. */
void mp_native_idle(void){extern void mp_native_gx_check(void);mp_native_gx_check();++mp_native_idle_count;u64 start=svcGetSystemTick();svcSleepThread(1000000);mp_native_idle_ticks+=svcGetSystemTick()-start;}
extern unsigned mp_native_frame_number(void);
/* Slippi replay/online runs turn the smoke build's scripted input off. */
volatile unsigned mp_slippi_no_scripts;
static unsigned scripted_keys(void){
#ifdef MP_SMOKE_TEST
    if(mp_slippi_no_scripts)return 0;
    /* Repeat a short A press to dismiss the original no-memory-card prompt. */
    unsigned f=mp_native_frame_number();if(f>90&&f<485&&f%120<5)return KEY_A|(f>=300?KEY_START:0);
    if(f>=600&&f<725&&f%60<5)return KEY_DDOWN;
    if(f>=780&&f<785)return KEY_A;
    if((f>=960&&f<964)||(f>=1140&&f<1144))return KEY_A;
    if(f>=1020&&f<1024)return KEY_START;
#endif
    return 0;
}
#ifdef MP_SMOKE_TEST
/* Only the automated validation build exposes controller injection. Values
 * are native little endian; no original game state is modified by the test. */
/* cx/cy (C-stick) follow the original five words; writers of the first
 * 20 bytes only leave them zero. */
volatile struct{u32 sequence,buttons,frames;s32 x,y,cx,cy;}mp_test_control;
static u32 test_sequence,test_until;
static void test_pad(u32*buttons,circlePosition*stick,circlePosition*sub){
    u32 f=mp_native_frame_number();
    if(mp_test_control.sequence){if(test_sequence!=mp_test_control.sequence){test_sequence=mp_test_control.sequence;test_until=f+mp_test_control.frames;}*buttons=f<test_until?mp_test_control.buttons:0;stick->dx=f<test_until?mp_test_control.x*156/80:0;stick->dy=f<test_until?mp_test_control.y*156/80:0;sub->dx=f<test_until?mp_test_control.cx*156/80:0;sub->dy=f<test_until?mp_test_control.cy*156/80:0;}
    else if(!mp_slippi_no_scripts){
        if(f>=850&&f<885){stick->dx=25*156/80;stick->dy=156;}
        if(f>=900&&f<908)stick->dx=-156;
        if(f>=930&&f<935)stick->dy=-117;
        if(f>=940&&f<946)stick->dx=117;
        if(f>=1080&&f<1092)stick->dy=156;
    }
}
#else
static void test_pad(u32*buttons,circlePosition*stick,circlePosition*sub){(void)buttons;(void)stick;(void)sub;}
#endif
#include "controls.h"
#ifdef MP_SMOKE_TEST
volatile u32 mp_test_keys; /* held 3DS keys, mapped like physical buttons */
#endif
/* Set by the engine before each PADRead: a match is running and not paused. */
static unsigned pad_battle;
void mp_native_pad_battle(unsigned battle){pad_battle=battle;}
unsigned mp_native_ticks(void){static u64 start;u64 now=svcGetSystemTick();if(!start)start=now;now-=start;return(u32)((now/SYSCLOCK_ARM11)*40500000ULL+((now%SYSCLOCK_ARM11)*40500000ULL)/SYSCLOCK_ARM11);}
void mp_native_pad(void*output){u8*p=output;memset(p,0,48);for(int i=1;i<4;++i)p[i*12+10]=0xff;hidScanInput();u32 key=hidKeysHeld()|scripted_keys(),button=0;extern void mp_native_display_keys(unsigned);mp_native_display_keys(key);if((key&(KEY_ZL|KEY_ZR|KEY_SELECT))==(KEY_ZL|KEY_ZR|KEY_SELECT))key&=~(KEY_ZL|KEY_ZR|KEY_SELECT);circlePosition stick,sub;hidCircleRead(&stick);hidCstickRead(&sub);
    /* The saved custom buttons during an unpaused match (not while port 1
     * steers the Camera Mode camera); the standard layout otherwise. */
    extern volatile unsigned mp_native_camera_mode,mp_native_camera_control,mp_native_camera_match;
    unsigned battle=pad_battle&&!(mp_native_camera_mode&&mp_native_camera_control&&mp_native_camera_match);
    button=mp_native_map_keys(key,battle);
    test_pad(&button,&stick,&sub);
    u32 down=hidKeysDown();
#ifdef MP_SMOKE_TEST
    if(mp_test_keys)button|=mp_native_map_keys(mp_test_keys,battle);
    {static u32 previous_test;down|=mp_test_keys&~previous_test;previous_test=mp_test_keys;}
#endif
    /* Slippi Direct: the code keyboard takes the controls; locked in, A/B/X/Y
     * cannot unselect or recolour (port/3ds/slippi/slippi_ui.c). */
    {extern unsigned slippi_ui_pad(unsigned,unsigned,int*);int zero;button=slippi_ui_pad(down,button,&zero);
     if(zero)stick.dx=stick.dy=sub.dx=sub.dy=0;}
    p[0]=button>>8;p[1]=button;p[2]=stick.dx*80/156;p[3]=stick.dy*80/156;p[4]=sub.dx*80/156;p[5]=sub.dy*80/156;p[6]=(button&0x40)?255:0;p[7]=(button&0x20)?255:0;p[8]=(button&0x100)?255:0;p[9]=(button&0x200)?255:0;
    /* Camera Mode: port 4 is the camera controller (bottom.c). */
    if(mp_native_camera_mode){p[3*12+10]=0;if(mp_native_camera_control&&mp_native_camera_match){memcpy(p+36,p,10);memset(p,0,10);}}}
