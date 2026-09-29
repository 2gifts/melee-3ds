/* GX command FIFO between the engine thread and a GX translator thread.
 *
 * On the GameCube the CPU only wrote commands into a FIFO; the GPU read them,
 * together with display lists, vertex arrays and textures, asynchronously.
 * HSD relies on that: HSD_VIGXSetDrawDone waits for the previous frame's
 * draw-done token before issuing the next one, so rendering of frame N may
 * overlap simulation of frame N+1. This file restores that split. The public
 * GX functions record their arguments (copying matrices, texture/light/TLUT
 * objects and other by-pointer payloads) into a ring of BE8 words, and a
 * translator thread on another core replays them through the original GX
 * implementation in gx.c (gxr_*), which feeds the native renderer.
 *
 * Getters are served from main-thread mirrors of the few states they read.
 * The draw-done callback runs on the engine thread, from its cooperative
 * interrupt poll, once the translator has finished the frame. Without a
 * translator (mp_gx_async==0) every call executes directly, as before. */
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/vi.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include "native.h"
#undef memcpy
#define memcpy(d,s,n) __builtin_memcpy(d,s,n)
#include "gx_fifo_ops.h"

unsigned mp_gx_async;          /* Set once, before the engine boots. */
unsigned mp_gx_list_uncached;  /* Translator: current list is a FIFO copy. */
extern unsigned mp_gx_stereo_depth;
extern void mp_platform_gx_sleep(unsigned who),mp_platform_gx_wake(unsigned who),mp_platform_gx_check(void);
extern void mp_platform_frame_render(void),mp_platform_frame_main(void);
extern void mp_platform_render_stereo(unsigned depth);
extern void mp_platform_texture_dirty(void*,u32,u32);

#define FIFO_WORDS (1u<<20) /* 4 MiB: several frames of commands. */
#define FIFO_MASK (FIFO_WORDS-1)
#define PUBLISH_BATCH 512u
#define CONSUME_BATCH 4096u
#define BYTES_CAPACITY 1024u
#define INLINE_LIST_BYTES 65536u
enum{CONSUMER,PRODUCER};
static u32*fifo;
/* Monotonic word indices; FIFO_WORDS divides 2^32, so wrap is harmless. */
/* Exported for the native stall report (log_io.c), read as BE8 words:
 * produce, published, consumed, stop, sleeping[2], frames recorded/done/
 * delivered, engine wait site, translator op. */
u32 mp_gx_fifo_state[12];
#define produce mp_gx_fifo_state[0]
#define gx_published mp_gx_fifo_state[1]
#define gx_consumed mp_gx_fifo_state[2]
#define gx_stop mp_gx_fifo_state[3]
#define gx_sleeping (mp_gx_fifo_state+4)
#define frames_recorded mp_gx_fifo_state[6]
#define frames_done mp_gx_fifo_state[7]
#define frames_delivered mp_gx_fifo_state[8]
#define engine_site mp_gx_fifo_state[9]
#define translator_op mp_gx_fifo_state[10]
static u32 produce_published;
/* Where the engine thread waits: 1 draw done, 2 FIFO space, 3 frame cap,
 * 4 retrace (video_pad.c), 5 disc (scheduler.c); 0 running. */
enum{SITE_RUNNING,SITE_DRAW_DONE,SITE_FIFO_SPACE,SITE_FRAME_CAP};
static unsigned gx_spin;
/* Diagnostics, read by the platform frame report. */
u32 mp_gx_producer_waits,mp_gx_producer_wait_ticks,mp_gx_consumer_sleeps,mp_gx_records,mp_gx_inline_lists;

static inline float gx_fifo_float(u32 w){float f;memcpy(&f,&w,4);return f;}
static inline GXColor gx_fifo_color(u32 w){GXColor c;memcpy(&c,&w,4);return c;}
static inline GXColorS10 gx_fifo_color_s10(const u32*w){GXColorS10 c;memcpy(&c,w,8);return c;}

/* ---- Producer (engine thread) ---- */
static u32*bytes_record;static unsigned bytes_count;
static void gx_bytes_close(void){
    if(!bytes_record)return;
    u32*r=bytes_record;bytes_record=NULL;
    unsigned words=2+(bytes_count+3)/4;r[1]=bytes_count;r[0]=(GXOP_BYTES<<24)|words;produce+=words;
}
static void gx_fifo_publish(void){
    gx_bytes_close();
    if(produce==produce_published)return;
    produce_published=produce;
    __atomic_store_n(&gx_published,produce,__ATOMIC_SEQ_CST);
    if(__atomic_load_n(&gx_sleeping[CONSUMER],__ATOMIC_SEQ_CST))mp_platform_gx_wake(CONSUMER);
}
/* Sleep until condition() holds; the consumer wakes a declared sleeper. */
static int gx_space_ready(unsigned words);
#define gx_space_ready_check gx_space_ready
static void gx_producer_wait(int(*ready)(unsigned),unsigned arg){
    if(ready(arg))return;
    gx_fifo_publish();
    u32 site=engine_site;if(!site)engine_site=ready==gx_space_ready_check?SITE_FIFO_SPACE:SITE_DRAW_DONE;
    u32 start=mp_platform_ticks();++mp_gx_producer_waits;
    while(!ready(arg)){
        __atomic_store_n(&gx_sleeping[PRODUCER],1,__ATOMIC_SEQ_CST);
        if(!ready(arg))mp_platform_gx_sleep(PRODUCER);
        __atomic_store_n(&gx_sleeping[PRODUCER],0,__ATOMIC_SEQ_CST);
        mp_platform_gx_check();
    }
    engine_site=site;
    mp_gx_producer_wait_ticks+=mp_platform_ticks()-start;
}
static int gx_space_ready(unsigned words){return produce+words-__atomic_load_n(&gx_consumed,__ATOMIC_SEQ_CST)<=FIFO_WORDS;}
static u32*gx_fifo_begin(unsigned words){
    gx_bytes_close();
    unsigned offset=produce&FIFO_MASK;
    if(offset+words>FIFO_WORDS){
        unsigned skip=FIFO_WORDS-offset;gx_producer_wait(gx_space_ready,skip+words);
        fifo[offset]=(GXOP_WRAP<<24)|skip;produce+=skip;
    }else gx_producer_wait(gx_space_ready,words);
    return fifo+(produce&FIFO_MASK);
}
static void gx_fifo_end(u32*r,unsigned op,unsigned words){
    r[0]=(op<<24)|words;produce+=words;++mp_gx_records;
    if(produce-produce_published>=PUBLISH_BATCH)gx_fifo_publish();
}
static void gx_bytes_put(const void*data,unsigned n){
    if(bytes_record&&bytes_count+n>BYTES_CAPACITY)gx_bytes_close();
    if(!bytes_record){bytes_record=gx_fifo_begin(2+BYTES_CAPACITY/4);bytes_count=0;}
    memcpy((u8*)(bytes_record+2)+bytes_count,data,n);bytes_count+=n;
}

/* ---- Main-thread mirrors for getters ---- */
static float mirror_viewport[6];
static f32 mirror_projection[4][4];static u32 mirror_projection_type;
static u16 mirror_copy_src[4]={0,0,640,480};
static unsigned mirror_left_capture;
static GXFifoObj mirror_fifo_object;
static GXDrawDoneCallback done_callback;
static void gx_mirror_GXSetViewport(float x,float y,float w,float h,float n,float f){
    mirror_viewport[0]=x;mirror_viewport[1]=y;mirror_viewport[2]=w;mirror_viewport[3]=h;mirror_viewport[4]=n;mirror_viewport[5]=f;}
static void gx_mirror_GXSetViewportJitter(float x,float y,float w,float h,float n,float f,u32 field){(void)field;gx_mirror_GXSetViewport(x,y,w,h,n,f);}
static void gx_mirror_GXSetProjection(Mtx44 m,GXProjectionType type){memcpy(mirror_projection,m,sizeof(mirror_projection));mirror_projection_type=type;}
static void gx_mirror_GXSetProjectionv(float*p){
    memset(mirror_projection,0,sizeof(mirror_projection));mirror_projection_type=p[0];
    mirror_projection[0][0]=p[1];mirror_projection[1][1]=p[3];mirror_projection[2][2]=p[5];mirror_projection[2][3]=p[6];
    if(mirror_projection_type==GX_PERSPECTIVE){mirror_projection[0][2]=p[2];mirror_projection[1][2]=p[4];mirror_projection[3][2]=-1;}
    else{mirror_projection[0][3]=p[2];mirror_projection[1][3]=p[4];mirror_projection[3][3]=1;}
}
static void gx_mirror_GXSetDispCopySrc(u16 x,u16 y,u16 w,u16 h){mirror_copy_src[0]=x;mirror_copy_src[1]=y;mirror_copy_src[2]=w;mirror_copy_src[3]=h;}
void GXGetViewportv(float*out){memcpy(out,mirror_viewport,sizeof(mirror_viewport));}
void GXGetProjectionv(float*p){
    int perspective=mirror_projection_type==GX_PERSPECTIVE;
    p[0]=mirror_projection_type;p[1]=mirror_projection[0][0];p[2]=perspective?mirror_projection[0][2]:mirror_projection[0][3];
    p[3]=mirror_projection[1][1];p[4]=perspective?mirror_projection[1][2]:mirror_projection[1][3];
    p[5]=mirror_projection[2][2];p[6]=mirror_projection[2][3];
}

#define GX_FIFO_RECORDERS
#include "gx_fifo_ops.h"

GXFifoObj*GXInit(void*base,u32 size){
    memset(mirror_projection,0,sizeof(mirror_projection));memset(mirror_viewport,0,sizeof(mirror_viewport));
    mirror_copy_src[0]=mirror_copy_src[1]=0;mirror_copy_src[2]=640;mirror_copy_src[3]=480;
    if(!mp_gx_async){gxr_GXInit(base,size);return &mirror_fifo_object;}
    u32*r=gx_fifo_begin(1);gx_fifo_end(r,GXOP_INIT,1);return &mirror_fifo_object;
}
u32 GXSetDispCopyYScale(float s){
    if(!mp_gx_async)gxr_GXSetDispCopyYScale(s);
    else{u32*r=gx_fifo_begin(2);memcpy(&r[1],&s,4);gx_fifo_end(r,GXOP_COPY_YSCALE,2);}
    return (unsigned)((mirror_copy_src[3]-1)*s)+1;
}
unsigned mp_gx_left_capture(unsigned enabled){
    unsigned old=mirror_left_capture;mirror_left_capture=!!enabled;
    if(!mp_gx_async)gxr_mp_gx_left_capture(enabled);
    else{u32*r=gx_fifo_begin(2);r[1]=enabled;gx_fifo_end(r,GXOP_LEFT_CAPTURE,2);}
    return old;
}
void mp_gx_set_stereo(unsigned depth){
    if(!mp_gx_async){mp_gx_stereo_depth=depth;return;}
    u32*r=gx_fifo_begin(2);r[1]=depth;gx_fifo_end(r,GXOP_STEREO,2);
}

/* A display list rewritten by the CPU (and flushed, as the GameCube GPU
 * required) within the last two frames is copied into the record, so a
 * later rewrite cannot race the translator. Other lists are read in place. */
static struct{u32 lo,hi,frame;}recent_flush[8];static unsigned recent_flush_next;
static int gx_recently_flushed(u32 lo,u32 n){
    for(unsigned i=0;i<8;++i){const u32 a=recent_flush[i].lo,b=recent_flush[i].hi;
        if(b>a&&frames_recorded-recent_flush[i].frame<=2&&lo<b&&a<lo+n)return 1;}
    return 0;
}
void mp_gx_cache_range(void*p,u32 n,u32 cpu_written){
    if(!n)return;
    if(!mp_gx_async){gxr_mp_gx_source_write(p,n);mp_platform_texture_dirty(p,n,cpu_written);return;}
    unsigned i=recent_flush_next++&7;recent_flush[i].lo=(u32)p;recent_flush[i].hi=(u32)p+n;recent_flush[i].frame=frames_recorded;
    u32*r=gx_fifo_begin(4);r[1]=(u32)p;r[2]=n;r[3]=cpu_written;gx_fifo_end(r,GXOP_DC_RANGE,4);
}
void GXCallDisplayList(void*list,u32 nbytes){
    if(!mp_gx_async){gxr_GXCallDisplayList(list,nbytes);return;}
    if(nbytes<=INLINE_LIST_BYTES&&gx_recently_flushed((u32)list,nbytes)){
        unsigned words=3+(nbytes+3)/4;u32*r=gx_fifo_begin(words);r[1]=(u32)list;r[2]=nbytes;memcpy(&r[3],list,nbytes);
        gx_fifo_end(r,GXOP_LIST_INLINE,words);++mp_gx_inline_lists;
    }else{u32*r=gx_fifo_begin(3);r[1]=(u32)list;r[2]=nbytes;gx_fifo_end(r,GXOP_LIST,3);}
    gx_fifo_publish();
}

/* Immediate-mode vertex data: the GX FIFO byte stream, in write order. */
void mp_gx_write_u8(u8 x){if(!mp_gx_async){gxr_mp_gx_write_u8(x);return;}gx_bytes_put(&x,1);}
void mp_gx_write_s8(s8 x){if(!mp_gx_async){gxr_mp_gx_write_s8(x);return;}gx_bytes_put(&x,1);}
void mp_gx_write_u16(u16 x){if(!mp_gx_async){gxr_mp_gx_write_u16(x);return;}gx_bytes_put(&x,2);}
void mp_gx_write_s16(s16 x){if(!mp_gx_async){gxr_mp_gx_write_s16(x);return;}gx_bytes_put(&x,2);}
void mp_gx_write_u32(u32 x){if(!mp_gx_async){gxr_mp_gx_write_u32(x);return;}gx_bytes_put(&x,4);}
void mp_gx_write_s32(s32 x){if(!mp_gx_async){gxr_mp_gx_write_s32(x);return;}gx_bytes_put(&x,4);}
void mp_gx_write_f32(float x){if(!mp_gx_async){gxr_mp_gx_write_f32(x);return;}gx_bytes_put(&x,4);}
void mp_gx_write_u64(u64 x){if(!mp_gx_async){gxr_mp_gx_write_u64(x);return;}gx_bytes_put(&x,8);}
void mp_gx_write_s64(s64 x){if(!mp_gx_async){gxr_mp_gx_write_s64(x);return;}gx_bytes_put(&x,8);}
void mp_gx_write_f64(double x){if(!mp_gx_async){gxr_mp_gx_write_f64(x);return;}gx_bytes_put(&x,8);}

/* ---- Frame boundaries ---- */
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb){GXDrawDoneCallback old=done_callback;done_callback=cb;return old;}
/* Engine thread only: run the callback once per completed frame, in order. */
void mp_gx_poll(void){
    if(!mp_gx_async)return;
    u32 done=__atomic_load_n(&frames_done,__ATOMIC_ACQUIRE);
    while(frames_delivered!=done){++frames_delivered;if(done_callback)done_callback();}
}
static int gx_frames_ready(unsigned unused){(void)unused;return __atomic_load_n(&frames_done,__ATOMIC_SEQ_CST)==frames_recorded;}
void GXWaitDrawDone(void){
    if(!mp_gx_async){gxr_GXWaitDrawDone();return;}
    gx_producer_wait(gx_frames_ready,0);mp_gx_poll();
}
/* Frame-rate cap: hold the engine at the end of a rendered frame until the
 * native mode's interval of retraces has passed since the previous one. The
 * game then runs exactly that many steps per rendered frame, evenly paced.
 * Engine polling (audio, alarms, disc, draw-done) continues while waiting. */
extern void mp_engine_poll(void),mp_platform_idle(void);
static u32 frame_retrace;
static void frame_pace(void){
    unsigned interval=mp_platform_frame_interval();u32 now=VIGetRetraceCount();
    if(interval>1&&(s32)(now-(frame_retrace+interval))<0){
        engine_site=SITE_FRAME_CAP;
        while((s32)(now-(frame_retrace+interval))<0){mp_engine_poll();mp_platform_idle();now=VIGetRetraceCount();}
        engine_site=SITE_RUNNING;
    }
    frame_retrace=now;
}
void GXSetDrawDone(void){
    if(!mp_gx_async){
        gxr_GXSetDrawDone();mp_platform_frame_render();
        if(done_callback)done_callback();
        mp_platform_frame_main();frame_pace();return;
    }
    u32*r=gx_fifo_begin(2);r[1]=++frames_recorded;gx_fifo_end(r,GXOP_FRAME_END,2);
    gx_fifo_publish();
    mp_platform_gx_check();
    mp_platform_frame_main();
    mp_gx_poll();
    frame_pace();
}
void GXDrawDone(void){GXSetDrawDone();GXWaitDrawDone();}

/* ---- Consumer (translator thread) ---- */
static void gx_replay(unsigned op,const u32*r){
    switch(op){
    case GXOP_WRAP:break;
    case GXOP_BYTES:{extern void mp_gxr_write_bytes(const u8*,unsigned);mp_gxr_write_bytes((const u8*)(r+2),r[1]);break;}
    case GXOP_FRAME_END:
        gxr_GXSetDrawDone();mp_platform_frame_render();
        __atomic_store_n(&frames_done,r[1],__ATOMIC_SEQ_CST);
        if(__atomic_load_n(&gx_sleeping[PRODUCER],__ATOMIC_SEQ_CST))mp_platform_gx_wake(PRODUCER);
        break;
    case GXOP_STEREO:mp_gx_stereo_depth=r[1];mp_platform_render_stereo(r[1]);break;
    case GXOP_LIST:gxr_GXCallDisplayList((void*)r[1],r[2]);break;
    case GXOP_LIST_INLINE:mp_gx_list_uncached=1;gxr_GXCallDisplayList((void*)(r+3),r[2]);mp_gx_list_uncached=0;break;
    case GXOP_DC_RANGE:gxr_mp_gx_source_write((const void*)r[1],r[2]);mp_platform_texture_dirty((void*)r[1],r[2],r[3]);break;
    case GXOP_INIT:gxr_GXInit(NULL,0);break;
    case GXOP_LEFT_CAPTURE:gxr_mp_gx_left_capture(r[1]);break;
    case GXOP_COPY_YSCALE:gxr_GXSetDispCopyYScale(gx_fifo_float(r[1]));break;
    default:gx_replay_generated(op,r);
    }
}
unsigned mp_gx_async_enable(void){
    if(mp_gx_async)return 1;
    fifo=mp_platform_alloc(FIFO_WORDS*4);if(!fifo)return 0;
    produce=produce_published=gx_published=gx_consumed=gx_stop=0;
    frames_recorded=frames_done=frames_delivered=0;mp_gx_async=1;return 1;
}
void mp_gx_async_stop(void){
    if(!mp_gx_async)return;
    gx_fifo_publish();
    __atomic_store_n(&gx_stop,1,__ATOMIC_SEQ_CST);mp_platform_gx_wake(CONSUMER);
}
/* Called after the translator thread has been joined. */
void mp_gx_async_disable(void){mp_gx_async=0;}
void mp_gx_translate(unsigned spin){
    gx_spin=spin;
    u32 cursor=__atomic_load_n(&gx_consumed,__ATOMIC_ACQUIRE),released=cursor;
    for(;;){
        if(cursor==__atomic_load_n(&gx_published,__ATOMIC_ACQUIRE)){
            if(cursor!=released){released=cursor;__atomic_store_n(&gx_consumed,cursor,__ATOMIC_SEQ_CST);}
            if(__atomic_load_n(&gx_sleeping[PRODUCER],__ATOMIC_SEQ_CST))mp_platform_gx_wake(PRODUCER);
            if(__atomic_load_n(&gx_stop,__ATOMIC_ACQUIRE))return;
            /* Commands arrive in bursts from one core; spin briefly first. */
            for(unsigned i=0;i<gx_spin&&cursor==__atomic_load_n(&gx_published,__ATOMIC_ACQUIRE);++i)__asm__ volatile("":::"memory");
            if(cursor!=__atomic_load_n(&gx_published,__ATOMIC_ACQUIRE))continue;
            __atomic_store_n(&gx_sleeping[CONSUMER],1,__ATOMIC_SEQ_CST);
            if(cursor==__atomic_load_n(&gx_published,__ATOMIC_SEQ_CST)&&!__atomic_load_n(&gx_stop,__ATOMIC_SEQ_CST)){
                ++mp_gx_consumer_sleeps;mp_platform_gx_sleep(CONSUMER);}
            __atomic_store_n(&gx_sleeping[CONSUMER],0,__ATOMIC_SEQ_CST);
            continue;
        }
        const u32*r=fifo+(cursor&FIFO_MASK);u32 header=r[0];
        translator_op=header>>24;
        gx_replay(header>>24,r);
        cursor+=header&0xffffff;
        if(cursor-released>=CONSUME_BATCH){
            released=cursor;__atomic_store_n(&gx_consumed,cursor,__ATOMIC_SEQ_CST);
            if(__atomic_load_n(&gx_sleeping[PRODUCER],__ATOMIC_SEQ_CST))mp_platform_gx_wake(PRODUCER);
        }
    }
}
