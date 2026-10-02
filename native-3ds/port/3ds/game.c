#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <setjmp.h>
#include <sys/stat.h>
#include "audio_trace.h"
#include "log_progress.h"
#include "bottom.h"
#include "render_worker.h"
#include "gx_thread.h"

extern const char mp_image_text_start[],mp_image_rodata_start[],mp_image_data_start[];
/* Filled after linking, without relocation records. Volatile prevents the
 * compiler from folding the unpatched sentinel values into this check. */
const volatile u32 mp_expected_image_layout[4]={0x4d50494d,0,0,0};
extern int mp_game_boot(void);
extern int mp_game_load(void *,unsigned);
extern void mp_game_draw(void);
extern int mp_renderer_init(void);
extern void mp_renderer_begin(void),mp_renderer_end(void),mp_renderer_exit(void);
extern void mp_renderer_counts(unsigned*,unsigned*);
extern unsigned log_flushes,log_flush_ticks,log_dropped,log_errors;
extern void mp_log_init(void),mp_log_append(const char*),mp_log_flush(int),mp_log_close(void);
extern void mp_native_cpu_init(int),mp_native_cpu_poll(void),mp_native_cpu_exit(void);
extern char mp_cpu_status[40];
static jmp_buf failure_return;
static unsigned engine_frames;
static int engine_failed;
static int exit_requested;
unsigned mp_native_expanded;
static unsigned display_toggle_pending,display_combo_previous;
static unsigned bottom_touch_previous,bottom_touch_pending,bottom_touch_x,bottom_touch_y;
void mp_native_toggle_display(void){display_toggle_pending^=1;}
extern void mp_game_set_display(unsigned);
extern void mp_game_set_stereo(unsigned);
extern unsigned mp_game_stereo_scene(void);
extern unsigned mp_native_stereo_depth;
/* Latest slider request from the engine thread (render stats only). */
static unsigned mp_native_stereo_request;
#ifdef MP_SMOKE_TEST
volatile unsigned mp_test_stereo_slider=~0u;
volatile unsigned mp_test_async_files;
#endif
extern void mp_game_configure_cache(unsigned);
extern unsigned mp_game_simulation(void);
unsigned mp_native_heap_used,mp_native_heap_available;
/* Process held keys from both pad polling and the frame boundary. A short
 * SELECT press must not disappear between the two hidScanInput calls. */
void mp_native_display_keys(unsigned keys){
    unsigned combo=(keys&(KEY_ZL|KEY_ZR|KEY_SELECT))==(KEY_ZL|KEY_ZR|KEY_SELECT);
    if(combo&&!display_combo_previous)display_toggle_pending^=1;
    display_combo_previous=combo;
    /* Pad and frame polling both scan HID. Latch the first contact here so
     * a short touch cannot be consumed by an intervening controller poll. */
    unsigned touched=!!(keys&KEY_TOUCH);
    if(touched&&!bottom_touch_previous){touchPosition p;hidTouchRead(&p);
        bottom_touch_x=p.px;bottom_touch_y=p.py;bottom_touch_pending=1;}
    bottom_touch_previous=touched;
}
#ifdef MP_SMOKE_TEST
volatile unsigned mp_test_capture;
volatile unsigned mp_test_frame_limit=12000;
volatile unsigned mp_test_memory_compare;
volatile unsigned mp_test_rotation_check;
volatile unsigned mp_test_ppc_math_check;
volatile unsigned mp_test_stall;
volatile unsigned mp_test_suspend,mp_test_suspends; /* HOME Menu suspend stand-in (1 with drain, 2 without) */
#endif
unsigned mp_native_frame_number(void){return engine_frames;}
void mp_native_log(const char*);
#include "frame_rate.h"
static void flush_log(void);
void mp_native_panic(const char*) __attribute__((noreturn));
static void capture_frame(void){
    static u8*copy;if(!copy)copy=malloc(400*240*3);if(!copy)return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    for(int screen=0;screen<3;++screen){
        u16 w,h;u8*fb=gfxGetFramebuffer(screen==2?GFX_BOTTOM:GFX_TOP,screen==1?GFX_RIGHT:GFX_LEFT,&w,&h);
        unsigned size=w*h*(screen==2?2:3);
        if(screen==2)fb=(u8*)mp_native_bottom_pixels;else GSPGPU_InvalidateDataCache(fb,size);
        /* Read on the CPU before file IPC; accelerated emulator surfaces
         * need materializing into RAM before a service can read the bytes. */
        for(unsigned i=0;i<size;++i)copy[i]=((volatile u8*)fb)[i];
        FILE*f=fopen(screen==2?"sdmc:/3ds/melee/engine-bottom.rgb565":screen==1?"sdmc:/3ds/melee/engine-right.bgr":"sdmc:/3ds/melee/engine-top.bgr","wb");
        if(f){fwrite(copy,1,size,f);fclose(f);}
    }
    C3D_FrameEnd(0);
}
#include "frame_stats.h"
static unsigned render_frames;
static u32 be32(u32 x){return __builtin_bswap32(x);}
/* Render side of a GX frame boundary. With the GX translator this runs on
 * the translator thread, one frame behind the engine; otherwise on the
 * engine thread just before mp_native_frame_main. It is the renderer's only
 * producer: nothing here may touch engine state or longjmp. */
void mp_native_frame_render(void)
{
    extern unsigned mp_native_ticks(void);
    static unsigned previous_tick,samples;static u64 frame_ticks;
    mp_renderer_end();++render_frames;
#ifdef MP_SMOKE_TEST
    extern void mp_renderer_benchmark_frame(unsigned);
    mp_renderer_benchmark_frame(render_frames);
#endif
    unsigned now=mp_native_ticks();if(previous_tick){frame_ticks+=(u32)(now-previous_tick);++samples;}
    static MPFrameStats pacing;
    extern u64 mp_gpu_total_wait_ticks,mp_early_queue_wait_ticks;
    static u64 previous_gpu_wait,previous_early_wait,worst_gpu_wait,worst_early_wait;
    static unsigned worst_ticks,worst_vertices,worst_draws,worst_frame;
    extern unsigned mp_native_context_serial;
    static unsigned pacing_context=~0u,pacing_display=~0u,pacing_stereo=~0u;
    unsigned stereo=__atomic_load_n(&mp_native_stereo_request,__ATOMIC_RELAXED);
    if(pacing_context!=mp_native_context_serial||pacing_display!=mp_native_expanded||pacing_stereo!=!!stereo){
        pacing.count=pacing.ready=0;pacing_context=mp_native_context_serial;
        pacing_display=mp_native_expanded;pacing_stereo=!!stereo;
        worst_ticks=0;
    }
    if(pacing.ready&&(u32)(now-pacing.previous)>worst_ticks){
        worst_ticks=now-pacing.previous;worst_frame=render_frames;
        mp_renderer_counts(&worst_vertices,&worst_draws);
        worst_gpu_wait=mp_gpu_total_wait_ticks-previous_gpu_wait;
        worst_early_wait=mp_early_queue_wait_ticks-previous_early_wait;
    }
    previous_gpu_wait=mp_gpu_total_wait_ticks;previous_early_wait=mp_early_queue_wait_ticks;
    if(mp_frame_stats_sample(&pacing,now)){
        unsigned over60=0,over30=0;char text[224];
        for(unsigned i=0;i<pacing.count;++i){over60+=pacing.ticks[i]>680400;over30+=pacing.ticks[i]>1360800;}
        mp_frame_stats_sort(&pacing);
        snprintf(text,sizeof(text),"Frame pacing/120: p50=%.2f p95=%.2f p99=%.2f max=%.2f ms; over16.8=%u over33.6=%u; stereo=%u\n",
            pacing.ticks[59]/40500.0,pacing.ticks[113]/40500.0,pacing.ticks[118]/40500.0,pacing.ticks[119]/40500.0,over60,over30,stereo);
        mp_native_log(text);pacing.count=0;
        snprintf(text,sizeof(text),"Worst paced frame %u: %.2f ms; vertices=%u draws=%u previous-GPU wait=%.2f ms in-frame-GPU wait=%.2f ms\n",
            worst_frame,worst_ticks/40500.0,worst_vertices,worst_draws,
            worst_gpu_wait*1000.0/SYSCLOCK_ARM11,worst_early_wait*1000.0/SYSCLOCK_ARM11);
        mp_native_log(text);worst_ticks=0;
    }
    if((render_frames%60)==0){extern unsigned mp_native_idle_count;char text[180];unsigned v,d;mp_renderer_counts(&v,&d);snprintf(text,sizeof(text),"Original engine frame %u: %u vertices, %u draws; frame %.2f ms (excluding capture), %u idle calls\n",render_frames,v,d,samples?(double)frame_ticks/samples/40500:0,mp_native_idle_count);mp_native_log(text);frame_ticks=0;samples=0;mp_native_idle_count=0;
        if(mp_gx_translator_active){
            /* Engine-side counters are BE8 words. */
            extern u32 mp_gx_producer_waits,mp_gx_producer_wait_ticks,mp_gx_consumer_sleeps,mp_gx_records,mp_gx_inline_lists;
            static u32 waits0,wait_ticks0,sleeps0,records0,inline0;
            u32 waits=be32(mp_gx_producer_waits),wait_ticks=be32(mp_gx_producer_wait_ticks),sleeps=be32(mp_gx_consumer_sleeps);
            u32 records=be32(mp_gx_records),inlined=be32(mp_gx_inline_lists);
            snprintf(text,sizeof(text),"GX pipeline/60 frames core=%u: engine waits=%u (%.2f ms/frame) translator sleeps=%u records/frame=%u inline lists=%u\n",
                mp_gx_translator_core,waits-waits0,(wait_ticks-wait_ticks0)/40500.0/60,sleeps-sleeps0,(records-records0)/60,inlined-inline0);
            mp_native_log(text);waits0=waits;wait_ticks0=wait_ticks;sleeps0=sleeps;records0=records;inline0=inlined;
        }
        {   /* Busy time per rendered frame for each pipeline thread: the one
             * nearest the frame budget limits the rate. Engine idle covers
             * retrace/alarm waits and a full GX FIFO; translator idle covers
             * an empty FIFO and waiting for the renderer worker. */
            extern u64 mp_native_idle_ticks,mp_gx_sleep_ticks[2],mp_gpu_busy_ticks;
#ifdef MP_RENDER_WORKER
            extern u64 mp_render_worker_wait_ticks,mp_render_worker_busy_ticks;
            u64 worker_wait=mp_render_worker_wait_ticks,worker_busy=mp_render_worker_busy_ticks;
#else
            u64 worker_wait=0,worker_busy=0;
#endif
            static u64 wall0,idle0,fifo0,empty0,worker_wait0,worker_busy0,gpu0;
            u64 wall=svcGetSystemTick(),idle=mp_native_idle_ticks,fifo=mp_gx_sleep_ticks[0],empty=mp_gx_sleep_ticks[1],gpu=mp_gpu_busy_ticks;
            if(wall0){
                double scale=1000.0/SYSCLOCK_ARM11/60,span=(wall-wall0)*scale;
                /* renderer=0 when it runs inline in the translator. */
                snprintf(text,sizeof(text),"Thread load/60 frames: engine=%.2f translator=%.2f renderer=%.2f gpu=%.2f ms busy per frame (frame %.2f ms)\n",
                    span-(idle-idle0+fifo-fifo0)*scale,mp_gx_translator_active?span-(empty-empty0+worker_wait-worker_wait0)*scale:0.0,
                    (worker_busy-worker_busy0)*scale,(gpu-gpu0)*scale,span);
                mp_native_log(text);
                rate_publish(span-(idle-idle0+fifo-fifo0)*scale,mp_gx_translator_active?span-(empty-empty0+worker_wait-worker_wait0)*scale:0.f,(gpu-gpu0)*scale);
            }
            wall0=wall;idle0=idle;fifo0=fifo;empty0=empty;worker_wait0=worker_wait;worker_busy0=worker_busy;gpu0=gpu;
        }
    }
#ifdef MP_RENDER_WORKER
    mp_render_worker_report(render_frames);
#endif
#ifdef MP_SMOKE_TEST
    /* The renderer is idle here: End has acknowledged every queued job. */
    if(mp_test_capture){capture_frame();mp_test_capture=0;}
#endif
    previous_tick=mp_native_ticks();mp_renderer_begin();
}
/* Engine side of a GX frame boundary: input, system services, display and
 * stereo requests, and the companion screen. Always on the engine thread. */
void mp_native_frame_main(void)
{
    extern unsigned mp_native_ticks(void);
    static unsigned previous_tick,samples;static u64 loop_ticks;
    ++engine_frames;mp_log_frame(engine_frames);
    unsigned now=mp_native_ticks();if(previous_tick){loop_ticks+=(u32)(now-previous_tick);++samples;}
    {   /* An engine frame over 50 ms: how much was disc traffic or waiting. */
        extern unsigned mp_file_sd_reads,mp_file_sd_bytes,mp_file_sd_opens,mp_file_sd_open_ticks;extern u64 mp_native_idle_ticks;
        static unsigned reads0,bytes0,opens0,open_us0;static u64 idle0;
        unsigned reads=mp_file_sd_reads,bytes=mp_file_sd_bytes,opens=mp_file_sd_opens,open_us=mp_file_sd_open_ticks;u64 idle=mp_native_idle_ticks;
        if(previous_tick&&(u32)(now-previous_tick)>40500u*50){char text[200];
            snprintf(text,sizeof(text),"Long engine frame %u: %.1f ms; SD reads +%u (+%u bytes), opens +%u (%.1f ms); idle %.1f ms\n",engine_frames,(u32)(now-previous_tick)/40500.0,
                reads-reads0,bytes-bytes0,opens-opens0,(open_us-open_us0)/1000.0,(idle-idle0)*1000.0/SYSCLOCK_ARM11);mp_native_log(text);}
        reads0=reads;bytes0=bytes;opens0=opens;open_us0=open_us;idle0=idle;
    }
    previous_tick=now;
    if(engine_frames%60==0&&mp_gx_translator_active){char text[96];
        snprintf(text,sizeof(text),"Engine loop/60 frames: %.2f ms per recorded frame\n",samples?(double)loop_ticks/samples/40500:0);
        mp_native_log(text);loop_ticks=0;samples=0;}
    if(engine_frames%300==0){extern unsigned mp_file_cache_hits,mp_file_cache_read_bytes,mp_file_sd_reads,mp_file_sd_bytes;char text[224];snprintf(text,sizeof(text),"Log batches=%u flush time=%.2f ms dropped=%u errors=%u; menu cache hits=%u bytes=%u; SD reads=%u bytes=%u\n",__atomic_load_n(&log_flushes,__ATOMIC_RELAXED),__atomic_load_n(&log_flush_ticks,__ATOMIC_RELAXED)/40500.0,log_dropped,__atomic_load_n(&log_errors,__ATOMIC_RELAXED),mp_file_cache_hits,mp_file_cache_read_bytes,mp_file_sd_reads,mp_file_sd_bytes);mp_native_log(text);flush_log();}
    if(engine_frames%300==0){extern unsigned __ctru_heap_size;struct mallinfo heap=mallinfo();char text[128];
        mp_native_heap_used=heap.uordblks;mp_native_heap_available=__ctru_heap_size>mp_native_heap_used?__ctru_heap_size-mp_native_heap_used:0;
        snprintf(text,sizeof(text),"Ordinary heap used=%u available total=%u bytes\n",mp_native_heap_used,mp_native_heap_available);mp_native_log(text);}
#ifdef MP_SMOKE_TEST
    if(mp_test_stall){mp_test_stall=0;u64 until=svcGetSystemTick()+12ULL*SYSCLOCK_ARM11;while(svcGetSystemTick()<until){}}
    if(mp_test_memory_compare==1){extern unsigned mp_game_memory_check(void);mp_test_memory_compare=mp_game_memory_check();if(!mp_test_memory_compare)mp_native_panic("ARM memory comparison self-test failed");}
    if(mp_test_rotation_check==1){extern unsigned mp_game_rotation_check(void);mp_test_rotation_check=mp_game_rotation_check();}
    if(mp_test_ppc_math_check==1){extern unsigned mp_game_ppc_math_check(void);mp_test_ppc_math_check=mp_game_ppc_math_check();}
    if(mp_test_async_files==1){extern int mp_native_async_self_test(void);if(mp_native_async_self_test())mp_test_async_files=2;}
#endif
#ifdef MP_SMOKE_TEST
    /* Azahar has no HOME Menu. Stand in for its suspend at the same point:
     * the APT hook's drain (1) or not (2), then citro3d's own queue drain
     * (C3Di_AptEventHook, APTHOOK_ONSUSPEND) and the VBlank hooks. */
    if(mp_test_suspend){
        extern void C3Di_RenderQueueWaitDone(void),C3Di_RenderQueueDisableVBlank(void),C3Di_RenderQueueEnableVBlank(void);
        if(mp_test_suspend==1)mp_gx_thread_drain();
        C3Di_RenderQueueWaitDone();C3Di_RenderQueueDisableVBlank();C3Di_RenderQueueEnableVBlank();
        ++mp_test_suspends;
    }
#endif
    hidScanInput();
    unsigned held=hidKeysHeld();mp_native_display_keys(held);
    if(!aptMainLoop()||((hidKeysDown()&KEY_SELECT)&&!(held&(KEY_ZL|KEY_ZR)))){
        /* A save written in the last 0.3 s is still in memory (card.c). */
        extern void mp_game_card_flush(void);mp_game_card_flush();
        exit_requested=1;longjmp(failure_return,1);}
    mp_native_cpu_poll();
    if(display_toggle_pending){mp_native_expanded^=1;display_toggle_pending=0;}
    {   /* Holding the FPS button (bottom-left) for two seconds runs the GPU
         * probe once (gpu_probe.h); results go to game.log. */
        static unsigned hold;
        touchPosition p;unsigned on=0;
        if(held&KEY_TOUCH){hidTouchRead(&p);on=p.px<80&&p.py>=215;}
        hold=on?hold+1:0;
#ifndef MP_SMOKE_TEST
        if(hold==120){extern void mp_native_gpu_probe_request(void);mp_native_gpu_probe_request();}
#endif
    }
    {extern unsigned mp_native_context_serial;rate_update(engine_frames,mp_native_context_serial);}
    mp_game_set_display(mp_native_expanded);
    float slider=osGet3DSliderState();
    unsigned depth=slider>0?(unsigned)(slider*1000.f):0;
    if(depth>1000)depth=1000;
#ifdef MP_SMOKE_TEST
    if(mp_test_stereo_slider<=1000)depth=mp_test_stereo_slider;
#endif
    /* Flat menus use one render view even when the slider is raised. */
    if(!mp_game_stereo_scene())depth=0;
    __atomic_store_n(&mp_native_stereo_request,depth,__ATOMIC_RELAXED);
    /* The translator applies the depth in GX order (renderer job). */
    if(!mp_gx_translator_active)mp_native_stereo_depth=depth;
    mp_game_set_stereo(depth);
    static unsigned rate_tick,rate_frame,rate_sim,rate_display=~0u,bottom_fps;
    if(!rate_tick||now-rate_tick>=40500000||rate_display!=mp_native_expanded){
        unsigned sim=mp_game_simulation(),elapsed=now-rate_tick,rendered=__atomic_load_n(&render_frames,__ATOMIC_RELAXED);
        float fps=elapsed?(rendered-rate_frame)*40500000.0/elapsed:0;
        float updates=elapsed&&sim>=rate_sim?(sim-rate_sim)*40500000.0/elapsed:0;
        bottom_fps=(unsigned)(fps+.5f);
        if(rate_tick){char text[128];snprintf(text,sizeof(text),"Rates: %.1f render FPS, %.1f game Hz; display=%s stereo=%u cap=%u mode=%s\n",fps,updates,mp_native_expanded?"expanded":"4:3",depth,rate_interval==2?30:60,rate_names[rate_mode]);mp_native_log(text);}
        rate_tick=now;rate_frame=rendered;rate_sim=sim;rate_display=mp_native_expanded;
    }
    mp_native_bottom_frame(bottom_fps,mp_native_expanded,rate_state(),bottom_touch_pending,bottom_touch_x,bottom_touch_y);
    bottom_touch_pending=0;
#ifdef MP_SMOKE_TEST
    if(mp_test_frame_limit&&engine_frames>=mp_test_frame_limit)mp_native_panic("End of automatic engine run");
#endif
}
#ifndef MP_RENDER_WORKER
void mp_native_render_stereo(unsigned depth){mp_native_stereo_depth=depth;}
#endif
void *mp_native_alloc(unsigned size){return memalign(32,size);}
void mp_native_free(void*p){free(p);}
void mp_native_geometry_retire(void){
#ifdef MP_RENDER_WORKER
    /* Cached BE8 vertex/index bytes are immutable until this fence. */
    mp_render_worker_retire_geometry();
#endif
}
static void flush_log(void){mp_log_flush(0);}
static void native_log(const char*s){mp_log_append(s);
    if(strstr(s,"Preload complete"))flush_log();
}
void mp_native_log(const char*s){MP_AUDIO_TRACE(0,native_log(s));}
void mp_native_panic(const char*s){
    if(mp_gx_thread_is_current())mp_gx_thread_panic(s);
#ifdef MP_RENDER_WORKER
    mp_render_worker_panic(s);
#endif
    /* Join the translator before this thread becomes the renderer's producer. */
    mp_gx_thread_stop();
    mp_native_log(s);mp_renderer_end();mp_log_flush(1);engine_failed=1;
#ifdef MP_SMOKE_TEST
    capture_frame();
#else
    consoleInit(GFX_BOTTOM,NULL);consoleClear();
    extern char mp_native_missing_file[];
    if(mp_native_missing_file[0]){char text[128];snprintf(text,sizeof(text),"\nGame file not found: %s\n",mp_native_missing_file);mp_native_log(text);mp_log_flush(1);}
    if(mp_native_missing_file[0])printf("Melee stopped: game file not found\n\n%s\n\nCopy the whole 3ds/melee/files folder\nfrom this version's SD package to the\nSD card.\n\n",mp_native_missing_file);
    else printf("Melee stopped\n\n%s\n\n",s);
    printf("SELECT: exit game\nDetails: /3ds/melee/game.log\n");
#endif
    longjmp(failure_return,1);}
/* A complete US v1.02 extraction has 1209 disc files, in either SD layout.
 * Without them Melee's own start-up waits forever for its sound banks
 * (stuck on PREPARING MENUS), so say where the files belong instead. With
 * only some missing, the player may still try. 1: start the game. */
enum{MP_DISC_FILES=1209};
static int disc_files_ready(void){
    extern unsigned mp_native_disc_file_count(void);
    unsigned found=mp_native_disc_file_count();
    char text[160];snprintf(text,sizeof(text),"Game files: %u of %u found in sdmc:/3ds/melee/files\n",found,MP_DISC_FILES);
    mp_native_log(text);
    if(found>=MP_DISC_FILES)return 1;
    flush_log();
    char lead[48];snprintf(lead,sizeof(lead),"%u OF %u GAME FILES FOUND",found,MP_DISC_FILES);
    mp_native_bottom_notice(found?"SOME FILES ARE MISSING":"GAME FILES NOT FOUND",found?lead:"MELEE NEEDS ITS GAME FILES",
        "THEY BELONG IN SD:/3DS/MELEE/FILES","COPY THE WHOLE 3DS FOLDER FROM YOUR BUILD","TO THE ROOT OF THE SD CARD, THEN TRY AGAIN",
        found?"A: START ANYWAY    SELECT: EXIT":"SELECT: EXIT");
#ifdef MP_SMOKE_TEST
    capture_frame();
#endif
    while(aptMainLoop()){
        hidScanInput();u32 keys=hidKeysDown();
        if(keys&KEY_SELECT)break;
        if(found&&(keys&KEY_A)){mp_native_log("Game files: starting anyway\n");return 1;}
        gspWaitForVBlank();
    }
    return 0;
}
static void *read_asset(const char *name,unsigned *size)
{
    extern int mp_native_file_path(const char*,char*,size_t);
    char path[256];if(!mp_native_file_path(name,path,sizeof(path)))return NULL;
    FILE*f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    if(n<0||n>16*1024*1024){fclose(f);return NULL;}
    void*p=mp_native_alloc(n);if(!p){fclose(f);return NULL;}
    if(fread(p,1,n,f)!=(size_t)n){free(p);p=NULL;}
    fclose(f);*size=n;return p;
}
int main(void)
{
    gfxInit(GSP_BGR8_OES,GSP_RGB565_OES,false);consoleInit(GFX_BOTTOM,NULL);
    setvbuf(stdout,NULL,_IONBF,0);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/melee",0777);
    mp_log_init();
    bool is_new=false;APT_CheckNew3DS(&is_new);
#ifdef MP_FEASIBILITY_AUTO
    mp_native_log("Melee feasibility capture 1 - update 21 baseline, sparse instrumentation, physical controls\n");
    mp_native_log("Measurement only: all rendering and simulation retained; profiling can affect FPS. Log: /3ds/melee/feasibility.log\n");
#else
    mp_native_log("Melee ARM BE8 engine startup - GPU material and early visibility update 22\n");
#endif
    mp_native_log("All-stage performance: low-detail fighters, projected shadows off, conservative off-screen mesh rejection; audited Diet scenery where available\n");
    if(is_new)mp_native_log("New 3DS family detected; fast CPU and L2 cache requested\n");
    u32 actual_layout[3]={(u32)mp_image_text_start,(u32)mp_image_rodata_start,(u32)mp_image_data_start};
    for(unsigned i=0;i<3;++i)if(actual_layout[i]!=mp_expected_image_layout[i+1]){
        char message[180];snprintf(message,sizeof(message),"Unsupported loader layout: segment %u is %08lx, expected %08lx\n",i,(unsigned long)actual_layout[i],(unsigned long)mp_expected_image_layout[i+1]);
        mp_native_log(message);printf("%s\nSELECT: exit\n",message);
        while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_SELECT)break;gspWaitForVBlank();}
        mp_log_close();gfxExit();return 1;
    }
    mp_native_log("Image layout verified; BE8 relocations prepared at build time\n");
    flush_log();
    /* Slippi experiment: network self-test when sdmc:/3ds/melee/slippi/config.ini
     * has selftest=1 (selftest_exit=1 quits afterwards). No config, no effect. */
    {extern int slippi_selftest_requested(void),slippi_selftest_run(void);
     int selftest=slippi_selftest_requested();
     if(selftest){printf("Slippi network self-test running...\n");
        int r=slippi_selftest_run();flush_log();
        printf("Slippi self-test %s (sdmc:/3ds/melee/game.log)\n",r==0?"PASSED":"FAILED");
        if(selftest==2){mp_native_log("Game application exit (self-test)\n");mp_log_close();gfxExit();return 0;}}}
    if(!mp_renderer_init()){mp_native_log("GPU initialization failed\n");mp_log_close();gfxExit();return 1;}
    mp_native_bottom_init();
    mp_native_cpu_init(is_new);
    extern void mp_native_files_init(void);mp_native_files_init();
    mp_native_bottom_art();
    if(!disc_files_ready()){
        mp_native_cpu_exit();
        {extern void mp_native_files_exit(void);mp_native_files_exit();}
        mp_native_log("Game application exit (game files missing)\n");mp_log_close();
        mp_native_bottom_exit();mp_renderer_exit();gfxExit();return 0;
    }
    extern void mp_native_settings_load(void);mp_native_settings_load();
    if(is_new){extern unsigned __ctru_heap_size;extern unsigned mp_native_cache_menus(unsigned);
        unsigned geometry=__ctru_heap_size>=80*1024*1024?12*1024*1024:6*1024*1024;
        mp_game_configure_cache(geometry);
        /* Retain the existing non-cache reserve when assigning more heap to
         * decoded geometry. The known title/menu sources still fit fully. */
        unsigned reserve=50*1024*1024+geometry;
#ifdef MP_RENDER_WORKER
        reserve+=9*1024*1024; /* Eight MiB of packets/source copies, plus metadata and worker stack. */
#endif
        unsigned budget=__ctru_heap_size>reserve?__ctru_heap_size-reserve:0;
        char text[160];
        unsigned cached=mp_native_cache_menus(budget);
        snprintf(text,sizeof(text),"Menu sources cached=%u bytes; ordinary heap=%u bytes, reserve=%u\n",cached,__ctru_heap_size,reserve);mp_native_log(text);flush_log();
        snprintf(text,sizeof(text),"Decoded geometry cache capacity=%u bytes\n",geometry);mp_native_log(text);
    }
    unsigned size=0;void*model=NULL;
    int gx_async=0;
#ifdef MP_BOOTMODE
    /* Engine thread records GX; a translator on core 2 renders one frame behind. */
    gx_async=mp_gx_thread_create(is_new);
#endif
#ifdef MP_RENDER_WORKER
    mp_render_worker_start(is_new,gx_async);
#endif
#ifdef MP_BOOTMODE
    if(gx_async)mp_gx_thread_go();
    if(!setjmp(failure_return)){if(!gx_async)mp_renderer_begin();mp_game_boot();if(!gx_async)mp_renderer_end();}
    else if(exit_requested)goto cleanup;
    else mp_native_log("Original engine paused at reported platform error\n");
#else
    model=read_asset("PlFxNr.dat",&size);
#endif
    volatile int loaded=0;
    if(!model){}
    else if(!setjmp(failure_return)){
        char result[80];int status=mp_game_load(model,size);
        snprintf(result,sizeof(result),"Engine model load returned %d\n",status);mp_native_log(result);
        loaded=status==0;
    }else mp_native_log("Engine stopped at a reported error\n");
    unsigned frames=0;
    while(aptMainLoop()){
        hidScanInput();if(hidKeysDown()&KEY_SELECT)break;
        mp_renderer_begin();
        if(loaded){if(!setjmp(failure_return))mp_game_draw();else loaded=0;}
        mp_renderer_end();
        if(frames==0){char text[120];unsigned v,d;mp_renderer_counts(&v,&d);snprintf(text,sizeof(text),"First engine frame: %u vertices, %u draws\n",v,d);mp_native_log(text);}
#ifdef MP_SMOKE_TEST
        if(++frames==180&&!engine_failed){
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            u16 w,h;u8*fb=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&w,&h);GSPGPU_InvalidateDataCache(fb,w*h*3);
            FILE*f=fopen("sdmc:/3ds/melee/top-screen.bgr","wb");if(f){fwrite(fb,1,w*h*3,f);fclose(f);}
            fb=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);GSPGPU_FlushDataCache(fb,w*h*2);
            f=fopen("sdmc:/3ds/melee/bottom-screen.rgb565","wb");if(f){fwrite(fb,1,w*h*2,f);fclose(f);}
            C3D_FrameEnd(0);break;
        }
#else
        ++frames;
#endif
    }
cleanup:
    mp_gx_thread_stop();
#ifdef MP_RENDER_WORKER
    mp_render_worker_stop();
#endif
    mp_native_cpu_exit();
    {extern void mp_native_audio_exit(void);mp_native_audio_exit();}
    {extern void mp_native_files_exit(void);mp_native_files_exit();}
    mp_native_log("Game application exit\n");mp_log_close();
    free(model);mp_native_bottom_exit();mp_renderer_exit();gfxExit();return 0;
}
