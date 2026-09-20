"""Generate the Citro3D queue-publication adapter from pinned source.

No installed SDK file is edited. The generated translation unit replaces only
renderqueue.o in a release or explicitly selected development link. Citro3D's existing
license remains in port/3ds/vendor/CITRO3D-LICENSE.txt.
"""
import hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PIN='db6571674f9e1679270630aef62a0640a59ff428a94c0a52b837f25811f9f3d1'


def source():
    path=ROOT/'references/citro3d/source/renderqueue.c'
    assert hashlib.sha256(path.read_bytes()).hexdigest()==PIN,'Pinned queue source changed'
    s=path.read_text()
    def replace(old,new,count=1):
        nonlocal s
        assert s.count(old)==count,(old[:90],s.count(old),count)
        s=s.replace(old,new)
    replace('#include "internal.h"','#include "vendor/citro3d_internal.h"\n#include <3ds.h>')
    replace('static bool inFrame, inSafeTransfer, measureGpuTime;', '''static bool inFrame, inSafeTransfer, measureGpuTime;
/* Serialize completion with publication and queue recycling. Callbacks can
 * be delayed after libctru marks the queue drained, including across a new
 * append. No private display flags are inspected by the game renderer. */
static LightLock completionLock = 1;
static bool publishing;
unsigned mp_queue_publications, mp_queue_completions, mp_queue_deferred_callbacks;
unsigned mp_queue_stale_callbacks, mp_queue_join_completions;
unsigned mp_queue_capacity_drains;
extern void mp_early_queue_capacity_completed(u64 wait_ticks);''')
    replace('static void onQueueFinish(gxCmdQueue_s* queue)\n{', '''static void completeLocked(gxCmdQueue_s* queue)
{
    if (publishing) { ++mp_queue_deferred_callbacks; return; }
    if (queue->lastEntry != queue->numEntries) { ++mp_queue_stale_callbacks; return; }
    if (measureGpuTime || inSafeTransfer || needSwapTop || needSwapBot) {
        ++mp_queue_completions;
    }''')
    at=s.index('\nvoid C3D_FrameSync(void)')
    s=s[:at]+'''
static void onQueueFinish(gxCmdQueue_s* queue)
{
    LightLock_Lock(&completionLock);
    completeLocked(queue);
    LightLock_Unlock(&completionLock);
}

static void beginPublication(void)
{
    LightLock_Lock(&completionLock);
    if (publishing) svcBreak(USERBREAK_PANIC);
    publishing = true;
    LightLock_Unlock(&completionLock);
}

static void publishQueue(gxCmdQueue_s* queue)
{
    LightLock_Lock(&completionLock);
    publishing = false;
    ++mp_queue_publications;
    /* It may have finished while the transfer list was being built. */
    completeLocked(queue);
    LightLock_Unlock(&completionLock);
}
''' +s[at:]
    replace('''	if (!gxCmdQueueWait(queue, timeout))
		return false;
	gxCmdQueueStop(queue);
	gxCmdQueueClear(queue);
	return true;''','''	if (!gxCmdQueueWait(queue, timeout))
		return false;
    LightLock_Lock(&completionLock);
    if (publishing || queue->lastEntry != queue->numEntries)
        svcBreak(USERBREAK_PANIC);
    /* libctru releases its lock before calling back. Service that pending
     * completion here before clearing/reusing this generation's storage. */
    unsigned before = mp_queue_completions;
    completeLocked(queue);
    mp_queue_join_completions += mp_queue_completions - before;
	gxCmdQueueStop(queue);
	gxCmdQueueClear(queue);
    LightLock_Unlock(&completionLock);
	return true;''')
    replace('\tinFrame = true;','\tLightLock_Lock(&completionLock);\n\tinFrame = true;\n\tLightLock_Unlock(&completionLock);')
    at=s.index('\nvoid C3D_FrameSplit(u8 flags)')
    s=s[:at]+'''
static void reserveQueueTail(void)
{
    gxCmdQueue_s* queue = &C3Di_GetContext()->gxQueue;
    /* One final command list plus up to three screen transfers. Keep an
     * extra entry so in-frame copies following FrameSplit also fit. */
    if (queue->numEntries + 4 < queue->maxEntries) return;
    if (publishing) svcBreak(USERBREAK_PANIC);
    u64 before = svcGetSystemTick();
    gxCmdQueueRun(queue);
    C3Di_WaitAndClearQueue(-1);
    mp_early_queue_capacity_completed(svcGetSystemTick() - before);
    ++mp_queue_capacity_drains;
}
''' +s[at:]
    replace('''	u32 *cmdBuf, cmdBufSize;
	if (!inFrame) return;
	if (C3Di_SplitFrame''','''	u32 *cmdBuf, cmdBufSize;
	if (!inFrame) return;
    reserveQueueTail();
	if (C3Di_SplitFrame''')
    replace('''	if (frameEndCb)
		frameEndCb(frameEndCbData);

	C3D_FrameSplit(flags);''','''	if (frameEndCb)
		frameEndCb(frameEndCbData);

    reserveQueueTail();
    beginPublication();
	C3D_FrameSplit(flags);''')
    # While publishing, callbacks return before reading these flags. They
    # become visible together through publishQueue's completionLock release.
    replace('''	osTickCounterStart(&gpuTime);
	gxCmdQueueRun(&ctx->gxQueue);
}''','''	osTickCounterStart(&gpuTime);
	gxCmdQueueRun(&ctx->gxQueue);
    publishQueue(&ctx->gxQueue);
}''')
    replace('''	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;''','''	C3Di_WaitAndClearQueue(-1);
    beginPublication();
	inSafeTransfer = true;''',3)
    replace('''	gxCmdQueueRun(&C3Di_GetContext()->gxQueue);
}''','''	gxCmdQueueRun(&C3Di_GetContext()->gxQueue);
    publishQueue(&C3Di_GetContext()->gxQueue);
}''',3)
    return ('/* DEVELOPMENT ONLY: generated by tools/async_renderqueue.py.\n'
            ' * Derived from pinned Citro3D renderqueue.c; see CITRO3D-LICENSE.txt. */\n'+s)


def generate(out):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    path=out/'async_renderqueue.c';path.write_text(source());return path


if __name__=='__main__':
    print(generate(ROOT/'build/async-queue-host'))
