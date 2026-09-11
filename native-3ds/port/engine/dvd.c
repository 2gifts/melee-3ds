#include <dolphin/dvd.h>
#include <sysdolphin/baselib/debug.h>
#include <string.h>
#include "native.h"
typedef struct{DVDFileInfo*file;int id;}Open;
static Open handles[128];
static struct{DVDFileInfo*f;void*dst;s32 length,offset;DVDCallback cb;}requests[128];
static unsigned head,tail;
static unsigned asynchronous_read;
static DVDDiskID disc={.gameName={'G','A','L','E'},.company={'0','1'},.gameVersion=2};
void DVDInit(void){}
s32 DVDConvertPathToEntrynum(const char*p){while(*p=='/')++p;return mp_platform_file_id(p);}
BOOL DVDFastOpen(s32 id,DVDFileInfo*f){if(id<0)return 0;int slot=-1;for(int i=0;i<128;++i){if(handles[i].file==f){slot=i;break;}if(!handles[i].file&&slot<0)slot=i;}if(slot<0)HSD_Panic(__FILE__,__LINE__,"DVD file handle table exhausted");memset(f,0,sizeof(*f));f->length=mp_platform_file_size(id);f->startAddr=id;handles[slot]=(Open){f,id};return 1;}
BOOL DVDOpen(char*name,DVDFileInfo*f){return DVDFastOpen(DVDConvertPathToEntrynum(name),f);}
BOOL DVDClose(DVDFileInfo*f){for(int i=0;i<128;++i)if(handles[i].file==f){handles[i].file=NULL;return 1;}return 0;}
static int file_id(DVDFileInfo*f){for(int i=0;i<128;++i)if(handles[i].file==f)return handles[i].id;return -1;}
long DVDReadPrio(DVDFileInfo*f,void*dst,long n,long off,long priority){(void)priority;int id=file_id(f);if(id<0||off<0||n<0)return-1;int got=mp_platform_file_read(id,dst,n,off);f->cb.state=got<0?-1:0;f->cb.transferredSize=got<0?0:got;return got;}
BOOL DVDReadAsyncPrio(DVDFileInfo*f,void*dst,s32 n,s32 off,DVDCallback cb,s32 priority){(void)priority;if(tail-head>=128)return 0;unsigned i=tail++&127;requests[i].f=f;requests[i].dst=dst;requests[i].length=n;requests[i].offset=off;requests[i].cb=cb;f->cb.state=1;return 1;}
int mp_dvd_pending(void){return head!=tail;}
void mp_dvd_pump(void){
    if(head==tail)return;unsigned i=head&127;DVDFileInfo*f=requests[i].f;
    if(!asynchronous_read)asynchronous_read=mp_platform_file_read_async_begin(file_id(f),requests[i].dst,requests[i].length,requests[i].offset);
    int result=asynchronous_read?mp_platform_file_read_async_poll():DVDReadPrio(f,requests[i].dst,requests[i].length,requests[i].offset,2);
    if(result==MP_FILE_READ_BUSY)return;
    DVDCallback cb=requests[i].cb;asynchronous_read=0;++head;
    f->cb.state=result<0?-1:0;f->cb.transferredSize=result<0?0:result;
    if(cb)cb(result,f);
}
long DVDGetFileInfoStatus(DVDFileInfo*f){mp_dvd_pump();return f->cb.state;}
long DVDGetCommandBlockStatus(DVDCommandBlock*b){mp_dvd_pump();return b->state;}
long DVDGetDriveStatus(void){extern void mp_engine_poll(void);mp_engine_poll();return 0;}
BOOL DVDCheckDisk(void){return 1;}
DVDDiskID*DVDGetCurrentDiskID(void){return &disc;}
