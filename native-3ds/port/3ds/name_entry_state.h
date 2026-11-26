#ifndef MP_NAME_ENTRY_STATE_H
#define MP_NAME_ENTRY_STATE_H
/* The game's name entry (mnNameNew: VS Name Entry, the character select's
 * name list and Tournament registration) as the bottom-screen keyboard sees
 * it (port/engine/name_entry.c writes it, bottom_draw.c draws it).
 * Word-only wire format, like bottom_state.h: the engine writes BE words.
 * Glyphs are indices into the game's font atlas (HSD_SisLib_FontAtlas, the
 * bottom screen's font), or one of the two values below. */
enum { MP_NAME_KEYS = 50, MP_NAME_VARIANTS = 8 };
enum { MP_NAME_GLYPH_NONE = 0xFFFF, MP_NAME_GLYPH_SPACE = 0xFFFE };
/* Key numbers are the game's cursor (mn_804A04F0.hovered_selection): the
 * 50 letters, column by column from the right (key / 5 is the column, 0 the
 * rightmost; key % 5 the row), then the function keys. */
enum {
    MP_NAME_KEY_BACK = 0x32,     /* leave without a name */
    MP_NAME_KEY_HIRAGANA = 0x33,
    MP_NAME_KEY_KATAKANA = 0x34,
    MP_NAME_KEY_ENGLISH = 0x35,
    MP_NAME_KEY_ERASE = 0x36,
    MP_NAME_KEY_RANDOM = 0x37,   /* a name from the game's list */
    MP_NAME_KEY_OK = 0x39,
};
/* A touch, for the engine's next read of the name entry's buttons: a key
 * number, a choice in the variant picker, or closing the picker. */
enum { MP_NAME_REQUEST_NONE = -1, MP_NAME_REQUEST_VARIANT = 0x100, MP_NAME_REQUEST_CLOSE = 0x200 };
typedef struct {
    unsigned open;        /* the name entry is on screen */
    unsigned mode;        /* 0 hiragana, 1 katakana, 2 English */
    unsigned hovered;     /* the game's cursor (a key number) */
    unsigned cursor;      /* 0-3: the character the next key writes */
    unsigned variants;    /* the variant picker's choices; 0 while it is closed */
    unsigned variant;     /* the picker's cursor */
    unsigned mode_glyph[2];             /* the hiragana and katakana mode keys' labels */
    unsigned name[4];                   /* the name so far */
    unsigned keys[MP_NAME_KEYS];        /* the current mode's key labels */
    unsigned variant_glyph[MP_NAME_VARIANTS];
} MPNameEntryState;
_Static_assert(sizeof(MPNameEntryState) == 70 * 4, "Name entry wire layout");
#endif
