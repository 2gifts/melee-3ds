"""CSS door-mirroring spike (kept as a build-time opt-in experiment).

Build-time opt-in: nothing here is applied unless MELEE_CSS_SPIKE=1 is in the
environment of the build (docker/emu-test.sh build-dev passes it through).
MELEE_CSS_SPIKE_DOOR=N (0-3, default 0) is the door the local pad drives.

What it adds to the VS character select (mncharsel.c):
  * mp_css_pad(port): the pad source per door (my door reads pad 0, the others
    read their own, unplugged, channel so their cursor takes the existing
    "no controller" branch and hides).
  * mp_css_apply_door(door, MpCssDoor*): applies kind / fighter / costume / team /
    CPU level to one door from a table, without a cursor and without sound.
  * a scripted timeline (mp_css_spike_frame, called once per CSS frame) that
    plays a fake lobby into the doors, draws a name + signal line under each
    door, checks the Rules gate, flips Teams, injects Start, and asks the
    emulator build for screenshots (mp_platform_css_spike -> port/3ds/game.c).
  * host-only gates on the Rules and Team-mode hot spots (mp_css_is_host).
  * mnmain.c: the main menu enters VS Melee by itself after 120 frames.
  * lbaudio_ax.c: a counter of sound-effect calls.
Every result is logged with OSReport as "CSSSPIKE ..." lines in game.log.
"""
import os

ENABLED = os.environ.get('MELEE_CSS_SPIKE') == '1'
MYDOOR = int(os.environ.get('MELEE_CSS_SPIKE_DOOR', '0'))
# Test with a locked fighter in the table (fresh profile): Roy, Pichu, Falco.
FIGHTERS = os.environ.get('MELEE_CSS_SPIKE_FIGHTERS', 'CKind_Emblem,CKind_Pichu,CKind_Falco').split(',')

CODE = r'''
/* ======== CSS spike (tools/slippi_edits/css_spike.py) ======== */
#include <string.h>
typedef struct MpCssDoor { u8 kind, ckind, costume, team, cpu_level; } MpCssDoor;
extern void mp_platform_css_spike(unsigned op, unsigned a, unsigned b);
extern int mp_css_sfx_count;
int mp_css_is_host = 1;
int mp_css_my_door = @MYDOOR@;
static u8 mp_css_remote_mask;
static int mp_css_ctx = -1;
static MpCssDoor mp_css_table[4], mp_css_last[4];
static u8 mp_css_table_valid[4];

HSD_PadStatus* mp_css_pad(int port)
{
    if (port == mp_css_my_door) {
        return &HSD_PadCopyStatus[0];
    }
    if (port == 0) {
        return &HSD_PadCopyStatus[mp_css_my_door];
    }
    return &HSD_PadCopyStatus[port];
}

int mp_css_remote(int door)
{
    return (mp_css_remote_mask >> door) & 1;
}

/* Apply one door from the table: no cursor, no sound (the SFX sites in
 * 8025DB34 / 8025FDEC are all skipped while the pending-scene byte is 3 or 4). */
void mp_css_apply_door(int d, const MpCssDoor* s)
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
    } else {
        mnCharSel_804A0BD0[d]->x5 = 0;
        dr->sel_icon = 0x19;
        p->ckind = ChKind_None;
    }
    mnCharSel_8025DB34(d);
    mnCharSel_804D6CF6 = save;
}

static void mp_css_set(int d, int kind, int ckind, int costume, int team, int level)
{
    mp_css_table[d].kind = kind;
    mp_css_table[d].ckind = ckind;
    mp_css_table[d].costume = costume;
    mp_css_table[d].team = team;
    mp_css_table[d].cpu_level = level;
    mp_css_table_valid[d] = 1;
}

static void mp_css_sync(void)
{
    int d;
    for (d = 0; d < 4; d++) {
        if (mp_css_table_valid[d] && memcmp(&mp_css_table[d], &mp_css_last[d], sizeof(MpCssDoor)) != 0) {
            mp_css_last[d] = mp_css_table[d];
            mp_css_apply_door(d, &mp_css_table[d]);
        }
    }
}

/* ---- door text: name and signal bars under each door ---- */
static HSD_Text* mp_css_dtext[4];
static HSD_Text* mp_css_btext[4];
static int mp_css_dname[4], mp_css_dbars[4];
static char mp_css_name_now[4][24], mp_css_bars_now[4][24];
static const char* mp_css_names[4] = { "HOST 3DS", "MIA", "", "" };
static u8 mp_css_signal[4] = { 4, 3, 0, 0 }; /* bars; the CPU / closed doors show none */
static float mp_css_text_dx = @TEXT_DX@, mp_css_text_dy = @TEXT_DY@, mp_css_text_size = @TEXT_SIZE@;
static u8 mp_css_text_joint = 3; /* 0 door_joint, 1 bg_joint, 2 player_indicator_joint, 3 nametag name joint */

static void mp_css_text_frame(int log)
{
    int d;
    if (mp_css_ctx < 0) {
        return;
    }
    for (d = 0; d < 4; d++) {
        HSD_JObj* j;
        Vec3 pos;
        u8 joint;
        GXColor col;
        char bars[24];
        const char* name = mp_css_names[d];
        int n = mp_css_signal[d], k;
        CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
        float dr_x;
        switch (mp_css_text_joint) {
        case 0: joint = dr->door_joint; break;
        case 1: joint = dr->bg_joint; break;
        case 2: joint = dr->player_indicator_joint; break;
        default: joint = mnCharSel_803F0E8C[d].name_jointl; break;
        }
        lb_80011E24(mnCharSel_804D6CC0, &j, joint, -1);
        lb_8000B1CC(j, NULL, &pos);
        {
            HSD_JObj* dj;
            Vec3 dp;
            lb_80011E24(mnCharSel_804D6CC0, &dj, dr->door_joint, -1);
            lb_8000B1CC(dj, NULL, &dp);
            dr_x = dp.x;
        }
        if (log) {
            Vec3 a, b, c;
            HSD_JObj* jj;
            lb_80011E24(mnCharSel_804D6CC0, &jj, dr->door_joint, -1); lb_8000B1CC(jj, NULL, &a);
            lb_80011E24(mnCharSel_804D6CC0, &jj, dr->bg_joint, -1); lb_8000B1CC(jj, NULL, &b);
            lb_80011E24(mnCharSel_804D6CC0, &jj, dr->player_indicator_joint, -1); lb_8000B1CC(jj, NULL, &c);
            OSReport("CSSSPIKE joints door%d: door(%d)=(%.2f,%.2f,%.2f) bg(%d)=(%.2f,%.2f,%.2f) indicator(%d)=(%.2f,%.2f,%.2f) name(%d)=(%.2f,%.2f,%.2f)\n",
                     d, dr->door_joint, a.x, a.y, a.z, dr->bg_joint, b.x, b.y, b.z, dr->player_indicator_joint, c.x, c.y, c.z,
                     mnCharSel_803F0E8C[d].name_jointl, pos.x, pos.y, pos.z);
        }
        if (mp_css_dtext[d] == NULL) {
            HSD_Text* t = HSD_SisLib_803A6754(0, mp_css_ctx);
            if (t == NULL) {
                continue;
            }
            t->default_fitting = 1;
            t->default_alignment = 0;
            t->font_size.x = mp_css_text_size;
            t->font_size.y = mp_css_text_size;
            t->box_size_x = 160.0f;
            t->box_size_y = 32.0f;
            mp_css_dname[d] = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
            mp_css_dtext[d] = t;
            t = HSD_SisLib_803A6754(0, mp_css_ctx);
            if (t == NULL) {
                continue;
            }
            t->default_fitting = 1;
            t->default_alignment = 0;
            t->font_size.x = mp_css_text_size;
            t->font_size.y = mp_css_text_size;
            t->box_size_x = 160.0f;
            t->box_size_y = 32.0f;
            mp_css_dbars[d] = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "");
            mp_css_btext[d] = t;
            mp_css_name_now[d][0] = 0;
            mp_css_bars_now[d][0] = 1;
        }
        mp_css_dtext[d]->pos_x = dr_x + mp_css_text_dx;
        mp_css_dtext[d]->pos_y = -pos.y + mp_css_text_dy;
        mp_css_dtext[d]->pos_z = pos.z;
        mp_css_btext[d]->pos_x = dr_x + mp_css_text_dx + (@BARTEST@ && (d & 1) ? 0.0f : @BARX@);
        mp_css_btext[d]->pos_y = -pos.y + mp_css_text_dy + (@BARTEST@ && (d & 1) ? 1.3f : 0.0f);
        mp_css_btext[d]->pos_z = pos.z;
        if (@BARTEST@) {
            static const char* cand[4] = { "||||", "####", "\x81\xA1\x81\xA1\x81\xA0\x81\xA0", "iiii" }; /* glyph test: only 'i' draws */
            strcpy(bars, cand[d]);
        } else {
            /* lowercase 'i' is a thin vertical bar in the CSS font ('|', '#' and the SJIS squares are blank) */
            for (k = 0; k < n && k < 4; k++) {
                bars[k] = 'i';
            }
            bars[k] = 0;
        }
        if (strcmp(name, mp_css_name_now[d]) != 0) {
            strncpy(mp_css_name_now[d], name, 23);
            HSD_SisLib_803A70A0(mp_css_dtext[d], mp_css_dname[d], "%s", name);
            OSReport("CSSSPIKE text door%d name='%s' t=%p bt=%p\n", d, name, mp_css_dtext[d], mp_css_btext[d]);
        }
        if (strcmp(bars, mp_css_bars_now[d]) != 0) {
            strncpy(mp_css_bars_now[d], bars, 23);
            HSD_SisLib_803A70A0(mp_css_btext[d], mp_css_dbars[d], "%s", bars);
            if (@BARTEST@ && (d == 0 || d == 2)) {
                char both[48];
                snprintf(both, sizeof both, "%s %s", d == 0 ? "HOST 3DS" : "CPU", bars);
                HSD_SisLib_803A70A0(mp_css_dtext[d], mp_css_dname[d], "%s", both);
            }
            OSReport("CSSSPIKE bars door%d '%s' now=%p\n", d, bars, mp_css_btext[d]);
            col.r = n >= 3 ? 0x33 : 0xFF;
            col.g = n >= 2 ? 0xFF : 0x40;
            col.b = n >= 3 ? 0x2F : 0x30;
            col.a = 0xFF;
            if (n == 2) {
                col.r = 0xFF; col.g = 0xE0; col.b = 0x20;
            }
            HSD_SisLib_803A74F0(mp_css_btext[d], mp_css_dbars[d], &col);
        }
    }
}

static void mp_css_dump(const char* tag)
{
    int d;
    OSReport("CSSSPIKE [%s] overlay=%d pend=%d sfx=%d teams=%d\n", tag, mnCharSel_804D6CF7, mnCharSel_804D6CF6,
             mp_css_sfx_count, mnCharSel_804D6CB0->vs.start.rules.is_teams);
    for (d = 0; d < 4; d++) {
        CSSDoor* dr = &mnCharSel_803F0DFC.doors[d];
        PlayerInitData* p = &mnCharSel_804D6CB0->vs.start.players[d];
        OSReport("CSSSPIKE   door%d p_kind=%d slot=%d icon=%d ckind=%d costume=%d color=%d team=%d/%d cpu=%d cursor_x5=%d tok_x5=%d unlocked=%d\n",
                 d, dr->p_kind, p->slot_type, dr->sel_icon, p->ckind, dr->costume, p->color, dr->team, p->team, p->cpu_level,
                 mnCharSel_804A0BC0[d]->x5, mnCharSel_804A0BD0[d]->x5,
                 p->ckind >= 0 && p->ckind < CKind_Playable_Count ? gm_IsCKindUnlocked(p->ckind) : -1);
    }
}

void mp_css_spike_frame(void)
{
    static u32 t;
    static int sfx0;
    t++;
    if (mnCharSel_804D6CF5 != 4 || mnCharSel_804D6CB0 == NULL || mnCharSel_804A0BC0[0] == NULL || mnCharSel_803F0E8C[3].data == NULL) {
        return;
    }
    if (mnCharSel_804D6CF6 == 0 || mnCharSel_804D6CF6 == 1) {
        mp_css_text_frame(t == 3);
        mp_css_sync();
    }
    switch (t) {
    case 40:
        OSReport("CSSSPIKE shot9\n");
        mp_platform_css_spike(0, 4, 9);
        break;
    case 100:
        mp_css_dump("baseline vanilla");
        break;
    case 110:
        /* fake lobby A: me = door 0 (test fighter), remote Human, remote CPU, closed */
        sfx0 = mp_css_sfx_count;
        mp_css_remote_mask = 0xF & ~(1 << mp_css_my_door);
        mp_css_set(0, 0, CKind_Fox, 0, 0, 0);
        mp_css_set(1, 0, CKind_Mars, 1, 0, 0);
        mp_css_set(2, 1, CKind_Falco, 0, 0, 5);
        mp_css_set(3, 3, ChKind_None, 0, 0, 0);
        break;
    case 200:
        mp_css_dump("lobby A applied");
        OSReport("CSSSPIKE sfx calls during lobby A apply: %d\n", mp_css_sfx_count - sfx0);
        mp_platform_css_spike(0, 4, 1);
        break;
    case 215:
        sfx0 = mp_css_sfx_count;
        mp_css_set(1, 0, CKind_Peach, 2, 0, 0);
        mp_css_set(2, 1, CKind_Fox, 1, 0, 9);
        mp_css_set(3, 1, CKind_Purin, 0, 0, 3);
        mp_css_names[2] = "CPU"; mp_css_names[3] = "CPU";
        mp_css_signal[2] = 0; mp_css_signal[3] = 0;
        break;
    case 300:
        mp_css_dump("lobby B (fighters changed, door 3 reopened as CPU)");
        OSReport("CSSSPIKE sfx calls during lobby B apply: %d\n", mp_css_sfx_count - sfx0);
        mp_platform_css_spike(0, 4, 2);
        break;
    case 310:
        sfx0 = mp_css_sfx_count;
        mp_css_set(1, 0, CKind_Mars, 3, 0, 0);
        mp_css_set(2, 3, ChKind_None, 0, 0, 0);
        mp_css_set(3, 3, ChKind_None, 0, 0, 0);
        mp_css_names[2] = ""; mp_css_names[3] = "";
        break;
    case 380:
        mp_css_dump("lobby C (costume change, two doors closed)");
        OSReport("CSSSPIKE sfx calls during lobby C apply: %d\n", mp_css_sfx_count - sfx0);
        mp_platform_css_spike(0, 4, 3);
        break;
    case 400:
        mp_css_is_host = 0;
        mnCharSel_804A0BC0[mp_css_my_door]->xC = 0.0f;
        mnCharSel_804A0BC0[mp_css_my_door]->x10 = 24.0f;
        break;
    case 404:
        OSReport("CSSSPIKE rules hover non-host: x8=%d\n", mnCharSel_804A0BC0[mp_css_my_door]->x8);
        mp_css_is_host = 1;
        break;
    case 408:
        OSReport("CSSSPIKE rules hover host: x8=%d\n", mnCharSel_804A0BC0[mp_css_my_door]->x8);
        mp_platform_css_spike(0, 4, 4);
        break;
    case 420:
        sfx0 = mp_css_sfx_count;
        mnCharSel_804D6CB0->vs.start.rules.is_teams = 1;
        mnCharSel_8025EE8C(mnCharSel_804D6CB0->match_type);
        mp_css_set(0, 0, CKind_Fox, 0, 0, 0);
        mp_css_set(1, 0, @F0@, 0, 1, 0);
        mp_css_set(2, 1, @F1@, 0, 2, 5);
        mp_css_set(3, 1, @F2@, 0, 1, 7);
        mp_css_names[2] = "CPU"; mp_css_names[3] = "CPU";
        break;
    case 500:
        mp_css_dump("teams + (possibly locked) fighters");
        OSReport("CSSSPIKE sfx calls during teams apply: %d\n", mp_css_sfx_count - sfx0);
        mp_platform_css_spike(0, 4, 5);
        break;
    case 520:
        if (mnCharSel_804D6CF7 != 0) {
            OSReport("CSSSPIKE injecting START (overlay up)\n");
            mnCharSel_804D6CF6 = 1;
            mnCharSel_804D6CF2 = 0xFF;
            mp_platform_css_spike(1, 150, 0);
            mp_platform_css_spike(0, 400, 6);
            mp_platform_css_spike(0, 1200, 7);
            mp_platform_css_spike(0, 2400, 8);
        } else {
            OSReport("CSSSPIKE overlay NOT up, no start\n");
        }
        break;
    }
}
/* ======== end CSS spike ======== */

'''

FIXES = {}
if ENABLED:
    code = (CODE.replace('@MYDOOR@', str(MYDOOR))
            .replace('@BARTEST@', os.environ.get('MELEE_CSS_SPIKE_BARTEST', '0'))
            .replace('@BARX@', os.environ.get('MELEE_CSS_SPIKE_BARX', '8.0f'))
            .replace('@TEXT_SIZE@', os.environ.get('MELEE_CSS_SPIKE_SIZE', '0.046f'))
            .replace('@TEXT_DX@', os.environ.get('MELEE_CSS_SPIKE_DX', '0.6f'))
            .replace('@TEXT_DY@', os.environ.get('MELEE_CSS_SPIKE_DY', '3.0f'))
            .replace('@F0@', FIGHTERS[0]).replace('@F1@', FIGHTERS[1]).replace('@F2@', FIGHTERS[2]))
    FIXES = {
        'melee/mn/mncharsel.c': [
            ('static inline void getStickDelta(int port, f32* dx, f32* dy)\n{\n'
             '    f32 stick_x = loadStickValue(&HSD_PadCopyStatus[(u8) port].stickX);\n'
             '    f32 stick_y = (f32) HSD_PadCopyStatus[(u8) port].stickY;\n',
             'HSD_PadStatus* mp_css_pad(int port);\nint mp_css_remote(int door);\nextern int mp_css_is_host;\n'
             'void mp_css_spike_frame(void);\n'
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
            ('                    trigger |= HSD_PadCopyStatus[tmp].trigger;\n',
             '                    trigger |= mp_css_pad(tmp)->trigger;\n', 1),
            ('                    if (cx3 > -17.0f && cx3 < 15.0f && cursor->x10 > 22.0f) {\n',
             '                    if (mp_css_is_host && cx3 > -17.0f && cx3 < 15.0f && cursor->x10 > 22.0f) {\n', 1),
            ('                if (cursor->xC < -25.5f && cursor->x10 > 22.0f) {\n',
             '                if (mp_css_is_host && cursor->xC < -25.5f && cursor->x10 > 22.0f) {\n', 1),
            ('    ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);\n',
             '    ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);\n    mp_css_ctx = ctx;\n', 1),
            ('void mnCharSel_CursorThink(HSD_GObj* gobj)\n{\n', code + 'void mnCharSel_CursorThink(HSD_GObj* gobj)\n{\n', 1),
            ('    mnCharSel_804D6CEC += 1;\n', '    mnCharSel_804D6CEC += 1;\n    mp_css_spike_frame();\n', 1),
        ],
        'melee/mn/mnmain.c': [
            ('    selection_count = mn_803EB6B0[MENU_KIND_MAIN].selection_count & 0xFF;\n'
             '    buttons = mn_80229624(4);\n',
             '    {\n'
             '        static int mp_css_boot_wait;\n'
             '        if (++mp_css_boot_wait == 120) {\n'
             '            data = gm_GetCurrentSceneExitData();\n'
             '            data->pending_mode = GM_VS;\n'
             '            gm_801A4B60();\n'
             '            return;\n'
             '        }\n'
             '    }\n'
             '    selection_count = mn_803EB6B0[MENU_KIND_MAIN].selection_count & 0xFF;\n'
             '    buttons = mn_80229624(4);\n', 1),
        ],
        'melee/lb/lbaudio_ax.c': [
            ('int lbAudioAx_800237A8(int id, int vol, int pan)\n{\n',
             'int mp_css_sfx_count;\nint lbAudioAx_800237A8(int id, int vol, int pan)\n{\n    mp_css_sfx_count++;\n', 1),
        ],
    }
