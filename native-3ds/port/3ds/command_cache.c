#include <3ds.h>

/* Citro3D's transfer/clear helpers split command lists with flags=0. Each
 * segment must be cleaned independently when FrameEnd no longer flushes the
 * entire linear heap. Link wrapping covers those library-internal splits as
 * well as the final segment. The GSP queue flushes a segment before reading it.
 * Textures, persistent geometry and streaming buffers have explicit flushes. */
Result __real_GX_ProcessCommandList(u32*,u32,u8);
#ifdef MP_SMOKE_TEST
u64 mp_render_command_bytes;
unsigned mp_render_command_lists,mp_render_command_errors;
#endif
/* Clean a range for the GPU with the kernel's process cache operation (SVC
 * 0x54, permitted by the CIA and Luma exheaders; audio.c uses it for NDSP)
 * instead of a GSP service request: every draw's coordinates, each streamed
 * vertex batch and every command-list segment flush, so the IPC round trip
 * was paid many times per frame. The service call remains the fallback. */
static int direct_flush_unavailable;
void mp_cache_flush(const void*data,unsigned bytes){
    if(!bytes)return;
    if(!direct_flush_unavailable&&R_SUCCEEDED(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)data,bytes)))return;
    direct_flush_unavailable=1;GSPGPU_FlushDataCache(data,bytes);
}
Result __wrap_GX_ProcessCommandList(u32* buffer,u32 bytes,u8 flags)
{
    mp_cache_flush(buffer,bytes);
    Result result=__real_GX_ProcessCommandList(buffer,bytes,flags&~GX_CMDLIST_FLUSH);
#ifdef MP_SMOKE_TEST
    if(R_SUCCEEDED(result)){mp_render_command_bytes+=bytes;++mp_render_command_lists;}else ++mp_render_command_errors;
#endif
    return result;
}
