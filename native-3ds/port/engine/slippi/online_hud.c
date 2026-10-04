/* Slippi online in-game text (Online/Menus/InGame/InitInGame.asm and
 * StartEngineLoop's status texts): each player's name above their damage
 * display, "Delay: Nf", and DISCONNECTED / DESYNC DETECTED. A SIS canvas on
 * a copy of the HUD's camera (IfAll's ScInfDmg scene), so it follows the
 * HUD and hides with it (pause). Positions, sizes and colours are Slippi's.
 */
#include <string.h>
#include <melee/if/ifall.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/pl/player.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/sislib.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>

enum { TEXT_GXLINK = 12, TEXT_GXPRI = 80, COBJ_GXPRI = 8 };

static HSD_Text* info_text;
static int info_shown;

static void hud_camera_draw(HSD_GObj* gobj, int code)
{
    if (!ifAll_IsHUDHidden()) {
        HSD_GObj_803910D8(gobj, code);
    }
}

static HSD_Text* new_text(int canvas, float scale, u8 align)
{
    HSD_Text* t = HSD_SisLib_803A6754(2, canvas);
    if (t == NULL) {
        return NULL;
    }
    t->default_kerning = 1;
    t->default_alignment = align;
    t->pos_z = 0.0f;
    t->font_size.x = scale;
    t->font_size.y = scale;
    return t;
}

/* After the HUD's per-player setup (ifStatus_802F665C), once per match. */
void mp_slippi_hud_init(void)
{
    int delay = mp_slippi_online_delay();
    SceneDesc* scene = NULL;
    HSD_GObj* gobj;
    HSD_Text* t;
    int canvas, i, idx;
    char name[64];

    info_text = NULL;
    info_shown = 0;
    if (!mp_slippi_online_active()) {
        return;
    }
    lbArchive_LoadSections(*ifAll_GetArchive(), (void**) &scene, "ScInfDmg_scene_data", 0);
    if (scene == NULL) {
        return;
    }
    gobj = GObj_Create(19, 20, 0);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_CameraKind,
                            lb_80013B14((HSD_CameraDescPerspective*) scene->cameras[0].desc));
    GObj_SetupGXLinkMax(gobj, hud_camera_draw, COBJ_GXPRI);
    gobj->gxlink_prios = 1 << TEXT_GXLINK;
    canvas = HSD_SisLib_803A611C(2, gobj, 9, 13, 0, TEXT_GXLINK, TEXT_GXPRI, COBJ_GXPRI);

    /* Centred status line (disconnects, desyncs). */
    info_text = new_text(canvas, 0.1f, 1);

    /* "Delay: Nf", bottom right, half transparent. */
    t = new_text(canvas, 0.1f, 2);
    if (t != NULL) {
        t->active_color.a = 0x80;
        idx = HSD_SisLib_803A6B98(t, 270.0f, 207.0f, "Delay: %df", delay);
        HSD_SisLib_803A7548(t, idx, 0.33f, 0.33f);
    }

    /* Names over the damage displays. */
    for (i = 0; i < 2; i++) {
        Vec3* hud;
        if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        if (mp_platform_slippi_ui_text(30 + i, name, sizeof name) < 0 || name[0] == 0) {
            continue;
        }
        name[sizeof name - 1] = 0;
        t = new_text(canvas, 0.06f, 1);
        if (t == NULL) {
            continue;
        }
        hud = ifAll_GetPlayerHUDPosition(i);
        t->default_fitting = 1;
        t->x4C = 1;
        t->pos_x = hud->x + 0.8f;
        t->pos_y = 20.64f;
        t->box_size_x = 150.0f;
        t->box_size_y = 150.0f;
        idx = HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "%s", name);
        HSD_SisLib_803A7548(t, idx, 0.54f, 0.54f);
    }
}

/* StartEngineLoop: 1 = DISCONNECTED (red), 2 = DESYNC DETECTED (amber). */
void mp_slippi_hud_message(int kind)
{
    static const GXColor red = { 0xFF, 0x00, 0x00, 0xFF }, amber = { 0xFF, 0xB8, 0x00, 0xFF };
    int idx;
    if (info_text == NULL || info_shown) {
        return;
    }
    info_shown = 1;
    idx = HSD_SisLib_803A6B98(info_text, 9.0f, -162.0f, kind == 1 ? "DISCONNECTED" : "DESYNC DETECTED");
    HSD_SisLib_803A7548(info_text, idx, kind == 1 ? 0.7f : 0.5f, kind == 1 ? 0.7f : 0.5f);
    HSD_SisLib_803A74F0(info_text, idx, (GXColor*) (kind == 1 ? &red : &amber));
}
