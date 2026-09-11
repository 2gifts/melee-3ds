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
void mp_platform_frame(void);
void mp_dvd_pump(void);
int mp_dvd_pending(void);
#endif
