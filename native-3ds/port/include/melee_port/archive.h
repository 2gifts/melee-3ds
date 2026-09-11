#ifndef MELEE_PORT_ARCHIVE_H
#define MELEE_PORT_ARCHIVE_H
#include <stddef.h>
#include <stdint.h>

/* Read-only view of a big-endian HSD DAT. Values are accessed explicitly;
 * this deliberately does NOT relocate mixed-endian data into native structs. */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t data_size, reloc_count, public_count, extern_count;
    size_t reloc_base, public_base, extern_base, string_base;
} MpArchive;

typedef struct { uint32_t offset; const char *name; } MpArchiveSymbol;

int mp_archive_open(MpArchive *out, const void *bytes, size_t size);
int mp_archive_public(const MpArchive *a, uint32_t index, MpArchiveSymbol *out);
int mp_archive_find(const MpArchive *a, const char *name, uint32_t *offset);
int mp_archive_u32(const MpArchive *a, uint32_t offset, uint32_t *out);
int mp_archive_f32(const MpArchive *a, uint32_t offset, float *out);
/* Only relocation-listed fields are pointers; an offset of zero can be valid. */
int mp_archive_pointer(const MpArchive *a, uint32_t field, uint32_t *target);
uint32_t mp_be32(const void *p);
uint16_t mp_be16(const void *p);
#endif
