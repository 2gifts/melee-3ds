/* Deterministic device-boundary interleavings around the generated SDK code.
 * Allocation/drawing helpers are omitted by the host driver; the actual
 * frame/publication/callback/wait/safe-transfer functions are included. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#undef assert
#define assert(test) do { if(!(test)){fprintf(stderr,"Queue assertion at line %d: %s\n",__LINE__,#test);exit(86);} } while(0)
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int64_t s64;typedef uint64_t u64;
typedef unsigned TickCounter;typedef int LightLock;
#define C3D_UNUSED __attribute__((unused))
enum {GFX_TOP,GFX_BOTTOM,GFX_LEFT=0,GFX_RIGHT=1};
enum {C3D_FRAME_SYNCDRAW=1,C3D_FRAME_NONBLOCK=2,GX_CMDLIST_FLUSH=2};
enum {GSPGPU_EVENT_VBlank0,GSPGPU_EVENT_VBlank1,USERBREAK_PANIC};
typedef struct Queue {unsigned numEntries,lastEntry,maxEntries;void(*callback)(struct Queue*);} gxCmdQueue_s;
typedef struct {gxCmdQueue_s gxQueue;u32 *cmdBuf;unsigned cmdBufSize;} C3D_Context;
typedef struct {unsigned width,height;} C3D_FrameBuf;
typedef struct Target {struct Target*next;bool used;C3D_FrameBuf frameBuf;unsigned screen,side,transferFlags;} C3D_RenderTarget;
static C3D_Context context;static C3D_Context*C3Di_GetContext(void){return &context;}
static unsigned commands[32],copied[3],swaps[2],frameId,expectedOutputs;
static unsigned completionMask,appendStep,delayedCallbacks,callbackDelay,held,queueActive,pendingDraw,swapCalls;
static u64 svcGetSystemTick(void){static u64 ticks;return ++ticks;}
static void deliver(void);
static void LightLock_Lock(LightLock*l){(void)l;assert(!held);held=1;}
static void LightLock_Unlock(LightLock*l){(void)l;assert(held);held=0;}
static void svcBreak(int kind){(void)kind;assert(!"Unexpected SDK panic");abort();}
static void osTickCounterStart(TickCounter*t){*t=1;}
static void osTickCounterUpdate(TickCounter*t){++*t;}
static float osTickCounterRead(TickCounter*t){return (float)*t;}
static void gxCmdQueueStop(gxCmdQueue_s*q){assert(q->lastEntry==q->numEntries);queueActive=0;}
static void gxCmdQueueClear(gxCmdQueue_s*q){assert(q->lastEntry==q->numEntries);q->lastEntry=q->numEntries=0;}
static void finishDevice(void){
 gxCmdQueue_s*q=&context.gxQueue;
 bool completed=q->lastEntry<q->numEntries;
 while(q->lastEntry<q->numEntries){unsigned kind=commands[q->lastEntry++];if(kind)copied[kind-1]=frameId;}
 if(completed && q->callback){++delayedCallbacks;if(!callbackDelay)deliver();}
}
static bool gxCmdQueueWait(gxCmdQueue_s*q,s64 timeout){
 if(q->lastEntry<q->numEntries){if(timeout==0)return false;finishDevice();}
 return true;
}
static void gxCmdQueueRun(gxCmdQueue_s*q){(void)q;queueActive=1;if(completionMask&(1u<<(appendStep%16)))finishDevice();}
static void gxCmdQueueSetCallback(gxCmdQueue_s*q,void(*cb)(gxCmdQueue_s*),void*user){(void)user;q->callback=cb;}
static void GX_BindQueue(gxCmdQueue_s*q){(void)q;}
static void gspSetEventCallback(int e,void(*cb)(void*),void*u,bool b){(void)e;(void)cb;(void)u;(void)b;}
static void gspWaitForAnyEvent(void){finishDevice();}
static void gspWaitForPPF(void){finishDevice();}
static void gspWaitForPSC0(void){finishDevice();}
static void append(unsigned kind){
 gxCmdQueue_s*q=&context.gxQueue;assert(q->numEntries<32);commands[q->numEntries++]=kind;++appendStep;
 /* A callback from an earlier drain can arrive during a later append. */
 if(delayedCallbacks && (completionMask&0x8000))deliver();
 if(queueActive && (completionMask&(1u<<(appendStep%16))))finishDevice();
}
static void GX_ProcessCommandList(u32*buf,u32 size,u8 flags){(void)buf;(void)size;(void)flags;append(0);}
static void GX_DisplayTransfer(u32*a,u32 b,u32*c,u32 d,u32 e){(void)a;(void)b;(void)c;(void)d;(void)e;append(0);}
static void GX_TextureCopy(u32*a,u32 b,u32*c,u32 d,u32 e,u32 f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;append(0);}
static void GX_MemoryFill(u32*a,u32 b,u32*c,u16 d,u32*e,u32 f,u32*g,u16 h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;append(0);}
static void C3D_FrameBufTransfer(C3D_FrameBuf*fb,unsigned screen,unsigned side,unsigned flags){(void)fb;(void)flags;append(screen==GFX_BOTTOM?3:side==GFX_RIGHT?2:1);}
static void gfxScreenSwapBuffers(unsigned screen,bool stereo);
static void GPUCMD_SetBuffer(u32*buf,unsigned size,unsigned offset){(void)offset;(void)size;if(buf){assert(context.gxQueue.numEntries==0);pendingDraw=1;}}
static bool C3Di_SplitFrame(u32**buf,u32*size){if(!pendingDraw)return false;pendingDraw=0;*buf=context.cmdBuf;*size=8;return true;}
static void C3D_SetFrameBuf(C3D_FrameBuf*f){(void)f;}
static void C3D_SetViewport(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;}
static void GSPGPU_FlushDataCache(void*p,unsigned n){(void)p;(void)n;}
u32 __ctru_linear_heap,__ctru_linear_heap_size;
static void C3Di_RenderTargetDestroy(C3D_RenderTarget*t){(void)t;}
#include "queue-under-test.inc"
#define MP_ASYNC_PRESENTATION
#include "early-queue-under-test.inc"
static void deliver(void){
 assert(!held);while(delayedCallbacks){--delayedCallbacks;if(context.gxQueue.callback)context.gxQueue.callback(&context.gxQueue);}
}
static void gfxScreenSwapBuffers(unsigned screen,bool stereo){
#ifndef MP_QUEUE_REFERENCE_TEST
 assert(held);
#endif
 assert(!publishing);
 assert(context.gxQueue.lastEntry==context.gxQueue.numEntries);
 if(screen==GFX_TOP){assert(expectedOutputs&1);assert(copied[0]==frameId);assert(stereo==!!(expectedOutputs&2));if(stereo)assert(copied[1]==frameId);}
 else{assert(expectedOutputs&4);assert(!stereo);assert(copied[2]==frameId);}
 ++swaps[screen];++swapCalls;
 assert(swaps[screen]==1);
}
static void drawHook(void*data){assert(data==&context);pendingDraw=1;}
int main(void){
 u32 buffer[128];context.cmdBuf=buffer;context.cmdBufSize=128;context.gxQueue.maxEntries=32;
 C3D_RenderTarget targets[3]={{.screen=GFX_TOP,.side=GFX_LEFT},{.screen=GFX_TOP,.side=GFX_RIGHT},{.screen=GFX_BOTTOM}};
 unsigned scenarios=0;
 for(unsigned delay=0;delay<2;++delay)for(unsigned mask=0;mask<256;++mask){
  callbackDelay=delay;completionMask=(mask&127)|((mask&128)?0x8000:0);appendStep=0;
  C3Di_RenderQueueInit();C3D_FrameEndHook(drawHook,&context);
  for(unsigned outputCase=0;outputCase<4;++outputCase){
   ++frameId;expectedOutputs=(unsigned[]){7,1,4,0}[outputCase];memset(swaps,0,sizeof(swaps));memset(copied,0,sizeof(copied));
   assert(C3D_FrameBegin(0));mp_early_queue_retire();
   /* Stale callback delivery after storage has moved to a new frame. */
   deliver();assert(!swaps[0]&&!swaps[1]);
   for(unsigned i=0;i<3;++i){linkedTarget[i]=targets+i;targets[i].used=!!(expectedOutputs&(1u<<i));}
   mp_early_queue_append();pendingDraw=1;mp_early_queue_append();
#ifndef MP_QUEUE_REFERENCE_TEST
   /* Repeated intermediate copies must not consume presentation's reserve. */
   for(unsigned j=0;j<70;++j){pendingDraw=1;C3D_FrameSplit(GX_CMDLIST_FLUSH);if(j%3==0)C3D_SyncTextureCopy(buffer,0,buffer,0,32,0);}
   /* Force a final capacity fence after the game has deferred its prefix. */
   C3Di_RenderQueueWaitDone();mp_early_queue_capacity_completed(1);
   pendingDraw=1;mp_early_queue_append();
   while(context.gxQueue.numEntries<28){pendingDraw=0;C3D_SyncTextureCopy(buffer,0,buffer,0,32,0);}
#endif
   mp_early_queue_defer();
   C3D_FrameEnd(GX_CMDLIST_FLUSH);
   if(context.gxQueue.lastEntry<context.gxQueue.numEntries)assert(!C3D_FrameBegin(C3D_FRAME_NONBLOCK));
   C3Di_RenderQueueWaitDone();
   mp_early_queue_retire();
   assert(swaps[0]==!!(expectedOutputs&1));assert(swaps[1]==!!(expectedOutputs&4));
   unsigned before=swapCalls;deliver();assert(swapCalls==before);
   /* Outside-frame safe transfer paths also publish before completion. */
   C3D_SyncTextureCopy(buffer,0,buffer,0,32,0);C3Di_RenderQueueWaitDone();deliver();
   C3D_SyncDisplayTransfer(buffer,0,buffer,0,0);C3Di_RenderQueueWaitDone();deliver();
   C3D_SyncMemoryFill(buffer,0,buffer,0,buffer,0,buffer,0);C3Di_RenderQueueWaitDone();deliver();
   assert(swapCalls==before);++scenarios;
  }
  C3D_FrameEndHook(NULL,NULL);C3Di_RenderQueueExit();deliver();
 }
 assert(mp_queue_deferred_callbacks>100 && mp_queue_stale_callbacks>0 && mp_queue_join_completions>0);
#ifndef MP_QUEUE_REFERENCE_TEST
 assert(mp_queue_capacity_drains>2048);
 assert(mp_early_queue_deferrals==mp_early_queue_retirements);
 assert(mp_early_queue_starts==mp_early_queue_finishes);
#endif
 printf("PASS %u queue scenarios; %u publications, %u deferred callbacks, %u stale callbacks, %u joined completions, %u swaps\n",scenarios,mp_queue_publications,mp_queue_deferred_callbacks,mp_queue_stale_callbacks,mp_queue_join_completions,swapCalls);
}
