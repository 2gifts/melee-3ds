/* Slippi experiment: SD file helpers for the engine (replay playback input
 * and the per-frame state dump). Called through game_bridge.S; only scalars
 * and byte buffers cross the endianness boundary. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static unsigned last_size;

void* mp_native_slippi_file_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    void* data = NULL;
    long size;
    last_size = 0;
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t) size);
        if (data && fread(data, 1, (size_t) size, f) != (size_t) size) {
            free(data);
            data = NULL;
        }
        if (data) {
            last_size = (unsigned) size;
        }
    }
    fclose(f);
    return data;
}

unsigned mp_native_slippi_file_size(void)
{
    return last_size;
}

int mp_native_slippi_file_write(const char* path, const void* data, unsigned size, int append)
{
    FILE* f;
    size_t written;
    mkdir("sdmc:/3ds/melee/slippi", 0777);
    f = fopen(path, append ? "ab" : "wb");
    if (!f) {
        return -1;
    }
    written = fwrite(data, 1, size, f);
    fclose(f);
    return (int) written;
}

#ifdef MP_SMOKE_TEST
extern volatile unsigned mp_test_frame_limit;
#endif

extern volatile unsigned mp_slippi_no_scripts;

void mp_native_slippi_unlimited(void)
{
    mp_slippi_no_scripts = 1;
#ifdef MP_SMOKE_TEST
    mp_test_frame_limit = 0;
#endif
}

#ifdef __3DS__
#include <3ds.h>
#endif
extern volatile unsigned mp_file_log_opens;
extern unsigned mp_file_sd_opens, mp_file_sd_open_ticks, mp_file_sd_bytes;

void mp_native_file_keep_mark(void);

void mp_native_slippi_load_log(int on)
{
    if (on && !mp_file_log_opens) {
        mp_native_file_keep_mark();   /* this load's files stay cached for it */
    }
    mp_file_log_opens = on ? 1u : 0u;
}

/* Milliseconds since boot, and the SD totals, for load timing lines. */
unsigned mp_native_slippi_ms(void)
{
#ifdef __3DS__
    return (unsigned) (svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000));
#else
    return 0;
#endif
}

unsigned mp_native_slippi_sd_stat(int which)
{
    return which == 0 ? mp_file_sd_opens : which == 1 ? mp_file_sd_open_ticks / 1000 : mp_file_sd_bytes;
}

extern unsigned mp_prefetch_hits, mp_prefetch_files;
unsigned mp_native_prefetch_bytes(void);

unsigned mp_native_slippi_prefetch_hits(void)
{
    return mp_prefetch_hits;
}

/* 0: files cached, 1: KB cached. */
unsigned mp_native_slippi_prefetch_stat(int which)
{
    return which == 0 ? mp_prefetch_files : mp_native_prefetch_bytes() / 1024;
}
