"""Melee -> Group: the vanilla four-door character select as the lobby of a local wireless group.

Every patch is inert unless mp_slippi_group_mode() (a group session in the HANYU scene), so Solo and the rest of the
game are untouched. What it does to the VS character select (mncharsel.c):
  * mp_css_pad(port): console n drives door n with its only pad (pad 0); the other ports read unplugged channels, so
    their cursors take the vanilla "no controller" branch and stay hidden (cursors are private).
  * mp_css_group_frame(), once per CSS frame: mirrors the host's lobby into the doors (portraits, costumes, Human/CPU,
    teams, CPU levels, closed doors) and sends this console's own door edits to the host; draws each console's name and
    signal under its door; sends the host's rules; leaves for the stage select when the host says so.
  * Start no longer ends the scene: it asks the host, which answers with the stage select for everyone.
  * Rules and the Teams switch are the host's only.
  * The Human/CPU/closed click ends on Human only where a console is present (a CPU elsewhere), as on a GameCube.
The stage select (mnstagesel.c) forwards the first pick and Back to the lobby and leaves when the host does.
The door mirroring was first proved by the spike in tools/slippi_edits/css_spike.py.
"""

CODE = r'''
/* ======== group lobby on the CSS (tools/slippi_edits/group_css.py) ======== */
#include <string.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>
typedef struct MpCssDoor { u8 kind, ckind, costume, team, cpu_level; } MpCssDoor;

#define MP_LOB_BYTES 88
#define MP_CSS_REMOTE_HELD 5   /* token state: held by another console (a cursor holding a token is port + 1, 1 to 4) */
static u8 mp_lob[MP_LOB_BYTES];   /* the newest lobby snapshot (slippi_bridge.c slippi_lobby_get) */
static int mp_lob_ok;
static int mp_css_ctx = -1;
static int mp_css_go;             /* the host sent us to the stage select */
static int mp_css_seen_css;       /* saw the lobby in its CSS phase since this scene began */
static int mp_css_start_wait;
static int mp_css_my_seen_open;   /* our own door has been seen open since this scene began */
static int mp_css_leave_asked;   /* Back is waiting for the host's answer to "End the session?" */
static u8 mp_css_last_view[4][14], mp_css_last_view_ok[4];
static u8 mp_css_applied[4][14], mp_css_applied_ok;
static u8 mp_css_pending[4];
static u8 mp_css_rules_sent[6], mp_css_rules_sent_ok;
static HSD_Text* mp_css_dtext[4];
static HSD_Text* mp_css_btext[4];
static int mp_css_dname[4], mp_css_dbars[4];
static char mp_css_name_now[4][32], mp_css_bars_now[4][8];
static int mp_css_col_now[4], mp_css_barcol_now[4];

static int mp_css_group(void) { return mp_slippi_group_mode(); }
static int mp_css_my(void) { int i = mp_platform_slippi_net_local_index(); return i < 0 || i > 3 ? 0 : i; }
static int mp_css_present(int d) { return mp_lob_ok && ((mp_lob[3] >> d) & 1); }

/* The pad a door's cursor reads: my door gets pad 0 (the only real pad), every other port an unplugged channel. */
HSD_PadStatus* mp_css_pad(int port)
{
    int my;
    if (!mp_css_group()) {
        return &HSD_PadCopyStatus[port];
    }
    my = mp_css_my();
    if (port == my) {
        return &HSD_PadCopyStatus[0];
    }
    if (port == 0) {
        return &HSD_PadCopyStatus[my];
    }
    return &HSD_PadCopyStatus[port];
}

/* A door whose cursor is not ours: never flipped to CPU for lack of a pad. */
int mp_css_remote(int door)
{
    return mp_css_group() && door != mp_css_my();
}

/* The Human/CPU/closed click's end of the cycle: Human where a console sits, else CPU. */
int mp_css_unplugged(int door)
{
    if (mp_css_group() && mp_lob_ok) {
        return !mp_css_present(door);
    }
    return HSD_PadCopyStatus[(u8) door].err != 0;
}

/* Rules and the Teams switch belong to the host. */
int mp_css_can_edit_rules(void)
{
    return !mp_css_group() || !mp_lob_ok || mp_lob[2] != 0;
}

/* Apply one door from a table: no cursor, no sound (every SFX site is skipped while the pending-scene byte is 4). */
static void mp_css_apply_door(int d, const MpCssDoor* s)
{
    u8 save = mnCharSel_804D6CF6;
    PlayerInitData* p = &mnCharSel_804D6CB0->vs.start.players[d];
    CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
    mnCharSel_804D6CF6 = 4;
    p->slot_type = dr->p_kind = s->kind;
    p->cpu_level = s->cpu_level;
    dr->team = p->team = s->team;
    p->nametag = 0x78;
    mnCharSel_803F0E8C[d].data->use_tag = 0;
    if (s->kind != 3 && s->ckind < CKind_Playable_Count) {
        if (dr->sel_icon >= 0x19 || icons[dr->sel_icon].char_kind != (s8) s->ckind) {
            p->ckind = s->ckind;
            mnCharSel_8025FDEC(d);
        }
        dr->costume = s->costume;
        mnCharSel_804A0BD0[d]->x5 = 0;
        HSD_JObjClearFlagsAll(mnCharSel_804A0BD0[d]->gobj->hsd_obj, JOBJ_HIDDEN);
    } else {
        mnCharSel_804A0BD0[d]->x5 = 0;
        dr->sel_icon = 0x19;
        p->ckind = ChKind_None;
    }
    mnCharSel_8025DB34(d);
    mnCharSel_804D6CF6 = save;
}

/* One door as the lobby writes it: kind (0 Human, 1 CPU, 2 closed), fighter, costume, team, CPU level, chosen. */
static void mp_css_read_door(int d, u8* o)
{
    PlayerInitData* p = &mnCharSel_804D6CB0->vs.start.players[d];
    CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
    int kind = dr->p_kind == 0 ? 0 : dr->p_kind == 1 ? 1 : 2;
    int chosen;
    if (d == mp_css_my()) {
        /* A console sits here: its door starts out closed in the vanilla CSS, which means Human until the door has been
         * seen open; after that a closed door is the player sitting this match out. */
        if (kind == 2 && !mp_css_my_seen_open) {
            kind = 0;
        } else if (kind != 2) {
            mp_css_my_seen_open = 1;
        }
    }
    int tok = mnCharSel_804A0BD0[d]->x5;
    if (tok == MP_CSS_REMOTE_HELD && mp_css_applied_ok) {
        memcpy(o, mp_css_applied[d], 14);   /* another console holds this token: nothing here to report */
        return;
    }
    int placed = kind != 2 && dr->sel_icon < 0x19 && mnCharSel_804A0BD0[d]->x5 == 0 && p->ckind >= 0 &&
                 p->ckind < CKind_Playable_Count;
    /* the token is held over a fighter: the door shows it see-through, and the others should too */
    int hover = kind != 2 && !placed && dr->sel_icon < 0x19 && mnCharSel_804A0BD0[d]->x5 == mp_css_my() + 1 &&
                icons[dr->sel_icon].char_kind >= 0 && icons[dr->sel_icon].char_kind < CKind_Playable_Count;
    int held = kind != 2 && !placed && !hover && mnCharSel_804A0BD0[d]->x5 == mp_css_my() + 1;
    chosen = placed ? 1 : hover ? 2 : held ? 3 : 0;
    o[0] = kind;
    o[1] = placed ? p->ckind : hover ? icons[dr->sel_icon].char_kind : 0;
    o[2] = chosen && chosen != 3 ? dr->costume : 0;
    o[3] = kind == 2 ? 0 : dr->team;
    o[4] = kind == 1 ? p->cpu_level : 3;
    o[5] = chosen;
    memset(o + 6, 0, 8);
    if (kind == 0 && d == mp_css_my() && mnCharSel_803F0E8C[d].data->use_tag && p->nametag != 0x78) {
        char* t = GetNameText(p->nametag);
        if (t != NULL) {
            strncpy((char*) o + 6, t, 8);   /* the tag's own text: an id means nothing in another save */
        }
    }
}

static void mp_css_view_door(int d, u8* o)
{
    const u8* v = mp_lob + 24 + 16 * d;
    int chosen = v[0] != 2 ? v[5] : 0;   /* 0 none, 1 placed, 2 held over a fighter, 3 held over nothing */
    o[0] = v[0];
    o[1] = chosen && chosen != 3 ? v[1] : 0;
    o[2] = chosen && chosen != 3 ? v[2] : 0;
    o[3] = v[0] == 2 ? 0 : v[3];
    o[4] = v[0] == 1 ? v[4] : 3;
    o[5] = chosen;
    memcpy(o + 6, v + 6, 8);
}

static void mp_css_put_door(int d, const u8* o)
{
    MpCssDoor t;
    t.kind = o[0] == 0 ? 0 : o[0] == 1 ? 1 : 3;
    t.ckind = o[5] == 1 ? o[1] : ChKind_None;   /* 2 and 3 show nothing in the door's slot of the roster */
    t.costume = o[2];
    t.team = o[3];
    t.cpu_level = o[4];
    mp_css_apply_door(d, &t);
    if (o[5] == 3) {
        mnCharSel_804A0BD0[d]->x5 = MP_CSS_REMOTE_HELD;   /* in someone's hand, over nothing: no puck here */
    }
    if (o[5] == 2) {
        /* a preview: the token is held over this fighter (what the local hand's door shows while it hovers) */
        int i;
        for (i = 0; i < 0x19; i++) {
            if (icons[i].char_kind == (s8) o[1] && icons[i].state >= 1) {
                u8 save = mnCharSel_804D6CF6;
                mnCharSel_804D6CF6 = 4;
                mnCharSel_803F0DFC.doors[d].sel_icon = i;
                mnCharSel_803F0DFC.doors[d].costume = o[2];
                mnCharSel_804A0BD0[d]->x5 = MP_CSS_REMOTE_HELD;
                mnCharSel_8025DB34(d);
                mnCharSel_804D6CF6 = save;
                break;
            }
        }
    }
}

/* The READY overlay waits while a token is held over a fighter (ours or one mirrored from another console). */
int mp_css_hovering(int door)
{
    return mp_css_group() && mnCharSel_803F0DFC.doors[door].p_kind != 3 && mnCharSel_804A0BD0[door]->x5 != 0;
}


/* A puck (token) in another console's hand is not drawn here at all (its own think hides it, see below); when the other
 * console lets go, show it again. */
static u8 mp_css_puck_gone[4];
static void mp_css_hide_held_pucks(void)
{
    int d;
    for (d = 0; d < 4; d++) {
        struct CSSCharModel* tok = mnCharSel_804A0BD0[d];
        if (tok == NULL) {
            continue;
        }
        if (tok->x5 == MP_CSS_REMOTE_HELD) {
            mp_css_puck_gone[d] = 1;
        } else if (mp_css_puck_gone[d]) {
            mp_css_puck_gone[d] = 0;
            HSD_JObjClearFlagsAll(tok->gobj->hsd_obj, JOBJ_HIDDEN);
        }
    }
}

/* The lobby decides; a local edit is sent to the host and held for a few frames so the answer does not undo it. */
static void mp_css_sync(void)
{
    int d;
    for (d = 0; d < 4; d++) {
        u8 now[14], view[14];
        mp_css_read_door(d, now);
        if (!mp_css_applied_ok || memcmp(now, mp_css_applied[d], 14) != 0) {
            if (mp_css_applied_ok) {
                mp_platform_slippi_lobby_set_door(d, now);
                OSReport("GROUPCSS send door %d: kind %d fighter %d color %d team %d level %d chosen %d\n", d, now[0], now[1],
                         now[2], now[3], now[4], now[5]);
                mp_css_pending[d] = 12;
                mp_css_last_view_ok[d] = 0;
            }
            memcpy(mp_css_applied[d], now, 14);
            if (!mp_css_applied_ok && d == mp_css_my()) {
                /* A CSS that was just (re)built, e.g. back from name entry: what this console's door holds (a new
                 * name tag, a fighter) is authoritative, so tell the host if the lobby does not have it yet. */
                mp_css_view_door(d, view);
                if (memcmp(now, view, 14) != 0) {
                    mp_platform_slippi_lobby_set_door(d, now);
                    mp_css_pending[d] = 12;
                    mp_css_last_view_ok[d] = 0;
                }
            }
            continue;
        }
        if (mp_css_pending[d] != 0) {
            mp_css_pending[d]--;
            continue;
        }
        mp_css_view_door(d, view);
        if (mp_css_last_view_ok[d] && memcmp(view, mp_css_last_view[d], 14) == 0) {
            continue;
        }
        memcpy(mp_css_last_view[d], view, 14);
        mp_css_last_view_ok[d] = 1;
        if (memcmp(view, now, 6) != 0) {
            OSReport("GROUPCSS apply door %d: kind %d fighter %d color %d team %d level %d chosen %d\n", d, view[0], view[1],
                     view[2], view[3], view[4], view[5]);
            mp_css_put_door(d, view);
            if (view[5] == 2) {
                memcpy(mp_css_applied[d], view, 14);   /* a mirrored hover: what we show, not a local edit */
            } else {
                mp_css_read_door(d, mp_css_applied[d]);
            }
        }
    }
    mp_css_applied_ok = 1;
}

/* ---- rules: the host sends its own, a joiner shows the host's teams switch ---- */
static void mp_css_rules_frame(void)
{
    struct GameRules* gr = gmMainLib_GetGameRules();
    VsModeData* vs = &mnCharSel_804D6CB0->vs;
    u8 r[6];
    if (mp_lob_ok && mp_lob[2] == 0) {
        if (mp_lob[15] != vs->start.rules.is_teams) {
            vs->start.rules.is_teams = mp_lob[15];
            mnCharSel_8025EE8C(mnCharSel_804D6CB0->match_type);
        }
        return;
    }
    r[0] = gr->mode != 1;
    r[1] = gr->stock_count;
    r[2] = r[0] ? gr->time_limit : gr->stock_time_limit;
    r[3] = gmMainLib_GetGamePrefs()->item_freq == 255 ? 0 : gmMainLib_GetGamePrefs()->item_freq + 1;   /* 0 off, else frequency + 1 */
    r[4] = vs->start.rules.is_teams ? 1 : 0;
    r[5] = gr->friendly_fire ? 1 : 0;
    if (!mp_css_rules_sent_ok || memcmp(r, mp_css_rules_sent, 6) != 0) {
        memcpy(mp_css_rules_sent, r, 6);
        mp_css_rules_sent_ok = 1;
        mp_platform_slippi_lobby_rules(r);
    }
}

/* ---- the name and signal bars under each door ---- */
static void mp_css_text_frame(void)
{
    static const GXColor port_col[4] = { { 0xE5, 0x4C, 0x4C, 0xFF }, { 0x4B, 0x4C, 0xE5, 0xFF },
                                         { 0xFF, 0xCB, 0x00, 0xFF }, { 0x00, 0xB2, 0x00, 0xFF } };
    static const GXColor team_col[3] = { { 0xE5, 0x4C, 0x4C, 0xFF }, { 0x4B, 0x4C, 0xE5, 0xFF },
                                         { 0x00, 0xB2, 0x00, 0xFF } };
    int d;
    if (mp_css_ctx < 0) {
        return;
    }
    for (d = 0; d < 4; d++) {
        HSD_JObj* j;
        HSD_JObj* dj;
        Vec3 pos, dp;
        char raw[32], name[40], bars[8];
        GXColor col;
        int n, k, here = mp_css_present(d);
        CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
        if (mp_css_dtext[d] == NULL) {
            HSD_Text* t = HSD_SisLib_803A6754(0, mp_css_ctx);
            if (t == NULL) {
                continue;
            }
            t->default_fitting = 1;
            t->default_alignment = 0;
            t->font_size.x = 0.046f;
            t->font_size.y = 0.046f;
            t->box_size_x = 160.0f;
            t->box_size_y = 32.0f;
            mp_css_dname[d] = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
            mp_css_dtext[d] = t;
            t = HSD_SisLib_803A6754(0, mp_css_ctx);
            if (t == NULL) {
                mp_css_dtext[d] = NULL;
                continue;
            }
            t->default_fitting = 1;
            t->default_alignment = 0;
            t->font_size.x = 0.046f;
            t->font_size.y = 0.046f;
            t->box_size_x = 160.0f;
            t->box_size_y = 32.0f;
            mp_css_dbars[d] = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
            mp_css_btext[d] = t;
            mp_css_name_now[d][0] = 1;
            mp_css_bars_now[d][0] = 1;
            mp_css_col_now[d] = mp_css_barcol_now[d] = -1;
        }
        lb_80011E24(mnCharSel_804D6CC0, &j, mnCharSel_803F0E8C[d].name_jointl, -1);
        lb_8000B1CC(j, NULL, &pos);
        lb_80011E24(mnCharSel_804D6CC0, &dj, dr->door_joint, -1);
        lb_8000B1CC(dj, NULL, &dp);
        mp_css_dtext[d]->pos_x = dp.x + 0.6f;
        mp_css_dtext[d]->pos_y = -pos.y + 3.0f;
        mp_css_dtext[d]->pos_z = pos.z;
        mp_css_btext[d]->pos_x = dp.x + 0.6f + 8.0f;
        mp_css_btext[d]->pos_y = -pos.y + 3.0f;
        mp_css_btext[d]->pos_z = pos.z;
        raw[0] = 0;
        if (here) {
            mp_platform_slippi_net_player_name(d, raw, 9);
            raw[8] = 0;
        }
        mp_platform_slippi_ui_sis(raw, name, sizeof name);
        n = !here ? 0 : d == mp_css_my() ? 4 : mp_lob[17 + d] + 1;
        for (k = 0; k < n && k < 4; k++) {
            bars[k] = 'i';   /* lowercase i is a thin bar in the CSS font ('|' and '#' draw nothing) */
        }
        bars[k] = 0;
        col = mnCharSel_804D6CB0->vs.start.rules.is_teams ? team_col[dr->team % 3] : port_col[d];
        if (strcmp(name, mp_css_name_now[d]) != 0) {
            strncpy(mp_css_name_now[d], name, 31);
            mp_css_name_now[d][31] = 0;
            HSD_SisLib_803A70A0(mp_css_dtext[d], mp_css_dname[d], "%s", name);
        }
        if (mp_css_col_now[d] != *(int*) &col) {
            mp_css_col_now[d] = *(int*) &col;
            HSD_SisLib_803A74F0(mp_css_dtext[d], mp_css_dname[d], &col);
        }
        if (strcmp(bars, mp_css_bars_now[d]) != 0) {
            strcpy(mp_css_bars_now[d], bars);
            HSD_SisLib_803A70A0(mp_css_btext[d], mp_css_dbars[d], "%s", bars);
        }
        col.r = n >= 3 ? 0x33 : 0xFF;
        col.g = n >= 2 ? 0xFF : 0x40;
        col.b = n >= 3 ? 0x2F : 0x30;
        col.a = 0xFF;
        if (n == 2) {
            col.r = 0xFF;
            col.g = 0xE0;
            col.b = 0x20;
        }
        if (mp_css_barcol_now[d] != *(int*) &col) {
            mp_css_barcol_now[d] = *(int*) &col;
            HSD_SisLib_803A74F0(mp_css_btext[d], mp_css_dbars[d], &col);
        }
    }
}


/* ---- join / leave toast on the top screen, drawn like the online CSS's chat lines (online_mode.c css_text_frame):
 * one SIS line at size 0.55 in the sender's chat colour (P1 red, P2 blue), narrowed to 22 characters, no animation. ---- */
static HSD_Text* mp_css_toast;
static int mp_css_toast_id;
static char mp_css_toast_now[48];
static int mp_css_toast_col_now = -1;
static void mp_css_toast_frame(void) __attribute__((unused));
static void mp_css_toast_frame(void)
{
    static const GXColor col_of[4] = { { 229, 76, 76, 0xFF }, { 59, 189, 255, 0xFF }, { 0xFF, 0xCB, 0x00, 0xFF },
                                       { 0x00, 0xB2, 0x00, 0xFF } };
    char buf[48], sis[96];
    int left = mp_platform_slippi_group_toast(buf, sizeof buf);
    int port = mp_platform_slippi_group_toast_port();
    if (mp_css_ctx < 0) {
        return;
    }
    if (mp_css_toast == NULL) {
        /* On the CSS's own canvas, like the door texts (positions are in the CSS's joint units: x 0 is the screen's
         * middle, the top banner is at y -22). The font size matches the chat lines' (0.1 x 0.55). */
        HSD_Text* t = HSD_SisLib_803A6754(0, mp_css_ctx);
        if (t == NULL) {
            return;
        }
        t->default_fitting = 1;
        t->default_alignment = 0;
        t->font_size.x = 0.055f;
        t->font_size.y = 0.055f;
        t->box_size_x = 160.0f;
        t->box_size_y = 32.0f;
        t->pos_x = -17.0f;
        t->pos_y = -22.0f;
        t->pos_z = mp_css_dtext[0] != NULL ? mp_css_dtext[0]->pos_z : 0.0f;
        mp_css_toast_id = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
        mp_css_toast = t;
        mp_css_toast_now[0] = 1;
        mp_css_toast_col_now = -1;
    }
    if (left <= 0) {
        if (mp_css_toast_now[0] != 0) {
            mp_css_toast_now[0] = 0;
            HSD_SisLib_803A70A0(mp_css_toast, mp_css_toast_id, "%s", "");
        }
        return;
    }
    if (strcmp(buf, mp_css_toast_now) != 0) {
        int chars = 0, k;
        float sx = 0.55f;
        mp_css_toast->font_size.x = 0.055f;
        strcpy(mp_css_toast_now, buf);
        mp_platform_slippi_ui_sis(buf, sis, sizeof sis);
        HSD_SisLib_803A70A0(mp_css_toast, mp_css_toast_id, "%s", sis);
        for (k = 0; sis[k] != 0; k++) {
            u8 c = (u8) sis[k];
            if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xEF)) && sis[k + 1] != 0) {
                k++;
            }
            chars++;
        }
        if (chars > 22) {
            sx = sx * 22.0f / (float) chars;
        }
        mp_css_toast->font_size.x = 0.055f * sx / 0.55f;   /* narrowed to the line's width, its height kept */
    }
    if (mp_css_toast_col_now != port) {
        mp_css_toast_col_now = port;
        HSD_SisLib_803A74F0(mp_css_toast, mp_css_toast_id, (GXColor*) &col_of[port & 3]);
    }
}

/* A new CSS scene: nothing is applied or sent yet, the text objects are gone with the old scene. */
void mp_css_group_reset(void)
{
    int d;
    mp_css_toast = NULL;
    mp_css_toast_col_now = -1;
    memset(mp_css_dtext, 0, sizeof mp_css_dtext);
    memset(mp_css_btext, 0, sizeof mp_css_btext);
    memset(mp_css_last_view_ok, 0, sizeof mp_css_last_view_ok);
    memset(mp_css_pending, 0, sizeof mp_css_pending);
    for (d = 0; d < 4; d++) {
        mp_css_name_now[d][0] = 1;
    }
    mp_css_applied_ok = 0;
    mp_css_rules_sent_ok = 0;
    memset(mp_css_puck_gone, 0, sizeof mp_css_puck_gone);
    mp_css_go = 0;
    mp_css_my_seen_open = 0;
    mp_css_leave_asked = 0;
    mp_css_seen_css = 0;
    mp_css_start_wait = 0;
}

/* Start was pressed with every door filled: ask the host (it answers with the stage select for everyone). */
int mp_css_back_intercept(void)
{
    if (!mp_css_group() || mp_platform_slippi_group_confirm_leave()) {
        return 0;
    }
    mp_css_leave_asked = 1;
    return 1;
}

int mp_css_start_intercept(void)
{
    if (!mp_css_group() || mp_css_go) {
        return 0;
    }
    OSReport("GROUPCSS Start pressed: asking the host\n");
    mp_platform_slippi_lobby_start();
    mp_css_start_wait = 30;
    return 1;
}

void mp_css_group_frame(void)
{
    if (!mp_css_group() || mnCharSel_804D6CF5 != 4 || mnCharSel_804D6CB0 == NULL || mnCharSel_804A0BC0[0] == NULL ||
        mnCharSel_803F0E8C[3].data == NULL)
    {
        return;
    }
    mp_lob_ok = mp_platform_slippi_lobby_get(mp_lob);
    if (!mp_lob_ok) {
        return;
    }
    if (mp_lob[4] == 0) {
        mp_css_seen_css = 1;
    }
    if (mnCharSel_804D6CF6 == 0 || mnCharSel_804D6CF6 == 1) {
        mp_css_text_frame();
    }
    if (mnCharSel_804D6CF6 == 0 && mp_platform_slippi_group_lost()) {
        mnCharSel_804D6CF6 = 2;   /* the session ended: back out (the VS menu reopens the Host / Join page) */
        return;
    }
    /* The join / leave toast stays on the bottom screen (the strip); mp_css_toast_frame is the top-screen version, which
     * did not look right and is not drawn. */
    if (mnCharSel_804D6CF6 > 1) {
        mp_css_toast = NULL;   /* name entry and the rules page free the CSS's texts; they are made again afterwards */
        memset(mp_css_dtext, 0, sizeof mp_css_dtext);
        memset(mp_css_btext, 0, sizeof mp_css_btext);
    }
    if (mp_css_leave_asked && mnCharSel_804D6CF6 == 0) {
        int a = mp_platform_slippi_group_confirm_state();
        if (a == 1) {
            mp_css_leave_asked = 0;
            mnCharSel_804D6CF6 = 2;   /* the answer was yes: leave for real (confirm_leave now says 1) */
        } else if (a == 2) {
            mp_css_leave_asked = 0;
        }
    }
    mp_css_hide_held_pucks();
    if (mnCharSel_804D6CF6 == 0) {
        mp_css_sync();
        mp_css_rules_frame();
        if (mp_css_start_wait > 0) {
            mp_css_start_wait--;
        }
        if (mp_css_seen_css && !mp_css_go && mp_lob[4] != 0) {
            /* the host sent everyone to the stage select */
            mp_css_go = 1;
            OSReport("GROUPCSS the host sent everyone to the stage select\n");
            mnCharSel_804D6CF6 = 1;
            mnCharSel_804D6CF2 = 0xFF;
        }
    }
}
/* ======== end group lobby on the CSS ======== */

'''

SSS_CODE = r'''
/* ---- the group's stage select: anyone's pick (the host's first) or Back ends it for everyone ---- */
static int mp_sss_remote, mp_sss_sent;
int mp_slippi_sss_remote_exit(void)
{
    int r = mp_sss_remote;
    mp_sss_remote = 0;
    return r;
}

void mp_slippi_sss_group_back(void)
{
    if (mp_slippi_group_mode()) {
        mp_platform_slippi_lobby_back();
    }
}

int mp_slippi_sss_sent(void)
{
    int r = mp_sss_sent;
    mp_sss_sent = 0;
    return r;
}

int mp_slippi_sss_group_frame(void)
{
    unsigned char l[88];
    int i, stage;
    if (!mp_slippi_group_mode() || !mp_platform_slippi_lobby_get(l)) {
        return 0;
    }
    if (mp_platform_slippi_group_lost()) {
        mnStageSel_804D6CAF = 0;   /* the session ended: back to the CSS, which leaves */
        mp_sss_remote = 1;
        gm_801A4B60();
        return 1;
    }
    if (mnStageSel_804D6CAF != 0) {
        /* The pick animation ("Now loading") is running here: tell the others at once. The cursor is already gone. */
        if (!mp_sss_remote && !mp_sss_sent) {
            mp_sss_sent = 1;
            mp_platform_slippi_lobby_stage(mnStageSel_803F06D0[mnStageSel_804D6CAE].stkind, 0);
        }
        return 0;
    }
    if (l[6] || l[4] == 2) {
        /* Someone else picked: show the same pick here, so this console also goes to "Now loading" and its cursor stops. */
        stage = (l[8] << 8) | l[9];
        mp_sss_remote = 1;
        OSReport("GROUPCSS stage select: stage %d was picked\n", stage);
        for (i = 0; i < 0x1E; i++) {
            if (mnStageSel_803F06D0[i].stkind == stage) {
                mnStageSel_804D6CAE = i;
                mnStageSel_804D6CA0 = 0x1100;
                mnStageSel_804D6CA4 = 0;
                mnStageSel_80259C28();
                mnStageSel_804D6CA0 = 0;
                if (mnStageSel_804D6CAF != 0) {
                    return 0;
                }
                break;
            }
        }
        mnStageSel_804D6CAF = 2;   /* a stage this console cannot show: leave straight away */
        sss_data->vs.start.rules.stkind = stage;
        gm_801A4B60();
        return 1;
    }
    if (l[4] == 0) {
        mnStageSel_804D6CAF = 0;
        mp_sss_remote = 1;
        gm_801A4B60();
        return 1;
    }
    return 0;
}

'''

FIXES = {
    'melee/mn/mncharsel.c': [
        ('static inline void getStickDelta(int port, f32* dx, f32* dy)\n{\n'
         '    f32 stick_x = loadStickValue(&HSD_PadCopyStatus[(u8) port].stickX);\n'
         '    f32 stick_y = (f32) HSD_PadCopyStatus[(u8) port].stickY;\n',
         'HSD_PadStatus* mp_css_pad(int port);\nint mp_css_remote(int door);\nint mp_css_unplugged(int door);\n'
         'int mp_css_can_edit_rules(void);\nint mp_css_start_intercept(void);\nint mp_css_back_intercept(void);\nint mp_css_hovering(int door);\nvoid mp_css_group_frame(void);\n'
         'void mp_css_group_reset(void);\n'
         'static inline void getStickDelta(int port, f32* dx, f32* dy)\n{\n'
         '    f32 stick_x = loadStickValue(&mp_css_pad((u8) port)->stickX);\n'
         '    f32 stick_y = (f32) mp_css_pad((u8) port)->stickY;\n', 1),
        ('            int port = cursor->x4;\n'
         '            trigger = HSD_PadCopyStatus[port].trigger;\n'
         '            buttons = HSD_PadCopyStatus[port].button;\n'
         '            getStickDelta(port, &dx, &dy);\n\n'
         '            if (HSD_PadCopyStatus[cursor->x4].err != 0) {\n',
         '            int port = cursor->x4;\n'
         '            trigger = mp_css_pad(port)->trigger;\n'
         '            buttons = mp_css_pad(port)->button;\n'
         '            getStickDelta(port, &dx, &dy);\n\n'
         '            if (mp_css_pad(cursor->x4)->err != 0) {\n', 1),
        ('                    if (mnCharSel_803F0DFC.doors[cursor->x4].p_kind == 0) {\n',
         '                    if (!mp_css_remote(cursor->x4) &&\n'
         '                        mnCharSel_803F0DFC.doors[cursor->x4].p_kind == 0) {\n', 1),
        ('                                                    if (HSD_PadCopyStatus\n'
         '                                                            [(u8) door]\n'
         '                                                                .err != 0)\n',
         '                                                    if (mp_css_unplugged(door))\n', 1),
        ('                    trigger |= HSD_PadCopyStatus[tmp].trigger;\n',
         '                    trigger |= mp_css_pad(tmp)->trigger;\n', 1),
        ('        trigger = HSD_PadCopyStatus[tag->port].trigger;\n',
         '        trigger = mp_css_pad(tag->port)->trigger;\n', 1),
        ('                    if (cx3 > -17.0f && cx3 < 15.0f && cursor->x10 > 22.0f) {\n',
         '                    if (mp_css_can_edit_rules() && cx3 > -17.0f && cx3 < 15.0f && cursor->x10 > 22.0f) {\n', 1),
        ('                if (cursor->xC < -25.5f && cursor->x10 > 22.0f) {\n',
         '                if (mp_css_can_edit_rules() && cursor->xC < -25.5f && cursor->x10 > 22.0f) {\n', 1),
        ('    ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);\n',
         '    ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);\n    mp_css_ctx = ctx;\n    mp_css_group_reset();\n', 1),
        ('void mnCharSel_CursorThink(HSD_GObj* gobj)\n{\n', CODE + 'void mnCharSel_CursorThink(HSD_GObj* gobj)\n{\n', 1),
        ('    mnCharSel_804D6CEC += 1;\n', '    mnCharSel_804D6CEC += 1;\n    mp_css_group_frame();\n', 1),
        ('        } else {\n            model->x8 = 2.7f + mnCharSel_804A0BC0[status - 1]->xC;\n',
         '        } else if (status <= 4) {\n            model->x8 = 2.7f + mnCharSel_804A0BC0[status - 1]->xC;\n', 1),
        ('    HSD_JObjSetTranslateZ(jobj, 1.0f);\n    HSD_JObjAnimAll(jobj);\n}\n\nvoid fn_80262F44',
         '    HSD_JObjSetTranslateZ(jobj, 1.0f);\n    HSD_JObjAnimAll(jobj);\n    if (model->x5 == MP_CSS_REMOTE_HELD) {\n        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);\n    }\n}\n\nvoid fn_80262F44', 1),
        # Start: in a group the host decides (a stage select for everyone), the scene does not end by itself.
        ('    case 2:\n        gm_801A4B60();\n        sfxBack();\n        break;\n',
         '    case 2:\n        if (mp_css_back_intercept()) {\n            mnCharSel_804D6CF6 = 0;\n            break;\n        }\n'
         '        gm_801A4B60();\n        sfxBack();\n        break;\n', 1),
        ('                if (mnCharSel_803F0DFC.doors[i].sel_icon >= 0x19) {\n                    goto hide;\n',
         '                if (mnCharSel_803F0DFC.doors[i].sel_icon >= 0x19 || mp_css_hovering(i)) {\n                    goto hide;\n', 1),
        ('        if (mnCharSel_804D6CF6 != 0) {\n            gm_801A4B60();\n            if (mnCharSel_804D6CF5 == 4) {\n',
         '        if (mnCharSel_804D6CF6 != 0 && mp_css_start_intercept()) {\n'
         '            mnCharSel_804D6CF6 = 0;\n'
         '        } else if (mnCharSel_804D6CF6 != 0) {\n            gm_801A4B60();\n            if (mnCharSel_804D6CF5 == 4) {\n', 1),
    ],
    # A group's name tags: a remote player's tag is its text (the sender's save id means nothing here), drawn as vanilla's.
    'melee/if/ifnametag.c': [
        ('#include "ifnametag.h"\n', '#include "ifnametag.h"\nint mp_slippi_tag_off(int slot);\nchar* mp_slippi_tag_text(int slot);\n', 1),
        ('                Player_GetNametagSlotID(i) == 0x78)\n', '                mp_slippi_tag_off(i))\n', 1),
        ('(un_804D6D70[*slot] || Player_GetNametagSlotID(*slot) != \'x\' ||', '(un_804D6D70[*slot] || !mp_slippi_tag_off(*slot) ||', 1),
        ('        Player_GetNametagSlotID(slot) == NAMETAG_DISABLED)\n', '        mp_slippi_tag_off(slot))\n', 1),
        ('                    un_804D6D78, -5000.0f, 0.0f,\n                    GetNameText(Player_GetNametagSlotID(slot)));\n', '                    un_804D6D78, -5000.0f, 0.0f,\n                    mp_slippi_tag_text(slot));\n', 1),
        ('                                GetNameText(Player_GetNametagSlotID(slot)));\n', '                                mp_slippi_tag_text(slot));\n', 1),
    ],
    # The puck's own think: a token held by another console (state 5) follows no cursor and is hidden.
    'melee/mn/mnstagesel.c': [
        ('#include "mnstagesel.h"\n',
         '#include <slippi_engine.h>\n#include <slippi_net_bridge.h>\n#include "mnstagesel.h"\n', 1),
        ('void mnStageSel_Scene_OnFrame(void)\n{\n',
         SSS_CODE + 'void mnStageSel_Scene_OnFrame(void)\n{\n    if (mp_slippi_sss_group_frame()) {\n        return;\n    }\n', 1),
        ('        sfxBack();\n        gm_801A4B60();\n    }\n    if (mnStageSel_804D6CAF == 2) {\n',
         '        sfxBack();\n        mp_slippi_sss_group_back();\n        gm_801A4B60();\n    }\n    if (mnStageSel_804D6CAF == 2) {\n', 1),
    ],
}
