#include <stdint.h>
#include <stdbool.h>
typedef int64_t s64;
typedef int32_t Result;
#define R_SUCCEEDED(r) ((r)>=0)
typedef enum {APTHOOK_ONSUSPEND,APTHOOK_ONRESTORE,APTHOOK_ONSLEEP,APTHOOK_ONWAKEUP,APTHOOK_ONEXIT} APT_HookType;
typedef void (*aptHookFn)(APT_HookType,void*);
typedef struct {aptHookFn callback;void*param;} aptHookCookie;
Result svcGetSystemInfo(s64*,int,int);
Result svcKernelSetState(int,...);
void osSetSpeedupEnable(bool);
void aptHook(aptHookCookie*,aptHookFn,void*);
void aptUnhook(aptHookCookie*);
