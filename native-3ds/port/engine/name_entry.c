/* The game's name entry (mnNameNew) and the bottom-screen keyboard.
 * presentation_mods.py hooks mnnamenew.c: it reports when the name entry
 * opens and closes, and the buttons its two input readers (the keyboard and
 * the variant picker) read pass through here. A touch on the bottom screen
 * becomes the game's own input: the cursor moves to the touched key and A
 * presses it, so the top screen shows the key, the sound plays and the game's
 * rules apply (variants, the 4-character limit, names taken or refused). */
#include "../3ds/name_entry_state.h"
#include <melee/mn/inlines.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <string.h>

extern HSD_GObj* mnNameNew_804D6C08;
extern char mnNameNew_CurrentNameText[0x10];
extern unsigned mp_bottom_scene;
/* mnnamenew.c (presentation_mods.py): table 0-2 the key labels of mode 0-2,
 * 3 and 4 the lower and upper variant rows of a key. */
extern const char* mp_name_entry_glyph(int table, int key, int variant);
extern int mp_platform_name_entry_request(void);

static unsigned open_scene = ~0u;
static unsigned polls, polls_seen, idle;

void mp_name_entry_opened(void)
{
    open_scene = mp_bottom_scene;
    idle = 0;
}

void mp_name_entry_closed(void) { open_scene = ~0u; }

/* Open, in the scene it opened in, and its input still read: a scene change
 * that skips the name entry's own exit frees it without telling us. */
static int is_open(void)
{
    return open_scene != ~0u && open_scene == mp_bottom_scene && idle < 8 && mnNameNew_804D6C08 != NULL;
}

static int variant_count(int key)
{
    int n = 0;
    const char* s;
    if (key < 0 || key >= MP_NAME_KEYS) return 0;
    while (n < MP_NAME_VARIANTS / 2 && (s = mp_name_entry_glyph(3, key, n)) != NULL && s[0] != 0) n++;
    return n * 2;
}

/* What mnNameNew_8023D130 writes at choice i of key's variant picker. */
static const char* variant_string(int key, int mode, int i)
{
    int lower;
    if (key == 0x30 || key == 0x31) lower = i % 2 == 0;
    else lower = (mode == 0 && i % 2 == 0) || (mode == 1 && i % 2 != 0);
    return mp_name_entry_glyph(lower ? 3 : 4, key, i / 2);
}

/* The name entry's buttons (mn_80229624's), or a touch from the bottom screen
 * in their place. A touch waits while the menus ignore input (the cooldown). */
u32 mp_name_entry_buttons(u32 buttons)
{
    NameNewEntry* data;
    int request;
    ++polls;
    if (mn_804D6BC8.cooldown != 0 || mnNameNew_804D6C08 == NULL) return buttons;
    request = mp_platform_name_entry_request();
    if (request < 0) return buttons;
    data = mnNameNew_804D6C08->user_data;
    if (data->variant_gobj != NULL) {
        if (request >= MP_NAME_REQUEST_VARIANT && request < MP_NAME_REQUEST_VARIANT + MP_NAME_VARIANTS) {
            if (request - MP_NAME_REQUEST_VARIANT >= variant_count(mn_804A04F0.hovered_selection)) return 0;
            mn_804A04F0.confirmed_selection = (u8) (request - MP_NAME_REQUEST_VARIANT);
            return MenuInput_AButton;
        }
        return MenuInput_Back;   /* anything else closes the picker, as B does */
    }
    if (request > MP_NAME_KEY_OK) return buttons;
    mn_804A04F0.hovered_selection = (u16) request;
    if (request < MP_NAME_KEYS) data->last_key_sel = (u8) request;
    return MenuInput_AButton;
}

/* The SIS font atlas index of the SJIS character at s (HSD_SisLib_803A67EC's
 * lookup). */
static unsigned glyph(const char* s)
{
    unsigned hi, lo, i;
    if (s == NULL || s[0] == 0) return MP_NAME_GLYPH_NONE;
    hi = (u8) s[0];
    lo = (u8) s[1];
    if (hi == ' ' || (hi == 0x81 && lo == 0x40)) return MP_NAME_GLYPH_SPACE;
    for (i = 0; i < sizeof(lbl_8040C8C0) / 2; ++i) {
        if (lbl_8040C8C0[i * 2] == hi && lbl_8040C8C0[i * 2 + 1] == lo) {
            unsigned code = HSD_SisLib_8040C680[i * 2] << 8 | HSD_SisLib_8040C680[i * 2 + 1];
            return code >= 0x2000 && code < 0x2200 ? code - 0x2000 : MP_NAME_GLYPH_NONE;
        }
    }
    return MP_NAME_GLYPH_NONE;
}

void mp_name_entry_snapshot(MPNameEntryState* s)
{
    NameNewEntry* data;
    int i;
    memset(s, 0, sizeof(*s));
    if (polls != polls_seen) {
        polls_seen = polls;
        idle = 0;
    } else if (idle < 8) {
        ++idle;
    }
    if (!is_open()) return;
    data = mnNameNew_804D6C08->user_data;
    s->open = 1;
    s->mode = data->mode;
    s->hovered = mn_804A04F0.hovered_selection;
    s->cursor = data->cursor_pos;
    s->mode_glyph[0] = glyph(mp_name_entry_glyph(0, 0, 0));
    s->mode_glyph[1] = glyph(mp_name_entry_glyph(1, 0, 0));
    for (i = 0; i < 4; ++i) {
        s->name[i] = glyph(&mnNameNew_CurrentNameText[i * 3]);
    }
    for (i = 0; i < MP_NAME_KEYS; ++i) {
        s->keys[i] = data->mode <= 2 ? glyph(mp_name_entry_glyph(data->mode, i, 0)) : MP_NAME_GLYPH_NONE;
    }
    if (data->variant_gobj != NULL) {
        int key = mn_804A04F0.hovered_selection;
        s->variants = variant_count(key);
        s->variant = mn_804A04F0.confirmed_selection;
        for (i = 0; i < (int) s->variants; ++i) {
            s->variant_glyph[i] = glyph(variant_string(key, data->mode, i));
        }
    }
}
