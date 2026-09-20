#ifndef MP_GEOMETRY_DIRTY_H
#define MP_GEOMETRY_DIRTY_H
#include <stdint.h>
/* Exact visibility ranges since a source's last byte comparison. Missing
 * journal history falls back to comparison. Frame/camera checks remain. */
#define MP_GEOMETRY_DIRTY_SLOTS 64
typedef struct {uintptr_t address;unsigned size;} MPGeometryDirtyRange;
typedef struct {unsigned revision;MPGeometryDirtyRange ranges[MP_GEOMETRY_DIRTY_SLOTS];} MPGeometryDirty;
static int mp_geometry_dirty_add(MPGeometryDirty*t,const void*address,unsigned size){
    int wrapped=0;if(!++t->revision){t->revision=1;wrapped=1;}
    t->ranges[t->revision&(MP_GEOMETRY_DIRTY_SLOTS-1)]=(MPGeometryDirtyRange){(uintptr_t)address,size};
    return wrapped;
}
static int mp_geometry_dirty_unchanged(const MPGeometryDirty*t,unsigned revision,const void*address,unsigned size){
    unsigned distance=t->revision-revision;
    if(distance>MP_GEOMETRY_DIRTY_SLOTS)return 0;
    uintptr_t a=(uintptr_t)address;if(size>UINTPTR_MAX-a)return 0;
    for(unsigned i=0;i<distance;++i){const MPGeometryDirtyRange*r=&t->ranges[(t->revision-i)&(MP_GEOMETRY_DIRTY_SLOTS-1)];
        if(r->size>UINTPTR_MAX-r->address)return 0;
        if(a<r->address+r->size&&r->address<a+size)return 0;
    }
    return 1;
}
#endif
