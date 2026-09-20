#include <3ds.h>
#include <string.h>
#include <stdio.h>
extern void mp_native_log(const char*);
enum{AUDIO_BLOCKS=8,AUDIO_SLICES=4,AUDIO_SAMPLES=96*AUDIO_SLICES};
static ndspWaveBuf wave[AUDIO_BLOCKS];static s16*pcm;static int ready,next,partial,prebuffering;
static unsigned audio_queued,audio_dropped,audio_underruns;
static unsigned audio_direct_flush_unavailable;
unsigned audio_flush_failures;
#ifdef MP_SMOKE_TEST
volatile unsigned audio_service_flush;
unsigned audio_flush_calls[2],audio_flush_ticks[2];
unsigned audio_flush_last_result;
#endif

static int flush_audio_block(const void*buffer,unsigned bytes){
    /* Flush the same virtual range in this process before publishing it to
     * NDSP. This avoids the DSP service's scheduling/IPC round trip. The
     * existing CIA already permits svcFlushProcessDataCache (SVC 0x54).
     * Keep the service fallback for environments that return an error.
     * Reference: SM64 3DS audio_3ds.c and devkitPro/libctru issue 556. */
    Result result=-1;
    unsigned service=audio_direct_flush_unavailable;
#ifdef MP_SMOKE_TEST
    service|=audio_service_flush!=0;
#endif
    if(!service){
#ifdef MP_SMOKE_TEST
        u64 start=svcGetSystemTick();
#endif
        result=svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)(uintptr_t)buffer,bytes);
#ifdef MP_SMOKE_TEST
        ++audio_flush_calls[0];audio_flush_ticks[0]+=(unsigned)(svcGetSystemTick()-start);
        audio_flush_last_result=(unsigned)result;
#endif
        if(R_FAILED(result)){
            audio_direct_flush_unavailable=1;
            mp_native_log("Direct audio cache flush unavailable; using DSP service\n");
        }
    }
    if(service||R_FAILED(result)){
#ifdef MP_SMOKE_TEST
        u64 start=svcGetSystemTick();
#endif
        result=DSP_FlushDataCache(buffer,bytes);
#ifdef MP_SMOKE_TEST
        ++audio_flush_calls[1];audio_flush_ticks[1]+=(unsigned)(svcGetSystemTick()-start);
#endif
    }
    if(R_FAILED(result)){
        ++audio_flush_failures;
        mp_native_log("Audio cache flush failed; block withheld from DSP\n");
        return 0;
    }
    return 1;
}
#ifdef MP_SMOKE_TEST
static volatile unsigned audio_prefill_blocks=16;
#define AUDIO_PREFILL_BLOCKS audio_prefill_blocks
#else
#define AUDIO_PREFILL_BLOCKS 16
#endif
#ifdef MP_AUDIO_HLE_TEST
#ifndef MP_SMOKE_TEST
#error Azahar audio fixture is forbidden in physical builds
#endif
/* Azahar's HLE DSP ignores component instructions and implements NDSP's
 * shared-memory protocol. This placeholder exercises that protocol only;
 * it contains no DSP program and is never included in the hardware build. */
static const u8 hle_component[32]={0};
static ndspWaveBuf audio_capture;
static s16 audio_capture_pcm[32000*2];
static volatile unsigned audio_snapshot_request;
/* External linkage preserves these debugger-consumed outputs under -O2. */
unsigned audio_snapshot_stats[12];
s16 audio_snapshot_pcm[32000*2];
static u64 audio_last_submit,audio_window_max_gap;
static unsigned audio_stalls[3];
void mp_native_audio_stall(unsigned kind,u64 ticks){unsigned us=ticks*1000000/SYSCLOCK_ARM11;if(kind<3&&us>audio_stalls[kind])audio_stalls[kind]=us;}
static void audio_test_snapshot(void*unused){
    (void)unused;if(!audio_snapshot_request)return;
    static unsigned initial_underruns;
    if(audio_snapshot_request==201){initial_underruns=audio_underruns;audio_window_max_gap=0;memset(audio_stalls,0,sizeof(audio_stalls));}
    if(audio_snapshot_request>1){--audio_snapshot_request;return;}
    /* The capture producer's own thread freezes a chronological ring after
     * updating it. Slow debugger reads can then inspect immutable samples. */
    unsigned offset=audio_capture.offset;
    memcpy(audio_snapshot_pcm,audio_capture_pcm+offset*2,(32000-offset)*4);
    memcpy(audio_snapshot_pcm+(32000-offset)*2,audio_capture_pcm,offset*4);
    extern unsigned mp_native_frame_number(void);
    audio_snapshot_stats[0]=mp_native_frame_number();audio_snapshot_stats[1]=audio_queued;
    audio_snapshot_stats[2]=audio_dropped;audio_snapshot_stats[3]=audio_underruns;audio_snapshot_stats[4]=prebuffering;
    audio_snapshot_stats[5]=audio_underruns-initial_underruns;audio_snapshot_stats[6]=AUDIO_PREFILL_BLOCKS;
    audio_snapshot_stats[7]=audio_window_max_gap*1000000/SYSCLOCK_ARM11;audio_snapshot_stats[8]=AUDIO_SAMPLES;
    memcpy(audio_snapshot_stats+9,audio_stalls,sizeof(audio_stalls));
    __sync_synchronize();audio_snapshot_request=0;
}
#endif
void mp_native_audio(const u8*be,unsigned samples){
    if(!ready){ready=-1;
#ifdef MP_RENDER_WORKER
        /* libctru's linear allocator is not synchronized. Audio allocates
         * once; wait for renderer allocation work before its initialization. */
        extern void mp_render_worker_barrier(void);mp_render_worker_barrier();
#endif
#ifdef MP_AUDIO_HLE_TEST
        ndspUseComponent(hle_component,sizeof(hle_component),0xff,0xff);mp_native_log("Azahar HLE audio fixture enabled; no physical DSP program\n");
#endif
        Result result=ndspInit();if(R_FAILED(result)){char text[90];snprintf(text,sizeof(text),"NDSP unavailable (%08lx): audio output disabled\n",(unsigned long)result);mp_native_log(text);return;}
        pcm=linearAlloc(AUDIO_BLOCKS*AUDIO_SAMPLES*4);if(!pcm){ndspExit();return;}ndspSetOutputMode(NDSP_OUTPUT_STEREO);ndspChnReset(0);ndspChnSetInterp(0,NDSP_INTERP_LINEAR);ndspChnSetRate(0,32000.f);ndspChnSetFormat(0,NDSP_FORMAT_STEREO_PCM16);for(int i=0;i<AUDIO_BLOCKS;++i){wave[i].data_vaddr=pcm+i*AUDIO_SAMPLES*2;wave[i].nsamples=AUDIO_SAMPLES;}ready=1;
        /* Accumulate 48 ms before playback, preserving the original mix
         * timeline while absorbing cooperative rendering/scheduling jitter. */
        prebuffering=1;ndspChnSetPaused(0,true);
#ifdef MP_AUDIO_HLE_TEST
        audio_capture.data_pcm16=audio_capture_pcm;audio_capture.nsamples=32000;ndspSetCapture(&audio_capture);
        ndspSetCallback(audio_test_snapshot,NULL);
#endif
        mp_native_log("NDSP stereo PCM output initialized at 32000 Hz\n");
    }
    if(ready!=1||samples!=96)return;
#ifdef MP_AUDIO_HLE_TEST
    u64 now=svcGetSystemTick();if(audio_last_submit&&now-audio_last_submit>audio_window_max_gap)audio_window_max_gap=now-audio_last_submit;audio_last_submit=now;
#endif
    unsigned queued=0;for(unsigned i=0;i<AUDIO_BLOCKS;++i)if(wave[i].status!=NDSP_WBUF_FREE&&wave[i].status!=NDSP_WBUF_DONE)++queued;
    if(!prebuffering&&!queued){++audio_underruns;prebuffering=1;ndspChnSetPaused(0,true);}
    ndspWaveBuf*w=&wave[next];if(w->status!=NDSP_WBUF_FREE&&w->status!=NDSP_WBUF_DONE){++audio_dropped;return;}
    /* NDSP exposes one playing buffer and four queued buffers to the DSP.
     * Combining four AX slices gives that hardware queue up to 60 ms of
     * coverage instead of 15 ms, and reduces cache/queue IPC frequency. */
    s16*out=pcm+next*AUDIO_SAMPLES*2+partial*192;for(unsigned i=0;i<192;++i)out[i]=(be[i*2]<<8)|be[i*2+1];++audio_queued;
    if(++partial<AUDIO_SLICES)return;
    partial=0;
    if(!flush_audio_block(pcm+next*AUDIO_SAMPLES*2,AUDIO_SAMPLES*4)){audio_dropped+=AUDIO_SLICES;return;}
    ndspChnWaveBufAdd(0,w);next=(next+1)%AUDIO_BLOCKS;
    if(prebuffering&&(queued+1)*AUDIO_SLICES>=AUDIO_PREFILL_BLOCKS){prebuffering=0;ndspChnSetPaused(0,false);}
}
void mp_native_audio_exit(void){if(ready==1){ndspChnWaveBufClear(0);ndspExit();linearFree(pcm);}ready=-1;}
