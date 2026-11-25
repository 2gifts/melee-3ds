/* The top-screen Solo / Group submenu's art (menu_art.c builds it, menu_art_hook.c puts it in the menu's tables). No
 * engine headers, so tools/group/menu_art_test.py builds it on the PC too. */
#ifndef MP_MENU_ART_H
#define MP_MENU_ART_H

#define MP_ART_MAX 96            /* table sizes accepted (vanilla: 58 labels and 21 tabs; Slippi: 64 and 21) */
#define MP_ART_SOLO 49           /* label slots: Solo, Group (frames 220, 222); 51 and 52 stay blank, for later */
#define MP_ART_GROUP 50
#define MP_ART_TAB 13            /* the tab slot: "Melee" */
#define MP_ART_TAB_FRAME 2400    /* kind 11's header frame (mn_803EAE8C[11]): its tab key moves from tab 6 to tab 13 */
#define MP_ART_TAB_WAS 6         /* "Tournament Melee", which no other menu shows */
#define MP_ART_LABEL_W 176
#define MP_ART_LABEL_H 30
#define MP_ART_TAB_W 168
#define MP_ART_TAB_H 28
#define MP_ART_LABEL_BYTES 5632  /* 176x30 IA4: 22 x 8 tiles of 32 bytes */
#define MP_ART_TAB_BYTES 2688    /* 168x28 I4: 21 x 4 tiles of 32 bytes */

typedef struct {
    const unsigned char* px;     /* the pixels (GameCube tiles) */
    unsigned w, h, fmt;          /* fmt: GXTexFmt (0 I4, 2 IA4) */
} MpArtImage;

typedef struct {
    MpArtImage label[MP_ART_MAX];   /* MenMainCursor_Top joint 3's image table */
    unsigned labels;
    MpArtImage tab[MP_ART_MAX];     /* MenMainPanel_Top joint 85's */
    unsigned tabs;
    const unsigned char* label_track;   /* their image tracks: the keys (HSD_FObjDesc ad, length, frac_value) */
    unsigned label_track_len, label_frac;
    const unsigned char* tab_track;
    unsigned tab_track_len, tab_frac;
} MpArtTables;

/* 0 when the tables are vanilla's (or Slippi's) and the art can be made and filled in; else why not. tab_key_offset:
 * the byte of the tab track to set to MP_ART_TAB << shift (frame 2400's key). */
const char* mp_menu_art_check(const MpArtTables* t, unsigned* tab_key_offset);
/* The three images, from the checked tables: MP_ART_LABEL_BYTES, MP_ART_LABEL_BYTES and MP_ART_TAB_BYTES. */
void mp_menu_art_compose(const MpArtTables* t, unsigned char* solo, unsigned char* group, unsigned char* tab);
/* The value of a u8 image track's key at `frame` (-1 when there is no key exactly there), and the offset of its value
 * byte. */
int mp_menu_art_key(const unsigned char* ad, unsigned len, unsigned frac, unsigned frame, unsigned* offset);
unsigned mp_menu_art_fnv(const unsigned char* p, unsigned n);
unsigned mp_menu_art_size(unsigned w, unsigned h, unsigned fmt);

#endif
