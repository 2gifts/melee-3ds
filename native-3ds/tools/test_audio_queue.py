"""Compile the production native audio queue against an ownership-checking DSP."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/audio-queue-test';out.mkdir(parents=True,exist_ok=True)
(out/'3ds.h').write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef int Result;
typedef struct { void *data_vaddr; unsigned nsamples; volatile unsigned status; } ndspWaveBuf;
enum {NDSP_WBUF_FREE,NDSP_WBUF_QUEUED,NDSP_WBUF_PLAYING,NDSP_WBUF_DONE};
enum {NDSP_OUTPUT_STEREO,NDSP_INTERP_LINEAR,NDSP_FORMAT_STEREO_PCM16};
#define R_FAILED(r) ((r)<0)
Result ndspInit(void);void ndspExit(void);void *linearAlloc(size_t);void linearFree(void*);
void ndspSetOutputMode(int);void ndspChnReset(int);void ndspChnSetInterp(int,int);
void ndspChnSetRate(int,float);void ndspChnSetFormat(int,int);void ndspChnSetPaused(int,int);
void DSP_FlushDataCache(const void*,size_t);void ndspChnWaveBufAdd(int,ndspWaveBuf*);
void ndspChnWaveBufClear(int);
''')
cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
exe=out/'audio-queue-tests.exe'
subprocess.run([str(cc),'-O2','-Wall','-Wextra','-I'+str(out),str(ROOT/'tests/audio_queue_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
