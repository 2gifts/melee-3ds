/* Isolated measurement build only. No callback is omitted; ablations stop
 * selected GX work and are restricted by the host runner to a frozen scene. */
#ifdef MP_FEASIBILITY_TEST
#include "native.h"
#include "feasibility.h"
#include <dolphin/os.h>
#include <sysdolphin/baselib/gobj.h>
#include <string.h>
typedef struct { unsigned domain,function,context,calls; unsigned long long inclusive,self; } ProbeRow;
ProbeRow mp_probe_rows[1024];
typedef struct {unsigned start,row; unsigned long long children;} ProbeStack;
static ProbeStack stack[32];
unsigned mp_probe_depth,mp_probe_errors,mp_probe_overflow;
volatile unsigned mp_probe_enabled,mp_probe_mode,mp_probe_links=~0u;
volatile unsigned mp_probe_request,mp_probe_done,mp_probe_frames=180,mp_probe_profile=1;
volatile unsigned mp_probe_stride=1;
unsigned mp_probe_result[8];
unsigned mp_probe_sampled_frames;
static unsigned render_link=31,request_seen,remaining,start_tick,frame_count,sample_index;
static unsigned auto_scene=255,auto_wait,auto_captures;
static unsigned long long frame_total;
unsigned mp_probe_begin(unsigned domain,unsigned function,unsigned context){
    if(!mp_probe_enabled)return 0;
    if(mp_probe_depth>=32){++mp_probe_errors;return 0;}
    unsigned slot=((function>>2)*2654435761u+domain*97+context*13)&1023,start=slot;
    for(;;){ProbeRow*r=mp_probe_rows+slot;
        if(!r->domain){r->domain=domain;r->function=function;r->context=context;break;}
        if(r->domain==domain&&r->function==function&&r->context==context)break;
        slot=(slot+1)&1023;if(slot==start){++mp_probe_overflow;return 0;}
    }
    stack[mp_probe_depth++]=(ProbeStack){mp_platform_ticks(),slot,0};
    return mp_probe_depth;
}
void mp_probe_end(unsigned token){
    if(!token)return;
    unsigned now=mp_platform_ticks();
    if(token!=mp_probe_depth){++mp_probe_errors;return;}
    ProbeStack*s=stack+--mp_probe_depth;unsigned elapsed=now-s->start;
    ProbeRow*r=mp_probe_rows+s->row;++r->calls;r->inclusive+=elapsed;
    if(s->children>elapsed)++mp_probe_errors;else r->self+=elapsed-s->children;
    if(mp_probe_depth)stack[mp_probe_depth-1].children+=elapsed;
}
void mp_probe_proc(void*object,void(*callback)(void*)){
    HSD_GObj*g=object;unsigned token=mp_probe_begin(1,(unsigned)callback,g->p_link);
    callback(object);mp_probe_end(token);
}
void mp_probe_render(void*object,int pass){
    HSD_GObj*g=object;unsigned saved=render_link;render_link=g->p_link;
    unsigned token=mp_probe_begin(2,(unsigned)g->render_cb,(g->p_link<<16)|(g->gx_link<<8)|(pass&255));
    g->render_cb(g,pass);mp_probe_end(token);render_link=saved;
}
unsigned mp_probe_drop(unsigned mode){
#ifdef MP_FEASIBILITY_AUTO
    (void)mode;return 0;
#else
    return mp_probe_mode>=mode && render_link<32 && (mp_probe_links&(1u<<render_link));
#endif
}
void mp_probe_scene(unsigned scene){
    auto_scene=scene;auto_wait=120;auto_captures=0;
#ifdef MP_FEASIBILITY_AUTO
    if(remaining){remaining=0;mp_probe_enabled=0;mp_probe_done=request_seen;OSReport("Feasibility window discarded at scene transition\n");}
#endif
}
static void report_rows(void){
    unsigned count=0;for(unsigned i=0;i<1024;++i)count+=mp_probe_rows[i].calls!=0;
    /* The BE8 OSReport bridge supports 32-bit integer varargs. Split wide
     * counters explicitly; %llu would misalign all subsequent arguments. */
    OSReport("FeasibilityResult id=%u scene=%u frames=%u sampled=%u stride=%u ticks_hi=%u ticks_lo=%u errors=%u overflow=%u rows=%u\n",request_seen,auto_scene,mp_probe_frames,mp_probe_sampled_frames,mp_probe_stride,(unsigned)(frame_total>>32),(unsigned)frame_total,mp_probe_errors,mp_probe_overflow,count);
    for(unsigned i=0;i<1024;++i){ProbeRow*r=mp_probe_rows+i;
        if(r->calls)OSReport("FeasibilityRow id=%u domain=%u fn=%08x context=%u calls=%u inclusive_hi=%u inclusive_lo=%u self_hi=%u self_lo=%u\n",request_seen,r->domain,r->function,r->context,r->calls,(unsigned)(r->inclusive>>32),(unsigned)r->inclusive,(unsigned)(r->self>>32),(unsigned)r->self);}
    OSReport("FeasibilityEnd id=%u\n",request_seen);
}
void mp_probe_checkpoint(void){
    unsigned now=mp_platform_ticks();++frame_count;
    if(remaining){
        mp_probe_sampled_frames+=!!mp_probe_enabled;
        frame_total+=(unsigned)(now-start_tick);start_tick=now;
        if(!--remaining){
            mp_probe_enabled=0;mp_probe_result[0]=request_seen;mp_probe_result[1]=mp_probe_frames;
            mp_probe_result[2]=frame_total>>32;mp_probe_result[3]=frame_total;
            mp_probe_result[4]=mp_probe_mode;mp_probe_result[5]=mp_probe_links;
            mp_probe_result[6]=mp_probe_errors;mp_probe_result[7]=mp_probe_overflow;
            mp_probe_mode=0;mp_probe_done=request_seen;
            OSReport("Feasibility measurement complete id=%u frames=%u errors=%u overflow=%u\n",request_seen,mp_probe_frames,mp_probe_errors,mp_probe_overflow);
#ifdef MP_FEASIBILITY_AUTO
            report_rows();++auto_captures;auto_wait=360;
#endif
        }
        else {++sample_index;mp_probe_enabled=mp_probe_profile&&!(sample_index%mp_probe_stride);}
    }
#ifdef MP_FEASIBILITY_AUTO
    if(!remaining&&auto_scene==2&&auto_captures<3){
        if(auto_wait)--auto_wait;
        /* Seven avoids always sampling the same phase of common 2/4/8-frame
         * schedules. This counter never reads or advances the game's RNG. */
        else {mp_probe_frames=120;mp_probe_stride=7;mp_probe_profile=1;mp_probe_mode=0;mp_probe_request=request_seen+1;}
    }
#endif
    if(!remaining&&mp_probe_request!=request_seen){
        request_seen=mp_probe_request;
        /* Called outside all measured engine callbacks at the draw boundary. */
        if(mp_probe_depth||mp_probe_frames<30||mp_probe_frames>1800||!mp_probe_stride){++mp_probe_errors;mp_probe_done=request_seen;return;}
        memset(mp_probe_rows,0,sizeof(mp_probe_rows));mp_probe_errors=mp_probe_overflow=0;
        remaining=mp_probe_frames;frame_total=0;sample_index=mp_probe_sampled_frames=0;start_tick=mp_platform_ticks();
        mp_probe_enabled=mp_probe_profile;
    }
}
#endif
