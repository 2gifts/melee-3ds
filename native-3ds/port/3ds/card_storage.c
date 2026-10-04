#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
/* SD storage behind the engine's virtual memory card (port/engine/card.c).
 * Each card file is one Dolphin-compatible .gci in the profile's folder:
 *   sdmc:/3ds/melee/saves/unlocked/  (everything unlocked build)
 *   sdmc:/3ds/melee/saves/fresh/     (fresh save build)
 *   sdmc:/3ds/melee/saves/slippi/    (Slippi Direct beta, everything unlocked)
 * The builds never share saves. The Slippi beta's folder starts as a copy of
 * the unlocked build's saves (name tags, rules), which it never writes. Writes go to a temporary file that
 * replaces the previous one, which is kept as .bak; deletions keep a
 * .deleted copy. A save interrupted mid-write therefore never replaces the
 * last good one, and an orphaned .bak is restored at the next listing. */
#ifdef MP_PROFILE_FRESH
#define MP_PROFILE 1
#define MP_SAVE_DIR "sdmc:/3ds/melee/saves/fresh/"
#elif defined(MP_PROFILE_SLIPPI)
#define MP_PROFILE 0
#define MP_SAVE_DIR "sdmc:/3ds/melee/saves/slippi/"
#define MP_SEED_DIR "sdmc:/3ds/melee/saves/unlocked/"
#else
#define MP_PROFILE 0
#define MP_SAVE_DIR "sdmc:/3ds/melee/saves/unlocked/"
#endif
extern void mp_native_log(const char* text);

unsigned mp_native_profile(void) { return MP_PROFILE; }

#ifdef MP_SEED_DIR
/* First start of the Slippi beta: copy the unlocked build's .gci saves. */
static void seed_saves(void) {
    DIR* dir = opendir(MP_SEED_DIR);
    struct dirent* entry;
    unsigned copied = 0;
    char text[96];
    if (!dir)
        return;
    while ((entry = readdir(dir)) != NULL) {
        size_t n = strlen(entry->d_name);
        char from[160], to[160];
        FILE *in, *out;
        long size;
        unsigned char* data;
        if (n < 5 || n > 95 || strcmp(entry->d_name + n - 4, ".gci") != 0)
            continue;
        snprintf(from, sizeof from, "%s%s", MP_SEED_DIR, entry->d_name);
        snprintf(to, sizeof to, "%s%s", MP_SAVE_DIR, entry->d_name);
        in = fopen(from, "rb");
        if (!in)
            continue;
        fseek(in, 0, SEEK_END);
        size = ftell(in);
        fseek(in, 0, SEEK_SET);
        data = size > 0 && size <= 4 * 1024 * 1024 ? malloc((size_t) size) : NULL;
        if (data && fread(data, 1, (size_t) size, in) == (size_t) size && (out = fopen(to, "wb")) != NULL) {
            if (fwrite(data, 1, (size_t) size, out) == (size_t) size)
                copied++;
            fclose(out);
        }
        free(data);
        fclose(in);
    }
    closedir(dir);
    snprintf(text, sizeof text, "Saves: copied %u from saves/unlocked to saves/slippi\n", copied);
    mp_native_log(text);
}
#endif

static void ensure_directory(void) {
    static int made;
    if (made)
        return;
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/melee", 0777);
    mkdir("sdmc:/3ds/melee/saves", 0777);
#ifdef MP_SEED_DIR
    if (mkdir(MP_SAVE_DIR, 0777) == 0)
        seed_saves();
#else
    mkdir(MP_SAVE_DIR, 0777);
#endif
    made = 1;
}

static int safe_name(const char* name) {
    return name && *name && strlen(name) < 96 && !strchr(name, '/') && !strchr(name, '\\') && !strstr(name, "..");
}

static void path_of(char* out, unsigned size, const char* name, const char* suffix) {
    snprintf(out, size, MP_SAVE_DIR "%s%s", name, suffix);
}

static int exists(const char* path) {
    struct stat st;
    return !stat(path, &st);
}

void mp_native_card_sync(void);

int mp_native_card_list(char* names, unsigned capacity) {
    mp_native_card_sync();
    ensure_directory();
    DIR* dir = opendir(MP_SAVE_DIR);
    if (!dir)
        return 0;
    /* Restore saves whose replacement was interrupted after the rename of
     * the old file to .bak. */
    struct dirent* e;
    while ((e = readdir(dir))) {
        size_t n = strlen(e->d_name);
        if (n > 8 && !strcmp(e->d_name + n - 8, ".gci.bak")) {
            char base[128], from[192], to[192];
            snprintf(base, sizeof(base), "%.*s", (int) (n - 4), e->d_name);
            path_of(to, sizeof(to), base, "");
            if (!exists(to)) {
                path_of(from, sizeof(from), e->d_name, "");
                rename(from, to);
            }
        }
    }
    closedir(dir);
    /* Reopen rather than rewinddir, which the SD devoptab does not reset. */
    dir = opendir(MP_SAVE_DIR);
    if (!dir)
        return 0;
    unsigned used = 0;
    int count = 0;
    while ((e = readdir(dir))) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".gci") || used + n + 1 > capacity)
            continue;
        memcpy(names + used, e->d_name, n + 1);
        used += n + 1;
        ++count;
    }
    closedir(dir);
    return count;
}

int mp_native_card_size(const char* name) {
    char path[192];
    struct stat st;
    if (!safe_name(name))
        return -1;
    path_of(path, sizeof(path), name, "");
    return stat(path, &st) ? -1 : (int) st.st_size;
}

int mp_native_card_read(const char* name, void* dst, unsigned size, unsigned offset) {
    char path[192];
    if (!safe_name(name))
        return -1;
    path_of(path, sizeof(path), name, "");
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    int got = -1;
    if (!fseek(f, offset, SEEK_SET))
        got = (int) fread(dst, 1, size, f);
    fclose(f);
    return got;
}

/* The SD work of a save (a 90 KB write and three directory updates) takes
 * most of a second on the console. Melee saves after many menu actions and
 * expects the card to work in the background, as a GameCube's does, so the
 * engine only hands over a snapshot and a writer thread does the rest.
 * A queued write superseded by a newer one for the same file is dropped;
 * order is otherwise preserved. mp_native_card_sync waits for all of it. */
enum { CARD_JOBS = 16 };
typedef struct {
    int remove, file; /* file: name is a full path (settings), no .bak */
    char name[96];
    void* data;
    unsigned size;
} CardJob;
static CardJob jobs[CARD_JOBS];
static unsigned job_head, job_tail, writer_busy;
static Thread writer;
static LightLock job_lock;
static LightEvent job_ready, job_done;
static unsigned card_writes, card_write_failures, card_writes_merged;

static int write_now(const char* name, const void* src, unsigned size);
static int remove_now(const char* name);
static int file_now(const char* path, const void* src, unsigned size);

static void writer_main(void* unused) {
    (void) unused;
    for (;;) {
        LightEvent_Wait(&job_ready);
        for (;;) {
            LightLock_Lock(&job_lock);
            if (job_head == job_tail) {
                writer_busy = 0;
                LightLock_Unlock(&job_lock);
                LightEvent_Signal(&job_done);
                break;
            }
            CardJob job = jobs[job_head++ % CARD_JOBS];
            writer_busy = 1;
            LightLock_Unlock(&job_lock);
            int result = job.remove ? remove_now(job.name)
                         : job.file ? file_now(job.name, job.data, job.size)
                                    : write_now(job.name, job.data, job.size);
            if (!job.remove) {
                free(job.data);
                if (result < 0) {
                    char text[160];
                    snprintf(text, sizeof(text), "Card: SD write of %s failed\n", job.name);
                    mp_native_log(text);
                    __atomic_fetch_add(&card_write_failures, 1, __ATOMIC_RELAXED);
                }
            }
        }
    }
}

static int start_writer(void) {
    if (writer)
        return 1;
    LightLock_Init(&job_lock);
    LightEvent_Init(&job_ready, RESET_ONESHOT);
    LightEvent_Init(&job_done, RESET_STICKY);
    LightEvent_Signal(&job_done);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    writer = threadCreate(writer_main, NULL, 16384, priority > 0x18 ? priority - 1 : priority, -2, false);
    return writer != NULL;
}

static int enqueue(int remove, int file, const char* name, const void* src, unsigned size) {
    void* copy = NULL;
    if (!remove && !(copy = malloc(size))) {
        /* No room for a snapshot: finish the queue, then write in place. */
        mp_native_card_sync();
        return file ? file_now(name, src, size) : write_now(name, src, size);
    }
    if (copy)
        memcpy(copy, src, size);
    for (;;) {
        LightLock_Lock(&job_lock);
        /* A newer state of a file replaces the file's latest queued job when
         * that is an unstarted write (never one before a queued deletion). */
        for (unsigned i = job_tail; !remove && i != job_head; --i) {
            CardJob* j = &jobs[(i - 1) % CARD_JOBS];
            if (strcmp(j->name, name))
                continue;
            if (!j->remove) {
                free(j->data);
                j->data = copy;
                j->size = size;
                ++card_writes_merged;
                LightLock_Unlock(&job_lock);
                return 0;
            }
            break;
        }
        if (job_tail - job_head < CARD_JOBS) {
            CardJob* j = &jobs[job_tail++ % CARD_JOBS];
            j->remove = remove;
            j->file = file;
            snprintf(j->name, sizeof(j->name), "%s", name);
            j->data = copy;
            j->size = size;
            LightEvent_Clear(&job_done);
            LightLock_Unlock(&job_lock);
            LightEvent_Signal(&job_ready);
            return 0;
        }
        LightLock_Unlock(&job_lock);
        LightEvent_Wait(&job_done); /* full: let the writer catch up */
    }
}

/* Wait until every queued save and deletion has reached the SD card. */
void mp_native_card_sync(void) {
    if (!writer)
        return;
    for (;;) {
        LightLock_Lock(&job_lock);
        int idle = job_head == job_tail && !writer_busy;
        if (!idle)
            LightEvent_Clear(&job_done); /* the writer signals once idle again */
        LightLock_Unlock(&job_lock);
        if (idle)
            return;
        LightEvent_Wait(&job_done);
    }
}

int mp_native_card_write(const char* name, const void* src, unsigned size) {
    if (!safe_name(name))
        return -1;
    ++card_writes;
    if (!start_writer())
        return write_now(name, src, size);
    return enqueue(0, 0, name, src, size);
}

int mp_native_card_remove(const char* name) {
    if (!safe_name(name))
        return -1;
    if (!start_writer())
        return remove_now(name);
    return enqueue(1, 0, name, NULL, 0);
}

static int write_now(const char* name, const void* src, unsigned size) {
    char path[192], temporary[192], backup[192];
    u64 start = svcGetSystemTick();
    ensure_directory();
    path_of(path, sizeof(path), name, "");
    path_of(temporary, sizeof(temporary), name, ".tmp");
    path_of(backup, sizeof(backup), name, ".bak");
    FILE* f = fopen(temporary, "wb");
    if (!f)
        return -1;
    int ok = fwrite(src, 1, size, f) == size;
    ok = !fflush(f) && ok;
    ok = !fclose(f) && ok;
    if (!ok) {
        remove(temporary);
        return -1;
    }
    if (exists(path)) {
        remove(backup);
        if (rename(path, backup)) {
            remove(temporary);
            return -1;
        }
    }
    if (rename(temporary, path))
        return -1;
    char text[192];
    snprintf(text, sizeof(text), "Card: saved %s (%u bytes, %u ms on the writer thread; %u merged)\n", name, size,
             (unsigned) ((svcGetSystemTick() - start) / (SYSCLOCK_ARM11 / 1000)), card_writes_merged);
    mp_native_log(text);
    return 0;
}

/* A small file outside the save folder (settings), replaced atomically. */
int mp_native_file_write_async(const char* path, const void* src, unsigned size) {
    if (!path || strlen(path) >= 96)
        return -1;
    if (!start_writer())
        return file_now(path, src, size);
    return enqueue(0, 1, path, src, size);
}

static int file_now(const char* path, const void* src, unsigned size) {
    char temporary[112];
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE* f = fopen(temporary, "wb");
    if (!f)
        return -1;
    int ok = fwrite(src, 1, size, f) == size;
    ok = !fclose(f) && ok;
    if (ok) {
        remove(path);
        ok = !rename(temporary, path);
    }
    if (!ok)
        remove(temporary);
    return ok ? 0 : -1;
}

static int remove_now(const char* name) {
    char path[192], kept[192];
    path_of(path, sizeof(path), name, "");
    path_of(kept, sizeof(kept), name, ".deleted");
    remove(kept);
    return rename(path, kept);
}

/* Seconds since 2000-01-01, the card directory's time base. */
unsigned mp_native_card_time(void) {
    u64 ms = osGetTime(); /* milliseconds since 1900-01-01 */
    u64 seconds = ms / 1000;
    return seconds > 3155673600ull ? (unsigned) (seconds - 3155673600ull) : 0;
}
