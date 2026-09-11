#include <dolphin/card.h>
#include <dolphin/mcc.h>
/* The 3DS has no GameCube memory card or development-host interface.
 * Return the SDK absence/failure result. No operation is accepted. */
void CARDInit(void){}
s32 CARDGetResultCode(s32 chan){return CARD_RESULT_NOCARD;}
s32 CARDCheckAsync(s32 chan, CARDCallback callback){return CARD_RESULT_NOCARD;}
s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed){return CARD_RESULT_NOCARD;}
s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName,
                    CARDCallback callback){return CARD_RESULT_NOCARD;}
s32 CARDFormatAsync(s32 chan, CARDCallback callback){return CARD_RESULT_NOCARD;}
long CARDGetEncoding(long chan, unsigned short * encode){return CARD_RESULT_NOCARD;}
long CARDGetMemSize(long chan, unsigned short * size){return CARD_RESULT_NOCARD;}
s32 CARDGetSectorSize(s32 chan, u32 *size){return CARD_RESULT_NOCARD;}
long CARDCheck(long chan){return CARD_RESULT_NOCARD;}
s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback){return CARD_RESULT_NOCARD;}
long CARDCreate(long chan, char * fileName, unsigned long size, struct CARDFileInfo * fileInfo){return CARD_RESULT_NOCARD;}
s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback){return CARD_RESULT_NOCARD;}
long CARDFastDelete(long chan, long fileNo){return CARD_RESULT_NOCARD;}
s32 CARDDeleteAsync(s32 chan, char *fileName, CARDCallback callback){return CARD_RESULT_NOCARD;}
s32 CARDDelete(s32 chan, char *fileName){return CARD_RESULT_NOCARD;}
long CARDFormat(long chan){return CARD_RESULT_NOCARD;}
int CARDProbe(long chan){return 0;}
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize){return CARD_RESULT_NOCARD;}
s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback,
                   CARDCallback attachCallback){return CARD_RESULT_NOCARD;}
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback){return CARD_RESULT_NOCARD;}
s32 CARDUnmount(s32 chan){return CARD_RESULT_NOCARD;}
s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo *fileInfo){return CARD_RESULT_NOCARD;}
s32 CARDOpen(s32 chan, char *fileName, CARDFileInfo *fileInfo){return CARD_RESULT_NOCARD;}
s32 CARDClose(CARDFileInfo *fileInfo){return CARD_RESULT_NOCARD;}
long CARDGetXferredBytes(long chan){return 0;}
s32 CARDReadAsync(CARDFileInfo *fileInfo, void *buf, s32 length, s32 offset, CARDCallback callback){return CARD_RESULT_NOCARD;}
long CARDRead(struct CARDFileInfo * fileInfo, void * buf, long length, long offset){return CARD_RESULT_NOCARD;}
s32 CARDCancel(CARDFileInfo *fileInfo){return CARD_RESULT_NOCARD;}
s32 CARDRename(s32 chan, char *oldName, char *newName){return CARD_RESULT_NOCARD;}
s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat *stat){return CARD_RESULT_NOCARD;}
s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat *stat, CARDCallback callback){return CARD_RESULT_NOCARD;}
long CARDSetStatus(long chan, long fileNo, struct CARDStat * stat){return CARD_RESULT_NOCARD;}
long CARDWriteAsync(struct CARDFileInfo * fileInfo, void * buf, long length, long offset, void (* callback)(long, long)){return CARD_RESULT_NOCARD;}
long CARDWrite(struct CARDFileInfo * fileInfo, void * buf, long length, long offset){return CARD_RESULT_NOCARD;}
int FIOInit(enum MCC_EXI exiChannel, enum MCC_CHANNEL chID, u8 blockSize){return 0;}
void FIOExit(void){}
int FIOQuery(void){return 0;}
int FIOFopen(const char *filename, u32 mode){return -1;}
int FIOFclose(int handle){return -1;}
u32 FIOFread(int handle, void *data, u32 size){return 0;}
u32 FIOFwrite(int handle, void * data, u32 size){return 0;}
u32 FIOFseek(int handle, long offset, u32 mode){return 0;}
int FIOFprintf(int handle, const char *format, ...){return 0;}
int FIOFflush(int handle){return 0;}
int FIOFstat(int handle, struct FIO_Stat *stat){return 0;}
int FIOFerror(int handle){return 0;}
int FIOFindFirst(const char *filename, struct FIO_Finddata *finddata){return -1;}
int FIOFindNext(struct FIO_Finddata *finddata){return -1;}
u32 FIOGetAsyncBufferSize(void){return 0;}
int FIOFreadAsync(int handle, void * data, u32 size){return 0;}
int FIOFwriteAsync(int handle, void * data, u32 size){return 0;}
int FIOCheckAsyncDone(u32 * result){return 0;}
int MCCStreamOpen(enum MCC_CHANNEL chID, u8 blockSize){return 0;}
int MCCStreamClose(enum MCC_CHANNEL chID){return 0;}
int MCCStreamWrite(enum MCC_CHANNEL chID, void *data, u32 dataBlockSize){return 0;}
u32 MCCStreamRead(enum MCC_CHANNEL chID, void *data){return 0;}
int MCCInit(enum MCC_EXI exiChannel, u8 timeout, MCC_CBSysEvent callbackSysEvent){return 0;}
void MCCExit(void){}
int MCCPing(void){return 0;}
int MCCEnumDevices(MCC_CBEnumDevices callbackEnumDevices){return 0;}
u8 MCCGetFreeBlocks(enum MCC_MODE mode){return 0;}
u8 MCCGetLastError(void){return 1;}
int MCCGetChannelInfo(enum MCC_CHANNEL chID, MCC_Info *info){return 0;}
int MCCGetConnectionStatus(enum MCC_CHANNEL chID, enum MCC_CONNECT *connect){if(connect)*connect=0;return 0;}
int MCCNotify(enum MCC_CHANNEL chID, u32 notify){return 0;}
u32 MCCSetChannelEventMask(enum MCC_CHANNEL chID, u32 event){return 0;}
int MCCOpen(enum MCC_CHANNEL chID, u8 blockSize, MCC_CBEvent callbackEvent){return 0;}
int MCCClose(enum MCC_CHANNEL chID){return 0;}
int MCCLock(enum MCC_CHANNEL chID){return 0;}
int MCCUnlock(enum MCC_CHANNEL chID){return 0;}
int MCCRead(enum MCC_CHANNEL chID, u32 offset, void *data, long size, enum MCC_SYNC_STATE async){return 0;}
int MCCWrite(enum MCC_CHANNEL chID, u32 offset, void *data, long size, enum MCC_SYNC_STATE async){return 0;}
