#include <3ds.h>

/* libctru's linear-heap allocator keeps an unsynchronized free list. The
 * engine thread (audio start-up), the GX translator and the renderer worker
 * (Citro3D textures and buffers) may allocate at the same time, so every
 * entry point is serialized here through the linker's --wrap option. */
/* Recursive: lld also wraps linearAlloc's own call to linearMemAlign. */
static RecursiveLock linear_lock={1,0,0};
extern void *__real_linearAlloc(size_t),*__real_linearMemAlign(size_t,size_t),*__real_linearRealloc(void*,size_t);
extern void __real_linearFree(void*);
extern u32 __real_linearSpaceFree(void);
void *__wrap_linearAlloc(size_t size){RecursiveLock_Lock(&linear_lock);void*p=__real_linearAlloc(size);RecursiveLock_Unlock(&linear_lock);return p;}
void *__wrap_linearMemAlign(size_t size,size_t alignment){RecursiveLock_Lock(&linear_lock);void*p=__real_linearMemAlign(size,alignment);RecursiveLock_Unlock(&linear_lock);return p;}
void *__wrap_linearRealloc(void*mem,size_t size){RecursiveLock_Lock(&linear_lock);void*p=__real_linearRealloc(mem,size);RecursiveLock_Unlock(&linear_lock);return p;}
void __wrap_linearFree(void*mem){RecursiveLock_Lock(&linear_lock);__real_linearFree(mem);RecursiveLock_Unlock(&linear_lock);}
u32 __wrap_linearSpaceFree(void){RecursiveLock_Lock(&linear_lock);u32 n=__real_linearSpaceFree();RecursiveLock_Unlock(&linear_lock);return n;}
