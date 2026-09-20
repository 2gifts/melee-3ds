"""Compare archived experimental block decoder with the preceding scalar implementation."""
import argparse,json,subprocess
from pathlib import Path
from build import ROOT,local_clang
out=ROOT/'build/update22-qa/audio';out.mkdir(parents=True,exist_ok=True)
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True,help='Archived rejected decoder source');args=ap.parse_args()
s=args.source.read_text()
header=(ROOT/'upstream/melee/extern/dolphin/include/dolphin/ax.h').read_text()
types=header[header.index('typedef struct _AXPBMIX'):header.index('typedef struct _AXVPB')]
fast=s[s.index('typedef struct {AXPB*p;const u8*aram;'):s.index('#ifdef MP_RENDER_REWORK_TEST\nvolatile unsigned audio_decoder_disable')]
reference=s[s.index('static s16 sample(void*context)'):s.index('#else\n#define audio_decoder_disable')]
pre='''#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "audio_math.h"
typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef uint32_t u32;typedef int32_t s32;
static u8 memory[16*1024*1024];static int available=1;
static const u8*mp_aram_data(u32 a,unsigned n){return available&&a<=sizeof(memory)-n?memory+a:NULL;}
#define mp_platform_panic(text) abort()
'''
test=r'''
static u32 seed=17;static u32 random32(void){seed=seed*1664525+1013904223;return seed;}
static void address(u16*hi,u16*lo,u32 x){*hi=x>>16;*lo=x;}
static void mix_reference(s32*accum,const s16*input,AXPB*p){
    for(int j=0;j<96;++j){int volume=p->ve.currentVolume;int v=(input[j]*volume)>>15;accum[j*2]+=(v*p->mix.vL)>>15;accum[j*2+1]+=(v*p->mix.vR)>>15;int next=volume+p->ve.currentDelta;p->ve.currentVolume=next<0?0:next>32767?32767:next;}
}
int main(void){
    for(unsigned i=0;i<sizeof(memory);++i)memory[i]=random32()>>24;
    unsigned formats[]={0,0xA,0x19},rates[]={0,1,32768,65535,65536,65537,131072,262144,524288};
    for(unsigned test=0;test<100000;++test){
        AXPB a;u8*bytes=(u8*)&a;for(unsigned i=0;i<sizeof(a);++i)bytes[i]=random32()>>24;
        a.state=test%13!=0;a.addr.format=formats[test%3];a.addr.loopFlag=test%2;
        u32 current=8+(random32()%8192),end=current+random32()%128,loop=current-4;
        if(test%11==0){current=0xffffffffu-(test%40);end=0xffffffffu;}
        if(test%17==0){current=sizeof(memory)-(test%40);end=current+4;}
        if(test%23==0){current=0;end=test%4;loop=0;}
        address(&a.addr.currentAddressHi,&a.addr.currentAddressLo,current);address(&a.addr.endAddressHi,&a.addr.endAddressLo,end);address(&a.addr.loopAddressHi,&a.addr.loopAddressLo,loop);
        available=test%31!=0;AXPB b=a;u32 phaseA=random32()%65536,phaseB=phaseA,ratio=rates[test%9];s16 hA[4],hB[4],oA[96],oB[96];
        for(unsigned j=0;j<4;++j)hA[j]=hB[j]=random32();
        for(unsigned block=0;block<3;++block){
            MPVoiceDecoder decoder;decoder_init(&decoder,&b,available?memory:NULL);
            mp_src_block(oA,96,ratio,&phaseA,hA,test%5==0,sample,&a);
            mp_src_block(oB,96,ratio,&phaseB,hB,test%5==0,sample_fast,&decoder);decoder_commit(&decoder);
            assert(!memcmp(oA,oB,sizeof(oA))&&!memcmp(&a,&b,sizeof(a))&&phaseA==phaseB&&!memcmp(hA,hB,sizeof(hA)));
            s32 accA[192],accB[192];for(unsigned j=0;j<192;++j)accA[j]=accB[j]=(s32)random32();
            mix_reference(accA,oA,&a);mix_voice(accB,oB,&b);assert(!memcmp(accA,accB,sizeof(accA))&&!memcmp(&a,&b,sizeof(a)));
        }
    }
    printf("300000 AX blocks: decoded PCM, complete AXPB state, interpolation phase/history and stereo accumulation are bit-identical\n");
}
'''
source=out/'test.c';source.write_text(pre+types+fast+reference+test)
exe=out/'audio-decoder-tests.exe'
subprocess.run([local_clang(),'-O2','-fwrapv','-Wall','-Wextra','-I'+str(ROOT/'port/engine'),str(source),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,text=True,capture_output=True);print(result.stdout)
(out/'result.json').write_text(json.dumps({'blocks':300000,'samples':28800000,'passed':True,'source':str(args.source),'shipped':False,'result':result.stdout},indent=2)+'\n')
