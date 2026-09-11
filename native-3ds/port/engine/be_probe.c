#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/random.h>
#include <melee/ft/types.h>

void *mp_be_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d=dst; const unsigned char *s=src;
    while(n--) *d++=*s++;
    return dst;
}
void *mp_be_memset(void *dst, int c, size_t n)
{
    unsigned char *d=dst; while(n--) *d++=(unsigned char)c; return dst;
}
int mp_be_strcmp(const char *a,const char *b)
{
    while(*a && *a==*b) { ++a; ++b; }
    return (unsigned char)*a-(unsigned char)*b;
}
void mp_be_report(const char *fmt, ...) { (void)fmt; }

int mp_be_probe_impl(void *bytes, unsigned size, unsigned *results)
{
    HSD_Archive archive;
    if(HSD_ArchiveParse(&archive,bytes,size)) return -1;
    struct ftData *data = HSD_ArchiveGetPublicAddress(&archive,"ftDataFox");
    if(!data || !data->x0) return -2;
    results[0] = 0x12345678;
    results[1] = archive.header.nb_public;
    results[2] = HSD_Rand();
    results[3] = *(unsigned *)data->x0;
    results[4] = (unsigned)data->x30->count;
    results[5] = data->x1C[0]->x0;
    return 0;
}
