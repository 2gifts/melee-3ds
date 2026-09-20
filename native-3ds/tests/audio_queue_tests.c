#include <3ds.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../port/3ds/audio.c"

static ndspWaveBuf *queued_wave[8];
static s16 expected[8][384*2];
static unsigned head,count,flushes,published,consumed,source_slice;
static unsigned direct_calls,service_calls,last_flushed;
static int fail_direct,fail_service;
static int paused=1;
Result ndspInit(void){return 0;}
void ndspExit(void){}
void *linearAlloc(size_t n){return malloc(n);}
void linearFree(void *p){free(p);}
void mp_native_log(const char *s){(void)s;}
void ndspSetOutputMode(int mode){(void)mode;}
void ndspChnReset(int channel){assert(channel==0);}
void ndspChnSetInterp(int channel,int interp){assert(channel==0);(void)interp;}
void ndspChnSetRate(int channel,float rate){assert(channel==0&&rate==32000.f);}
void ndspChnSetFormat(int channel,int format){assert(channel==0);(void)format;}
void ndspChnSetPaused(int channel,int value){assert(channel==0);paused=value;}
Result DSP_FlushDataCache(const void *p,u32 n){
    assert(p&&n==1536);++service_calls;if(fail_service)return -1;
    last_flushed=(u32)(uintptr_t)p;++flushes;return 0;
}
Result svcFlushProcessDataCache(Handle process,u32 p,u32 n){
    assert(process==CUR_PROCESS_HANDLE&&p&&n==1536);++direct_calls;
    if(fail_direct)return -1;
    last_flushed=p;++flushes;return 0;
}
void ndspChnWaveBufAdd(int channel,ndspWaveBuf *w){
    assert(channel==0&&count<8&&w->nsamples==384);
    assert(flushes==published+1&&last_flushed==(u32)(uintptr_t)w->data_vaddr);
    assert(w->status==NDSP_WBUF_FREE||w->status==NDSP_WBUF_DONE);
    unsigned index=(head+count)%8;queued_wave[index]=w;
    memcpy(expected[index],w->data_vaddr,1536);w->status=NDSP_WBUF_QUEUED;
    ++count;++published;
}
void ndspChnWaveBufClear(int channel){
    assert(channel==0);while(count){queued_wave[head]->status=NDSP_WBUF_DONE;head=(head+1)%8;--count;}
}
static void immutable(void){
    for(unsigned i=0;i<count;++i){unsigned j=(head+i)%8;
        assert(!memcmp(expected[j],queued_wave[j]->data_vaddr,1536));}
}
static s16 value(unsigned slice,unsigned i){return (s16)((slice*197+i*331)^0x9713);}
static void produce(void){
    u8 be[384];s16 previous[384*2];int before_partial=partial,before_next=next;
    if(pcm)memcpy(previous,pcm+next*384*2,sizeof(previous));
    unsigned drops=audio_dropped,accepted=audio_queued;
    for(unsigned i=0;i<192;++i){u16 v=value(source_slice,i);be[2*i]=v>>8;be[2*i+1]=v;}
    mp_native_audio(be,96);
    if(audio_dropped==drops){
        assert(audio_queued==accepted+1);
        s16 *data=pcm+before_next*384*2;
        for(unsigned i=0;i<192;++i)assert(data[before_partial*192+i]==value(source_slice,i));
        if(before_partial)assert(!memcmp(data,previous,before_partial*384));
    }else{
        assert(audio_dropped==drops+1&&audio_queued==accepted);
        assert(next==before_next&&partial==before_partial);
        assert(!memcmp(previous,pcm+next*384*2,sizeof(previous)));
    }
    ++source_slice;immutable();
}
static void consume(void){
    assert(count);immutable();queued_wave[head]->status=NDSP_WBUF_DONE;
    head=(head+1)%8;--count;++consumed;
}
int main(int argc,char**argv){
    assert(argc==2);
    fail_direct=strcmp(argv[1],"direct")!=0;
    if(!strcmp(argv[1],"failure")){
        for(unsigned i=0;i<3;++i)produce();
        fail_service=1;u8 be[384]={0};mp_native_audio(be,96);++source_slice;
        assert(!published&&!count&&!flushes&&audio_flush_failures==1);
        assert(audio_dropped==4&&audio_queued==4&&partial==0&&next==0);
        assert(direct_calls==1&&service_calls==1&&audio_direct_flush_unavailable);
        fail_service=0;for(unsigned i=0;i<4;++i)produce();
        assert(published==1&&count==1&&flushes==1&&direct_calls==1&&service_calls==2);
        mp_native_audio_exit();puts("Native audio cache: failed flush withheld; service recovery passed");return 0;
    }
    /* All four input slices retain their signed stereo samples, and the
     * first 48 ms are submitted before the channel starts playing. */
    for(unsigned i=0;i<15;++i){produce();assert(paused);}
    produce();assert(count==4&&!paused&&flushes==4);
    while(count)consume();produce();assert(paused&&audio_underruns==1);
    for(unsigned i=0;i<15;++i)produce();assert(count==4&&!paused);
    /* A full queue must not overwrite any memory still owned by NDSP. */
    for(unsigned i=0;i<16;++i)produce();assert(count==8);
    unsigned dropped=audio_dropped;for(unsigned i=0;i<13;++i)produce();
    assert(audio_dropped==dropped+13);
    /* Repeated wraps, partial buffers, complete starvation and recovery. */
    unsigned random=0x41554449;
    for(unsigned i=0;i<100000;++i){
        random=random*1664525+1013904223;
        if((random>>28)<5&&count)consume();else produce();
        assert(count<=8&&partial<4);
    }
    while(count)consume();
    assert(consumed==published&&published==flushes);
    assert(!audio_flush_failures);
    if(fail_direct)assert(direct_calls==1&&service_calls==published);
    else assert(direct_calls==published&&service_calls==0);
    mp_native_audio_exit();assert(ready==-1);
    printf("Native audio queue (%s): %u buffers, %u accepted AX slices, %u full-queue drops; PCM ordering/ownership and recovery passed\n",argv[1],published,audio_queued,audio_dropped);
}
