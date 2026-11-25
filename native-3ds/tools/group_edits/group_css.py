"""Melee -> Group: the vanilla four-door character select as the lobby of a local wireless group.

Every patch is inert unless mp_group_mode() (a group session in the HANYU scene), so Solo and the rest of the game are
untouched. What it does to the VS character select (mncharsel.c):
  * mp_css_pad(port): console n drives door n with its only pad (pad 0); the other ports read unplugged channels, so
    their cursors take the vanilla "no controller" branch and stay hidden (cursors are private).
  * mp_css_group_frame(), once per CSS frame: mirrors the host's lobby into the doors (portraits, costumes, Human/CPU,
    teams, CPU levels, closed doors) and sends this console's door edits to the host; draws each console's name
    under its door (not in builds made with --no-door-names); sends the host's rules; leaves for the stage
    select when the host says so.
  * A door another console sits on is that console's: vanilla's "no controller" branch never gives it a random fighter
    here, and this console never sends its fighter, costume or tag (only Human / CPU / closed and the team, which any
    player may change; the host enforces the same rule).
  * Start no longer ends the scene: it asks the host, which answers with the stage select for everyone.
  * Rules and the Teams switch are the host's only.
  * A console in a name tag list, name entry or the rules menu, or holding a CPU level or handicap slider, is busy (the
    lobby's busy bit): READY waits on every console and the host refuses Start until it is done. A console watching a
    match in progress shows no READY either.
  * A console that joined during a match watches it (and its results screen) from a read-only CSS: its cursor moves,
    Back is its only action.
  * The stars over the doors are the lobby's (the host counts them after each match, clears a port's when its name tag
    changes, keeps them when a console leaves and comes back), so every console shows the same, late joiners included.
  * The Human/CPU/closed click ends on Human only where a console is present (a CPU elsewhere), as on a GameCube.
  * A door in use on another console (its slider held, its name tag list open, its name entry, its token in hand: the
    lobby's in_use, sent with the busy bit) cannot be switched between Human, CPU and closed here, as on one GameCube;
    the host refuses it too. Each door edit carries the door's kind before it, so the host drops an edit made from an
    older view instead of undoing another console's switch.
  * A joiner shows the host's rules: every rules read in mncharsel.c goes through mp_css_game_rules() (the banner, the
    handicap mode), and each door's handicap is the value of the console that owns it (mp_css_auto_hc, the lobby).
The stage select (mnstagesel.c) forwards the first pick and Back to the lobby and leaves when the host does.
Name entry (mnnamenew.c) opened for this console's door reads pad 0 (mp_group_menu_pad).
"""

CODE = r'''
/* ======== group lobby on the CSS (tools/group_edits/group_css.py) ======== */
#include <string.h>
#include <group_bridge.h>
#include <group_engine.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>
void gm_80167BC8(VsModeData* vs_data);
typedef struct MpCssDoor { u8 kind, ckind, costume, team, cpu_level; } MpCssDoor;

#define MP_CSS_REMOTE_HELD 5   /* token state: held by another console (a cursor holding a token is port + 1, 1 to 4) */
static MpGroupLobby mp_lob;       /* the newest lobby snapshot (group_bridge.h) */
static int mp_lob_ok;
static int mp_css_ctx = -1;
static int mp_css_go;             /* the host sent us to the stage select */
static int mp_css_seen_css;       /* saw the lobby in its CSS phase since this scene began */
static int mp_css_start_wait;
static int mp_css_leave_asked;   /* Back is waiting for the host's answer to "End the session?" */
static int mp_css_busy_now;      /* this console is busy: name tags, name entry, rules, a slider (the lobby's busy bit) */
static int mp_css_busy_wait;
static int mp_css_doors_now;     /* the doors this console is using (a slider, a name tag list, name entry, a held token) */
static int mp_css_doors_sent;    /* ... as last told to the host */
static int mp_css_naming = -1;   /* the door whose name entry this console opened, until the CSS is back */
static int mp_css_ruling;        /* this console opened the rules menu, until the CSS is back */
static int mp_css_why_sent;      /* the busy reason last sent (LOBBY_BUSY_*: 1 slider, 2 name tags, 3 rules, 4 name entry,
                                 * 6 a dialog) */
static HSD_PadStatus mp_css_no_pad;   /* what the CSS reads while a top-screen dialog is up: nothing pressed */
#define MP_CSS_DOOR 15     /* a door as the lobby holds it: kind, fighter, costume, team, level, chosen, tag[8], handicap */
static u8 mp_css_last_view[4][MP_CSS_DOOR], mp_css_last_view_ok[4];
static u8 mp_css_applied[4][MP_CSS_DOOR], mp_css_applied_ok;
static u8 mp_css_hc_shown[4];    /* each door's handicap as the lobby last showed it (auto handicap of a door not ours) */
HSD_Text* mp_css_banner;         /* the rules banner at the top (the CSS scene's setup keeps it here) */
static int mp_css_banner_key = -1;
static u8 mp_css_pending[4];
static MpGroupRules mp_css_rules_sent;
static int mp_css_rules_sent_ok;
static HSD_Text* mp_css_dtext[4];       /* the name under each door */
static HSD_Text* mp_css_plate[4];       /* the dark plate behind it */
static int mp_css_dname[4];
static char mp_css_name_now[4][32];
static int mp_css_col_now[4];

static int mp_css_group(void) { return mp_group_mode(); }
static int mp_css_my(void) { int i = mp_platform_slippi_net_local_index(); return i < 0 || i > 3 ? 0 : i; }
static int mp_css_present(int d) { return mp_lob_ok && ((mp_lob.present >> d) & 1); }
/* Another console sits on this door's port and it is Human in the lobby: its fighter, costume and tag are that
 * console's own. */
static int mp_css_owned_elsewhere(int d) { return d != mp_css_my() && mp_css_present(d) && mp_lob.door[d].kind == 0; }

/* The rules this character select shows. Every gmMainLib_GetGameRules() in mncharsel.c comes here (a macro at the top
 * of the file; this file calls the real one as (gmMainLib_GetGameRules)()). On a joiner it is a copy of its own rules
 * with the host's mode, time and stock limits and handicap mode, so the banner and the handicap sliders show the
 * host's rules. The joiner's own rules (and save) are never changed. */
GameRules* mp_css_game_rules(void)
{
    static GameRules over;
    GameRules* real = (gmMainLib_GetGameRules)();
    if (!mp_lob_ok || mp_lob.host || !mp_lob.rules.melee_valid) {
        return real;
    }
    over = *real;
    over.mode = mp_lob.rules.mode;
    over.time_limit = mp_lob.rules.time_limit;
    over.stock_count = mp_lob.rules.stock_count;
    over.stock_time_limit = mp_lob.rules.stock_time_limit;
    over.handicap = mp_lob.rules.handicap;
    return &over;
}

/* Auto handicap (every gm_801685D4 in mncharsel.c comes here). Each door's value comes from one console's save, the
 * one the match will use: this console's own Human door from this save (its tag, or its port's value), a CPU door from
 * the host's save, any other door as the lobby last showed it. */
s8 mp_css_auto_hc(u8 port, u8 tag)
{
    s8* h;
    int kind;
    if (!mp_lob_ok || port > 3) {
        return (gm_801685D4)(port, tag);
    }
    if (mp_css_game_rules()->handicap != 1) {
        return 0;
    }
    kind = mnCharSel_803F0DFC.doors[port].p_kind;
    if ((kind == 0 && port == mp_css_my()) || (kind == 1 && mp_lob.host)) {
        h = gmMainLib_8015CE44(port, tag);
        return h != NULL ? *h : 5;
    }
    return mp_css_hc_shown[port];
}

/* Move a door's handicap slider to the value the door holds now (as the name tag list does after a tag is chosen). */
static void mp_css_show_hc(int d)
{
    HSD_JObj* j;
    s32 h;
    if (mp_css_game_rules()->handicap == 0 || mnCharSel_803F0DFC.doors[d].p_kind == 3 ||
        mnCharSel_803F0DFC.doors[d].is_hold_handicap_slider) {
        return;
    }
    h = getHandicapValue(d);
    j = animateJoint(mnCharSel_804D6CC0, mnCharSel_803F0DFC.doors[d].cpuslider_joint, TOBJ_MASK, (f32) h);
    HSD_JObjSetTranslateX(j, 1.25f * (f32) (h - 1));
}

/* Move a CPU door's level slider (its number and its knob) to the level the door holds now. Vanilla redraws the slider
 * only when a door changes between Human, CPU and closed (8025DB34), and places the knob only in the scene setup and
 * while dragging, so a level set on another console, or one already set when this CSS was built, needs this. The joint
 * is cpuslider2 with handicap on, else cpuslider (as in the setup). A slider this console is dragging is left alone. */
static void mp_css_show_level(int d)
{
    CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
    HSD_JObj* j;
    s32 level = mnCharSel_804D6CB0->vs.start.players[d].cpu_level;
    if (dr->p_kind != 1 || dr->is_hold_cpu_slider) {
        return;
    }
    if (level < 1) {
        level = 1;
    }
    j = animateJoint(mnCharSel_804D6CC0, mp_css_game_rules()->handicap != 0 ? dr->cpuslider2_joint : dr->cpuslider_joint,
                     TOBJ_MASK, (f32) level);
    HSD_JObjSetTranslateX(j, 1.25f * (f32) (level - 1));
}

/* A door's handicap as the lobby holds it: 0 with handicap off, else 1..9 (auto: mp_css_auto_hc; fixed: the slider). */
static u8 mp_css_read_hc(int d, int kind)
{
    PlayerInitData* p = &mnCharSel_804D6CB0->vs.start.players[d];
    int mode = mp_css_game_rules()->handicap, h;
    if (kind == 2 || mode == 0) {
        return 0;
    }
    h = mode == 1 ? mp_css_auto_hc(d, p->nametag) : p->handicap;
    return h >= 1 && h <= 9 ? h : 0;
}

/* The pad a door's cursor reads: my door gets pad 0 (the only real pad), every other port an unplugged channel. */
HSD_PadStatus* mp_css_pad(int port)
{
    int my;
    if (!mp_css_group()) {
        return &HSD_PadCopyStatus[port];
    }
    if (mp_group_dialog_up()) {
        return &mp_css_no_pad;   /* the dialog reads pad 0 itself; the CSS sees no buttons and the stick at rest */
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
    return !mp_css_group() || !mp_lob_ok || mp_lob.host != 0;
}

/* The cursor is on the Rules banner or the Teams switch: 1 when this console may use it (as vanilla). Elsewhere the
 * cursor does not point at it, and A there plays the "not allowed" sound (vanilla's, as for a locked stage). */
int mp_css_rules_hit(u32 trigger)
{
    if (mp_css_can_edit_rules()) {
        return 1;
    }
    if (trigger & (1 << 8)) {   /* A */
        lbAudioAx_80024030(3);
        OSReport("GROUPCSS the rules and the Teams switch are the host's: not allowed here\n");
    }
    return 0;
}

/* A console that joined during a match watches it from the CSS until it ends: the CSS is read-only there. */
static int mp_css_watching(void)
{
    return mp_css_group() && mp_lob_ok && mp_lob.watching;
}

/* The buttons a cursor acts on this frame (mnCharSel_CursorThink, after the cursor moved and before any action). While
 * watching, the cursor still moves and its only action is A on the red Back button (top right, the vanilla test);
 * nothing else responds: no puck, no Human / CPU / closed, no slider, no team, no name tag list, no costume. Holding B
 * to leave reads the held buttons, not these, and stays. */
u32 mp_css_trigger(int hand, f32 x, f32 y, u32 trigger)
{
    if (!mp_css_watching()) {
        return trigger;
    }
    if (hand != 1 && x > 17.3f && y > 22.0f) {   /* hand 1: holding a puck or a slider */
        return trigger & HSD_PAD_A;
    }
    return 0;
}

/* READY waits while another console is busy (a name tag list, name entry, the rules page, a slider in hand; vanilla
 * waits for this console's own tag list), and never shows while watching a match or under a dialog ("End the
 * session?"): Start does nothing there. */
int mp_css_hide_ready(void)
{
    if (mp_css_group() && mp_group_dialog_up()) {
        return 1;
    }
    return mp_css_group() && mp_lob_ok && ((mp_lob.busy & 0xF & ~(1u << mp_css_my())) != 0 || mp_lob.watching);
}

/* Tell the host while this console is busy: a name tag list open, a CPU level or handicap slider in hand, name entry or
 * the rules page. The host refuses Start until it is back. Name entry and the rules page run inside this scene
 * (pending-scene byte 4 or 3 for the frame they are asked for, then 5 while they are up): the bit is set when a list
 * opens, a slider is grabbed or one of them begins, and cleared only when the CSS is back (byte 0) with every list
 * shut and no slider held. The lobby's copy of the bit is checked every frame, so a lost command is sent again. */
static void mp_css_busy_frame(void)
{
    static int idle_resend;
    int my = mp_css_my(), i, doors = 0, resend = 0, why = 0;
    if (mnCharSel_804D6CF6 == 0) {
        mp_css_busy_now = 0;
        mp_css_naming = -1;
        mp_css_ruling = 0;
        for (i = 0; i < 4; i++) {
            if (mnCharSel_803F0E8C[i].data != NULL && mnCharSel_803F0E8C[i].data->state != 0) {
                mp_css_busy_now = 1;
                doors |= 1 << i;
                why = 2;                                  /* a name tag list (wins over a slider) */
            }
            /* only this console's cursor acts here, so a held slider is ours whichever door it belongs to */
            if (mnCharSel_803F0DFC.doors[i].is_hold_cpu_slider || mnCharSel_803F0DFC.doors[i].is_hold_handicap_slider) {
                mp_css_busy_now = 1;
                doors |= 1 << i;
                if (why == 0) {
                    why = 1;
                }
            }
        }
        /* this console's cursor holds something (vanilla's mnCharSel_804A0BC0[door]->x5 == 1 on the toggle) */
        if (mnCharSel_804A0BC0[my] != NULL && mnCharSel_804A0BC0[my]->x5 == 1) {
            doors |= 1 << my;
        }
    } else if (mnCharSel_804D6CF6 == 3 || mnCharSel_804D6CF6 == 4) {
        mp_css_busy_now = 1;
        if (mnCharSel_804D6CF6 == 4) {
            mp_css_naming = mnCharSel_804D6CF9 & 3;   /* name entry for this door's tag */
        } else {
            mp_css_ruling = 1;
        }
    }
    if (mnCharSel_804D6CF6 != 0) {
        /* 3 rules or 4 name entry, as seen when asked for; kept while it is up (byte 5) */
        why = mp_css_naming >= 0 ? 4 : mp_css_ruling ? 3 : mp_css_busy_now ? 1 : 0;
    }
    if (mp_group_dialog_up()) {
        mp_css_busy_now = 1;   /* "End the session?" is up: busy like the rules page (READY waits, Start is refused) */
        why = 6;
    }
    if (mp_css_naming >= 0) {
        doors |= 1 << mp_css_naming;                  /* the door is in use while its name entry lasts */
    }
    mp_css_doors_now = doors;
    if (mp_css_busy_wait > 0) {
        mp_css_busy_wait--;
        return;
    }
    /* resend when the lobby lacks our busy bit or one of our doors; a lost release of our own door (the only one a held
     * token marks without the busy bit) is sent again now and then */
    if (mp_css_busy_now && why == 0) {
        why = 1;
    }
    resend = (int) ((mp_lob.busy >> my) & 1) != mp_css_busy_now || doors != mp_css_doors_sent ||
             (doors & ~mp_lob.in_use & 0xF) != 0 || why != mp_css_why_sent ||
             (mp_css_busy_now && mp_lob.busy_why[my] != why);
    if (!resend && !mp_css_busy_now && doors == 0 && ((mp_lob.in_use >> my) & 1) && ++idle_resend >= 60) {
        resend = 1;
    }
    if (resend) {
        idle_resend = 0;
        OSReport("GROUPCSS busy %d why %d doors %x (1 slider, 2 name tag list, 3 rules, 4 name entry, 6 dialog; doors in use)\n",
                 mp_css_busy_now, why, doors);
        mp_platform_group_lobby_busy(why | doors << 4);
        mp_css_doors_sent = doors;
        mp_css_why_sent = why;
        mp_css_busy_wait = 15;
    }
}

/* Door d is in another console's hands (the lobby's in_use, and not ours): as on one GameCube, its Human / CPU / closed
 * switch does nothing here (the toggle's condition in mnCharSel_CursorThink). */
int mp_css_door_in_use(int d)
{
    return mp_css_group() && mp_lob_ok && d >= 0 && d < 4 && ((mp_lob.in_use >> d) & 1) && !((mp_css_doors_now >> d) & 1);
}

/* A fighter this save has not unlocked can be picked on another console. Every fighter has a fixed tile here (a "?" one,
 * or a hidden one at a row end), so the door and its puck use that tile; but applying a fighter flashes its tile
 * (texture frame 10, then frame 0, the portrait), which would show the locked fighter's face on a "?" tile. Put the
 * tile back the way the character select draws a locked one (Scene_OnEnter: all animations at frame 30). */
static void mp_css_keep_locked_icon(int i)
{
    HSD_JObj* j;
    if (i < 0 || i >= 0x19 || icons[i].state != 0) {
        return;
    }
    icons[i].anim_timer = 0;
    lb_80011E24(mnCharSel_804D6CC0, &j, icons[i].joint_id_vs, -1);
    HSD_ForeachAnim(j, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF, 30.0f);
    HSD_JObjAnimAll(j);
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
            mp_css_keep_locked_icon(dr->sel_icon);
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
    int tok = mnCharSel_804A0BD0[d]->x5;
    if (tok == MP_CSS_REMOTE_HELD && mp_css_applied_ok) {
        memcpy(o, mp_css_applied[d], MP_CSS_DOOR);   /* another console holds this token: nothing here to report */
        return;
    }
    int placed = kind != 2 && dr->sel_icon < 0x19 && mnCharSel_804A0BD0[d]->x5 == 0 && p->ckind >= 0 &&
                 p->ckind < CKind_Playable_Count;
    /* The token is held over a fighter: the door shows it see-through, and the others should too. Over no fighter the
     * character select keeps the last one in sel_icon and only marks sel_icon_prev 0x19 (and hides the door's fighter),
     * so both must agree, or the others would keep showing the fighter it left (held over nothing, below). */
    int hover = kind != 2 && !placed && dr->sel_icon < 0x19 && dr->sel_icon_prev == dr->sel_icon &&
                mnCharSel_804A0BD0[d]->x5 == mp_css_my() + 1 &&
                icons[dr->sel_icon].char_kind >= 0 && icons[dr->sel_icon].char_kind < CKind_Playable_Count;
    int held = kind != 2 && !placed && !hover && mnCharSel_804A0BD0[d]->x5 == mp_css_my() + 1;
    chosen = placed ? 1 : hover ? 2 : held ? 3 : 0;
    o[0] = kind;
    o[1] = placed ? p->ckind : hover ? icons[dr->sel_icon].char_kind : 0;
    /* the costume; with Teams the team's (what the door shows and the match uses) */
    o[2] = chosen && chosen != 3 ? (mnCharSel_804D6CB0->vs.start.rules.is_teams ? p->color : dr->costume) : 0;
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
    o[14] = mp_css_read_hc(d, kind);
}

static void mp_css_view_door(int d, u8* o)
{
    const MpGroupDoor* v = &mp_lob.door[d];
    int chosen = v->kind != 2 ? v->chosen : 0;   /* 0 none, 1 placed, 2 held over a fighter, 3 held over nothing */
    o[0] = v->kind;
    o[1] = chosen && chosen != 3 ? v->fighter : 0;
    o[2] = chosen && chosen != 3 ? v->color : 0;
    o[3] = v->kind == 2 ? 0 : v->team;
    o[4] = v->kind == 1 ? v->level : 3;
    o[5] = chosen;
    memcpy(o + 6, v->tag, 8);
    o[14] = v->kind != 2 ? v->handicap : 0;
}

/* A door as 15 bytes (kind, fighter, costume, team, level, chosen, tag[8], handicap) to the host. */
static void mp_css_send_door(int d, const u8* o)
{
    MpGroupDoor door;
    memset(&door, 0, sizeof door);
    memcpy(&door, o, 14);
    door.handicap = o[14];
    /* the door's kind before this edit (as last sent or applied), so the host can drop an edit made from an older view */
    door.spare = mp_css_applied_ok ? mp_css_applied[d][0] + 1 : 0;
    mp_platform_group_lobby_set_door(d, &door);
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
    mp_css_hc_shown[d] = o[14];
    if (mp_css_game_rules()->handicap == 2 && o[14] >= 1 && o[14] <= 9) {
        mnCharSel_804D6CB0->vs.start.players[d].handicap = o[14];   /* the fixed slider's value */
    }
    mp_css_show_hc(d);
    mp_css_show_level(d);
    if (o[5] == 3) {
        mnCharSel_804A0BD0[d]->x5 = MP_CSS_REMOTE_HELD;   /* in someone's hand, over nothing: no puck here */
    }
    if (o[5] == 2) {
        /* A preview: the token is held over this fighter (what the local hand's door shows while it hovers). The
         * fighter may be locked in this save: it still has a tile here (a "?" one, see mp_css_keep_locked_icon), so the
         * door shows it as for a placed one. The token is marked held in every case, or this CSS would read it as
         * dropped (no fighter under it) and, for a CPU's door, which anyone may edit, send that to the lobby: the
         * holding console's puck would fall out of its hand. */
        int i, found = -1;
        for (i = 0; i < 0x19; i++) {
            if (icons[i].char_kind == (s8) o[1] && (found < 0 || icons[i].state >= 1)) {
                found = i;
                if (icons[i].state >= 1) {
                    break;
                }
            }
        }
        if (found >= 0) {
            u8 save = mnCharSel_804D6CF6;
            mnCharSel_804D6CF6 = 4;
            mnCharSel_803F0DFC.doors[d].sel_icon = found;
            mnCharSel_803F0DFC.doors[d].costume = o[2];
            mnCharSel_804A0BD0[d]->x5 = MP_CSS_REMOTE_HELD;
            mnCharSel_8025DB34(d);
            mp_css_keep_locked_icon(found);
            mnCharSel_804D6CF6 = save;
        } else {
            mnCharSel_804A0BD0[d]->x5 = MP_CSS_REMOTE_HELD;   /* no tile for it here: as held over nothing */
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
    static int hc_mode = -1;
    int d;
    if (mp_css_game_rules()->handicap != hc_mode) {
        /* The host's handicap mode changed: every door's handicap now reads differently here (mp_css_read_hc), but no
         * one moved a slider. Take the new reading as already applied, so it is not sent as an edit (a joiner's saved
         * value would replace a CPU's handicap in the lobby), and show the lobby's value instead. This console's own
         * Human door keeps the change: its handicap is this console's to give (its save or its slider). */
        if (hc_mode != -1 && mp_css_applied_ok) {
            for (d = 0; d < 4; d++) {
                u8 now[MP_CSS_DOOR];
                if (d == mp_css_my() && mnCharSel_803F0DFC.doors[d].p_kind == 0) {
                    continue;
                }
                mp_css_read_door(d, now);
                mp_css_applied[d][14] = now[14];
                mp_css_last_view_ok[d] = 0;
            }
        }
        hc_mode = mp_css_game_rules()->handicap;
    }
    for (d = 0; d < 4; d++) {
        u8 now[MP_CSS_DOOR], view[MP_CSS_DOOR];
        mp_css_read_door(d, now);
        if (mp_css_applied_ok && memcmp(now, mp_css_applied[d], MP_CSS_DOOR) != 0 &&
            (mp_css_owned_elsewhere(d) || mp_css_watching())) {
            /* Another console's door changed here without that console: only Human / CPU / closed and the team are
             * anyone's to change. Anything else (its fighter, costume, tag or hand) is this CSS's own doing, never a
             * player's edit: do not send it, show the lobby's door again. While watching a match, the CSS is read-only
             * (mp_css_trigger) and no door is this console's to change: nothing is sent, every door is shown again. */
            mp_css_view_door(d, view);
            if (mp_css_watching() || (now[0] == mp_css_applied[d][0] && now[3] == mp_css_applied[d][3])) {
                OSReport("GROUPCSS door %d changed here without its console: restored\n", d);
                mp_css_put_door(d, view);
                mp_css_read_door(d, mp_css_applied[d]);
                mp_css_last_view_ok[d] = 0;
                continue;
            }
        }
        if (!mp_css_applied_ok || memcmp(now, mp_css_applied[d], MP_CSS_DOOR) != 0) {
            if (mp_css_applied_ok) {
                mp_css_send_door(d, now);
                OSReport("GROUPCSS send door %d: kind %d fighter %d color %d team %d level %d chosen %d handicap %d\n", d,
                         now[0], now[1], now[2], now[3], now[4], now[5], now[14]);
                mp_css_pending[d] = 12;
                mp_css_last_view_ok[d] = 0;
            }
            memcpy(mp_css_applied[d], now, MP_CSS_DOOR);
            if (!mp_css_applied_ok && d == mp_css_my()) {
                /* A CSS that was just (re)built. Back from name entry (the door is Human in the lobby), what this
                 * console's door holds (a new name tag, a fighter) is authoritative: tell the host if the lobby does
                 * not have it yet. Otherwise the lobby's door wins: joining opens no door (a closed door stays closed
                 * and a CPU stays a CPU until this player opens or clicks it, as with a controller plugged in), so
                 * a door left open in this console's VS data, e.g. from an earlier session, is not sent. */
                mp_css_view_door(d, view);
                if (view[0] != 0 || (now[5] == 0 && now[6] == 0) || mp_css_watching()) {
                    mp_css_put_door(d, view);
                    mp_css_read_door(d, mp_css_applied[d]);
                } else if (memcmp(now, view, MP_CSS_DOOR) != 0) {
                    mp_css_send_door(d, now);
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
        if (mp_css_last_view_ok[d] && memcmp(view, mp_css_last_view[d], MP_CSS_DOOR) == 0) {
            continue;
        }
        memcpy(mp_css_last_view[d], view, MP_CSS_DOOR);
        mp_css_last_view_ok[d] = 1;
        if (memcmp(view, now, 6) != 0 || view[14] != now[14]) {
            OSReport("GROUPCSS apply door %d: kind %d fighter %d color %d team %d level %d chosen %d handicap %d\n", d,
                     view[0], view[1], view[2], view[3], view[4], view[5], view[14]);
            mp_css_put_door(d, view);
            if (view[5] == 2) {
                memcpy(mp_css_applied[d], view, MP_CSS_DOOR);   /* a mirrored hover: what we show, not a local edit */
            } else {
                mp_css_read_door(d, mp_css_applied[d]);
            }
        }
    }
    mp_css_applied_ok = 1;
}

/* ---- rules: the host sends its own, a joiner shows the host's teams switch ---- */
/* Put a door's sliders where the CSS setup puts them for the handicap mode in force (mnCharSel setup, the
 * cpuslider loop). The same joint changes job with the mode: with handicap on, cpuslider is the handicap slider and
 * cpuslider2 the CPU level; with it off, cpuslider is the CPU level. Only the setup places them, so a joiner whose
 * host changes the mode on its CSS must place them again, or a CPU level slider keeps the handicap's position. */
static void mp_css_place_sliders(int d)
{
    HSD_JObj* j;
    s32 level = mnCharSel_804D6CB0->vs.start.players[d].cpu_level;
    u8 level_joint = mnCharSel_803F0DFC.doors[d].cpuslider_joint;
    if (level == 0) {
        level = 1;
    }
    if (mp_css_game_rules()->handicap != 0) {
        lb_80011E24(mnCharSel_804D6CC0, &j, mnCharSel_803F0DFC.doors[d].cpuslider_joint, -1);
        HSD_JObjSetTranslateX(j, 1.25f * (f32) (getHandicapValue(d) - 1));
        level_joint = mnCharSel_803F0DFC.doors[d].cpuslider2_joint;
    }
    lb_80011E24(mnCharSel_804D6CC0, &j, level_joint, -1);
    HSD_JObjSetTranslateX(j, 1.25f * (f32) (level - 1));
}

static void mp_css_rules_frame(void)
{
    struct GameRules* gr = (gmMainLib_GetGameRules)();
    VsModeData* vs = &mnCharSel_804D6CB0->vs;
    static VsModeData scratch;
    MpGroupRules r;
    if (mp_lob_ok && mp_lob.host == 0) {
        if (mp_lob.rules.teams != vs->start.rules.is_teams) {
            vs->start.rules.is_teams = mp_lob.rules.teams;
            mnCharSel_8025EE8C(mnCharSel_804D6CB0->match_type);
        }
        if (mp_lob.rules.melee_valid) {
            /* The banner shows mp_css_game_rules(), the host's: write it again whenever those rules change (the scene's
             * setup wrote it from whatever the lobby held then). A new handicap mode also redraws the doors' sliders,
             * through the door's Human/CPU/closed change path. */
            int key = mp_lob.rules.mode | mp_lob.rules.time_limit << 8 | mp_lob.rules.stock_count << 16 |
                      (mp_lob.rules.stock_time_limit & 0x3F) << 24 | (mp_lob.rules.handicap & 3) << 30;
            if (key != mp_css_banner_key) {
                int d;
                int relayout = mp_css_banner_key != -1 && ((mp_css_banner_key >> 30) & 3) != mp_lob.rules.handicap;
                mp_css_banner_key = key;
                if (mp_css_banner != NULL) {
                    mnCharSel_8025BD30();
                    HSD_SisLib_803A6368(mp_css_banner, 0x4A);
                }
                for (d = 0; relayout && d < 4; d++) {
                    u8 save = mnCharSel_804D6CF6;
                    mnCharSel_804D6CF6 = 4;   /* no sounds */
                    /* A closed door has no label or slider showing, and the change path would play its closing
                     * animation again (8025DB34 with p_kind_prev 0xFF): only its sliders are placed, for when it opens. */
                    if (mnCharSel_803F0DFC.doors[d].p_kind != 3) {
                        mnCharSel_803F0DFC.doors[d].p_kind_prev = 0xFF;
                        mnCharSel_8025DB34(d);   /* the label's frame; its slider timer settles it 10 frames on */
                    }
                    mp_css_place_sliders(d);
                    mp_css_show_hc(d);
                    mp_css_show_level(d);
                    mnCharSel_804D6CF6 = save;
                }
                OSReport("GROUPCSS the host's rules: mode %d time %d stocks %d handicap %d\n", mp_lob.rules.mode,
                         mp_lob.rules.time_limit, mp_lob.rules.stock_count, mp_lob.rules.handicap);
            }
        }
        return;
    }
    memset(&r, 0, sizeof r);
    r.time_mode = gr->mode != 1;
    r.stocks = gr->stock_count;
    r.minutes = r.time_mode ? gr->time_limit : gr->stock_time_limit;
    r.items = gmMainLib_GetGamePrefs()->item_freq == 255 ? 0 : gmMainLib_GetGamePrefs()->item_freq + 1;   /* 0 off, else frequency + 1 */
    r.teams = vs->start.rules.is_teams ? 1 : 0;
    r.friendly_fire = gr->friendly_fire ? 1 : 0;
    /* The match's rules exactly as vanilla builds them from this console's rules menu when a VS match starts
     * (match kind, timer, items, damage ratio, pause...), on a copy; the match block takes its rules fields from them. */
    scratch = *vs;
    gm_80167BC8(&scratch);
    r.melee_valid = 1;
    memcpy(r.melee, &scratch.start.rules, sizeof r.melee);
    r.mode = gr->mode;
    r.time_limit = gr->time_limit;
    r.stock_count = gr->stock_count;
    r.stock_time_limit = gr->stock_time_limit;
    r.handicap = gr->handicap;
    r.stage_sel = gr->stage_sel;   /* the host's Stage Selection rule decides who picks the stage for everyone */
    if (!mp_css_rules_sent_ok || memcmp(&r, &mp_css_rules_sent, sizeof r) != 0) {
        mp_css_rules_sent = r;
        mp_css_rules_sent_ok = 1;
        mp_platform_group_lobby_rules(&r);
    }
}

/* ---- the name under each door ----
 * A dark plate under the door's own name plate with the console's name on it in the cursor colour, centred and, when
 * too long, squeezed into the plate as the results screen's names are (group_results.py: SIS alignment 1, fitting,
 * kerning). No signal bars (owner, 2026-11-25: the bottom screen's are enough). The plate is a SIS text with no
 * characters, only its background box (bg_color). Sizes and offsets in CSS units, from the door's left edge and the
 * door's name joint (the vanilla name plate). */
#define MP_DOOR_PLATE_X 0.7f      /* from the door's left edge */
#define MP_DOOR_PLATE_Y 6.4f      /* below the name joint: under the door, above the screen edge */
#define MP_DOOR_PLATE_W 13.4f
#define MP_DOOR_PLATE_H 2.4f
#define MP_DOOR_NAME_PAD 0.4f     /* the name's box: the plate less this on each side */
#define MP_DOOR_NAME_Y 0.34f      /* the name's top, from the plate's top: centred on its 2.4 (32 x size tall) */
#define MP_DOOR_NAME_SIZE 0.054f  /* was 0.046 with the signal bars beside it */

/* A box with no characters: only its background shows. */
static HSD_Text* mp_css_box(float w, float h)
{
    HSD_Text* t = HSD_SisLib_803A6754(0, mp_css_ctx);
    if (t == NULL) {
        return NULL;
    }
    t->font_size.x = 0.05f;
    t->font_size.y = 0.05f;
    t->box_size_x = w / 0.05f;
    t->box_size_y = h / 0.05f;
    HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
    return t;
}

static void mp_css_box_color(HSD_Text* t, u8 r, u8 g, u8 b, u8 a)
{
    t->bg_color.r = r;
    t->bg_color.g = g;
    t->bg_color.b = b;
    t->bg_color.a = a;
}

static int mp_css_door_text_make(int d)
{
    HSD_Text* t;
    /* drawn in the order made: the plate, then the name over it */
    if ((mp_css_plate[d] = mp_css_box(MP_DOOR_PLATE_W, MP_DOOR_PLATE_H)) == NULL) {
        return 0;
    }
    mp_css_box_color(mp_css_plate[d], 25, 36, 51, 0xE0);   /* the bottom screen's card colour */
    t = HSD_SisLib_803A6754(0, mp_css_ctx);
    if (t == NULL) {
        return 0;
    }
    t->default_alignment = 1;   /* centred on pos_x, as the results screen's names */
    t->default_fitting = 1;     /* squeezed into the box when too wide */
    t->default_kerning = 1;
    t->font_size.x = MP_DOOR_NAME_SIZE;
    t->font_size.y = MP_DOOR_NAME_SIZE;
    t->box_size_x = (MP_DOOR_PLATE_W - 2 * MP_DOOR_NAME_PAD) / MP_DOOR_NAME_SIZE;
    t->box_size_y = 32.0f;
    mp_css_dname[d] = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
    mp_css_dtext[d] = t;
    mp_css_name_now[d][0] = 1;
    mp_css_col_now[d] = -1;
    return 1;
}

static void mp_css_door_text_gone(void)
{
    memset(mp_css_dtext, 0, sizeof mp_css_dtext);
    memset(mp_css_plate, 0, sizeof mp_css_plate);
}

static void mp_css_text_frame(void)
{
    static const GXColor port_col[4] = { { 0xE5, 0x4C, 0x4C, 0xFF }, { 0x4B, 0x4C, 0xE5, 0xFF },
                                         { 0xFF, 0xCB, 0x00, 0xFF }, { 0x00, 0xB2, 0x00, 0xFF } };
    static const GXColor team_col[3] = { { 0xE5, 0x4C, 0x4C, 0xFF }, { 0x4B, 0x4C, 0xE5, 0xFF },
                                         { 0x00, 0xB2, 0x00, 0xFF } };
    int d;
    if (mp_css_ctx < 0 || !mp_platform_group_door_names()) {
        return;   /* no canvas yet, or a build without door names (--no-door-names) */
    }
    for (d = 0; d < 4; d++) {
        HSD_JObj* j;
        HSD_JObj* dj;
        Vec3 pos, dp;
        char raw[32], name[40];
        GXColor col;
        float px, py;
        int here = mp_css_present(d);
        CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
        if (mp_css_dtext[d] == NULL && !mp_css_door_text_make(d)) {
            mp_css_door_text_gone();   /* out of text objects: try again next frame */
            continue;
        }
        lb_80011E24(mnCharSel_804D6CC0, &j, mnCharSel_803F0E8C[d].name_jointl, -1);
        lb_8000B1CC(j, NULL, &pos);
        lb_80011E24(mnCharSel_804D6CC0, &dj, dr->door_joint, -1);
        lb_8000B1CC(dj, NULL, &dp);
        px = dp.x + MP_DOOR_PLATE_X;
        py = -pos.y + MP_DOOR_PLATE_Y;
        mp_css_plate[d]->pos_x = px;
        mp_css_plate[d]->pos_y = py;
        mp_css_plate[d]->pos_z = pos.z;
        mp_css_plate[d]->hidden = !here;
        mp_css_dtext[d]->pos_x = px + MP_DOOR_PLATE_W / 2;   /* the plate's centre (alignment 1) */
        mp_css_dtext[d]->pos_y = py + MP_DOOR_NAME_Y;
        mp_css_dtext[d]->pos_z = pos.z;
        mp_css_dtext[d]->hidden = !here;
        raw[0] = 0;
        if (here) {
            mp_platform_group_player_name(d, raw, sizeof raw);   /* already cut to 10 characters (name_clean) */
        }
        mp_platform_slippi_ui_sis(raw, name, sizeof name);
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
    int left = mp_platform_group_toast(buf, sizeof buf);
    int port = mp_platform_group_toast_port();
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

/* The stars over the doors are the lobby's: the host counts them after each match and clears a port's when its name tag
 * changes (vanilla's rules), so every console, late joiners included, shows the same. A door's are redrawn when they
 * change. While the host's results screen is still up, this console's own count from the same match stands. */
static void mp_css_stars_frame(void)
{
    u8* ko = mnCharSel_804D6CB0->ko_counts;
    int d;
    if (ko == NULL || mp_lob.phase == 3 || mnCharSel_804D6CF6 != 0) {
        return;
    }
    for (d = 0; d < 4; d++) {
        if (ko[d] == mp_lob.stars[d]) {
            continue;
        }
        ko[d] = mp_lob.stars[d];
        if (mnCharSel_803F0DFC.doors[d].p_kind != 3) {
            mnCharSel_8025D1C4(d, 2);
        }
    }
}

/* A new CSS scene: nothing is applied or sent yet, the text objects are gone with the old scene. */
void mp_css_group_reset(void)
{
    int d;
    mp_css_toast = NULL;
    mp_css_toast_col_now = -1;
    mp_css_door_text_gone();
    memset(mp_css_last_view_ok, 0, sizeof mp_css_last_view_ok);
    memset(mp_css_pending, 0, sizeof mp_css_pending);
    for (d = 0; d < 4; d++) {
        mp_css_name_now[d][0] = 1;
    }
    mp_css_applied_ok = 0;
    mp_css_rules_sent_ok = 0;
    memset(mp_css_puck_gone, 0, sizeof mp_css_puck_gone);
    memset(mp_css_hc_shown, 0, sizeof mp_css_hc_shown);
    mp_css_banner = NULL;
    mp_css_banner_key = -1;
    /* The lobby as it is now, before the scene's setup draws the banner and the doors' sliders (mp_css_game_rules);
     * outside a group, none, so the rules are this console's own. */
    mp_lob_ok = mp_css_group() && mp_platform_group_lobby_get(&mp_lob);
    mp_css_go = 0;
    mp_css_leave_asked = 0;
    mp_group_dialog_scene();
    memset(&mp_css_no_pad, 0, sizeof mp_css_no_pad);
    mp_css_seen_css = 0;
    mp_css_start_wait = 0;
    mp_css_doors_now = 0;
    mp_css_doors_sent = 0;
    mp_css_why_sent = 0;
}

/* Start was pressed with every door filled: ask the host (it answers with the stage select for everyone). */
int mp_css_back_intercept(void)
{
    if (!mp_css_group() || mp_platform_group_confirm_leave()) {
        return 0;
    }
    mp_css_leave_asked = 1;
    if (!mp_group_dialog_up() &&
        !mp_group_dialog_open("End the session?", "Yes", "No", 1))   /* No highlighted first */
    {
        OSReport("GROUPCSS no dialog for \"End the session?\": staying\n");
        mp_platform_group_confirm_answer(0);   /* no box: the safe answer is to keep the session */
    }
    return 1;
}

int mp_css_start_intercept(void)
{
    if (!mp_css_group() || mp_css_go) {
        return 0;
    }
    if (mp_css_watching()) {
        return 1;   /* a match is running without this console: Start does nothing */
    }
    OSReport("GROUPCSS Start pressed: asking the host\n");
    mp_platform_group_lobby_start();
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
    mp_lob_ok = mp_platform_group_lobby_get(&mp_lob);
    if (!mp_lob_ok) {
        return;
    }
    if (mp_lob.phase == 0) {
        mp_css_seen_css = 1;
    }
    mp_css_stars_frame();
    mp_css_busy_frame();
    if (mnCharSel_804D6CF6 == 0 || mnCharSel_804D6CF6 == 1) {
        mp_css_text_frame();
    }
    if (mnCharSel_804D6CF6 == 0 && mp_platform_group_lost()) {
        mp_group_dialog_close();
        mnCharSel_804D6CF6 = 2;   /* the session ended: back out (to "Choose a game.", which says why) */
        return;
    }
    /* The join / leave toast stays on the bottom screen (the strip); mp_css_toast_frame is the top-screen version, which
     * did not look right and is not drawn. */
    if (mnCharSel_804D6CF6 >= 3) {
        /* name entry and the rules page (3, 4, then 5 while open) free the CSS's texts; they are made again afterwards.
         * Not on 2 (Back): the texts stay, and Back is often answered by "End the session?" (back to 0), so dropping
         * them there would make a new set each time without freeing the old one, until the text memory runs out. */
        mp_css_toast = NULL;
        mp_css_banner = NULL;
        mp_css_door_text_gone();
    }
    if (mp_css_leave_asked && mnCharSel_804D6CF6 == 0) {
        int a, d = mp_group_dialog_poll();
        if (d > 0) {
            OSReport("GROUPCSS \"End the session?\": %s\n", d == 1 ? "Yes" : "No");
            mp_platform_group_confirm_answer(d == 1);
        }
        a = mp_platform_group_confirm_state();
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
        if (mp_css_seen_css && !mp_css_go && mp_lob.phase != 0) {
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

# The second half of the vanilla pick (mnStageSel_80259C28 after skip_randomize): the "Now loading" overlay, the pick
# state and the forward sound. Moved into a helper so the group's hidden pick can start it without the unlocked check.
SSS_LOADING_BODY = '''skip_randomize:

    gobj = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 5, 0x80);
    jobj = HSD_JObjLoadJoint(mnStageSel_804D6C98->xB0);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x87);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 0);
    HSD_JObjAddAnimAll(jobj, mnStageSel_804D6C98->xB4,
                       mnStageSel_804D6C98->xB8, mnStageSel_804D6C98->xBC);
    HSD_JObjReqAnimAll(gobj->hsd_obj, 0.0F);
    HSD_JObjAnimAll(gobj->hsd_obj);
    mnStageSel_804D6CAF = 1;
    mnStageSel_804D6CA4 = 0x1E;
    sfxForward();
}
'''
SSS_LOADING = '''
/* Another console picked a stage this save has not unlocked: its number, -1 otherwise. The selected icon is then the
 * stage select's "nothing selected" index (0x1E: no outline, no name, an empty preview) during "Now loading", so the
 * stage is not revealed; the vanilla exit would read the table at that index, so the group code leaves with this. */
static int mp_sss_hidden_stage = -1;
#define MP_SSS_FROM_LOBBY 0x7FFF   /* mp_sss_hidden_stage: a random pick the host resolves; the stage is the lobby's */
static int mp_sss_resolving;       /* host: a random pick from another console is being resolved from this save */
static int mp_sss_random_sent;     /* this console sent a random pick (it is not the host) */
static int mp_sss_picker = -1;     /* the port that picks the stage (the host's Stage Selection rule), -1 any */
HSD_JObj* mp_sss_cursor;           /* the cursor, labelled with its picker's port (vanilla's do_anim frame: port + 1);
                                   * tests read its position */
static void mp_sss_start_loading(void);

/* Only the picker's console has a cursor and reads its pad here (Turns, Loser, and the host's Random or Ordered). */
static int mp_sss_no_cursor(void)
{
    return mp_group_mode() && mp_sss_picker >= 0 && mp_sss_picker != mp_platform_slippi_net_local_index();
}

/* The Random icon on a console that is not the host: the host picks the stage from its own save (its Random Stage
 * Switch, unlocks and no-repeat history), as Random would on one console. This console sends the random pick and shows
 * "Now loading" without a stage until the match starts. */
static int mp_sss_random_to_host(void)
{
    MpGroupLobby l;
    if (!mp_group_mode() || !mp_platform_group_lobby_get(&l) || l.host) {
        return 0;
    }
    OSReport("GROUPCSS stage select: Random; the host picks the stage\\n");
    mp_platform_group_lobby_stage(MP_GROUP_STAGE_RANDOM, 0);
    mp_sss_random_sent = 1;
    mp_sss_hidden_stage = MP_SSS_FROM_LOBBY;
    mnStageSel_804D6CAE = 0x1E;
    mp_sss_start_loading();
    return 1;
}

static void mp_sss_start_loading(void)
{
    HSD_JObj* jobj;
    HSD_GObj* gobj;
''' + SSS_LOADING_BODY.split('\n', 2)[2] + '\n'

SSS_CODE = r'''
/* ---- the group's stage select: anyone's pick (the host's first) or Back ends it for everyone ---- */
static int mp_sss_remote, mp_sss_sent;
static int mp_sss_reset;   /* this frame's Back is L+R+Start (any console's), not the picker's B */
int mp_group_sss_remote_exit(void)
{
    int r = mp_sss_remote;
    mp_sss_remote = 0;
    return r;
}

static void mp_group_sss_back(void)
{
    if (mp_group_mode()) {
        if (mp_sss_reset) {
            mp_platform_group_lobby_back_reset();
        } else {
            mp_platform_group_lobby_back();
        }
    }
}

int mp_group_sss_sent(void)
{
    int r = mp_sss_sent || mp_sss_random_sent;
    mp_sss_sent = 0;
    mp_sss_random_sent = 0;
    return r;
}

static int mp_group_sss_frame(void)
{
    MpGroupLobby l;
    int i, stage;
    if (!mp_group_mode() || !mp_platform_group_lobby_get(&l)) {
        return 0;
    }
    if (mp_sss_picker != (l.sss_picker < 4 ? l.sss_picker : -1)) {
        /* the picker left: the host passed the pick on (its cursor appears on that console only) */
        mp_sss_picker = l.sss_picker < 4 ? l.sss_picker : -1;
        mnStageSel_804D50A0 = mp_sss_picker;
        if (mp_sss_cursor != NULL) {
            do_anim(mp_sss_cursor, mp_sss_picker + 1);   /* the new picker's colour, as vanilla colours it at setup */
        }
        OSReport("GROUPCSS stage select: port %d picks the stage now\n", mp_sss_picker + 1);
    }
    if (mp_platform_group_lost()) {
        mnStageSel_804D6CAF = 0;   /* the session ended: back to the CSS, which leaves */
        mp_sss_remote = 1;
        mp_sss_hidden_stage = -1;
        gm_801A4B60();
        return 1;
    }
    if (mp_sss_hidden_stage >= 0) {
        /* A hidden pick's "Now loading" (30 to 90 frames, as for any pick): leave with its stage once it is over. */
        if (mnStageSel_804D6CAF == 2 && mnStageSel_804D6CA4 == 0) {
            stage = (l.stage_hi << 8) | l.stage_lo;
            if (mp_sss_hidden_stage != MP_SSS_FROM_LOBBY) {
                sss_data->vs.start.rules.stkind = mp_sss_hidden_stage;
            } else if (l.stage_picked && stage != MP_GROUP_STAGE_RANDOM) {
                sss_data->vs.start.rules.stkind = stage;   /* else the match's block names it */
            }
            mp_sss_hidden_stage = -1;
            gm_801A4B60();
            return 1;
        }
        return 0;
    }
    if (mnStageSel_804D6CAF != 0) {
        /* The pick animation ("Now loading") is running here: tell the others at once. The cursor is already gone. */
        if (!mp_sss_remote && !mp_sss_sent) {
            mp_sss_sent = 1;
            mp_platform_group_lobby_stage(mnStageSel_803F06D0[mnStageSel_804D6CAE].stkind, 0);
        }
        return 0;
    }
    if (l.stage_picked || l.phase == 2) {
        /* Someone else picked: show the same pick here, so this console also goes to "Now loading" and its cursor stops. */
        stage = (l.stage_hi << 8) | l.stage_lo;
        mp_sss_remote = 1;
        if (stage == MP_GROUP_STAGE_RANDOM) {
            if (l.host) {
                /* another console pressed Random: this save picks, and the next frame shows it as any remote pick */
                if (!mp_sss_resolving) {
                    mp_sss_resolving = 1;
                    i = mnStageSel_802599EC();
                    OSReport("GROUPCSS stage select: a random pick; this save picks stage %d\n", mnStageSel_803F06D0[i].stkind);
                    mp_platform_group_lobby_stage(mnStageSel_803F06D0[i].stkind, 0);
                }
                return 0;
            }
            OSReport("GROUPCSS stage select: a random pick; the host picks the stage\n");
            mp_sss_hidden_stage = MP_SSS_FROM_LOBBY;
            mnStageSel_804D6CAE = 0x1E;
            mp_sss_start_loading();
            return 0;
        }
        OSReport("GROUPCSS stage select: stage %d was picked\n", stage);
        for (i = 0; i < 0x1E; i++) {
            if (mnStageSel_803F06D0[i].stkind == stage && mnStageSel_803F06D0[i].x8 < 2) {
                /* not unlocked here: "Now loading" without showing which stage (vanilla would refuse the pick) */
                OSReport("GROUPCSS stage select: stage %d is locked here; loading without showing it\n", stage);
                mp_sss_hidden_stage = stage;
                mnStageSel_804D6CAE = 0x1E;
                mp_sss_start_loading();
                return 0;
            }
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
    if (l.phase == 0) {
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
        # The soft reset (L+R+Start) in the lobby: as Back held (leaving, the host asked "End the session?" first), so
        # the session is closed the usual way and the console lands on "Choose a game." (vanilla's would jump to the main
        # menu with the session still open).
        ('    if (!mp_slippi_css_online() && mn_8022F218() != 0) {\n',
         '    if (mp_group_mode() && mn_8022F218() != 0) {\n'
         '        if (mnCharSel_804D6CF6 == 0 && !mp_group_dialog_up()) {\n'
         '            mnCharSel_804D6CF6 = 2;\n'
         '        }\n'
         '    } else if (!mp_slippi_css_online() && mn_8022F218() != 0) {\n', 1),
        # A joiner's character select shows the host's rules (banner, handicap mode) and each door's auto handicap from
        # the console that owns it: every read of the rules and of auto handicap in this file goes through the group
        # code (inert outside a group). The group code calls the real functions as (gmMainLib_GetGameRules)() and
        # (gm_801685D4)(...), which these function-like macros leave alone.
        ('#include <sysdolphin/baselib/sislib.h>\n\nstatic u8 mnCharSel_804D50C8[4]',
         '#include <sysdolphin/baselib/sislib.h>\n'
         'GameRules* mp_css_game_rules(void);\ns8 mp_css_auto_hc(u8 port, u8 tag);\nextern HSD_Text* mp_css_banner;\n'
         '#define gmMainLib_GetGameRules() mp_css_game_rules()\n#define gm_801685D4(port, tag) mp_css_auto_hc(port, tag)\n'
         '\nstatic u8 mnCharSel_804D50C8[4]', 1),
        # Keep the rules banner's text so a joiner can write it again when the host's rules change.
        ('        HSD_SisLib_803A6368(text, 0x4A);\n        mnCharSel_8025BD30();\n',
         '        HSD_SisLib_803A6368(text, 0x4A);\n        mp_css_banner = text;\n        mnCharSel_8025BD30();\n', 1),
        ('static inline void getStickDelta(int port, f32* dx, f32* dy)\n{\n'
         '    f32 stick_x = loadStickValue(&HSD_PadCopyStatus[(u8) port].stickX);\n'
         '    f32 stick_y = (f32) HSD_PadCopyStatus[(u8) port].stickY;\n',
         'HSD_PadStatus* mp_css_pad(int port);\nint mp_css_remote(int door);\nint mp_css_unplugged(int door);\n'
         'int mp_css_door_in_use(int d);\n'
         'int mp_css_can_edit_rules(void);\nint mp_css_rules_hit(u32 trigger);\nint mp_css_hide_ready(void);\nu32 mp_css_trigger(int hand, f32 x, f32 y, u32 trigger);\nint mp_css_start_intercept(void);\nint mp_css_back_intercept(void);\nint mp_css_hovering(int door);\nvoid mp_css_group_frame(void);\n'
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
        # The same branch gives an open door with no valid fighter a random one (a controller that went away). A door
        # mirrored from the lobby can be like that for a frame (a rebuilt CSS drops a fighter this console has not
        # unlocked); a random fighter there would be read as an edit and sent to the host.
        ('                        if (mnCharSel_803F0DFC.doors[cursor->x4].sel_icon >=\n'
         '                                0x19U &&\n',
         '                        if (!mp_css_remote(cursor->x4) &&\n'
         '                            mnCharSel_803F0DFC.doors[cursor->x4].sel_icon >=\n'
         '                                0x19U &&\n', 1),
        ('                                                    if (HSD_PadCopyStatus\n'
         '                                                            [(u8) door]\n'
         '                                                                .err != 0)\n',
         '                                                    if (mp_css_unplugged(door))\n', 1),
        # A console watching a match it joined during: its cursor moves, and Back is its only action.
        ('        {\n            u8 port = cursor->x4;\n            CSSTagData* tag = mnCharSel_803F0E8C[port].data;\n            if (tag->state == 0) {\n',
         '        trigger = mp_css_trigger(cursor->x5, cursor->xC, cursor->x10, trigger);\n'
         '        {\n            u8 port = cursor->x4;\n            CSSTagData* tag = mnCharSel_803F0E8C[port].data;\n            if (tag->state == 0) {\n', 1),
        ('                    trigger |= HSD_PadCopyStatus[tmp].trigger;\n',
         '                    trigger |= mp_css_pad(tmp)->trigger;\n', 1),
        ('        trigger = HSD_PadCopyStatus[tag->port].trigger;\n',
         '        trigger = mp_css_pad(tag->port)->trigger;\n', 1),
        ('                    if (cx3 > -17.0f && cx3 < 15.0f && cursor->x10 > 22.0f) {\n',
         '                    if (cx3 > -17.0f && cx3 < 15.0f && cursor->x10 > 22.0f && mp_css_rules_hit(trigger)) {\n', 1),
        ('                if (cursor->xC < -25.5f && cursor->x10 > 22.0f) {\n',
         '                if (cursor->xC < -25.5f && cursor->x10 > 22.0f && mp_css_rules_hit(trigger)) {\n', 1),
        # The camera turns with every console's C-stick (group_engine.h).
        ('    HSD_GObj_SetupProc(gobj, mn_8022BA1C, 5);\n    ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);\n',
         '    HSD_GObj_SetupProc(gobj, mp_group_camera_proc, 5);\n    ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);\n', 1),
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
        # READY also waits while another console is busy (the lobby's busy bits) or while this one watches a match.
        # A door in use on another console (its slider, name tag list, name entry or held token) cannot be switched
        # between Human, CPU and closed here, as on one GameCube (the lobby's in_use).
        ('                                    mnCharSel_804A0BD0[door]->x5 == 0 &&\n'
         '                                    mnCharSel_804A0BC0[door]->x5 != 1)\n',
         '                                    mnCharSel_804A0BD0[door]->x5 == 0 &&\n'
         '                                    mnCharSel_804A0BC0[door]->x5 != 1 &&\n'
         '                                    !mp_css_door_in_use(door))\n', 1),
        ('                if (mnCharSel_804A0BC0[i]->x5 == 1 || tag[i].data->state != 0)\n',
         '                if (mnCharSel_804A0BC0[i]->x5 == 1 || tag[i].data->state != 0 || mp_css_hide_ready())\n', 1),
        ('                if (mnCharSel_803F0DFC.doors[i].sel_icon >= 0x19) {\n                    goto hide;\n',
         '                if (mnCharSel_803F0DFC.doors[i].sel_icon >= 0x19 || mp_css_hovering(i)) {\n                    goto hide;\n', 1),
        ('        if (mnCharSel_804D6CF6 != 0) {\n            gm_801A4B60();\n            if (mnCharSel_804D6CF5 == 4) {\n',
         '        if (mnCharSel_804D6CF6 != 0 && mp_css_start_intercept()) {\n'
         '            mnCharSel_804D6CF6 = 0;\n'
         '        } else if (mnCharSel_804D6CF6 != 0) {\n            gm_801A4B60();\n            if (mnCharSel_804D6CF5 == 4) {\n', 1),
    ],
    # A group's name tags: a remote player's tag is its text (the sender's save id means nothing here), drawn as vanilla's.
    'melee/if/ifnametag.c': [
        ('#include "ifnametag.h"\n', '#include "ifnametag.h"\n#include <group_engine.h>\n', 1),
        ('                Player_GetNametagSlotID(i) == 0x78)\n', '                mp_group_tag_off(i))\n', 1),
        ('(un_804D6D70[*slot] || Player_GetNametagSlotID(*slot) != \'x\' ||', '(un_804D6D70[*slot] || !mp_group_tag_off(*slot) ||', 1),
        ('        Player_GetNametagSlotID(slot) == NAMETAG_DISABLED)\n', '        mp_group_tag_off(slot))\n', 1),
        ('                    un_804D6D78, -5000.0f, 0.0f,\n                    GetNameText(Player_GetNametagSlotID(slot)));\n', '                    un_804D6D78, -5000.0f, 0.0f,\n                    mp_group_tag_text(slot));\n', 1),
        ('                                GetNameText(Player_GetNametagSlotID(slot)));\n', '                                mp_group_tag_text(slot));\n', 1),
    ],
    # Name entry opened from the lobby reads its buttons from the door's port (mnNameNew_PortInUse); a console's only pad
    # is pad 0, so its own port reads pad 0 there, as on the CSS (mp_css_pad). PortInUse itself stays: the new tag is
    # stored for that door and its rumble setting is read from that port.
    'melee/mn/mnnamenew.c': [
        ('#include "mnnamenew.h"\n', '#include "mnnamenew.h"\n#include <group_engine.h>\n', 1),
        ('mn_80229624((u32) mnNameNew_PortInUse)', 'mn_80229624((u32) mp_group_menu_pad(mnNameNew_PortInUse))', 2),
    ],
    # The menu camera reads each port's C-stick through the group (group_engine.h); nothing changes outside a group.
    'melee/mn/mnmain.c': [
        ('#include "mnmain.h"\n', '#include "mnmain.h"\n#include <group_engine.h>\n', 1),
        ('        y = new_var[(u8) i].nml_subStickY;\n',
         '        y = new_var[(u8) i].nml_subStickY;\n        mp_group_camera_stick(i, &x, &y);\n', 1),
    ],
    # The puck's own think: a token held by another console (state 5) follows no cursor and is hidden.
    'melee/mn/mnstagesel.c': [
        # The soft reset (L+R+Start) on a group's stage select, from any console (as vanilla, which reads every
        # controller for it, even when one player picks): everyone back to the character select (as the picker's B),
        # not vanilla's jump to the main menu.
        ('    if (sss_data->no_lras == 0 && mn_8022F218()) {\n',
         '    if (!mp_group_mode() && sss_data->no_lras == 0 && mn_8022F218()) {\n', 1),
        ('#include "mnstagesel.h"\n',
         '#include <group_bridge.h>\n#include <group_engine.h>\n#include <slippi_net_bridge.h>\n#include "mnstagesel.h"\n', 1),
        ('            HSD_GObj_SetupProc(gobj, mn_8022BA1C, 5);\n',
         '            HSD_GObj_SetupProc(gobj, mp_group_camera_proc, 5);\n', 1),
        ('void mnStageSel_80259C28(void)\n{\n', SSS_LOADING + 'void mnStageSel_80259C28(void)\n{\n', 1),
        (SSS_LOADING_BODY, 'skip_randomize:\n    mp_sss_start_loading();\n}\n', 1),
        ('        mnStageSel_804D6CAE = 0x1E;\n',
         '        mnStageSel_804D6CAE = 0x1E;\n        mp_sss_hidden_stage = -1;\n        mp_sss_resolving = 0;\n'
         '        mp_sss_random_sent = 0;\n        mp_sss_picker = sss_data->unk_stage - 1;\n'
         '        mp_sss_cursor = NULL;\n', 1),
        # Random from a console that is not the host: the host picks (mp_sss_random_to_host).
        ('    mnStageSel_804D6CAE = mnStageSel_802599EC();\n',
         '    if (mp_sss_random_to_host()) {\n        return;\n    }\n    mnStageSel_804D6CAE = mnStageSel_802599EC();\n', 1),
        # One picker (Turns, Loser): its console reads its own pad, pad 0, in place of its port's; the others read none,
        # and their cursor is hidden.
        ('get_pad(mnStageSel_804D50A0)', 'get_pad(mp_group_menu_pad(mnStageSel_804D50A0))', 3),
        ('                do_anim(jobj2, mnStageSel_804D50A0 + 1);\n',
         '                do_anim(jobj2, mnStageSel_804D50A0 + 1);\n                mp_sss_cursor = jobj2;\n', 1),
        ('    if (mnStageSel_804D6CAC < -0x1E) {\n',
         '    if (mp_sss_no_cursor()) {\n        mnStageSel_804D6CA0 = 0;\n        mnStageSel_804D6CAC = 0;\n'
         '        mnStageSel_804D6CAD = 0;\n    }\n'
         '    if (mp_group_mode() && mn_8022F218()) {\n'
         '        /* L+R+Start from any console, the picker or not (vanilla reads every controller for it): B, so everyone\n'
         '         * goes back to the character select. B (0x200, as vanilla tests it), not Start, which would pick. */\n'
         '        mnStageSel_804D6CA0 = 0x200;\n'
         '        mp_sss_reset = 1;\n'
         '    } else {\n'
         '        mp_sss_reset = 0;\n'
         '    }\n'
         '    if (mnStageSel_804D6CAC < -0x1E) {\n', 1),
        ('    jobj = gobj->hsd_obj;\n    if (mnStageSel_804D6CAF != 0) {\n        HSD_JObjSetFlags(jobj, JOBJ_HIDDEN);\n'
         '        return;\n    }\n',
         '    jobj = gobj->hsd_obj;\n    if (mnStageSel_804D6CAF != 0 || mp_sss_no_cursor()) {\n'
         '        HSD_JObjSetFlags(jobj, JOBJ_HIDDEN);\n        return;\n    }\n'
         '    if (mp_group_mode()) {\n        HSD_JObjClearFlags(jobj, JOBJ_HIDDEN);   /* the pick passed to this console */\n    }\n', 1),
        ('void mnStageSel_Scene_OnFrame(void)\n{\n',
         SSS_CODE + 'void mnStageSel_Scene_OnFrame(void)\n{\n    if (mp_group_sss_frame()) {\n        return;\n    }\n', 1),
        ('        sfxBack();\n        gm_801A4B60();\n    }\n    if (mnStageSel_804D6CAF == 2) {\n',
         '        sfxBack();\n        mp_group_sss_back();\n        gm_801A4B60();\n    }\n    if (mnStageSel_804D6CAF == 2) {\n', 1),
    ],
}
