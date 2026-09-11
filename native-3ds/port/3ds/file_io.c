#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
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
#ifdef MP_SMOKE_TEST
volatile unsigned mp_file_trace;
#endif
static int path_for(char *path,size_t capacity,const char *name){
    if(!name||!*name||*name=='/'||strstr(name,"..")||strchr(name,':')||strchr(name,'\\'))return 0;
    int n=snprintf(path,capacity,"%s%s",MP_DISC_ROOT,name);return n>=0&&(size_t)n<capacity;
}
int mp_native_file_id(const char *name){
    char path[512];if(!path_for(path,sizeof(path),name))return -1;
    for(unsigned i=0;i<file_count;++i)if(!strcmp(name,files[i].name))return i;
    if(file_count==MAX_FILES)return -1;
    FILE *stream=fopen(path,"rb");if(!stream)return -1;
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
    FILE *stream=fopen(files[id].path,"rb");if(!stream)return NULL;
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
static void file_read_finished(int id,void*dst,unsigned n,unsigned offset,int result,unsigned ticks){
#ifdef __3DS__
    extern void mp_native_texture_dirty(unsigned,unsigned,unsigned);
    if(result>0)mp_native_texture_dirty((unsigned)dst,result,1);
#endif
#ifdef MP_SMOKE_TEST
    extern void mp_native_log(const char*);
    if(mp_file_trace){char text[200];snprintf(text,sizeof(text),"File read %s offset=%u bytes=%u ticks=%u\n",id>=0&&(unsigned)id<file_count?files[id].name:"invalid",offset,n,ticks);mp_native_log(text);}
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
    file_read_finished(io_request.id,io_request.dst,io_request.n,io_request.offset,result,io_request.ticks);
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
}
