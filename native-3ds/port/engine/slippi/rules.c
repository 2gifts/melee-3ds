/* Slippi's gameplay-affecting Gecko codes, as C.
 *
 * The codes come from project-slippi/slippi-ssbm-asm (references/
 * slippi-ssbm-asm; the netplay code set is netplay.json) and are checked
 * against the code list recorded in Slippi replays. The structure follows
 * Melee Unlocked's Source Port (GPL-3.0-or-later): shim/mu_online_rules.c,
 * shim/mu_replay.c (mu_offscreen_damage_zone) and the MU_NATIVE hunks of
 * patches/melee-native.patch (neutral spawns, PreventWobbling, dead-up-fall,
 * Stadium). Each function names the injection it reproduces; the call sites
 * are in tools/slippi_edits/online_rules.py. docs/slippi/rules.md lists
 * every code and how it was checked.
 *
 * Arithmetic follows the PowerPC code: an fsub is a double subtraction of
 * single values, fadds/fmuls are single operations, compares keep the
 * branch sense the code uses (so NaN takes the same side).
 */
#include <string.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/it/types.h>
#include <melee/mn/types.h>
#include <melee/pl/plbonuslib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <dolphin/gx/GXTexture.h>
#include <slippi_rules.h>

volatile unsigned mp_slippi_rules_enabled = 1;
volatile unsigned mp_slippi_rules_mask = 0xFFFFFFFFu;

/* The console offsets the codes use. */
_Static_assert(__builtin_offsetof(Fighter, victim_gobj) == 0x1A58, "grabber gobj");
_Static_assert(__builtin_offsetof(Fighter, dmg.x1868_source) == 0x1868, "damage source");
_Static_assert(__builtin_offsetof(Fighter, x2074.x2088) == 0x2088, "attack instance id");
_Static_assert(__builtin_offsetof(Fighter, mv) == 0x2340, "state variables");
_Static_assert(__builtin_offsetof(Fighter, cur_pos) == 0xB0, "position");
_Static_assert(__builtin_offsetof(Fighter, self_vel) == 0x80, "self velocity");
_Static_assert(__builtin_offsetof(Item, owner) == 0x518, "item owner");
_Static_assert(__builtin_offsetof(Item, xDA8_short) == 0xDA8, "item attack id");

/* (minor << 8) | major, as Slippi's getMinorMajor macro builds it. */
static unsigned scene_id(void)
{
    return ((unsigned) gm_GetCurrentSceneIndex() << 8) | gm_GetCurrentGameMode();
}

/* ---- per-match settings ---- */

static int frozen_stadium;
static int online_mode = -1;
static int online_local_player = -1;

void mp_slippi_rules_set_frozen_stadium(int frozen)
{
    frozen_stadium = frozen != 0;
}

int mp_slippi_rules_frozen_stadium(void)
{
    return frozen_stadium;
}

void mp_slippi_rules_set_online(int mode, int local_player)
{
    online_mode = mode;
    online_local_player = mode < 0 ? -1 : local_player;
}

/* InitOnlinePlay (8016e748), "Clear A inputs to prevent transformation": the
 * held buttons of every port's copy status are cleared, so a held A cannot
 * start Zelda as Sheik (or the reverse). Online matches only. */
void mp_slippi_rules_match_start(void)
{
    int i;
    if (online_mode < 0 || !mp_slippi_rule(MP_SR_ONLINE_INIT)) {
        return;
    }
    for (i = 0; i < 4; i++) {
        HSD_PadCopyStatus[i].button = 0;
    }
}

/* ---- General Codes: Neutral Spawns [UnclePunch], 8016e510 ---- */

static const struct {
    int stkind;
    float singles[4][2];
    float teams[4][2];
} neutral_spawns[] = {
    { 0x20, { { -60, 10 }, { 60, 10 }, { -20, 10 }, { 20, 10 } },
            { { -60, 10 }, { -20, 10 }, { 60, 10 }, { 20, 10 } } },
    { 0x1F, { { -38.8f, 35.2f }, { 38.8f, 35.2f }, { 0, 8 }, { 0, 62.4f } },
            { { -38.8f, 35.2f }, { -38.8f, 5 }, { 38.8f, 35.2f }, { 38.8f, 5 } } },
    { 0x08, { { -42, 26.6f }, { 42, 28 }, { 0, 46.9f }, { 0, 4.9f } },
            { { -42, 26.6f }, { -42, 5 }, { 42, 28 }, { 42, 5 } } },
    { 0x1C, { { -46.6f, 37.2f }, { 47.4f, 37.3f }, { 0, 7 }, { 0, 58.5f } },
            { { -46.6f, 37.2f }, { -46.6f, 5 }, { 47.4f, 37.3f }, { 47.4f, 5 } } },
    { 0x02, { { -41.25f, 21 }, { 41.25f, 27 }, { 0, 5.25f }, { 0, 48 } },
            { { -41.25f, 21 }, { -41.25f, 5 }, { 41.25f, 27 }, { 41.25f, 5 } } },
    { 0x03, { { -40, 32 }, { 40, 32 }, { 70, 7 }, { -70, 7 } },
            { { -40, 32 }, { -40, 5 }, { 40, 32 }, { 40, 5 } } },
};

/* SetSpawn: the index-th neutral position of a tournament stage; elsewhere
 * the stage's own spawn point (singles by order, teams mapped 0, 3, 1, 2).
 * Then face the middle (x > 0 faces left). */
static void neutral_spawn_at(int slot, int index, int teams)
{
    static const unsigned char team_order[4] = { 0, 3, 1, 2 };
    int stage = (int) Stage_80225194();
    Vec3 pos;
    unsigned e;

    for (e = 0; e < sizeof neutral_spawns / sizeof neutral_spawns[0]; e++) {
        if (neutral_spawns[e].stkind == stage) {
            break;
        }
    }
    if (e < sizeof neutral_spawns / sizeof neutral_spawns[0]) {
        const float* p = teams == 1 ? neutral_spawns[e].teams[index & 3]
                                    : neutral_spawns[e].singles[index & 3];
        pos.x = p[0];
        pos.y = p[1];
        pos.z = 0.0f;
    } else {
        Stage_80224E64(teams == 1 ? team_order[index & 3] : index, &pos);
    }
    Player_80032768(slot, &pos);
    Player_LoadPlayerCoords(slot, &pos);
    Player_SetFacingDirection(slot, pos.x <= 0.0f ? 1.0f : -1.0f);
}

/* Called after fn_8016E2BC stores a player's start position. The code's
 * single-player test (8016b41c) always answers no under the General Codes
 * ("C-Stick in Single Player" nops its "yes"), so only the slot and the two
 * scene tests remain. */
void mp_slippi_neutral_spawn(int slot, int is_teams)
{
    unsigned scene;
    int index, s, team;

    if (!mp_slippi_rule(MP_SR_NEUTRAL_SPAWN) || slot >= 5) {
        return;
    }
    scene = scene_id();
    if (scene == 0x021C || scene == 0x010F) {   /* training, target test */
        return;
    }
    if (is_teams != 1) {
        index = 0;
        for (s = 0; s <= 4; s++) {
            if (Player_GetPlayerSlotType(s) == Gm_PKind_NA) {
                continue;
            }
            if (s == slot) {
                break;
            }
            index++;
        }
        neutral_spawn_at(slot, index, is_teams);
        return;
    }
    /* Teams: only when every team has zero or two players. */
    for (team = 0; team < 3; team++) {
        int count = 0;
        for (s = 0; s < 4; s++) {
            if (Player_GetPlayerSlotType(s) != Gm_PKind_NA && Player_GetTeam(s) == team) {
                count++;
            }
        }
        if (count == 1 || count > 2) {
            return;
        }
    }
    {
        int order[4] = { -1, -1, -1, -1 };
        int n = 0;
        for (team = 0; team < 3; team++) {
            for (s = 0; s < 4; s++) {
                if (Player_GetPlayerSlotType(s) != Gm_PKind_NA && Player_GetTeam(s) == team) {
                    order[n++] = s;
                }
            }
        }
        for (index = 0; index < 4; index++) {
            if (order[index] == slot) {
                break;
            }
        }
        neutral_spawn_at(slot, index, 1);
    }
}

/* ---- Online/Core/BrawlOffscreenDamage.asm, 8006a880 ----
 * Replaces the magnifier-bubble test with the stage's camera limits, so the
 * live camera (which differs between machines) cannot decide damage. */
int mp_slippi_offscreen_zone(Fighter* fp)
{
    float x, y;
    if (scene_id() == 0x0120) {          /* Home-Run Contest: no damage */
        return 0;
    }
    if (fp->x221F_b1) {                  /* dead */
        return 0;
    }
    if (fp->motion_id == 4 || fp->motion_id == 6) {   /* star KO, screen KO */
        return 0;
    }
    x = fp->cur_pos.x;
    if (x < Stage_GetCamBoundsLeftOffset()) {
        return 1;
    }
    if (x > Stage_GetCamBoundsRightOffset()) {
        return 1;
    }
    y = fp->cur_pos.y;
    if (y > Stage_GetCamBoundsTopOffset()) {
        return 1;
    }
    if (y < Stage_GetCamBoundsBottomOffset()) {
        return 1;
    }
    return 0;
}

/* ---- External/PreventWobbling [UnclePunch] ----
 * fp+0x2384 counts the distinct Ice Climbers hits taken in a grab, fp+0x2386
 * holds the last hit's attack id. Both sit in the state variables, as on the
 * console. */
#define WOBBLE_COUNT(fp) (*((u8*) (fp) + 0x2384))
#define WOBBLE_LAST(fp) (*(u16*) ((u8*) (fp) + 0x2386))

/* Init Wobble Count Air/Ground (800db880, 800dbbd4): entering the grab. */
void mp_slippi_wobble_reset(Fighter* fp)
{
    if (!mp_slippi_rule(MP_SR_WOBBLE)) {
        return;
    }
    WOBBLE_COUNT(fp) = 0;
    WOBBLE_LAST(fp) = 0xFFFF;
}

/* Wobble Check (8008f090): the grabbed fighter takes a hit. The fourth
 * distinct hit from an Ice Climbers grabber (or its item) breaks the grab,
 * in singles only. Nonzero: the grab broke and the capture-damage entries
 * are skipped (the code branches to 8008f0c8). */
int mp_slippi_wobble_check(HSD_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    HSD_GObj* grabber;
    Fighter* gfp;
    HSD_GObj* src;
    HSD_GObj* nana;
    unsigned id, prev;

    if (!mp_slippi_rule(MP_SR_WOBBLE)) {
        return 0;
    }
    if (fp->motion_id < 0xDF || fp->motion_id > 0xE4) {
        return 0;
    }
    grabber = fp->victim_gobj;
    if (grabber == NULL) {
        return 0;
    }
    gfp = grabber->user_data;
    if (!gfp->x2222_b5) {               /* has a follower */
        return 0;
    }
    src = fp->dmg.x1868_source;
    if (src == grabber) {
        id = gfp->x2074.x2088;
    } else if (src != NULL && src->classifier == 6 &&
               ((Item*) src->user_data)->owner == grabber)
    {
        id = ((Item*) src->user_data)->xDA8_short;
    } else {
        return 0;
    }
    prev = WOBBLE_LAST(fp);
    if (id == prev) {
        return 0;
    }
    WOBBLE_LAST(fp) = (u16) id;
    WOBBLE_COUNT(fp)++;
    if (gm_8016B168() || WOBBLE_COUNT(fp) <= 3) {
        return 0;
    }
    /* AS_218_CatchCut gets r4 as the code leaves it: the previous id. */
    ftCo_800DA698(grabber, prev != 0);
    nana = Player_GetEntityAtIndex(gfp->player_id, 1);
    if (nana != NULL) {
        Fighter* nfp = nana->user_data;
        if (!nfp->x221F_b1 && !nfp->x2219_b5 && nfp->x2070.x2071_b0_3 != 13) {
            if (nfp->ground_or_air == GA_Ground) {
                ftCo_800DA698(nana, 0);
            } else {
                ftCommon_8007D5D4(nfp);
                nfp->self_vel.x = p_ftCommonData->x374 * -nfp->facing_dir;
                nfp->self_vel.y = p_ftCommonData->x378;
                *(float*) ((u8*) nfp + 0x2340) = 0.0f;
                Fighter_ChangeMotionState(nana, 0xE6, 0, 0.0f, 1.0f, 0.0f, NULL);
            }
        }
    }
    return 1;
}

/* ---- Online/Core/FreezeDeadUpFallPhysics ----
 * The fall after a star KO keeps its velocity in the state variables
 * (0x2348 y, 0x234C z) and moves only the state's position (0x2350), never
 * the fighter's velocity or cur_pos: the camera-dependent model position
 * cannot feed back into the simulation. */

/* InitHitVelocity (800d4c1c), DeadUpFall_Anim case 2. */
void mp_slippi_dead_up_init(Fighter* fp, float vel_y, float vel_z)
{
    memcpy((u8*) fp + 0x2348, &vel_y, 4);
    memcpy((u8*) fp + 0x234C, &vel_z, 4);
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    fp->self_vel.z = 0.0f;
}

/* UpdateFallVelocity (800d4d68), DeadUpFall_Phys case 3, in place of
 * ftCommon_Fall and the add of the fighter's velocity. */
void mp_slippi_dead_up_fall(Fighter* fp, float gravity, float terminal)
{
    float vy, vz, limit = -terminal;
    double next;
    Vec3* temp = &fp->mv.co.unk_deadup.x5C;

    memcpy(&vy, (u8*) fp + 0x2348, 4);
    next = (double) vy - (double) gravity;      /* fsub */
    if (!(next > (double) limit)) {
        vy = limit;
    } else {
        vy = (float) next;                       /* stfs */
    }
    memcpy((u8*) fp + 0x2348, &vy, 4);
    temp->y = temp->y + vy;
    memcpy(&vz, (u8*) fp + 0x234C, 4);
    temp->z = vz + temp->z;
}

/* ---- Common/PSCameraIndependentMonitor (801d24fc) ----
 * The Stadium screen's close-up keeps its fighter while the fighter is inside
 * a fixed box, not while the local camera sees it. The replaced call still
 * runs (the caller passes its result) for its side effects. */
int mp_slippi_ps_monitor_ok(int slot, int vanilla)
{
    HSD_GObj* gobj;
    Fighter* fp;
    float x, y;

    if (!mp_slippi_rule(MP_SR_PS_MONITOR)) {
        return vanilla;
    }
    gobj = Player_GetEntity(slot);
    fp = gobj->user_data;
    x = fp->cur_pos.x;
    if (x < -120.0f || x > 120.0f) {
        return 0;
    }
    y = fp->cur_pos.y;
    if (y > 80.0f || y < -20.0f) {
        return 0;
    }
    return 1;
}

/* ---- Online/Core/Hacks/Stadium/GrPsxIsValid.asm (801d4760) ----
 * The transformation counts as loaded once its archive is in place
 * (StadiumFileLoad loads it synchronously when the transformation starts). */
int mp_slippi_ps_archive_valid(void)
{
    HSD_GObj* map = Ground_GetMapGObj(2);
    UnkArchiveStruct* arc = grDatFiles_801C6330(((Ground*) map->user_data)->map_id);
    if (arc == NULL || arc->unk0 == NULL || *(u32*) arc->unk0 == 0) {
        return 0;
    }
    return 1;
}

/* Visual companion of the Stadium edits: the port does not capture the
 * screen's live views (modes 7 and 8), so their images start black rather
 * than as stale preload memory. */
void mp_slippi_clear_image(void* image_desc)
{
    HSD_ImageDesc* desc = image_desc;
    if (desc->image_ptr != NULL) {
        memset(desc->image_ptr, 0,
               GXGetTexBufferSize(desc->width, desc->height, desc->format, 0, 0));
    }
}

/* ---- Common/FastForward/DynamicsFix.asm (8009e090) ----
 * After the bone dynamics, set up the fighter's dirty joint matrices at once
 * instead of leaving them to the shadow render, so bone positions read later
 * in the frame do not depend on what was drawn. */
static void setup_matrix_all(HSD_JObj* jobj)
{
    while (jobj != NULL) {
        u32 flags = jobj->flags;
        if (!(flags & 0x00800000) && (flags & 0x40)) {
            HSD_JObjSetupMatrixSub(jobj);
        }
        setup_matrix_all(jobj->child);
        jobj = jobj->next;
    }
}

void mp_slippi_dynamics_fix(Fighter* fp)
{
    if (!mp_slippi_rule(MP_SR_DYNAMICS)) {
        return;
    }
    setup_matrix_all(GET_JOBJ(fp->gobj));
}

/* ---- Online/Core/LGLExceededGameEnd.asm (802f70c4) ----
 * A timeout in an online singles match where one player exceeded 45 ledge
 * grabs: the GAME! screen shows Success/Failure for the local player and is
 * held longer (0xFD frames). The subtext is not drawn here. Returns the
 * message id for ifStatus_802F6EA4 (0, "Time", when nothing applies). */
#define LGL_LIMIT 45

int mp_slippi_lgl_timeout_message(void)
{
    int p1, p2, loser;
    if (online_mode < 0 || !mp_slippi_rule(MP_SR_LGL)) {
        return 0;
    }
    if (online_mode == 3 || online_mode == 4) {   /* teams, party */
        return 0;
    }
    p1 = (int) pl_80040AF0(0);
    p2 = (int) pl_80040AF0(1);
    if (p1 > LGL_LIMIT && p2 > LGL_LIMIT) {
        return 0;
    }
    if (p1 > LGL_LIMIT) {
        loser = 0;
    } else if (p2 > LGL_LIMIT) {
        loser = 1;
    } else {
        return 0;
    }
    ((u8*) gm_GetStartMeleeRules())[0xD] = 0xFD;   /* GAME! hold time */
    return online_local_player == loser ? 6 : 2;
}
