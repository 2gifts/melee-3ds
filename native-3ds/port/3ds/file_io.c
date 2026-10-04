#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#ifdef __3DS__
#include <3ds.h>
#endif
#include "audio_trace.h"
#include "log_progress.h"
#ifndef MP_DISC_ROOT
#define MP_DISC_ROOT "sdmc:/3ds/melee/files/"
#endif
#ifndef MP_VISUAL_ROOT
#define MP_VISUAL_ROOT "sdmc:/3ds/melee/visuals/"
#endif
enum {MAX_FILES=2048,READERS=8,READ_BUFFER_BYTES=65536};
static struct{char *name,*path;unsigned size;}files[MAX_FILES];
static unsigned file_count,reader_clock;
#ifdef __3DS__
static Thread io_thread;
static LightEvent io_wake;
static LightLock io_lock;
static unsigned io_state,io_stop;
static struct {int id,result;void*dst;unsigned n,offset,ticks;}io_request;
unsigned mp_async_reads,mp_async_busy_polls;
#ifdef MP_SMOKE_TEST
volatile unsigned mp_test_disc_delay_ms,mp_async_read_disable;
#endif
#endif
typedef struct{FILE *stream;int id;unsigned stamp,position;unsigned char buffer[READ_BUFFER_BYTES];}Reader;
static Reader readers[READERS];
enum {MENU_FILES=9,MENU_CACHE_MAX=9*1024*1024};
static struct {int id;unsigned char *data;}menu_cache[MENU_FILES];
static unsigned menu_cache_count,menu_cache_bytes;
unsigned mp_file_cache_hits,mp_file_cache_read_bytes,mp_file_sd_reads,mp_file_sd_bytes;
unsigned mp_file_sd_opens,mp_file_sd_open_ticks; /* stream opens, microseconds opening */
/* Slippi experiment: log each stream open (name, size, ms) while a match loads. */
volatile unsigned mp_file_log_opens;
unsigned mp_file_log_bytes;
/* Slippi experiment: files read into RAM ahead of a match by a background
 * thread (while matchmaking and waiting for the opponent), kept for later
 * matches. On the console every file open costs 130-250 ms and a match load
 * opens about 25 files, so a cold load takes several seconds while the PC
 * peer, which loads in about one, waits at its first frame. */
/* Files cached in RAM before any networking starts (the boot menu's START,
 * slippi_prefetch.c), read by the main thread with nothing else running.
 * Reading in the background while the 3DS matched and connected (October 3)
 * left the opponent's selections undelivered on the console twice, so the
 * cache is only ever filled up front. Budget: the ordinary heap is 86 MB and
 * a running match uses about 59 MB of it. */
enum {PREFETCH_MAX=64,PREFETCH_BUDGET=10*1024*1024};
static struct {int id;unsigned char *data;} prefetch[PREFETCH_MAX];
static unsigned prefetch_count,prefetch_bytes;
unsigned mp_prefetch_hits,mp_prefetch_hit_bytes,mp_prefetch_files;
static const unsigned char *prefetched(int id){
    unsigned n=__atomic_load_n(&prefetch_count,__ATOMIC_ACQUIRE);
    for(unsigned i=0;i<n;++i)if(prefetch[i].id==id)return prefetch[i].data;
    return NULL;
}
#ifdef MP_SMOKE_TEST
volatile unsigned mp_file_trace;
#endif
/* Names and sizes of every disc and visual file, listed once at startup.
 * Resolving a name (DVDConvertPathToEntrynum, on the engine thread) then
 * needs no SD access: opening a file in the 1000-entry disc directory costs
 * a FAT lookup, and a fighter pick on the CSS resolves several new files.
 * Streams are still opened by the reader that actually reads the file.
 *
 * The console's FAT driver scans a directory linearly on every open, and a
 * disc file in the original flat 1000-entry folder took about 130 ms to
 * open on hardware (the trophy Collection opens ~70). The SD package
 * therefore spreads the top-level files over small folders whose names start
 * with '_' (files/_Ty03/TyMycR1A.dat). Such folders are transparent: their
 * files keep their disc names. The flat layout still works. */
enum {INDEX_BUCKETS=4096};
typedef struct IndexEntry {struct IndexEntry *next;const char *path;unsigned size;char name[];} IndexEntry;
static IndexEntry *disc_index[INDEX_BUCKETS],*visual_index[INDEX_BUCKETS],*slippi_index[INDEX_BUCKETS];
static unsigned disc_index_count,visual_index_count,slippi_index_count,disc_foldered,disc_duplicates;
static unsigned index_hash(const char *name){unsigned h=2166136261u;while(*name)h=(h^(unsigned char)*name++)*16777619u;return h&(INDEX_BUCKETS-1);}
static const IndexEntry *index_find(IndexEntry *const *table,const char *name){
    for(const IndexEntry *e=table[index_hash(name)];e;e=e->next)if(!strcmp(e->name,name))return e;
    return NULL;
}
#ifdef __3DS__
/* DIR is the folder's path below ROOT; PREFIX is the name prefix its files
 * have on the disc (the same for a transparent '_' folder as its parent). */
static unsigned index_directory(FS_Archive sdmc,IndexEntry **table,const char *root,const char *dir,const char *prefix,unsigned depth){
    char path[512];int n=snprintf(path,sizeof(path),"%s%s",root,dir);if(n<0||(size_t)n>=sizeof(path))return 0;
    Handle handle;if(R_FAILED(FSUSER_OpenDirectory(&handle,sdmc,fsMakePath(PATH_ASCII,path))))return 0;
    static FS_DirectoryEntry entries[32];unsigned count=0;u32 read=0;
    typedef struct{char dir[96],prefix[96];}Subdir;
    Subdir *subdirs=NULL;unsigned subdir_count=0,subdir_capacity=0;
    while(R_SUCCEEDED(FSDIR_Read(handle,&read,32,entries))&&read){
        for(u32 i=0;i<read;++i){
            char name[256];ssize_t units=utf16_to_utf8((uint8_t*)name,entries[i].name,sizeof(name)-1);
            if(units<=0||units>=(ssize_t)sizeof(name))continue;name[units]=0;
            char key[320],location[320];
            if(snprintf(key,sizeof(key),"%s%s",prefix,name)>=(int)sizeof(key)||
               snprintf(location,sizeof(location),"%s%s",dir,name)>=(int)sizeof(location))continue;
            if(entries[i].attributes&FS_ATTRIBUTE_DIRECTORY){
                if(depth>=3||strlen(location)+1>=sizeof(subdirs[0].dir)||strlen(key)+1>=sizeof(subdirs[0].prefix))continue;
                if(subdir_count==subdir_capacity){
                    unsigned capacity=subdir_capacity?subdir_capacity*2:16;
                    Subdir *grown=realloc(subdirs,capacity*sizeof(Subdir));if(!grown)continue;
                    subdirs=grown;subdir_capacity=capacity;
                }
                Subdir *s=&subdirs[subdir_count++];snprintf(s->dir,sizeof(s->dir),"%s/",location);
                if(name[0]=='_')snprintf(s->prefix,sizeof(s->prefix),"%s",prefix);
                else snprintf(s->prefix,sizeof(s->prefix),"%s/",key);
                continue;
            }
            if(entries[i].fileSize>UINT_MAX)continue;
            /* A file in both layouts (a new package copied over the flat
             * one) keeps its first location; the leftover copies still make
             * every open scan the big folder, which the log points out. */
            if(index_find(table,key)){++disc_duplicates;continue;}
            size_t key_bytes=strlen(key)+1;
            IndexEntry *e=malloc(sizeof(IndexEntry)+key_bytes+strlen(location)+1);if(!e)continue;
            memcpy(e->name,key,key_bytes);e->path=strcpy(e->name+key_bytes,location);e->size=(unsigned)entries[i].fileSize;
            if(strcmp(key,location))++disc_foldered;
            unsigned h=index_hash(e->name);e->next=table[h];table[h]=e;++count;
        }
    }
    FSDIR_Close(handle);
    for(unsigned i=0;i<subdir_count;++i)count+=index_directory(sdmc,table,root,subdirs[i].dir,subdirs[i].prefix,depth+1);
    free(subdirs);
    return count;
}
static int disc_indexed;
static void index_files(void){
    if(disc_indexed)return;disc_indexed=1;
    FS_Archive sdmc;if(R_FAILED(FSUSER_OpenArchive(&sdmc,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,""))))return;
    u64 start=svcGetSystemTick();
    disc_index_count=index_directory(sdmc,disc_index,"/3ds/melee/files/","","",0);
    unsigned foldered=disc_foldered,duplicates=disc_duplicates;
    visual_index_count=index_directory(sdmc,visual_index,"/3ds/melee/visuals/","","",0);
    /* Slippi fork: Slippi's patched menu files (tools/slippi/slippi_files.py
     * applies Slippi Dolphin's VCDIFF patches to this disc's own files). */
    slippi_index_count=index_directory(sdmc,slippi_index,"/3ds/melee/slippi/files/","","",0);
    FSUSER_CloseArchive(sdmc);
    char text[256];snprintf(text,sizeof(text),"Disc index: %u files (%u in folders%s), %u visual files, %u Slippi files; %u ms\n",disc_index_count,foldered,
        duplicates?"; old flat copies also present, delete them for faster loading":"",visual_index_count,slippi_index_count,(unsigned)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000)));
    extern void mp_native_log(const char*);mp_native_log(text);
}
/* Slippi boot prefetch: the index must exist before files are looked up,
 * or files kept in subfolders are not found. Later init reuses it. */
void mp_native_files_index(void){index_files();}
#else
void mp_native_files_index(void){}
#endif
/* Slippi's patched menu files (MnMaAll + SdMenu) are on the SD card. */
#ifdef __3DS__
int mp_native_slippi_menu_files(void){return slippi_index_count&&index_find(slippi_index,"MnMaAll.usd")&&index_find(slippi_index,"SdMenu.usd");}
int mp_native_slippi_has_file(const char* name){return slippi_index_count&&index_find(slippi_index,name)!=NULL;}
#else
int mp_native_slippi_menu_files(void){return 0;}
int mp_native_slippi_has_file(const char* name){(void)name;return 0;}
#endif
/* Disc files found at startup (a complete US v1.02 extraction has 1209). */
unsigned mp_native_disc_file_count(void){return disc_index_count;}
static int path_for(char *path,size_t capacity,const char *name){
    if(!name||!*name||*name=='/'||strstr(name,"..")||strchr(name,':')||strchr(name,'\\'))return 0;
    int n=snprintf(path,capacity,"%s%s",MP_DISC_ROOT,name);return n>=0&&(size_t)n<capacity;
}
/* The SD path of a disc file, in either layout (for native readers). */
int mp_native_file_path(const char *name,char *path,size_t capacity){
    if(!path_for(path,capacity,name))return 0;
    const IndexEntry *e=disc_index_count?index_find(disc_index,name):NULL;
    if(!e)return 1;
    int n=snprintf(path,capacity,"%s%s",MP_DISC_ROOT,e->path);return n>=0&&(size_t)n<capacity;
}
/* The disc file of the latest lookup, when the SD card lacks it, for the
 * stop screen (an incomplete copy, or files from another version's package).
 * Melee probes for an optional develop.ini at boot; any later successful
 * lookup clears the name, so only a failure that stops the game shows it. */
char mp_native_missing_file[64];
static int file_missing(const char *name){
    snprintf(mp_native_missing_file,sizeof(mp_native_missing_file),"%s",name?name:"");
    return -1;
}
int mp_native_file_id(const char *name){
    char path[512];if(!path_for(path,sizeof(path),name))return file_missing(name);
    mp_native_missing_file[0]=0;
    for(unsigned i=0;i<file_count;++i)if(!strcmp(name,files[i].name))return i;
    if(file_count==MAX_FILES)return -1;
    {const IndexEntry *s=slippi_index_count?index_find(slippi_index,name):NULL;
     int written=s?snprintf(path,sizeof(path),"sdmc:/3ds/melee/slippi/files/%s",name):-1;
     if(s&&written>0&&(size_t)written<sizeof(path)){
        char *copy=strdup(name);if(!copy)return -1;
        char *resolved=strdup(path);if(!resolved){free(copy);return -1;}
        unsigned id=file_count;files[id].name=copy;files[id].path=resolved;files[id].size=s->size;
        __atomic_store_n(&file_count,id+1,__ATOMIC_RELEASE);return id;}}
    /* A name missing from the index (for example, different letter case on
     * the FAT volume) takes the original open-based path below. */
    const IndexEntry *e=disc_index_count?index_find(disc_index,name):NULL;
    if(e){
        unsigned n=e->size;
        if(!mp_native_file_path(name,path,sizeof(path)))return -1;
        /* Same audited visual replacements as below, recognised by size. */
        unsigned visual_size=!strcmp(name,"GrIz.dat")?492294:!strcmp(name,"GrSt.dat")?248938:!strcmp(name,"GrOp.dat")?118854:!strcmp(name,"GrNBa.dat")?67235:!strcmp(name,"GrNLa.dat")?660692:0;
        const IndexEntry *v=visual_size?index_find(visual_index,name):NULL;
        if(v&&v->size==visual_size){
            char visual[512];int written=snprintf(visual,sizeof(visual),"%s%s",MP_VISUAL_ROOT,name);
            FILE *alternate=written>=0&&(size_t)written<sizeof(visual)?fopen(visual,"rb"):NULL;
            if(alternate){
                unsigned char header[4]={0};int valid=fread(header,1,4,alternate)==4;
                unsigned declared=((unsigned)header[0]<<24)|((unsigned)header[1]<<16)|((unsigned)header[2]<<8)|header[3];
                fclose(alternate);
                if(valid&&declared==visual_size){strcpy(path,visual);n=visual_size;}
            }
        }
        char *copy=strdup(name);if(!copy)return -1;
        char *resolved=strdup(path);if(!resolved){free(copy);return -1;}
        unsigned id=file_count;files[id].name=copy;files[id].path=resolved;files[id].size=n;
        __atomic_store_n(&file_count,id+1,__ATOMIC_RELEASE);return id;
    }
    FILE *stream=fopen(path,"rb");if(!stream)return file_missing(name);
    /* Optional, audited visual archive. Keep the original disc files intact.
     * Resolve once per file ID so lengths and reopened streams always agree. */
    unsigned visual_size=!strcmp(name,"GrIz.dat")?492294:!strcmp(name,"GrSt.dat")?248938:!strcmp(name,"GrOp.dat")?118854:!strcmp(name,"GrNBa.dat")?67235:!strcmp(name,"GrNLa.dat")?660692:0;
    if(visual_size){
        char visual[512];int written=snprintf(visual,sizeof(visual),"%s%s",MP_VISUAL_ROOT,name);
        if(written>=0&&(size_t)written<sizeof(visual)){
            FILE *alternate=fopen(visual,"rb");
            if(alternate){
                unsigned char header[4]={0};int valid=fread(header,1,4,alternate)==4;
                unsigned declared=((unsigned)header[0]<<24)|((unsigned)header[1]<<16)|((unsigned)header[2]<<8)|header[3];
                valid=valid&&!fseek(alternate,0,SEEK_END)&&ftell(alternate)==(long)visual_size&&declared==visual_size;
                if(valid){fclose(stream);stream=alternate;strcpy(path,visual);}else fclose(alternate);
            }
        }
    }
    int error=fseek(stream,0,SEEK_END);long n=ftell(stream);fclose(stream);
    if(error||n<0||(unsigned long)n>UINT_MAX)return -1;
    char *copy=strdup(name);if(!copy)return -1;
    char *resolved=strdup(path);if(!resolved){free(copy);return -1;}
    unsigned id=file_count;files[id].name=copy;files[id].path=resolved;files[id].size=n;
    __atomic_store_n(&file_count,id+1,__ATOMIC_RELEASE);return id;
}
unsigned mp_native_file_size(int id){return id>=0&&(unsigned)id<file_count?files[id].size:0;}
static Reader *open_reader(int id){
    Reader *slot=NULL;
    for(unsigned i=0;i<READERS;++i){Reader *r=&readers[i];
        if(r->stream&&r->id==id){r->stamp=++reader_clock;return r;}
        if(!slot||!r->stream||(slot->stream&&r->stamp<slot->stamp))slot=r;
    }
    if(slot->stream){fclose(slot->stream);slot->stream=NULL;}
#ifdef __3DS__
    u64 start=svcGetSystemTick();
#endif
    FILE *stream=fopen(files[id].path,"rb");
#ifdef __3DS__
    unsigned open_us=(unsigned)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000000));
    __atomic_fetch_add(&mp_file_sd_opens,1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&mp_file_sd_open_ticks,open_us,__ATOMIC_RELAXED);
    if(mp_file_log_opens){
        extern void mp_native_log(const char*);char text[160];
        snprintf(text,sizeof(text),"Load: open %s (%u bytes) %u.%u ms\n",files[id].name,files[id].size,open_us/1000,open_us/100%10);
        mp_native_log(text);
    }
#endif
    if(!stream)return NULL;
    /* Keep a bounded set of open, buffered streams. HPS playback revisits
     * the same file; reopening it for each DVD block adds avoidable IPC and
     * can starve the cooperative mixer. Buffer ownership ends after fclose. */
    if(setvbuf(stream,(char*)slot->buffer,_IOFBF,sizeof(slot->buffer))){fclose(stream);return NULL;}
    slot->stream=stream;slot->id=id;slot->position=0;slot->stamp=++reader_clock;return slot;
}
static int native_file_read(int id,void *dst,unsigned n,unsigned offset){
    if(id<0||(unsigned)id>=__atomic_load_n(&file_count,__ATOMIC_ACQUIRE)||offset>files[id].size||n>INT_MAX||(!dst&&n))return -1;
    if(!n)return 0;
    unsigned real=n;if(real>files[id].size-offset)real=files[id].size-offset;
    for(unsigned i=0;i<menu_cache_count;++i)if(menu_cache[i].id==id){
        memcpy(dst,menu_cache[i].data+offset,real);
        if(real<n)memset((unsigned char*)dst+real,0,n-real);
        __atomic_fetch_add(&mp_file_cache_hits,1,__ATOMIC_RELAXED);__atomic_fetch_add(&mp_file_cache_read_bytes,real,__ATOMIC_RELAXED);return n;
    }
    {const unsigned char *cached=prefetched(id);
     if(cached){memcpy(dst,cached+offset,real);if(real<n)memset((unsigned char*)dst+real,0,n-real);
        __atomic_fetch_add(&mp_prefetch_hits,1,__ATOMIC_RELAXED);__atomic_fetch_add(&mp_prefetch_hit_bytes,real,__ATOMIC_RELAXED);return n;}}
    Reader *r=open_reader(id);if(!r)return -1;
    if(r->position!=offset&&fseek(r->stream,offset,SEEK_SET))return -1;
    r->position=offset;
    size_t got=fread(dst,1,real,r->stream);r->position+=got;
    __atomic_fetch_add(&mp_file_sd_reads,1,__ATOMIC_RELAXED);__atomic_fetch_add(&mp_file_sd_bytes,got,__ATOMIC_RELAXED);
    if(got!=real){fclose(r->stream);r->stream=NULL;return -1;}
    if(real<n)memset((unsigned char*)dst+real,0,n-real);return n;
}
static int locked_file_read(int id,void*dst,unsigned n,unsigned offset){
#ifdef __3DS__
    if(io_thread)LightLock_Lock(&io_lock);
#endif
    int result=native_file_read(id,dst,n,offset);
#ifdef __3DS__
    if(io_thread)LightLock_Unlock(&io_lock);
#endif
    return result;
}
static const char*file_read_kind="sync";
static void file_read_finished(int id,void*dst,unsigned n,unsigned offset,int result,unsigned ticks){
    /* Texture/geometry invalidation for the read is ordered with GX by the
     * engine's DVD layer (mp_gx_cache_range); this may not be a GX thread. */
    (void)dst;(void)result;
#ifdef MP_SMOKE_TEST
    extern void mp_native_log(const char*);
    if(mp_file_trace){char text[200];snprintf(text,sizeof(text),"File read %s %s offset=%u bytes=%u ticks=%u\n",file_read_kind,id>=0&&(unsigned)id<file_count?files[id].name:"invalid",offset,n,ticks);mp_native_log(text);}
#else
    (void)id;(void)dst;(void)n;(void)offset;(void)result;(void)ticks;
#endif
}
int mp_native_file_read(int id,void *dst,unsigned n,unsigned offset){
    unsigned previous=mp_log_phase(MP_LOG_DISC_READ);
    unsigned ticks=0;
#ifdef MP_SMOKE_TEST
    extern unsigned mp_native_ticks(void);
    unsigned start=mp_native_ticks();
#endif
    int result;MP_AUDIO_TRACE(1,result=locked_file_read(id,dst,n,offset));
#ifdef MP_SMOKE_TEST
    ticks=mp_native_ticks()-start;
#endif
    file_read_finished(id,dst,n,offset,result,ticks);
    mp_log_phase(previous);return result;
}
/* Read a whole file into the RAM cache now (main thread, before the engine
 * and the network start). 1 cached, 0 skipped (budget, missing), -1 error. */
int mp_native_file_cache(const char *name){
    int id=mp_native_file_id(name);
    if(id<0)return 0;
    if(prefetched(id))return 1;
    unsigned size=files[id].size;
    if(!size||prefetch_bytes+size>PREFETCH_BUDGET||prefetch_count>=PREFETCH_MAX)return 0;
    unsigned char *data=malloc(size);if(!data)return -1;
    if(native_file_read(id,data,size,0)!=(int)size){free(data);return -1;}
    prefetch[prefetch_count].id=id;prefetch[prefetch_count].data=data;
    prefetch_bytes+=size;++mp_prefetch_files;
    __atomic_store_n(&prefetch_count,prefetch_count+1,__ATOMIC_RELEASE);
    return 1;
}
unsigned mp_native_prefetch_bytes(void){return prefetch_bytes;}
#ifdef __3DS__
static void file_worker(void*unused){
    (void)unused;
    for(;;){
        LightEvent_Wait(&io_wake);
        if(__atomic_load_n(&io_stop,__ATOMIC_ACQUIRE))return;
        if(__atomic_load_n(&io_state,__ATOMIC_ACQUIRE)!=1)continue;
        unsigned ticks=0;
#ifdef MP_SMOKE_TEST
        extern unsigned mp_native_ticks(void);unsigned start=mp_native_ticks();
        unsigned delay=mp_test_disc_delay_ms;if(delay>200)delay=200;
        if(delay)svcSleepThread((s64)delay*1000000);
#endif
        io_request.result=locked_file_read(io_request.id,io_request.dst,io_request.n,io_request.offset);
#ifdef MP_SMOKE_TEST
        ticks=mp_native_ticks()-start;
#endif
        io_request.ticks=ticks;__atomic_store_n(&io_state,2,__ATOMIC_RELEASE);
    }
}
void mp_native_files_init(void){
    index_files();
    LightEvent_Init(&io_wake,RESET_ONESHOT);LightLock_Init(&io_lock);
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    io_thread=threadCreate(file_worker,NULL,32768,priority>0x18?priority-1:priority,-2,false);
    extern void mp_native_log(const char*);mp_native_log(io_thread?"Background SD reader enabled\n":"Background SD reader unavailable; synchronous fallback\n");
}
int mp_native_file_read_async_begin(int id,void*dst,unsigned n,unsigned offset){
#ifdef MP_SMOKE_TEST
    if(mp_async_read_disable)return 0;
#endif
    if(!io_thread||__atomic_load_n(&io_state,__ATOMIC_ACQUIRE))return 0;
    /* Boot-cached menu reads remain immediate; no thread handoff is needed. */
    io_request.id=id;io_request.dst=dst;io_request.n=n;io_request.offset=offset;io_request.ticks=0;
    for(unsigned i=0;i<menu_cache_count;++i)if(menu_cache[i].id==id){
        io_request.result=native_file_read(id,dst,n,offset);__atomic_store_n(&io_state,2,__ATOMIC_RELEASE);return 1;}
    ++mp_async_reads;__atomic_store_n(&io_state,1,__ATOMIC_RELEASE);LightEvent_Signal(&io_wake);return 1;
}
int mp_native_file_read_async_poll(void){
    if(__atomic_load_n(&io_state,__ATOMIC_ACQUIRE)!=2){++mp_async_busy_polls;return -2147483647;}
    int result=io_request.result;
    file_read_kind="async";
    file_read_finished(io_request.id,io_request.dst,io_request.n,io_request.offset,result,io_request.ticks);
    file_read_kind="sync";
    __atomic_store_n(&io_state,0,__ATOMIC_RELEASE);return result;
}
#ifdef MP_SMOKE_TEST
unsigned mp_async_file_checks;
int mp_native_async_self_test(void){
    if(__atomic_load_n(&io_state,__ATOMIC_ACQUIRE))return 0;
    extern void mp_native_panic(const char*);
    u8*expected=malloc(90032),*actual=malloc(90032);u8 interleaved[32];
    if(!expected||!actual)mp_native_panic("Async file fixture allocation failed");
    int ids[2]={mp_native_file_id("PlPeAJ.dat"),mp_native_file_id("MnSlChr.usd")};
    if(ids[0]<0||ids[1]<0)mp_native_panic("Async file fixture assets missing");
    unsigned old=mp_async_read_disable;mp_async_read_disable=0;
    for(unsigned i=0;i<64;++i){
        int id=ids[i&1];unsigned size=mp_native_file_size(id),lengths[]={0,1,17,16384,65539,89999,32,19};
        unsigned n=lengths[i%8],offset=(i*37013u)%(size-n+1);
        if(i%16==14)offset=size;if(i%16==15)offset=size+1;
        if(i==60)id=-1;
        memset(expected,0xa5,90032);memset(actual,0xa5,90032);
        int want=mp_native_file_read(id,expected+16,n,offset);
        if(!mp_native_file_read_async_begin(id,actual+16,n,offset))mp_native_panic("Async file fixture request rejected");
        /* A synchronous read of another stream may overlap the worker. */
        if(mp_native_file_read(ids[(i&1)^1],interleaved,sizeof(interleaved),13)!=32)mp_native_panic("Interleaved file read failed");
        int got;while((got=mp_native_file_read_async_poll())==-2147483647)svcSleepThread(1000000);
        if(got!=want||memcmp(expected,actual,90032))mp_native_panic("Async file result/data differs from synchronous read");
        ++mp_async_file_checks;
    }
    mp_async_read_disable=old;free(expected);free(actual);return 1;
}
#endif
#endif
unsigned mp_native_cache_menus(unsigned budget){
    /* Cache immutable source bytes before engine boot/audio. The engine
     * still gets a private writable copy for archive relocation. Reserve
     * ordinary heap headroom at the caller and fall back to SD on failure. */
    static const char *names[MENU_FILES]={"GmTtAll.usd","MnMaAll.usd","MnSlChr.usd","MnSlMap.usd","MnExtAll.usd",
        "audio/us/nr_title.ssm","audio/us/nr_select.ssm","audio/us/nr_name.ssm","audio/us/nr_vs.ssm"};
    if(budget>MENU_CACHE_MAX)budget=MENU_CACHE_MAX;
    for(unsigned i=0;i<MENU_FILES&&menu_cache_count<MENU_FILES;++i){
        int id=mp_native_file_id(names[i]);if(id<0)continue;
        unsigned size=files[id].size;int cached=0;
        for(unsigned j=0;j<menu_cache_count;++j)if(menu_cache[j].id==id)cached=1;
        if(cached||!size||menu_cache_bytes>budget||size>budget-menu_cache_bytes)continue;
        unsigned char *data=malloc(size);if(!data)continue;
        if(native_file_read(id,data,size,0)!=(int)size){free(data);continue;}
        menu_cache[menu_cache_count].id=id;menu_cache[menu_cache_count++].data=data;menu_cache_bytes+=size;
    }
    return menu_cache_bytes;
}
void mp_native_files_exit(void){
#ifdef __3DS__
    if(io_thread){__atomic_store_n(&io_stop,1,__ATOMIC_RELEASE);LightEvent_Signal(&io_wake);threadJoin(io_thread,U64_MAX);threadFree(io_thread);io_thread=NULL;}
#endif
    for(unsigned i=0;i<menu_cache_count;++i)free(menu_cache[i].data);
    menu_cache_count=menu_cache_bytes=0;
    for(unsigned i=0;i<READERS;++i)if(readers[i].stream){fclose(readers[i].stream);readers[i].stream=NULL;}
    for(unsigned i=0;i<file_count;++i){free(files[i].name);free(files[i].path);}
    file_count=reader_clock=0;
    for(unsigned b=0;b<INDEX_BUCKETS;++b){
        for(IndexEntry *e=disc_index[b],*next;e;e=next){next=e->next;free(e);}
        for(IndexEntry *e=visual_index[b],*next;e;e=next){next=e->next;free(e);}
        disc_index[b]=visual_index[b]=NULL;
    }
    disc_index_count=visual_index_count=0;
}
