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

void mp_native_slippi_unlimited(void)
{
#ifdef MP_SMOKE_TEST
    mp_test_frame_limit = 0;
#endif
}
