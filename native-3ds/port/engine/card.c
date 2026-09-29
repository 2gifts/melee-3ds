#include <dolphin/card.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include "native.h"
/* Virtual memory card in slot A, stored on the SD card.
 *
 * Every card file is kept in memory while mounted and persisted as a
 * Dolphin-compatible .gci (the 64-byte directory entry, big-endian as on
 * the GameCube, followed by the file's blocks) in the build profile's save
 * folder (card_storage.c). Melee's own save logic (lbcardgame.c, the HSD
 * card manager) runs unchanged on top of the SDK interface below.
 *
 * The card looks like a Memory Card 251: 16 Mbit, 8 KiB sectors, five
 * system blocks. Asynchronous calls complete immediately but deliver their
 * callbacks from the engine poll (mp_card_pump), after interrupts are
 * enabled again: callers count pending operations after the call returns.
 * Melee's card manager reopens and closes the file for every sector, so
 * written files persist 0.3 s after the last card access instead of on
 * each close (the whole 11-block save is then written once). Slot B has no
 * card. */
#define CARD_SECTOR 8192u
#define CARD_MEGABITS 16
#define CARD_BLOCKS (CARD_MEGABITS * 1024u * 1024u / 8u / CARD_SECTOR)
#define CARD_USER_BLOCKS (CARD_BLOCKS - CARD_NUM_SYSTEM_BLOCK)
#define GCI_HEADER 64u
_Static_assert(sizeof(CARDDir) == GCI_HEADER, "CARDDir layout");

int mp_platform_card_list(char* names, unsigned capacity);
int mp_platform_card_size(const char* name);
int mp_platform_card_read(const char* name, void* dst, unsigned size, unsigned offset);
int mp_platform_card_write(const char* name, const void* src, unsigned size);
int mp_platform_card_remove(const char* name);
void mp_platform_card_sync(void);
unsigned mp_platform_card_time(void);

typedef struct {
    u8* image; /* GCI_HEADER bytes of directory entry, then the blocks */
    int dirty;
    char path[80];
} CardFile;
static CardFile files[CARD_MAX_FILE];
static int mounted, loaded;
static s32 result_code[2] = {CARD_RESULT_NOCARD, CARD_RESULT_NOCARD};
static s32 transferred[2];
static struct {
    CARDCallback callback;
    s32 chan, result;
} pending[32];
static unsigned pending_head, pending_tail;
static u32 last_access; /* nonzero while a written file awaits persisting */
#define PERSIST_DELAY_TICKS 12150000u /* 0.3 s */
static void touch(void) { last_access = mp_platform_ticks() | 1; }
static const u8 game_name[4] = {'G', 'A', 'L', 'E'}, company[2] = {'0', '1'};

static CARDDir* entry(int n) { return (CARDDir*) files[n].image; }
static u8* data(int n) { return files[n].image + GCI_HEADER; }
static u32 file_bytes(int n) { return (u32) entry(n)->length * CARD_SECTOR; }

static void log_line(const char* text) { mp_platform_log(text); }

static void sd_name(const CARDDir* d, char* out) {
    /* Dolphin's naming, with characters FAT cannot hold escaped. */
    static const char hex[] = "0123456789ABCDEF";
    unsigned n = 0;
    out[n++] = d->company[0];
    out[n++] = d->company[1];
    out[n++] = '-';
    for (int i = 0; i < 4; ++i)
        out[n++] = d->gameName[i];
    out[n++] = '-';
    for (int i = 0; i < CARD_FILENAME_MAX && d->fileName[i] && n < 70; ++i) {
        u8 c = d->fileName[i];
        if (c < 0x20 || c > 0x7E || strchr("\\/:*?\"<>|%", c)) {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        } else {
            out[n++] = c;
        }
    }
    memcpy(out + n, ".gci", 5);
}

static int persist(int n) {
    CardFile* f = &files[n];
    if (!f->image)
        return 0;
    sd_name(entry(n), f->path);
    if (mp_platform_card_write(f->path, f->image, GCI_HEADER + file_bytes(n)) < 0) {
        log_line("Card: SD write failed\n");
        return CARD_RESULT_IOERROR;
    }
    f->dirty = 0;
    return 0;
}

static void flush_all(void) {
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        if (files[i].image && files[i].dirty)
            persist(i);
}

static void release(int n) {
    if (files[n].image)
        mp_platform_free(files[n].image);
    memset(&files[n], 0, sizeof(files[n]));
}

static int used_blocks(void) {
    int blocks = 0;
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        if (files[i].image)
            blocks += entry(i)->length;
    return blocks;
}

static void load_directory(void) {
    static char names[8192];
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        release(i);
    int count = mp_platform_card_list(names, sizeof(names));
    const char* name = names;
    int slot = 0, start = CARD_NUM_SYSTEM_BLOCK;
    for (int i = 0; i < count && slot < CARD_MAX_FILE; ++i, name += strlen(name) + 1) {
        int size = mp_platform_card_size(name);
        if (size < (int) (GCI_HEADER + CARD_SECTOR) || (size - GCI_HEADER) % CARD_SECTOR) {
            OSReport("Card: skipping %s (size %d)\n", name, size);
            continue;
        }
        u8* image = mp_platform_alloc(size);
        if (!image || mp_platform_card_read(name, image, size, 0) != size) {
            if (image)
                mp_platform_free(image);
            OSReport("Card: could not read %s\n", name);
            continue;
        }
        CARDDir* d = (CARDDir*) image;
        u32 blocks = (size - GCI_HEADER) / CARD_SECTOR;
        if (d->gameName[0] == 0xFF || blocks > CARD_USER_BLOCKS) {
            mp_platform_free(image);
            continue;
        }
        d->length = blocks;
        d->startBlock = start;
        start += blocks;
        files[slot].image = image;
        strncpy(files[slot].path, name, sizeof(files[slot].path) - 1);
        OSReport("Card: %.32s (%u blocks)\n", (const char*) d->fileName, blocks);
        ++slot;
    }
    loaded = 1;
}

static s32 access(int n) {
    if (n < 0 || n >= CARD_MAX_FILE)
        return CARD_RESULT_FATAL_ERROR;
    if (!files[n].image)
        return CARD_RESULT_NOFILE;
    CARDDir* d = entry(n);
    if (!memcmp(d->gameName, game_name, 4) && !memcmp(d->company, company, 2))
        return CARD_RESULT_READY;
    return (d->permission & CARD_ATTR_PUBLIC) ? CARD_RESULT_READY : CARD_RESULT_NOPERM;
}

static int find(const char* name) {
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        if (files[i].image && !memcmp(entry(i)->gameName, game_name, 4) &&
            !memcmp(entry(i)->company, company, 2) &&
            !strncmp((const char*) entry(i)->fileName, name, CARD_FILENAME_MAX))
            return i;
    return -1;
}

static s32 ready(s32 chan) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    if (!mounted)
        return CARD_RESULT_NOCARD;
    return CARD_RESULT_READY;
}

/* Complete an operation: synchronous callers get the result; asynchronous
 * ones see BUSY until the poll delivers the callback. */
static s32 finish(s32 chan, s32 result, CARDCallback callback) {
    if (chan < 0 || chan > 1)
        return result;
    if (!callback) {
        result_code[chan] = result;
        return result;
    }
    if (pending_tail - pending_head >= 32)
        HSD_Panic(__FILE__, __LINE__, "Card callback queue full");
    unsigned i = pending_tail++ & 31;
    pending[i].callback = callback;
    pending[i].chan = chan;
    pending[i].result = result;
    result_code[chan] = CARD_RESULT_BUSY;
    return CARD_RESULT_READY;
}

void mp_card_pump(void) {
    while (pending_head != pending_tail) {
        unsigned i = pending_head++ & 31;
        result_code[pending[i].chan] = pending[i].result;
        pending[i].callback(pending[i].chan, pending[i].result);
    }
    if (last_access && mp_platform_ticks() - last_access > PERSIST_DELAY_TICKS) {
        last_access = 0;
        flush_all();
    }
}

/* Persist pending writes now and wait for them (the app is exiting). */
void mp_card_flush(void) {
    last_access = 0;
    flush_all();
    mp_platform_card_sync();
}

void CARDInit(void) {}
s32 CARDGetResultCode(s32 chan) { return chan == 0 ? result_code[0] : CARD_RESULT_NOCARD; }
int CARDProbe(long chan) { return chan == 0; }
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    if (memSize)
        *memSize = CARD_MEGABITS;
    if (sectorSize)
        *sectorSize = CARD_SECTOR;
    return CARD_RESULT_READY;
}
s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
    (void) workArea;
    (void) detachCallback;
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    /* The SD folder is read once. Melee mounts and unmounts around every
     * save; the in-memory card stays authoritative in between, and the SD
     * copy follows it through the background writer (card_storage.c). */
    if (!loaded)
        load_directory();
    mounted = 1;
    return finish(chan, CARD_RESULT_READY, attachCallback);
}
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback) {
    (void) detachCallback;
    return CARDMountAsync(chan, workArea, NULL, NULL);
}
s32 CARDUnmount(s32 chan) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    flush_all();
    mounted = 0;
    result_code[0] = CARD_RESULT_NOCARD;
    return CARD_RESULT_READY;
}
s32 CARDCheckAsync(s32 chan, CARDCallback callback) {
    s32 r = ready(chan);
    return r < 0 ? r : finish(chan, CARD_RESULT_READY, callback);
}
long CARDCheck(long chan) { return CARDCheckAsync(chan, NULL); }
s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
    s32 r = ready(chan);
    if (r < 0)
        return r;
    int count = 0;
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        count += files[i].image != NULL;
    if (byteNotUsed)
        *byteNotUsed = (s32) ((CARD_USER_BLOCKS - used_blocks()) * CARD_SECTOR);
    if (filesNotUsed)
        *filesNotUsed = CARD_MAX_FILE - count;
    return finish(chan, CARD_RESULT_READY, NULL);
}
long CARDGetEncoding(long chan, unsigned short* encode) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    if (encode)
        *encode = 0;
    return CARD_RESULT_READY;
}
long CARDGetMemSize(long chan, unsigned short* size) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    if (size)
        *size = CARD_MEGABITS;
    return CARD_RESULT_READY;
}
s32 CARDGetSectorSize(s32 chan, u32* size) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    if (size)
        *size = CARD_SECTOR;
    return CARD_RESULT_READY;
}
long CARDGetXferredBytes(long chan) { return chan == 0 ? transferred[0] : 0; }

static s32 fill_info(s32 chan, int n, CARDFileInfo* info) {
    info->chan = chan;
    info->fileNo = n;
    info->offset = 0;
    info->length = (s32) file_bytes(n);
    info->iBlock = entry(n)->startBlock;
    return CARD_RESULT_READY;
}
s32 CARDOpen(s32 chan, char* fileName, CARDFileInfo* fileInfo) {
    fileInfo->chan = -1;
    s32 r = ready(chan);
    if (r < 0)
        return r;
    int n = find(fileName);
    return n < 0 ? CARD_RESULT_NOFILE : fill_info(chan, n, fileInfo);
}
s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
    fileInfo->chan = -1;
    s32 r = ready(chan);
    if (r < 0)
        return r;
    r = access(fileNo);
    return r < 0 ? r : fill_info(chan, fileNo, fileInfo);
}
s32 CARDClose(CARDFileInfo* fileInfo) {
    if (fileInfo->chan < 0)
        return CARD_RESULT_NOFILE;
    int n = fileInfo->fileNo;
    fileInfo->chan = -1;
    if (n >= 0 && n < CARD_MAX_FILE && files[n].dirty)
        touch();
    return CARD_RESULT_READY;
}
s32 CARDCancel(CARDFileInfo* fileInfo) {
    (void) fileInfo;
    return CARD_RESULT_READY;
}

static s32 transfer(CARDFileInfo* info, void* buf, s32 length, s32 offset, CARDCallback callback, int write) {
    s32 chan = info->chan, r = ready(chan);
    if (r < 0)
        return r;
    int n = info->fileNo;
    r = access(n);
    if (r < 0)
        return r;
    u32 align = write ? CARD_SECTOR : CARD_READ_SIZE;
    if (length < 0 || offset < 0 || offset % align || length % align)
        return CARD_RESULT_FATAL_ERROR;
    if ((u32) (offset + length) > file_bytes(n))
        return CARD_RESULT_LIMIT;
    if (write) {
        memcpy(data(n) + offset, buf, length);
        entry(n)->time = mp_platform_card_time();
        files[n].dirty = 1;
        touch();
    } else {
        memcpy(buf, data(n) + offset, length);
        /* Card data can become texture images (snapshots) and GX sources. */
        mp_gx_cache_range(buf, length, 1);
    }
    info->offset = offset + length;
    transferred[chan] = length;
    return finish(chan, CARD_RESULT_READY, callback);
}
s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset, CARDCallback callback) {
    return transfer(fileInfo, buf, length, offset, callback, 0);
}
long CARDRead(struct CARDFileInfo* fileInfo, void* buf, long length, long offset) {
    return transfer(fileInfo, buf, length, offset, NULL, 0);
}
long CARDWriteAsync(struct CARDFileInfo* fileInfo, void* buf, long length, long offset, void (*callback)(long, long)) {
    return transfer(fileInfo, buf, length, offset, (CARDCallback) callback, 1);
}
long CARDWrite(struct CARDFileInfo* fileInfo, void* buf, long length, long offset) {
    return transfer(fileInfo, buf, length, offset, NULL, 1);
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
    s32 r = ready(chan);
    if (r < 0)
        return r;
    if (strlen(fileName) > CARD_FILENAME_MAX)
        return CARD_RESULT_NAMETOOLONG;
    if (!size || size % CARD_SECTOR)
        return CARD_RESULT_FATAL_ERROR;
    if (find(fileName) >= 0)
        return CARD_RESULT_EXIST;
    int n = -1;
    for (int i = 0; i < CARD_MAX_FILE && n < 0; ++i)
        if (!files[i].image)
            n = i;
    if (n < 0)
        return CARD_RESULT_NOENT;
    u32 blocks = size / CARD_SECTOR;
    if (used_blocks() + blocks > CARD_USER_BLOCKS)
        return CARD_RESULT_INSSPACE;
    u8* image = mp_platform_alloc(GCI_HEADER + size);
    if (!image)
        return CARD_RESULT_IOERROR;
    memset(image, 0, GCI_HEADER + size);
    files[n].image = image;
    CARDDir* d = entry(n);
    memset(d, 0xFF, GCI_HEADER);
    memcpy(d->gameName, game_name, 4);
    memcpy(d->company, company, 2);
    d->bannerFormat = 0;
    memset(d->fileName, 0, CARD_FILENAME_MAX);
    strncpy((char*) d->fileName, fileName, CARD_FILENAME_MAX);
    d->time = mp_platform_card_time();
    d->iconFormat = 0;
    d->iconSpeed = 0;
    d->permission = 0;
    d->copyTimes = 0;
    int start = CARD_NUM_SYSTEM_BLOCK;
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        if (i != n && files[i].image && entry(i)->startBlock + entry(i)->length > start)
            start = entry(i)->startBlock + entry(i)->length;
    d->startBlock = start;
    d->length = blocks;
    r = persist(n);
    if (r < 0) {
        release(n);
        return r;
    }
    if (fileInfo)
        fill_info(chan, n, fileInfo);
    OSReport("Card: created %.32s (%u blocks)\n", fileName, blocks);
    return finish(chan, CARD_RESULT_READY, callback);
}
long CARDCreate(long chan, char* fileName, unsigned long size, struct CARDFileInfo* fileInfo) {
    return CARDCreateAsync(chan, fileName, size, fileInfo, NULL);
}

static s32 remove_file(s32 chan, int n, CARDCallback callback) {
    s32 r = access(n);
    if (r < 0)
        return r;
    sd_name(entry(n), files[n].path);
    mp_platform_card_remove(files[n].path);
    OSReport("Card: deleted %.32s\n", (const char*) entry(n)->fileName);
    release(n);
    return finish(chan, CARD_RESULT_READY, callback);
}
s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback) {
    s32 r = ready(chan);
    return r < 0 ? r : remove_file(chan, fileNo, callback);
}
long CARDFastDelete(long chan, long fileNo) { return CARDFastDeleteAsync(chan, fileNo, NULL); }
s32 CARDDeleteAsync(s32 chan, char* fileName, CARDCallback callback) {
    s32 r = ready(chan);
    if (r < 0)
        return r;
    int n = find(fileName);
    return n < 0 ? CARD_RESULT_NOFILE : remove_file(chan, n, callback);
}
s32 CARDDelete(s32 chan, char* fileName) { return CARDDeleteAsync(chan, fileName, NULL); }
s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
    if (chan != 0)
        return CARD_RESULT_NOCARD;
    if (!loaded)
        load_directory();
    /* Removed files are kept as .deleted copies by the storage layer. */
    for (int i = 0; i < CARD_MAX_FILE; ++i)
        if (files[i].image) {
            sd_name(entry(i), files[i].path);
            mp_platform_card_remove(files[i].path);
            release(i);
        }
    log_line("Card: formatted\n");
    return finish(chan, CARD_RESULT_READY, callback);
}
long CARDFormat(long chan) { return CARDFormatAsync(chan, NULL); }
s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, CARDCallback callback) {
    s32 r = ready(chan);
    if (r < 0)
        return r;
    if (strlen(newName) > CARD_FILENAME_MAX)
        return CARD_RESULT_NAMETOOLONG;
    int n = find(oldName);
    if (n < 0)
        return CARD_RESULT_NOFILE;
    if (find(newName) >= 0)
        return CARD_RESULT_EXIST;
    sd_name(entry(n), files[n].path);
    mp_platform_card_remove(files[n].path);
    memset(entry(n)->fileName, 0, CARD_FILENAME_MAX);
    strncpy((char*) entry(n)->fileName, newName, CARD_FILENAME_MAX);
    entry(n)->time = mp_platform_card_time();
    r = persist(n);
    return r < 0 ? r : finish(chan, CARD_RESULT_READY, callback);
}
s32 CARDRename(s32 chan, char* oldName, char* newName) { return CARDRenameAsync(chan, oldName, newName, NULL); }

/* The SDK derives banner, icon and data offsets from the formats. */
static void icon_offsets(const CARDDir* d, CARDStat* stat) {
    u32 offset = d->iconAddr;
    int tlut = 0;
    if (offset == 0xFFFFFFFFu) {
        stat->bannerFormat = 0;
        stat->iconFormat = 0;
        stat->iconSpeed = 0;
        offset = 0;
    }
    switch (d->bannerFormat & 3) {
    case 1: /* C8 */
        stat->offsetBanner = offset;
        offset += 96 * 32;
        stat->offsetBannerTlut = offset;
        offset += 2 * 256;
        break;
    case 2: /* RGB5A3 */
        stat->offsetBanner = offset;
        offset += 2 * 96 * 32;
        stat->offsetBannerTlut = 0xFFFFFFFFu;
        break;
    default:
        stat->offsetBanner = stat->offsetBannerTlut = 0xFFFFFFFFu;
        break;
    }
    for (int i = 0; i < CARD_ICON_MAX; ++i) {
        switch ((d->iconFormat >> (2 * i)) & 3) {
        case 1:
            stat->offsetIcon[i] = offset;
            offset += 32 * 32;
            tlut = 1;
            break;
        case 2:
            stat->offsetIcon[i] = offset;
            offset += 2 * 32 * 32;
            break;
        default:
            stat->offsetIcon[i] = 0xFFFFFFFFu;
            break;
        }
    }
    if (tlut) {
        stat->offsetIconTlut = offset;
        offset += 2 * 256;
    } else {
        stat->offsetIconTlut = 0xFFFFFFFFu;
    }
    stat->offsetData = offset;
}
s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
    s32 r = ready(chan);
    if (r < 0)
        return r;
    r = access(fileNo);
    if (r < 0)
        return r;
    const CARDDir* d = entry(fileNo);
    memcpy(stat->gameName, d->gameName, 4);
    memcpy(stat->company, d->company, 2);
    stat->length = file_bytes(fileNo);
    memcpy(stat->fileName, d->fileName, CARD_FILENAME_MAX);
    stat->time = d->time;
    stat->bannerFormat = d->bannerFormat;
    stat->iconAddr = d->iconAddr;
    stat->iconFormat = d->iconFormat;
    stat->iconSpeed = d->iconSpeed;
    stat->commentAddr = d->commentAddr;
    icon_offsets(d, stat);
    return CARD_RESULT_READY;
}
s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback) {
    s32 r = ready(chan);
    if (r < 0)
        return r;
    r = access(fileNo);
    if (r < 0)
        return r;
    CARDDir* d = entry(fileNo);
    d->bannerFormat = stat->bannerFormat;
    d->iconAddr = stat->iconAddr;
    d->iconFormat = stat->iconFormat;
    d->iconSpeed = stat->iconSpeed;
    d->commentAddr = stat->commentAddr;
    icon_offsets(d, stat);
    if (d->iconAddr == 0xFFFFFFFFu)
        d->iconSpeed = (d->iconSpeed & ~3) | CARD_STAT_SPEED_FAST;
    d->time = mp_platform_card_time();
    files[fileNo].dirty = 1;
    touch();
    return finish(chan, CARD_RESULT_READY, callback);
}
long CARDSetStatus(long chan, long fileNo, struct CARDStat* stat) {
    return CARDSetStatusAsync(chan, fileNo, stat, NULL);
}
