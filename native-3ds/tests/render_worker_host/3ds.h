#ifndef MP_WORKER_HOST_3DS_H
#define MP_WORKER_HOST_3DS_H
/* Windows synchronization adapter for the real render_worker.c transport.
 * This cannot establish 3DS core permissions or GPU-driver thread safety. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef struct TestThread *Thread;
typedef struct { void *event; } LightEvent;
#define RESET_ONESHOT 1
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
u64 testTickFrequency(void);
#define SYSCLOCK_ARM11 testTickFrequency()
void LightEvent_Init(LightEvent*, int);
void LightEvent_Wait(LightEvent*);
void LightEvent_Signal(LightEvent*);
Thread threadCreate(void(*)(void*),void*,size_t,int,int,bool);
void threadJoin(Thread,u64);
void threadFree(Thread);
Thread threadGetCurrent(void);
void threadExit(int) __attribute__((noreturn));
u64 svcGetSystemTick(void);
s32 svcGetProcessorID(void);
void svcGetThreadPriority(s32*,int);
#endif
