#ifndef MP_NATIVE_H
#define MP_NATIVE_H
#include <stddef.h>
/* These functions switch to the SDK's little-endian mode at the boundary. */
void *mp_platform_alloc(unsigned size);
void mp_platform_free(void *ptr);
void mp_platform_log(const char *text);
void mp_platform_panic(const char *text) __attribute__((noreturn));
unsigned mp_platform_ticks(void);
int mp_platform_file_id(const char *name);
unsigned mp_platform_file_size(int id);
int mp_platform_file_read(int id,void *dst,unsigned size,unsigned offset);
int mp_platform_file_read_async_begin(int id,void *dst,unsigned size,unsigned offset);
int mp_platform_file_read_async_poll(void);
#define MP_FILE_READ_BUSY (-2147483647)
void mp_platform_pad(void *statuses);
void mp_platform_pad_battle(unsigned battle);
void mp_platform_frame_render(void);
void mp_platform_frame_main(void);
/* Top-screen VBlank count (video_pad.c) and the frame-rate cap in retraces
 * per rendered frame: 1 for 60 FPS, 2 for 30 FPS (gx_fifo.c). */
unsigned mp_platform_vblank_count(void);
unsigned mp_platform_frame_interval(void);
void mp_dvd_pump(void);
/* Marks engine memory as rewritten for the GX geometry-source cache (gx.c). */
void mp_gx_source_write(const void *data, unsigned size);
/* CPU cache flush/invalidate of engine memory seen by GX (gx_fifo.c). */
void mp_gx_cache_range(void *data, unsigned size, unsigned cpu_written);
int mp_dvd_pending(void);
/* Build profile: 0 everything unlocked, 1 fresh save (card_storage.c). */
unsigned mp_platform_profile(void);
#endif
