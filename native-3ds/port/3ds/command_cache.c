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
Result __wrap_GX_ProcessCommandList(u32* buffer,u32 bytes,u8 flags)
{
    Result result=__real_GX_ProcessCommandList(buffer,bytes,flags|GX_CMDLIST_FLUSH);
#ifdef MP_SMOKE_TEST
    if(R_SUCCEEDED(result)){mp_render_command_bytes+=bytes;++mp_render_command_lists;}else ++mp_render_command_errors;
#endif
    return result;
}
