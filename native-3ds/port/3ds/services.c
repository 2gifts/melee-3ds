#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern void mp_native_panic(const char*),mp_native_log(const char*);
unsigned mp_native_idle_count;
void mp_native_idle(void){++mp_native_idle_count;svcSleepThread(1000000);}
extern unsigned mp_native_frame_number(void);
static unsigned scripted_keys(void){
#ifdef MP_SMOKE_TEST
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
volatile struct{u32 sequence,buttons,frames;s32 x,y;}mp_test_control;
static u32 test_sequence,test_until;
static void test_pad(u32*buttons,circlePosition*stick){
    u32 f=mp_native_frame_number();
    if(mp_test_control.sequence){if(test_sequence!=mp_test_control.sequence){test_sequence=mp_test_control.sequence;test_until=f+mp_test_control.frames;}*buttons=f<test_until?mp_test_control.buttons:0;stick->dx=f<test_until?mp_test_control.x*156/80:0;stick->dy=f<test_until?mp_test_control.y*156/80:0;}
    else {
        if(f>=850&&f<885){stick->dx=25*156/80;stick->dy=156;}
        if(f>=900&&f<908)stick->dx=-156;
        if(f>=930&&f<935)stick->dy=-117;
        if(f>=940&&f<946)stick->dx=117;
        if(f>=1080&&f<1092)stick->dy=156;
    }
}
#else
static void test_pad(u32*buttons,circlePosition*stick){(void)buttons;(void)stick;}
#endif
unsigned mp_native_ticks(void){static u64 start;u64 now=svcGetSystemTick();if(!start)start=now;now-=start;return(u32)((now/SYSCLOCK_ARM11)*40500000ULL+((now%SYSCLOCK_ARM11)*40500000ULL)/SYSCLOCK_ARM11);}
void mp_native_pad(void*output){u8*p=output;memset(p,0,48);for(int i=1;i<4;++i)p[i*12+10]=0xff;hidScanInput();u32 key=hidKeysHeld()|scripted_keys(),button=0;extern void mp_native_display_keys(unsigned);mp_native_display_keys(key);if((key&(KEY_ZL|KEY_ZR|KEY_SELECT))==(KEY_ZL|KEY_ZR|KEY_SELECT))key&=~(KEY_ZL|KEY_ZR|KEY_SELECT);circlePosition stick,sub;hidCircleRead(&stick);hidCstickRead(&sub);if(key&KEY_A)button|=0x100;if(key&KEY_B)button|=0x200;if(key&KEY_X)button|=0x400;if(key&KEY_Y)button|=0x800;if(key&KEY_START)button|=0x1000;if(key&KEY_L)button|=0x40;if(key&KEY_R)button|=0x20;if(key&(KEY_ZL|KEY_ZR))button|=0x10;if(key&KEY_DLEFT)button|=1;if(key&KEY_DRIGHT)button|=2;if(key&KEY_DDOWN)button|=4;if(key&KEY_DUP)button|=8;test_pad(&button,&stick);p[0]=button>>8;p[1]=button;p[2]=stick.dx*80/156;p[3]=stick.dy*80/156;p[4]=sub.dx*80/156;p[5]=sub.dy*80/156;p[6]=(button&0x40)?255:0;p[7]=(button&0x20)?255:0;p[8]=(button&0x100)?255:0;p[9]=(button&0x200)?255:0;}
