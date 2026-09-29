#include <3ds.h>

/* GPU busy time, measured without changing submission. libctru hands every
 * queued GX command (command list, transfer, fill) to GSP through
 * gspSubmitGxCommand and learns of each completion in gxCmdQueueInterrupt.
 * The GPU counts as busy while any submitted command is incomplete, so this
 * covers early command-list prefixes too, which C3D_GetDrawingTime (queue
 * tail only) cannot. Both are wrapped at link time (build_game.py). */
extern Result __real_gspSubmitGxCommand(const u32 gxCommand[0x8]);
extern void __real_gxCmdQueueInterrupt(GSPGPU_Event irq);
static LightLock lock=1;
static unsigned pending;
static u64 started;
u64 mp_gpu_busy_ticks;
unsigned mp_gpu_commands;

Result __wrap_gspSubmitGxCommand(const u32 gxCommand[0x8]){
    LightLock_Lock(&lock);
    if(!pending++)started=svcGetSystemTick();
    ++mp_gpu_commands;
    LightLock_Unlock(&lock);
    return __real_gspSubmitGxCommand(gxCommand);
}
void __wrap_gxCmdQueueInterrupt(GSPGPU_Event irq){
    /* The same events libctru's queue treats as command completions. */
    if(irq!=GSPGPU_EVENT_PSC1&&irq!=GSPGPU_EVENT_VBlank0&&irq!=GSPGPU_EVENT_VBlank1){
        LightLock_Lock(&lock);
        if(pending&&!--pending)mp_gpu_busy_ticks+=svcGetSystemTick()-started;
        LightLock_Unlock(&lock);
    }
    __real_gxCmdQueueInterrupt(irq);
}
