#include <dolphin/types.h>
#include <dolphin/mcc.h>
/* The 3DS has no development-host interface. Return the SDK
 * absence/failure result. The memory card is card.c. */
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
