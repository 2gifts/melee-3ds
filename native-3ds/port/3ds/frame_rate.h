#ifndef MP_FRAME_RATE_H
#define MP_FRAME_RATE_H
/* Frame-rate mode, cycled with the bottom-screen RATE button: AUTO, 30, 60.
 * The engine holds each rendered frame until mp_native_frame_interval()
 * retraces have passed (gx_fifo.c frame_pace): 1 renders every game step,
 * 2 renders every second step at an even 30 FPS. Retraces follow the top
 * screen's VBlank (video_pad.c), so steps and displayed frames stay aligned.
 *
 * AUTO starts at 60 and drops to 30 when 6 of the last 30 rendered frames
 * missed a VBlank. It returns to 60 only after three consecutive thread-load
 * reports (about 6 s) show headroom on the engine, translator and GPU. A drop
 * within 10 s of returning holds 30 until the scene or roster changes. */
enum{MP_RATE_AUTO,MP_RATE_30,MP_RATE_60,MP_RATE_MODES};
static const char*const rate_names[MP_RATE_MODES]={"auto","30","60"};
static unsigned rate_mode=MP_RATE_AUTO,rate_interval=1,rate_cycle_pending;
/* Published by the translator's thread-load report (game.c). */
static volatile float rate_engine_ms,rate_translator_ms,rate_gpu_ms;
static volatile unsigned rate_load_serial;
void mp_native_cycle_rate(void){rate_cycle_pending=1;}
unsigned mp_native_frame_interval(void){return rate_interval;}
unsigned mp_native_vblank_count(void){return C3D_FrameCounter(0);}
/* Bottom-screen label: mode*2 + (capped at 30). */
static unsigned rate_state(void){return rate_mode*2+(rate_interval==2);}

static void rate_publish(float engine,float translator,float gpu){
    rate_engine_ms=engine;rate_translator_ms=translator;rate_gpu_ms=gpu;
    __atomic_add_fetch(&rate_load_serial,1,__ATOMIC_RELEASE);
}
/* Engine thread, once per rendered frame. */
static void rate_update(unsigned frame,unsigned context){
    static u32 previous_vblank,misses;static unsigned seen_context=~0u,seen_serial,good,returned,hold,skip;
    static unsigned logged_interval=1;
    if(rate_cycle_pending){
        rate_cycle_pending=0;rate_mode=(rate_mode+1)%MP_RATE_MODES;hold=0;good=0;misses=0;
        char text[64];snprintf(text,sizeof(text),"Frame rate mode: %s\n",rate_names[rate_mode]);mp_native_log(text);
    }
    u32 vblank=C3D_FrameCounter(0);unsigned span=vblank-previous_vblank;previous_vblank=vblank;
    if(rate_mode==MP_RATE_30)rate_interval=2;
    else if(rate_mode==MP_RATE_60)rate_interval=1;
    else{
        if(context!=seen_context){seen_context=context;hold=0;good=0;misses=0;rate_interval=1;}
        if(rate_interval==1){
            misses=(misses<<1)|(span>=2);
            if(__builtin_popcount(misses&0x3fffffffu)>=6){
                rate_interval=2;good=0;
                /* The next report still covers frames rendered at 60. */
                seen_serial=__atomic_load_n(&rate_load_serial,__ATOMIC_ACQUIRE);skip=1;
                if(returned&&frame-returned<600)hold=1;
            }
        }else if(!hold){
            unsigned serial=__atomic_load_n(&rate_load_serial,__ATOMIC_ACQUIRE);
            if(serial!=seen_serial&&skip){seen_serial=serial;skip=0;}
            else if(serial!=seen_serial){seen_serial=serial;
                /* At 30 the engine report covers two game steps and one draw. */
                if(rate_engine_ms*.6f<13.f&&rate_translator_ms<13.f&&rate_gpu_ms<13.f)++good;else good=0;
                if(good>=3){rate_interval=1;returned=frame;misses=0;good=0;}
            }
        }
    }
    if(rate_interval!=logged_interval){logged_interval=rate_interval;
        char text[80];snprintf(text,sizeof(text),"Frame rate cap: %s FPS (mode %s)\n",rate_interval==2?"30":"60",rate_names[rate_mode]);mp_native_log(text);}
}
#endif
