/* The top-screen menu art (port/engine/group/menu_art.c) over one menu archive's tables, written by menu_art_test.py:
 *
 *   menu_art_tests TABLES OUT_DIR
 *
 * TABLES (little-endian u32s): label count, tab count; each image: width, height, format, byte count, the bytes; then
 * the label track and the tab track: frac_value, byte count, the bytes. Prints "check ok key N" or "check refused WHY";
 * when ok, writes OUT_DIR/solo.bin, group.bin, tab.bin, and prints the tab track's key at frame 2400 before and after
 * the fill sets it (as menu_art_hook.c does), and every key frame whose value the fill changed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu_art.h"

static unsigned char* data;
static long size, at;

static unsigned u32(void)
{
    unsigned v;
    if (at + 4 > size) {
        fprintf(stderr, "short tables file\n");
        exit(2);
    }
    v = data[at] | data[at + 1] << 8 | data[at + 2] << 16 | (unsigned) data[at + 3] << 24;
    at += 4;
    return v;
}

static const unsigned char* bytes(unsigned n)
{
    const unsigned char* p = data + at;
    if (at + (long) n > size) {
        fprintf(stderr, "short tables file\n");
        exit(2);
    }
    at += n;
    return p;
}

static void image(MpArtImage* m)
{
    unsigned n;
    m->w = u32();
    m->h = u32();
    m->fmt = u32();
    n = u32();
    m->px = n ? bytes(n) : NULL;
}

static void save(const char* dir, const char* name, const unsigned char* p, unsigned n)
{
    char path[1024];
    FILE* f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if ((f = fopen(path, "wb")) == NULL || fwrite(p, 1, n, f) != n) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fclose(f);
}

int main(int argc, char** argv)
{
    static MpArtTables t;
    static unsigned char solo[MP_ART_LABEL_BYTES], group[MP_ART_LABEL_BYTES], tab[MP_ART_TAB_BYTES];
    unsigned char* track;
    unsigned i, key = 0, frame;
    const char* why;
    FILE* f;

    if (argc != 3) {
        fprintf(stderr, "usage: menu_art_tests TABLES OUT_DIR\n");
        return 2;
    }
    if ((f = fopen(argv[1], "rb")) == NULL) {
        return 2;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t) size);
    if (fread(data, 1, (size_t) size, f) != (size_t) size) {
        return 2;
    }
    fclose(f);
    t.labels = u32();
    t.tabs = u32();
    if (t.labels > MP_ART_MAX || t.tabs > MP_ART_MAX) {
        printf("check refused table too large for the test\n");
        return 0;
    }
    for (i = 0; i < t.labels; i++) {
        image(&t.label[i]);
    }
    for (i = 0; i < t.tabs; i++) {
        image(&t.tab[i]);
    }
    t.label_frac = u32();
    t.label_track_len = u32();
    t.label_track = bytes(t.label_track_len);
    t.tab_frac = u32();
    t.tab_track_len = u32();
    t.tab_track = bytes(t.tab_track_len);

    why = mp_menu_art_check(&t, &key);
    if (why != NULL) {
        printf("check refused %s\n", why);
        return 0;
    }
    printf("check ok key %u\n", key);
    mp_menu_art_compose(&t, solo, group, tab);
    save(argv[2], "solo.bin", solo, sizeof solo);
    save(argv[2], "group.bin", group, sizeof group);
    save(argv[2], "tab.bin", tab, sizeof tab);
    /* the fill: frame 2400's key to tab 13; nothing else may change */
    track = malloc(t.tab_track_len);
    memcpy(track, t.tab_track, t.tab_track_len);
    track[key] = (unsigned char) (MP_ART_TAB << (t.tab_frac & 0x1F));
    printf("tab %d before %d after %d\n", MP_ART_TAB_FRAME,
           mp_menu_art_key(t.tab_track, t.tab_track_len, t.tab_frac, MP_ART_TAB_FRAME, NULL),
           mp_menu_art_key(track, t.tab_track_len, t.tab_frac, MP_ART_TAB_FRAME, NULL));
    for (frame = 0; frame <= 10000; frame++) {
        int a = mp_menu_art_key(t.tab_track, t.tab_track_len, t.tab_frac, frame, NULL);
        int b = mp_menu_art_key(track, t.tab_track_len, t.tab_frac, frame, NULL);
        if (a != b) {
            printf("tab changed at %u: %d -> %d\n", frame, a, b);
        }
    }
    printf("label 220 %d 222 %d 224 %d 226 %d\n", mp_menu_art_key(t.label_track, t.label_track_len, t.label_frac, 220, NULL),
           mp_menu_art_key(t.label_track, t.label_track_len, t.label_frac, 222, NULL),
           mp_menu_art_key(t.label_track, t.label_track_len, t.label_frac, 224, NULL),
           mp_menu_art_key(t.label_track, t.label_track_len, t.label_frac, 226, NULL));
    return 0;
}
