/* Names in the game's text font: see name_text.h. */
#include "name_text.h"
#include "name_glyphs.h"
#include <string.h>

uint32_t name_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = p[0], cp;
    int n, i;
    if (c == 0) return 0;
    if (c < 0x80) { *s += 1; return c; }
    if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
    else { *s += 1; return 0xFFFD; }
    for (i = 1; i <= n; ++i) {
        if ((p[i] & 0xC0) != 0x80) { *s += 1; return 0xFFFD; }   /* cut off (a NUL stops here too: no read past it) */
        cp = cp << 6 | (p[i] & 0x3F);
    }
    /* overlong forms, surrogates and values past U+10FFFF are invalid */
    if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        *s += 1;
        return 0xFFFD;
    }
    *s += n + 1;
    return cp;
}

int name_utf8_put(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | cp >> 6); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | cp >> 12); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int name_clean(const char *in, char *out, int cap, int max_chars)
{
    int n = 0, chars = 0;
    if (cap <= 0) return 0;
    if (!in) in = "";
    while (*in && chars < max_chars) {
        uint32_t cp = name_utf8_next(&in);
        char b[4];
        int k;
        if (cp == 0xFFFD || cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) continue;   /* invalid, control */
        k = name_utf8_put(cp, b);
        if (n + k > cap - 1) break;
        memcpy(out + n, b, (size_t)k);
        n += k;
        ++chars;
    }
    out[n] = 0;
    return n;
}

int name_from_utf16(const uint16_t *in, int units, char *out, int cap, int max_chars)
{
    char tmp[4 * 32 + 1];
    int n = 0, i;
    for (i = 0; i < units && in[i]; ++i) {
        uint32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            continue;                              /* a lone surrogate */
        }
        if (n + 4 >= (int)sizeof(tmp)) break;
        n += name_utf8_put(cp, tmp + n);
    }
    tmp[n] = 0;
    return name_clean(tmp, out, cap, max_chars);
}

/* ASCII as the game's SIS text takes it (slippi_ui.c sis() since the beta): letters, digits and a few signs as they
 * are; the other printable signs as their full-width Shift-JIS forms. */
static int ascii_to_game(uint32_t c, char *out)
{
    static const struct { char c; unsigned short sjis; } wide[] = {
        {'!', 0x8149}, {'#', 0x8194}, {'$', 0x8190}, {'%', 0x8193}, {'&', 0x8195}, {'(', 0x8169}, {')', 0x816a},
        {'*', 0x8196}, {'+', 0x817b}, {'/', 0x815e}, {';', 0x8147}, {'<', 0x8183}, {'=', 0x8181}, {'>', 0x8184},
        {'?', 0x8148}, {'@', 0x8197}, {'[', 0x816d}, {'\\', 0x815f}, {']', 0x816e}, {'^', 0x814f}, {'_', 0x8151},
        {'`', 0x814d}, {'{', 0x816f}, {'|', 0x8162}, {'}', 0x8170}, {'~', 0x8160},
    };
    unsigned i;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
        c == ',' || c == '-' || c == ':' || c == '\'' || c == '"') {
        out[0] = (char)c;
        return 1;
    }
    for (i = 0; i < sizeof wide / sizeof wide[0]; i++)
        if ((uint32_t)(unsigned char)wide[i].c == c) {
            out[0] = (char)(wide[i].sjis >> 8);
            out[1] = (char)wide[i].sjis;
            return 2;
        }
    return 0;                                      /* a control character */
}

static int glyph_find(uint32_t cp)
{
    int lo = 0, hi = (int)NAME_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (name_glyphs[mid].cp == cp) return mid;
        if (name_glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static const char *fold_find(uint32_t cp)
{
    int lo = 0, hi = (int)NAME_FOLD_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (name_fold[mid].cp == cp) return name_fold[mid].to;
        if (name_fold[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int name_char_to_game(uint32_t cp, char *out)
{
    const char *f;
    int g;
    if (cp < 0x80) return ascii_to_game(cp, out);
    g = glyph_find(cp);
    if (g >= 0) {
        out[0] = (char)(name_glyphs[g].sjis >> 8);
        out[1] = (char)name_glyphs[g].sjis;
        return 2;
    }
    f = fold_find(cp);
    if (f) {                                      /* ASCII letters or one full-width character, at most 6 bytes */
        int n = 0;
        while (*f && n <= 4) {
            uint32_t c = name_utf8_next(&f);
            g = c < 0x80 ? -1 : glyph_find(c);
            if (c < 0x80) n += ascii_to_game(c, out + n);
            else if (g >= 0) { out[n++] = (char)(name_glyphs[g].sjis >> 8); out[n++] = (char)name_glyphs[g].sjis; }
        }
        return n;
    }
    out[0] = (char)(NAME_TEXT_QUESTION >> 8);
    out[1] = (char)(NAME_TEXT_QUESTION & 0xFF);
    return 2;
}

int name_to_game(const char *utf8, char *out, int len)
{
    int n = 0;
    if (len <= 0) return 0;
    if (utf8) {
        while (*utf8) {
            char b[8];
            int k = name_char_to_game(name_utf8_next(&utf8), b);
            if (n + k > len - 1) break;
            memcpy(out + n, b, (size_t)k);
            n += k;
        }
    }
    out[n] = 0;
    return n;
}

int name_sjis_to_atlas(unsigned sjis)
{
    unsigned i;
    for (i = 0; i < NAME_GLYPH_COUNT; ++i)
        if (name_glyphs[i].sjis == sjis) return name_glyphs[i].atlas;
    return -1;
}

int name_to_atlas(uint32_t cp)
{
    const char *f;
    int g = glyph_find(cp);
    if (g >= 0) return name_glyphs[g].atlas;
    f = fold_find(cp);
    if (f) {
        uint32_t c;
        if (!*f) return -1;
        c = name_utf8_next(&f);
        if (c >= 0x80) { g = glyph_find(c); return g >= 0 ? name_glyphs[g].atlas : -1; }
        /* an ASCII letter: its full-width form's glyph (the bottom screen draws ASCII itself; this is for completeness) */
        g = glyph_find(c - 0x21 + 0xFF01);
        return g >= 0 ? name_glyphs[g].atlas : -1;
    }
    g = glyph_find(0xFF1F);                       /* "？" */
    return g >= 0 ? name_glyphs[g].atlas : -1;
}
