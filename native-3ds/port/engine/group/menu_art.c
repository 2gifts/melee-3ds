/* The top-screen Solo / Group submenu's art, built from the menu archive's own letters (menu_art_hook.c fills it in).
 *
 * MnMaAll's row label table (MenMainCursor_Top joint 3, 176x30 IA4) and its submenu tab table (MenMainPanel_Top joint
 * 85, 168x28 I4) hold blank 8x4 dummy images that no menu of vanilla or Slippi shows: labels 49 to 52 (frames 220 to
 * 226, the rows of menu kind 11, unused in vanilla) and tab 13 (no frame). The hook points labels 49 and 50 and tab 13
 * at images made here and moves the one tab key of frame 2400 (kind 11's header) from tab 6 to tab 13. Nothing that
 * draws changes: no existing texture or animation frame is touched.
 *
 * The images are cut from vanilla's own labels, so they look exactly like the menu and no art ships with the port:
 *   Solo   S, o from "Sound" (label 20), l from "Melee" (label 10)
 *   Group  G from "Gallery" (label 15), r, o, p from "Trophies" (label 2), u from "Sound"
 *   Melee  (the tab) the word "Melee" of the "Special Melee" tab (tab 7), widened as the short tabs are
 * A letter is its white fill (alpha > 0, intensity >= 8) plus every pixel within MP_ART_EDGE of it (the black outline);
 * pixels nearer another letter's fill belong to that letter. Letters are set apart as in vanilla's words (2 or 3
 * columns of outline between fills) and the word is centred.
 *
 * Every source image is checked by size, format and hash, so only vanilla's English art is used (vanilla's and
 * Slippi's MnMaAll.usd hold the same images; the Japanese MnMaAll.dat does not, and is left alone).
 *
 * No engine headers: tools/group/menu_art_test.py compiles this file on the PC and runs it over the disc's archives. */
#include "menu_art.h"

#define MP_ART_EDGE 3   /* the outline: no vanilla label has an outline pixel farther than 3 from its fill */

enum { FMT_I4 = 0, FMT_IA4 = 2 };

unsigned mp_menu_art_fnv(const unsigned char* p, unsigned n)
{
    unsigned h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x01000193u;
    }
    return h;
}

unsigned mp_menu_art_size(unsigned w, unsigned h, unsigned fmt)
{
    return fmt == FMT_IA4 ? (w + 7) / 8 * ((h + 3) / 4) * 32 : (w + 7) / 8 * ((h + 7) / 8) * 32;
}

/* IA4: 8x4 tiles of one byte a texel (alpha in the high nibble, intensity in the low). */
static unsigned ia4_at(unsigned w, unsigned x, unsigned y)
{
    return ((y / 4) * ((w + 7) / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8;
}

/* I4: 8x8 tiles of 4 bits a texel, the even column in the high nibble. */
static unsigned i4_get(const unsigned char* p, unsigned w, unsigned x, unsigned y)
{
    unsigned b = p[((y / 8) * ((w + 7) / 8) + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];
    return x % 2 ? b & 15 : b >> 4;
}

static void i4_set(unsigned char* p, unsigned w, unsigned x, unsigned y, unsigned v)
{
    unsigned char* b = &p[((y / 8) * ((w + 7) / 8) + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];
    *b = x % 2 ? (unsigned char) ((*b & 0xF0) | v) : (unsigned char) ((*b & 0x0F) | (v << 4));
}

/* The source images: label or tab index, size, format and hash (vanilla's MnMaAll.usd; Slippi's is the same). */
static const struct {
    unsigned char tab, index;
    unsigned hash;
} sources[] = {
    { 0, 20, 0xFA6049D4u },   /* "Sound" */
    { 0, 15, 0x1BCF3D50u },   /* "Gallery" */
    { 0, 2, 0x2188E941u },    /* "Trophies" */
    { 0, 10, 0x56681CC9u },   /* "Melee" */
    { 1, 7, 0x1BCB7046u },    /* the "Special Melee" tab */
};

/* A letter: the label it comes from and the columns of its fill there; gap: fill-free columns before it. */
typedef struct {
    unsigned char label, x0, x1, gap;
} Glyph;

static const Glyph solo[] = { { 20, 46, 62, 0 }, { 20, 66, 79, 3 }, { 10, 90, 93, 3 }, { 20, 66, 79, 3 } };
static const Glyph group[] = { { 15, 39, 57, 0 }, { 2, 49, 57, 3 }, { 2, 60, 73, 2 }, { 20, 83, 95, 3 }, { 2, 76, 89, 3 } };
#define TAB_SOURCE 7
#define TAB_X0 94   /* "Melee" in "Special Melee": its columns (a fill-free column on each side) */
#define TAB_X1 164
#define TAB_STRETCH 1.4F    /* about as wide as "Sound" (5 letters too) */
#define TAB_SLANT 0.33F     /* the italic's slant: the M's left edge moves 5 columns over 15 rows */

/* A key of a texture animation's image track, as HSD_FObj reads it: constant or linear keys of one unsigned byte. */
static int track_key(const unsigned char* ad, unsigned len, unsigned frac, unsigned frame, unsigned* offset)
{
    unsigned at = 0, nb = 0, op = 0, now = 0, found = 0, have = 0;
    if ((frac & 0xE0) != 0x80) {
        return -1;   /* not u8 values */
    }
    while (at < len) {
        unsigned value_at, wait = 0, shift = 0, d;
        if (nb == 0) {
            d = ad[at++];
            op = d & 15;
            nb = ((d >> 4) & 7) + 1;
            shift = 3;
            while ((d & 0x80) && at < len) {
                d = ad[at++];
                nb += (d & 0x7F) << shift;
                shift += 7;
            }
        }
        nb--;
        if (op != 1 && op != 2) {
            return -1;   /* a key kind the menu's tracks do not use */
        }
        if (at >= len) {
            return -1;
        }
        value_at = at++;
        shift = 0;
        do {
            if (at >= len) {
                return -1;
            }
            d = ad[at++];
            wait |= (d & 0x7F) << shift;
            shift += 7;
        } while (d & 0x80);
        if (now == frame) {
            found = value_at;
            have = 1;
        }
        now += wait;
    }
    if (!have) {
        return -1;
    }
    *offset = found;
    return ad[found] >> (frac & 0x1F);
}

int mp_menu_art_key(const unsigned char* ad, unsigned len, unsigned frac, unsigned frame, unsigned* offset)
{
    unsigned off = 0;
    int v = track_key(ad, len, frac, frame, &off);
    if (offset != 0) {
        *offset = off;
    }
    return v;
}

static int blank(const MpArtImage* m, unsigned fmt)
{
    return m->px != 0 && m->w == 8 && m->h == 4 && m->fmt == fmt;
}

const char* mp_menu_art_check(const MpArtTables* t, unsigned* tab_key_offset)
{
    unsigned i, off = 0;
    if (t->labels < 53 || t->labels > MP_ART_MAX || t->tabs < 21 || t->tabs > MP_ART_MAX) {
        return "unexpected table sizes";
    }
    for (i = MP_ART_SOLO; i <= MP_ART_SOLO + 3; i++) {
        if (!blank(&t->label[i], FMT_IA4)) {
            return "label slots 49 to 52 are not blank";
        }
    }
    if (!blank(&t->tab[MP_ART_TAB], FMT_I4)) {
        return "tab slot 13 is not blank";
    }
    for (i = 0; i < sizeof sources / sizeof sources[0]; i++) {
        const MpArtImage* m = sources[i].tab ? &t->tab[sources[i].index] : &t->label[sources[i].index];
        unsigned w = sources[i].tab ? MP_ART_TAB_W : MP_ART_LABEL_W, h = sources[i].tab ? MP_ART_TAB_H : MP_ART_LABEL_H;
        unsigned fmt = sources[i].tab ? FMT_I4 : FMT_IA4;
        if (m->px == 0 || m->w != w || m->h != h || m->fmt != fmt ||
            mp_menu_art_fnv(m->px, mp_menu_art_size(w, h, fmt)) != sources[i].hash)
        {
            return "a source image is not vanilla's English one";
        }
    }
    /* kind 11's rows (frames 220 and 222) show labels 49 and 50 */
    if (mp_menu_art_key(t->label_track, t->label_track_len, t->label_frac, 220, 0) != MP_ART_SOLO ||
        mp_menu_art_key(t->label_track, t->label_track_len, t->label_frac, 222, 0) != MP_ART_GROUP)
    {
        return "the label track does not show slots 49 and 50 at frames 220 and 222";
    }
    if (mp_menu_art_key(t->tab_track, t->tab_track_len, t->tab_frac, MP_ART_TAB_FRAME, &off) != MP_ART_TAB_WAS) {
        return "the tab track does not show tab 6 at frame 2400";
    }
    if (tab_key_offset != 0) {
        *tab_key_offset = off;
    }
    return 0;
}

static const unsigned char* label_px(const MpArtTables* t, unsigned index)
{
    return t->label[index].px;
}

/* d: Chebyshev distance from (x, y) to the nearest fill pixel of columns [x0, x1] (own) or outside them (other), at
 * most MP_ART_EDGE + 1. */
static void distances(const unsigned char* px, int x, int y, int x0, int x1, int* own, int* other)
{
    int yy, xx;
    *own = *other = MP_ART_EDGE + 1;
    for (yy = y - MP_ART_EDGE; yy <= y + MP_ART_EDGE; yy++) {
        for (xx = x - MP_ART_EDGE; xx <= x + MP_ART_EDGE; xx++) {
            unsigned v;
            int dx = xx > x ? xx - x : x - xx, dy = yy > y ? yy - y : y - yy, d = dx > dy ? dx : dy;
            if (yy < 0 || yy >= MP_ART_LABEL_H || xx < 0 || xx >= MP_ART_LABEL_W) {
                continue;
            }
            v = px[ia4_at(MP_ART_LABEL_W, (unsigned) xx, (unsigned) yy)];
            if ((v >> 4) == 0 || (v & 15) < 8) {
                continue;   /* not fill */
            }
            if (xx >= x0 && xx <= x1) {
                if (d < *own) {
                    *own = d;
                }
            } else if (d < *other) {
                *other = d;
            }
        }
    }
}

/* Over-draw: the more opaque texel wins; of two equally opaque, the darker (outline over outline stays outline). */
static void put(unsigned char* out, unsigned x, unsigned y, unsigned v)
{
    unsigned char* o = &out[ia4_at(MP_ART_LABEL_W, x, y)];
    if ((v >> 4) > (unsigned) (*o >> 4) || ((v >> 4) == (unsigned) (*o >> 4) && (v & 15) < (unsigned) (*o & 15))) {
        *o = (unsigned char) v;
    }
}

static void word(const MpArtTables* t, const Glyph* g, unsigned n, unsigned char* out)
{
    unsigned i, width = 2 * MP_ART_EDGE, at;
    for (i = 0; i < mp_menu_art_size(MP_ART_LABEL_W, MP_ART_LABEL_H, FMT_IA4); i++) {
        out[i] = 0;
    }
    for (i = 0; i < n; i++) {
        width += g[i].x1 - g[i].x0 + 1 + g[i].gap;
    }
    at = (MP_ART_LABEL_W - width) / 2 + MP_ART_EDGE;   /* where the first letter's fill starts */
    for (i = 0; i < n; i++) {
        const unsigned char* px = label_px(t, g[i].label);
        int x, y;
        at += g[i].gap;
        for (y = 0; y < MP_ART_LABEL_H; y++) {
            for (x = g[i].x0 - MP_ART_EDGE; x <= g[i].x1 + MP_ART_EDGE; x++) {
                int own, other;
                unsigned v;
                distances(px, x, y, g[i].x0, g[i].x1, &own, &other);
                if (own > MP_ART_EDGE) {
                    continue;
                }
                v = px[ia4_at(MP_ART_LABEL_W, (unsigned) x, (unsigned) y)];
                if ((v & 15) != 0 && other < own) {
                    v = own <= 2 ? 0xF0 : 0;   /* another letter's fill or its edge: this letter's outline */
                }
                put(out, at + (unsigned) (x - g[i].x0), (unsigned) y, v);
            }
        }
        at += g[i].x1 - g[i].x0 + 1;
    }
}

void mp_menu_art_compose(const MpArtTables* t, unsigned char* solo_out, unsigned char* group_out, unsigned char* tab_out)
{
    const unsigned char* src = t->tab[TAB_SOURCE].px;
    unsigned x, y, n = mp_menu_art_size(MP_ART_TAB_W, MP_ART_TAB_H, FMT_I4);
    word(t, solo, sizeof solo / sizeof solo[0], solo_out);
    word(t, group, sizeof group / sizeof group[0], group_out);
    for (x = 0; x < n; x++) {
        tab_out[x] = 0;
    }
    /* Widened as the short tabs are drawn ("Sound", "Rumble": "Special Melee" is squeezed to fit): each row is
     * stretched about the word's slant, so the letters stay as slanted as the other tabs'. Linear between texels. */
    for (y = 0; y < MP_ART_TAB_H; y++) {
        float shift = TAB_SLANT * (13.5F - (float) y);
        float start = ((float) MP_ART_TAB_W - (float) (TAB_X1 - TAB_X0 + 1) * TAB_STRETCH) / 2.0F;
        for (x = 0; x < MP_ART_TAB_W; x++) {
            float u = ((float) x - start - shift) / TAB_STRETCH + (float) TAB_X0 + shift;
            int i = u < 0.0F ? -1 : (int) u;
            float f = u - (float) i;
            unsigned a = i >= TAB_X0 && i <= TAB_X1 ? i4_get(src, MP_ART_TAB_W, (unsigned) i, y) : 0;
            unsigned b = i + 1 >= TAB_X0 && i + 1 <= TAB_X1 ? i4_get(src, MP_ART_TAB_W, (unsigned) (i + 1), y) : 0;
            i4_set(tab_out, MP_ART_TAB_W, x, y, (unsigned) ((float) a * (1.0F - f) + (float) b * f + 0.5F));
        }
    }
}
