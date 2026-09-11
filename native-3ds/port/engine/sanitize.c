#include <sysdolphin/baselib/debug.h>
#include <dolphin/os.h>
#include <stdint.h>

struct TypeMismatch {struct {const char*file;uint32_t line,column;}source;void*type;unsigned char alignment,kind;};
__attribute__((no_sanitize("null")))
void __ubsan_handle_type_mismatch_v1(struct TypeMismatch*info,uintptr_t address){
    OSReport("Invalid engine access at %s:%u:%u address=%x kind=%u\n",info->source.file,info->source.line,info->source.column,(unsigned)address,info->kind);
    HSD_Panic(info->source.file,info->source.line,"Null access in original engine");
}
