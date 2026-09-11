#include "melee_port/archive.h"
#include <string.h>

uint32_t mp_be32(const void *v)
{
    const uint8_t *p = v;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

uint16_t mp_be16(const void *v)
{
    const uint8_t *p = v;
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static int table(size_t *cursor, uint32_t n, size_t stride, size_t size)
{
    if (*cursor > size || n > (size - *cursor) / stride) return -1;
    *cursor += (size_t)n * stride;
    return 0;
}

static int symbol(const MpArchive *a, size_t base, uint32_t i,
                  MpArchiveSymbol *out)
{
    const uint8_t *p = a->bytes + base + (size_t)i * 8;
    uint32_t off = mp_be32(p), name = mp_be32(p + 4);
    if (name >= a->size - a->string_base) return -1;
    const char *s = (const char *)a->bytes + a->string_base + name;
    if (!memchr(s, 0, a->size - a->string_base - name)) return -1;
    if (out) { out->offset = off; out->name = s; }
    return 0;
}

int mp_archive_open(MpArchive *out, const void *bytes, size_t size)
{
    MpArchive a = {0};
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!bytes || size < 32 || size > UINT32_MAX) return -1;
    a.bytes = bytes;
    a.size = size;
    if (mp_be32(a.bytes) != size) return -1;
    a.data_size = mp_be32(a.bytes + 4);
    a.reloc_count = mp_be32(a.bytes + 8);
    a.public_count = mp_be32(a.bytes + 12);
    a.extern_count = mp_be32(a.bytes + 16);
    size_t cursor = 32;
    if (table(&cursor, a.data_size, 1, size)) return -1;
    a.reloc_base = cursor;
    if (table(&cursor, a.reloc_count, 4, size)) return -1;
    a.public_base = cursor;
    if (table(&cursor, a.public_count, 8, size)) return -1;
    a.extern_base = cursor;
    if (table(&cursor, a.extern_count, 8, size)) return -1;
    a.string_base = cursor;
    for (uint32_t i = 0; i < a.reloc_count; ++i) {
        uint32_t field = mp_be32(a.bytes + a.reloc_base + (size_t)i * 4), dst;
        if ((field & 3) || mp_archive_u32(&a, field, &dst) || dst >= a.data_size)
            return -1;
    }
    for (uint32_t i = 0; i < a.public_count; ++i) {
        MpArchiveSymbol s;
        if (symbol(&a, a.public_base, i, &s) || s.offset >= a.data_size)
            return -1;
    }
    for (uint32_t i = 0; i < a.extern_count; ++i) {
        MpArchiveSymbol s;
        if (symbol(&a, a.extern_base, i, &s)) return -1;
        /* Externals are linked lists of pointer-field offsets, terminated by
         * 0xffffffff. Bound the walk so corrupt cycles cannot hang the loader. */
        uint32_t off = s.offset, hops = 0;
        while (off != UINT32_MAX) {
            if (++hops > a.data_size / 4 || (off & 3) ||
                mp_archive_u32(&a, off, &off)) return -1;
        }
    }
    *out = a;
    return 0;
}

int mp_archive_public(const MpArchive *a, uint32_t index, MpArchiveSymbol *out)
{
    if (!a || !out || index >= a->public_count) return -1;
    return symbol(a, a->public_base, index, out);
}

int mp_archive_find(const MpArchive *a, const char *name, uint32_t *offset)
{
    if (!a || !name || !offset) return -1;
    for (uint32_t i = 0; i < a->public_count; ++i) {
        MpArchiveSymbol s;
        if (mp_archive_public(a, i, &s)) return -1;
        if (!strcmp(name, s.name)) { *offset = s.offset; return 0; }
    }
    return -1;
}

int mp_archive_u32(const MpArchive *a, uint32_t offset, uint32_t *out)
{
    if (!a || !out || offset > a->data_size || a->data_size - offset < 4)
        return -1;
    *out = mp_be32(a->bytes + 32 + offset);
    return 0;
}

int mp_archive_f32(const MpArchive *a, uint32_t offset, float *out)
{
    uint32_t bits;
    if (!out || mp_archive_u32(a, offset, &bits)) return -1;
    _Static_assert(sizeof(float) == 4, "IEEE binary32 required");
    memcpy(out, &bits, sizeof(bits));
    return 0;
}

int mp_archive_pointer(const MpArchive *a, uint32_t field, uint32_t *target)
{
    if (!a || !target) return -1;
    for (uint32_t i = 0; i < a->reloc_count; ++i)
        if (mp_be32(a->bytes + a->reloc_base + (size_t)i * 4) == field)
            return mp_archive_u32(a, field, target);
    return -1;
}
