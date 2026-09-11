#include <dolphin/ax.h>
#include <dolphin/axfx.h>
#include <dolphin/ai.h>
#include <string.h>
#include "native.h"
#include "audio_math.h"
/* CPU AX voice mixer. Game-side addresses and ADPCM state retain their
 * GameCube layout. Output crosses to the 3DS NDSP service as stereo PCM. */
extern const u8 *mp_aram_data(u32,unsigned);
extern void mp_platform_audio(const s16*,unsigned);
static AXVPB voices[64];static u8 used[64];static u32 frac[64];
static void(*frame_callback)(void);static void(*aux_callback[2])(void*,void*);static void*aux_context[2];
u32 mp_profile_audio;
static u32 last_tick;static int initialized;static u8 stream_left,stream_right;static u32 sample_rate;
void AIInit(u8*stack){(void)stack;}void AISetDSPSampleRate(u32 rate){sample_rate=rate;}
void AISetStreamVolLeft(u8 v){stream_left=v;}void AISetStreamVolRight(u8 v){stream_right=v;}
void AXInit(void){memset(voices,0,sizeof(voices));memset(used,0,sizeof(used));last_tick=mp_platform_ticks();initialized=1;}
void AXRegisterCallback(void(*cb)()){frame_callback=cb;}
void AXRegisterAuxACallback(void(*cb)(void*,void*),void*ctx){aux_callback[0]=cb;aux_context[0]=ctx;}
void AXRegisterAuxBCallback(void(*cb)(void*,void*),void*ctx){aux_callback[1]=cb;aux_context[1]=ctx;}
void AXFreeVoice(AXVPB*p){if(p>=voices&&p<voices+64){used[p-voices]=0;p->pb.state=0;p->priority=0;}}
AXVPB*AXAcquireVoice(u32 priority,void(*cb)(void*),u32 ctx){int slot=-1;for(int i=0;i<64;++i)if(!used[i]){slot=i;break;}if(slot<0){for(int i=0;i<64;++i)if(voices[i].priority<(int)priority&&(slot<0||voices[i].priority<voices[slot].priority))slot=i;if(slot>=0&&voices[slot].callback)voices[slot].callback(&voices[slot]);}if(slot<0)return NULL;AXVPB*p=&voices[slot];memset(p,0,sizeof(*p));p->index=slot;p->priority=priority;p->callback=cb;p->userContext=ctx;p->updateWrite=p->updateData;used[slot]=1;frac[slot]=0;return p;}
void AXSetVoicePriority(AXVPB*p,u32 n){p->priority=n;}
void AXSetVoiceMix(AXVPB*p,AXPBMIX*x){p->pb.mix=*x;}
void AXSetVoiceItdOn(AXVPB*p){p->pb.itd.flag=1;}
void AXSetVoiceItdTarget(AXVPB*p,u16 l,u16 r){p->pb.itd.targetShiftL=l;p->pb.itd.targetShiftR=r;}
void AXSetVoiceVe(AXVPB*p,AXPBVE*x){p->pb.ve=*x;}void AXSetVoiceVeDelta(AXVPB*p,s16 x){p->pb.ve.currentDelta=x;}
void AXSetVoiceAddr(AXVPB*p,AXPBADDR*x){p->pb.addr=*x;}
void AXSetVoiceLoop(AXVPB*p,u16 x){p->pb.addr.loopFlag=x;}
void AXSetVoiceLoopAddr(AXVPB*p,u32 x){p->pb.addr.loopAddressHi=x>>16;p->pb.addr.loopAddressLo=x;}
void AXSetVoiceEndAddr(AXVPB*p,u32 x){p->pb.addr.endAddressHi=x>>16;p->pb.addr.endAddressLo=x;}
void AXSetVoiceCurrentAddr(AXVPB*p,u32 x){p->pb.addr.currentAddressHi=x>>16;p->pb.addr.currentAddressLo=x;}
void AXSetVoiceAdpcm(AXVPB*p,AXPBADPCM*x){p->pb.adpcm=*x;}
void AXSetVoiceSrc(AXVPB*p,AXPBSRC*x){p->pb.src=*x;frac[p-voices]=x->currentAddressFrac;}
void AXSetVoiceSrcRatio(AXVPB*p,float x){u32 ratio=x*65536.f;p->pb.src.ratioHi=ratio>>16;p->pb.src.ratioLo=ratio;}
void AXSetVoiceAdpcmLoop(AXVPB*p,AXPBADPCMLOOP*x){p->pb.adpcmLoop=*x;}
void AXSetVoiceState(AXVPB*p,u16 x){p->pb.state=x;}
static s16 clamp(int x){return x>32767?32767:x< -32768?-32768:x;}
static s16 sample(void*context){
    AXPB*p=context;if(!p->state)return 0;
    u32 a=((u32)p->addr.currentAddressHi<<16)|p->addr.currentAddressLo;
    u32 end=((u32)p->addr.endAddressHi<<16)|p->addr.endAddressLo;
    if(a>end){if(!p->addr.loopFlag){p->state=0;return 0;}a=((u32)p->addr.loopAddressHi<<16)|p->addr.loopAddressLo;p->adpcm.pred_scale=p->adpcmLoop.loop_pred_scale;p->adpcm.yn1=p->adpcmLoop.loop_yn1;p->adpcm.yn2=p->adpcmLoop.loop_yn2;}
    int value=0;
    if(p->addr.format==0){if((a&15)<2){const u8*b=mp_aram_data(a/2,1);if(!b){p->state=0;return 0;}p->adpcm.pred_scale=*b;a=(a&~15u)+2;}const u8*b=mp_aram_data(a/2,1);if(!b){p->state=0;return 0;}int n=(a&1)?(*b&15):(*b>>4);if(n>=8)n-=16;unsigned pred=(p->adpcm.pred_scale>>4)&7,shift=p->adpcm.pred_scale&15;
        value=mp_adpcm_sample(n,shift,p->adpcm.a[pred][0],p->adpcm.a[pred][1],p->adpcm.yn1,p->adpcm.yn2);p->adpcm.yn2=p->adpcm.yn1;p->adpcm.yn1=value;
    }else if(p->addr.format==0xA){const u8*b=mp_aram_data(a*2,2);if(!b){p->state=0;return 0;}value=(s16)((b[0]<<8)|b[1]);}
    else if(p->addr.format==0x19){const u8*b=mp_aram_data(a,1);if(!b){p->state=0;return 0;}value=(s8)*b*256;}
    else mp_platform_panic("Unsupported AX sample format");
    ++a;p->addr.currentAddressHi=a>>16;p->addr.currentAddressLo=a;return value;
}
static void mix_block(void){s32 accum[192]={0};s16 output[192];if(frame_callback)frame_callback();
    for(int i=0;i<64;++i){AXPB*p=&voices[i].pb;if(!used[i]||!p->state)continue;u32 ratio=((u32)p->src.ratioHi<<16)|p->src.ratioLo;s16 input[96],history[4];
        for(unsigned j=0;j<4;++j)history[j]=p->src.last_samples[j];
        mp_src_block(input,96,ratio,&frac[i],history,p->srcSelect==2,sample,p);
        for(int j=0;j<96;++j){int volume=p->ve.currentVolume;int v=(input[j]*volume)>>15;accum[j*2]+=(v*p->mix.vL)>>15;accum[j*2+1]+=(v*p->mix.vR)>>15;int next=volume+p->ve.currentDelta;p->ve.currentVolume=next<0?0:next>32767?32767:next;}
        p->src.currentAddressFrac=frac[i];for(unsigned j=0;j<4;++j)p->src.last_samples[j]=history[j];}
    for(int i=0;i<192;++i)output[i]=clamp(accum[i]);mp_platform_audio(output,96);
}
void mp_audio_poll_at(u32 now){if(!initialized)return;unsigned blocks=(now-last_tick)/121500;if(!blocks)return;u32 profile_start=mp_platform_ticks();if(blocks>12){last_tick=now-12*121500;blocks=12;}while(blocks--){last_tick+=121500;mix_block();}mp_profile_audio+=mp_platform_ticks()-profile_start;}
/* Auxiliary effects are optional. Return failure so the caller can disable
 * them until the reverb/chorus processors are implemented. Dry mixing works. */
void*(*__AXFXAlloc)(unsigned long);void(*__AXFXFree)(void*);
void AXFXSetHooks(void*(*a)(unsigned long),void(*f)(void*)){__AXFXAlloc=a;__AXFXFree=f;}
#define FX(name,type) int AXFX##name##Init(struct type*x){return 0;} int AXFX##name##Shutdown(struct type*x){return 1;} void AXFX##name##Callback(struct AXFX_BUFFERUPDATE*b,struct type*x){}
FX(ReverbHi,AXFX_REVERBHI) FX(ReverbStd,AXFX_REVERBSTD) FX(Delay,AXFX_DELAY) FX(Chorus,AXFX_CHORUS)
