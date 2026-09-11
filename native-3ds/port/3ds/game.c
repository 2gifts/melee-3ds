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
#endif
unsigned mp_native_frame_number(void){return engine_frames;}
void mp_native_log(const char*);
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
void mp_native_frame(void)
{
    extern unsigned mp_native_ticks(void);
    static unsigned previous_tick,samples;static u64 frame_ticks;
    mp_renderer_end();++engine_frames;mp_log_frame(engine_frames);
    unsigned now=mp_native_ticks();if(previous_tick){frame_ticks+=(u32)(now-previous_tick);++samples;}
    if((engine_frames%60)==0){extern unsigned mp_native_idle_count;char text[180];unsigned v,d;mp_renderer_counts(&v,&d);snprintf(text,sizeof(text),"Original engine frame %u: %u vertices, %u draws; frame %.2f ms (excluding capture), %u idle calls\n",engine_frames,v,d,samples?(double)frame_ticks/samples/40500:0,mp_native_idle_count);mp_native_log(text);frame_ticks=0;samples=0;mp_native_idle_count=0;
    }
    if(engine_frames%300==0){extern unsigned mp_file_cache_hits,mp_file_cache_read_bytes,mp_file_sd_reads,mp_file_sd_bytes;char text[224];snprintf(text,sizeof(text),"Log batches=%u flush time=%.2f ms dropped=%u errors=%u; menu cache hits=%u bytes=%u; SD reads=%u bytes=%u\n",__atomic_load_n(&log_flushes,__ATOMIC_RELAXED),__atomic_load_n(&log_flush_ticks,__ATOMIC_RELAXED)/40500.0,log_dropped,__atomic_load_n(&log_errors,__ATOMIC_RELAXED),mp_file_cache_hits,mp_file_cache_read_bytes,mp_file_sd_reads,mp_file_sd_bytes);mp_native_log(text);flush_log();}
    if(engine_frames%300==0){extern unsigned __ctru_heap_size;struct mallinfo heap=mallinfo();char text[128];
        mp_native_heap_used=heap.uordblks;mp_native_heap_available=__ctru_heap_size>mp_native_heap_used?__ctru_heap_size-mp_native_heap_used:0;
        snprintf(text,sizeof(text),"Ordinary heap used=%u available total=%u bytes\n",mp_native_heap_used,mp_native_heap_available);mp_native_log(text);}
#ifdef MP_SMOKE_TEST
    if(mp_test_stall){mp_test_stall=0;u64 until=svcGetSystemTick()+12ULL*SYSCLOCK_ARM11;while(svcGetSystemTick()<until){}}
    if(mp_test_memory_compare==1){extern unsigned mp_game_memory_check(void);mp_test_memory_compare=mp_game_memory_check();if(!mp_test_memory_compare)mp_native_panic("ARM memory comparison self-test failed");}
    if(mp_test_rotation_check==1){extern unsigned mp_game_rotation_check(void);mp_test_rotation_check=mp_game_rotation_check();}
    if(mp_test_ppc_math_check==1){extern unsigned mp_game_ppc_math_check(void);mp_test_ppc_math_check=mp_game_ppc_math_check();}
    if(mp_test_capture){capture_frame();mp_test_capture=0;}
    if(mp_test_async_files==1){extern int mp_native_async_self_test(void);if(mp_native_async_self_test())mp_test_async_files=2;}
#endif
    hidScanInput();
    unsigned held=hidKeysHeld();mp_native_display_keys(held);
    if(!aptMainLoop()||((hidKeysDown()&KEY_SELECT)&&!(held&(KEY_ZL|KEY_ZR)))){exit_requested=1;longjmp(failure_return,1);}
    mp_native_cpu_poll();
    if(display_toggle_pending){mp_native_expanded^=1;display_toggle_pending=0;}
    mp_game_set_display(mp_native_expanded);
    float slider=osGet3DSliderState();
    mp_native_stereo_depth=slider>0?(unsigned)(slider*1000.f):0;
    if(mp_native_stereo_depth>1000)mp_native_stereo_depth=1000;
#ifdef MP_SMOKE_TEST
    if(mp_test_stereo_slider<=1000)mp_native_stereo_depth=mp_test_stereo_slider;
#endif
    /* Flat menus use one render view even when the slider is raised. */
    if(!mp_game_stereo_scene())mp_native_stereo_depth=0;
    mp_game_set_stereo(mp_native_stereo_depth);
    static unsigned rate_tick,rate_frame,rate_sim,rate_display=~0u,bottom_fps;
    if(!rate_tick||now-rate_tick>=40500000||rate_display!=mp_native_expanded){
        unsigned sim=mp_game_simulation(),elapsed=now-rate_tick;
        float fps=elapsed?(engine_frames-rate_frame)*40500000.0/elapsed:0;
        float updates=elapsed&&sim>=rate_sim?(sim-rate_sim)*40500000.0/elapsed:0;
        bottom_fps=(unsigned)(fps+.5f);
        if(rate_tick){char text[128];snprintf(text,sizeof(text),"Rates: %.1f render FPS, %.1f game Hz; display=%s stereo=%u\n",fps,updates,mp_native_expanded?"expanded":"4:3",mp_native_stereo_depth);mp_native_log(text);}
        rate_tick=now;rate_frame=engine_frames;rate_sim=sim;rate_display=mp_native_expanded;
    }
    mp_native_bottom_frame(bottom_fps,mp_native_expanded,bottom_touch_pending,bottom_touch_x,bottom_touch_y);
    bottom_touch_pending=0;
#ifdef MP_SMOKE_TEST
    if(mp_test_frame_limit&&engine_frames>=mp_test_frame_limit)mp_native_panic("End of automatic engine run");
#endif
    previous_tick=mp_native_ticks();mp_renderer_begin();
}
void *mp_native_alloc(unsigned size){return memalign(32,size);}
void mp_native_free(void*p){free(p);}
static void flush_log(void){mp_log_flush(0);}
static void native_log(const char*s){mp_log_append(s);
    if(strstr(s,"Preload complete"))flush_log();
}
void mp_native_log(const char*s){MP_AUDIO_TRACE(0,native_log(s));}
void mp_native_panic(const char*s){mp_native_log(s);mp_renderer_end();mp_log_flush(1);engine_failed=1;
#ifdef MP_SMOKE_TEST
    capture_frame();
#else
    consoleInit(GFX_BOTTOM,NULL);consoleClear();printf("Melee stopped\n\n%s\n\nSELECT: return to Homebrew Launcher\nDetails: /3ds/melee/game.log\n",s);
#endif
    longjmp(failure_return,1);}
static void *read_asset(const char *name,unsigned *size)
{
    char path[256];snprintf(path,sizeof(path),"sdmc:/3ds/melee/files/%s",name);
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
    mp_native_log("Melee ARM BE8 engine startup - Pipelined stereo performance update 15\n");
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
    if(!mp_renderer_init()){mp_native_log("GPU initialization failed\n");mp_log_close();gfxExit();return 1;}
    mp_native_bottom_init();
    mp_native_cpu_init(is_new);
    extern void mp_native_files_init(void);mp_native_files_init();
    if(is_new){extern unsigned __ctru_heap_size;extern unsigned mp_native_cache_menus(unsigned);
        unsigned geometry=__ctru_heap_size>=80*1024*1024?12*1024*1024:6*1024*1024;
        mp_game_configure_cache(geometry);
        /* Retain the existing non-cache reserve when assigning more heap to
         * decoded geometry. The known title/menu sources still fit fully. */
        unsigned reserve=50*1024*1024+geometry,budget=__ctru_heap_size>reserve?__ctru_heap_size-reserve:0;
        char text[160];
        unsigned cached=mp_native_cache_menus(budget);
        snprintf(text,sizeof(text),"Menu sources cached=%u bytes; ordinary heap=%u bytes, reserve=%u\n",cached,__ctru_heap_size,reserve);mp_native_log(text);flush_log();
        snprintf(text,sizeof(text),"Decoded geometry cache capacity=%u bytes\n",geometry);mp_native_log(text);
    }
    unsigned size=0;void*model=NULL;
#ifdef MP_BOOTMODE
    if(!setjmp(failure_return)){mp_renderer_begin();mp_game_boot();mp_renderer_end();}
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
    mp_native_cpu_exit();
    {extern void mp_native_audio_exit(void);mp_native_audio_exit();}
    {extern void mp_native_files_exit(void);mp_native_files_exit();}
    mp_native_log("Game application exit\n");mp_log_close();
    free(model);mp_native_bottom_exit();mp_renderer_exit();gfxExit();return 0;
}
