/* Group play, engine side (see group_engine.h): the scene flow of a group (lobby CSS -> stage select -> match ->
 * results -> back), the results screen's presses, the stars and auto handicap after a match, the CPU takeover of a
 * dropped console's fighter, and remote players' name tags. The network and the lobby are
 * native (port/3ds/group, through group_bridge.h). */
#include <stdio.h>
#include <string.h>
#include <dolphin/pad.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gm_1BFA.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmresult.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/inlines.h>
#include <melee/lb/lbtime.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnname.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <group_bridge.h>
#include <group_engine.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/controller.h>

int mp_group_top_menu(void);          /* group_menu.py: the top-screen Solo / Group submenu is there */
extern int mp_group_lobby_screen;     /* group_lobby_screen.py: the "Choose a game." screen */

void mp_platform_log(const char* text);
void mp_platform_idle(void);

static void logf_(const char* fmt, int a, int b)
{
    char line[160];
    snprintf(line, sizeof line, fmt, a, b);
    mp_platform_log(line);
}

int mp_group_mode(void)
{
    return gm_GetCurrentGameMode() == GM_HANYU_CSS && mp_platform_group_active();
}

int mp_group_menu_pad(int port)
{
    if (mp_group_mode() && port == mp_platform_slippi_net_local_index()) {
        return 0;
    }
    return port;
}

/* ---- the menu camera ---- */

static int camera_group;              /* mn_8022BA1C is running for the group */
static signed char camera_sticks[8];  /* every port's C-stick as it last came (x, y per port) */

static int stick_byte(float v)
{
    v *= 127.0f;
    if (v > 127.0f) {
        v = 127.0f;
    } else if (v < -127.0f) {
        v = -127.0f;
    }
    return (int) (v < 0.0f ? v - 0.5f : v + 0.5f);
}

/* Where this console's menu camera looks from, in a group (the tests read it through the debugger). */
Vec3 mp_test_camera_eye;

void mp_group_camera_proc(HSD_GObj* gobj)
{
    if (!mp_group_mode()) {
        mn_8022BA1C(gobj);
        return;
    }
    /* this console's only pad is pad 0, whatever its port */
    mp_platform_group_menu_sticks(stick_byte(HSD_PadCopyStatus[0].nml_subStickX),
                                  stick_byte(HSD_PadCopyStatus[0].nml_subStickY), camera_sticks);
    camera_group = 1;
    mn_8022BA1C(gobj);
    camera_group = 0;
    HSD_CObjGetEyePosition(GET_COBJ(gobj), &mp_test_camera_eye);
}

void mp_group_camera_stick(int port, float* x, float* y)
{
    if (!camera_group || port < 0 || port > 3) {
        return;
    }
    if (port == mp_platform_slippi_net_local_index()) {
        *x = HSD_PadCopyStatus[0].nml_subStickX;   /* our own stick, live: our camera never lags it */
        *y = HSD_PadCopyStatus[0].nml_subStickY;
    } else {
        *x = camera_sticks[2 * port] / 127.0f;
        *y = camera_sticks[2 * port + 1] / 127.0f;
    }
}

/* ---- the scene flow ---- */

static int session_seen;   /* this group session's first lobby has been entered */

/* This console's part in the session's last group match, for a challenger fight when it leaves the lobby (see
 * mp_group_css_exit): its fighter, costume and name tag. */
static int last_played;
static u8 last_ckind, last_color, last_tag;

/* Vanilla's VS mode checks this save after each match for an earned hidden fighter (gmVsMelee_ExitResults: by VS
 * matches played, Mewtwo by VS play time, Marth by every starter played) and runs "A new challenger approaches". A group
 * cannot stop for one console's fight, so it is run when this console leaves the lobby (Back, or the session ended),
 * from the last group match it played, as the GM_CHALLENGER_APPROACH mode, which returns to the menus. */
static int challenger_on_leave(GameModeKind after)
{
    u8 ck;
    if (!last_played) {
        return 0;
    }
    last_played = 0;
    ck = gm_80172DD4(gmMainLib_8015ED98()->x0);
    if (ck == ChKind_None) {
        ck = gm_80172D78();
    }
    if (ck == ChKind_None) {
        ck = gm_80172E74();
    }
    if (ck == ChKind_None) {
        return 0;
    }
    logf_("Group: a new challenger approaches (fighter %d), on leaving the lobby\n", ck, 0);
    gm_InitChallengerData(last_ckind, last_color, 0, last_tag, ck, after);
    gm_ChangeGameModeAfterCurrentScene(GM_CHALLENGER_APPROACH);
    return 1;
}

/* The stars over the doors are the host's (the lobby): every console draws them from its local array. */
static void stars_from_lobby(void)
{
    MpGroupLobby lobby;
    /* while the host's results screen is still up, this console's own count from the same result stands (the lobby's
     * arrives as the host reopens it; the character select follows it from then) */
    if (mp_platform_group_lobby_get(&lobby) && lobby.phase != 3) {
        memcpy(gmVsMelee_GetKOCounts(), lobby.stars, 4);
    }
}

void mp_group_css_enter(GameModeState* state)
{
    if (!session_seen) {
        /* What vanilla does on entering VS mode (gmVsMelee_Mode_OnInit / OnLoad), which a group's mode does not run:
         * each port's auto handicap without a name tag starts at 5, and no stars. */
        session_seen = 1;
        gmMainLib_8015CDEC();
        gmVsMelee_ResetKOCounts();
        {
            int n = mp_platform_group_test_value(0);
            if (n > 0) {
                /* tests: this save as one match short of a milestone (the counters gm_80162800 advances per match) */
                if (gmMainLib_8015ED98()->x0 < (u32) n) {
                    gmMainLib_8015ED98()->x0 = n;
                }
                if ((u32) gmMainLib_8015EDB0()->x0 < (u32) n) {
                    gmMainLib_8015EDB0()->x0 = n;
                }
                logf_("Group: test_vs_matches: this save's VS match counters are now %d\n", n, 0);
            }
        }
    }
    stars_from_lobby();
    gmVsMelee_EnterCss(state, gmVsMelee_GetVsData(), VS_MELEE);
}

void mp_group_css_exit(GameModeState* state, int sss_state)
{
    CSSData* css = gm_GetGameModeStateExitData(state);
    int leaving = css->pending_scene_change == 2;
    gmVsMelee_ExitCss(state, gmVsMelee_GetVsData());   /* a Back sends the game to the menus */
    if (leaving) {
        /* Out of the lobby (Back, or the session ended): the "Choose a game." screen with the Group list again (a
         * notice first if the session ended under this console), or the menus without the top-screen submenu. */
        int choose = mp_group_top_menu();
        session_seen = 0;
        gmVsMelee_ResetKOCounts();   /* the next lobby, or Solo, starts with none */
        mp_platform_group_leave();
        if (choose) {
            mp_group_lobby_screen = 1;
            (void) mp_platform_group_returning();   /* not the menus' business this time */
        }
        /* the session is closed: this console is alone again (a challenger fight first, then that screen) */
        if (!challenger_on_leave(choose ? GM_TOURNAMENT : GM_MENU) && choose) {
            gm_ChangeGameModeAfterCurrentScene(GM_TOURNAMENT);
        }
    } else {
        gm_SetNextGameModeStateId(sss_state);
    }
}

/* The stage select follows the host's Stage Selection rule (the lobby's sss_picker), never this console's own: on the
 * host, Random and Ordered pick the stage from its save as vanilla does (the stage select is skipped, and its pick goes
 * to everyone); elsewhere, and for Turns and Loser, only the picker's console has a cursor (in the picker's colour), and
 * with Any every console has its own. */
void mp_group_sss_enter(GameModeState* state)
{
    SSSData* sss = gm_GetGameModeStateEnterData(state);
    MpGroupLobby lobby;
    int rule;
    if (!mp_platform_group_lobby_get(&lobby)) {
        gmVsMelee_EnterSss(state, gmVsMelee_GetVsData());
        return;
    }
    rule = lobby.rules.stage_sel;
    if (lobby.host && (rule == 1 || rule == 2)) {
        gmVsMelee_EnterSss(state, gmVsMelee_GetVsData());
        logf_("Group: stage selection rule %d: the host picks stage %d\n", rule, sss->force_stage_id);
        return;
    }
    sss->vs = *gmVsMelee_GetVsData();
    sss->force_stage_id = -1;
    sss->unk_stage = lobby.sss_picker < 4 ? lobby.sss_picker + 1 : 0;
    logf_("Group: stage selection rule %d, picker %d\n", rule, lobby.sss_picker < 4 ? lobby.sss_picker + 1 : 0);
}

/* Block until the host has started the match (its block is then pending), or the lobby went back to the CSS, or the
 * session ended. No time limit: the host starts as soon as its last game has finished going out (a few seconds at
 * most), and a host that goes away ends the session, which ends this wait. A sudden death is waited for whatever the
 * lobby says meanwhile (it shows the character select between the tied match and its sudden death), for at most about
 * 20 seconds. */
static int wait_match(int sudden)
{
    MpGroupLobby lobby;
    int idles = 0;
    mp_platform_group_notice(1);
    for (;;) {
        mp_platform_slippi_net_poll();
        if (mp_platform_group_lost()) {
            return 0;
        }
        if (mp_slippi_online_take_match()) {
            return 1;
        }
        if (sudden ? idles > 20000 : (mp_platform_group_lobby_get(&lobby) && lobby.phase == 0)) {
            return 0;
        }
        mp_platform_idle();   /* about 1 ms */
        idles++;
    }
}

void mp_group_sss_exit(GameModeState* state, int css_state, int vs_state)
{
    SSSData* sss = gm_GetGameModeStateExitData(state);
    int remote = mp_group_sss_remote_exit();
    int sent = mp_group_sss_sent();
    gmVsMelee_ExitSss(state, gmVsMelee_GetVsData(), (u8) css_state);
    if (!sss->start_game) {
        return;   /* Back: everyone returns to the lobby's character select */
    }
    /* The first pick wins: send ours (the host ignores it if another came first), then wait for the host's start, whose
     * block carries the stage that was picked. */
    if (!sent && !remote) {
        mp_platform_group_lobby_stage(sss->vs.start.rules.stkind, 0);
    }
    if (wait_match(0)) {
        mp_slippi_prepare_match_files(mp_slippi_online_pending_block());
        gm_SetNextGameModeStateId(vs_state);
    } else {
        gm_SetNextGameModeStateId(css_state);
    }
}

void mp_group_vs_enter(GameModeState* state)
{
    gmVsMelee_EnterVs(state, gmVsMelee_GetVsData(), NULL, NULL);
}

/* The result a sudden death is set up from, and whether the match that just ran was one. */
static MatchEnd sudden_end;
static int in_sudden;

/* After the match: the results screen, or, when the match was tied (as vanilla decides it), a sudden death. Every
 * console reaches the same decision from the same result; the host starts the sudden death game, everyone waits for
 * it (one that does not come shows the tied match's results). */
void mp_group_vs_exit(GameModeState* state, int results_state, int sudden_state)
{
    MatchExitInfo* exit = gm_GetGameModeStateExitData(state);
    gmVsMelee_ExitVs(state, (u8) results_state, (u8) results_state);
    gm_SetNextGameModeStateId(results_state);
    in_sudden = 0;
    if (!gm_MatchHasMultipleWinners(&exit->match_end)) {
        return;
    }
    sudden_end = exit->match_end;
    logf_("Group: the match was tied (%d winners); sudden death\n", exit->match_end.n_winners, 0);
    mp_platform_group_sudden_death();
    if (wait_match(1)) {
        mp_slippi_prepare_match_files(mp_slippi_online_pending_block());
        gm_SetNextGameModeStateId(sudden_state);
    }
}

/* The sudden death is over: as vanilla, its result is merged into the tied match's, which the results screen shows. */
void mp_group_sudden_exit(GameModeState* state, int results_state)
{
    gmVsMelee_ExitSuddenDeath(state);
    in_sudden = 0;
    gm_SetNextGameModeStateId(results_state);
}

/* ---- the results screen ---- */

static int match_auto;             /* the match was played with the host's handicap on Auto */
static u8 match_hc[4];             /* each door's handicap in it (the lobby's, 0 = not known) */

static int busy_wait;              /* frames before this console's "busy" goes to the host again */
static int prize_due;              /* this save earned a notice or trophy in the match: the prize screen follows */
static int early_advance;          /* a press made before the host reached its results screen: the step it asks for */
static int shown_step, shown_ready;   /* what the log last said about this screen (-1: nothing yet) */

/* Vanilla's checks of this save after a VS match (gmVsMelee_ExitResults), each console on its own save: the KO total,
 * stage unlocks by VS matches played, trophies for totals, the special messages ("You played 1000 VS matches"...) and
 * the trophies for VS matches with one fighter. Not the challenger checks (challenger_on_leave). Run as the results
 * screen opens, not as it ends: they only change save data, and so this console knows during the results whether its
 * prize screen follows, while its busy bit (results_pads) still holds the lobby. Returns 1 when a notice or trophy waits.
 * Unlike vanilla, a challenger being due does not hold the notices back: the fight comes when the lobby is left. */
static int save_checks(MatchEnd* end)
{
    u16 stage;
    if (!gmVsMelee_WasAnyPlayerHuman(end)) {
        return 0;
    }
    gm_8016247C(gm_801688AC(end));
    stage = gm_80172F00(gmMainLib_8015EDB0()->x0);
    logf_("Group: the save checks: %d VS matches, stage to unlock %d (328: none)\n", gmMainLib_8015EDB0()->x0, stage);
    if (stage != 328) {
        gm_80164504(stage);
    }
    gm_80173DE4(end);
    gm_80172898(1);
    gm_80173EEC();
    return gm_801721EC() ? 1 : 0;
}

void mp_group_results_enter(GameModeState* state)
{
    MatchEnd* end = &gmVsMelee_VsExitInfo.match_end;
    int me = mp_platform_slippi_net_local_index() & 3;
    busy_wait = 0;
    early_advance = 0;
    shown_step = shown_ready = -1;
    logf_("Group: the results screen\n", 0, 0);
    gmVsMelee_EnterResults(state);
    prize_due = save_checks(end);
    if (prize_due) {
        logf_("Group: this save earned a notice or trophy; the prize screen follows the results\n", 0, 0);
    }
    if (end->player_standings[me].pkind == Gm_PKind_Human) {
        last_played = 1;
        last_ckind = end->player_standings[me].ckind;
        last_color = end->player_standings[me].x3;
        last_tag = end->player_standings[me].x4;
    }
}

/* The prize screen after a group match (vanilla's, GS_PRIZE_INTERFACE): this console stays busy meanwhile, so the
 * others wait for it (reason "looking at a prize"); its character select clears that on return. */
void mp_group_prize_enter(GameModeState* state)
{
    logf_("Group: the prize screen\n", 0, 0);
    mp_platform_group_lobby_busy(5);   /* LOBBY_BUSY_PRIZE */
    gm_ModeState_Prize_OnEnter(state);
}

/* Each console runs the results screen itself. Its presses are the lobby's (results_pads below), so every console's
 * screen takes the same steps and ends once every Human player is ready, together. Then, in place of vanilla's
 * gmVsMelee_ExitResults (whose save checks ran as the screen opened, save_checks, and whose challenger fight waits for
 * leaving the lobby, challenger_on_leave):
 *   - the VS Records (vanilla's gm_801623A4, not for a cancelled match), and on the host the loser for the Loser stage
 *     selection rule (gm_80168710);
 *   - the stars: vanilla's count (gmVsMelee_UpdateKOCounts: the Human players, cancelled matches too) on the lobby's;
 *   - auto handicap (vanilla's gm_80168638, not for a cancelled match): every console computes every door's new value
 *     from the same result and the handicaps the match was played with, and writes into its own save only what
 *     belongs to it: its own Human door, and on the host every CPU door (as mp_css_auto_hc reads them);
 *   - the host puts both into the lobby as it reopens it;
 *   - the prize screen next, when save_checks found a notice or trophy, else the character select. */
/* The VS Records this match touches, in the log before and after they are written (tests compare them): each port's
 * fighter's matches, wins and losses, and each port's tag (0x78: none) with its play time with that fighter and the sum
 * of its KOs against every tag. */
static void record_log(MatchEnd* end, int after)
{
    char line[200];
    int i, k, n = 0;
    n += snprintf(line + n, sizeof line - n, "Group: VS Records %s:", after ? "after" : "before");
    for (i = 0; i < 4; i++) {
        MatchPlayerData* p = &end->player_standings[i];
        struct FighterData* fd;
        unsigned kos = 0, play = 0;
        if (p->pkind == 3) {
            continue;
        }
        fd = GetPersistentFighterData(gm_CKindToSelKind(p->ckind));
        if (p->x4 != 0x78) {
            struct NameTagData* nt = GetPersistentNameData(p->x4);
            for (k = 0; k < 120; k++) {
                kos += nt->vs_kos[k];
            }
            play = nt->play_time_by_fighter[gm_CKindToSelKind(p->ckind)];
        }
        n += snprintf(line + n, sizeof line - n, " p%d f%d m%u w%u l%u t%u tp%u tk%u;", i + 1, p->ckind, fd->match_count,
                      fd->victories, fd->losses, p->x4, play, kos);
    }
    snprintf(line + n, sizeof line - n, "\n");
    mp_platform_log(line);
}

void mp_group_results_exit(GameModeState* state, int css_state, int prize_state)
{
    MatchEnd* end = &gmVsMelee_VsExitInfo.match_end;
    MpGroupLobby lobby;
    u8 stars[4], hc[4];
    int me = mp_platform_slippi_net_local_index();
    int host, i;
    (void) state;
    memset(hc, 0, sizeof hc);
    memset(stars, 0, sizeof stars);
    host = mp_platform_group_lobby_get(&lobby) && lobby.host;
    if (!gm_WasMatchCanceled(end->outcome)) {
        /* VS Records, as vanilla: every fighter in the match (the same on every console) and this console's own name
         * tag; remote players' slots carry tag 0x78 (none), so no tag of this save is credited for them */
        record_log(end, 0);
        gm_801623A4(end);
        logf_("Group: VS Records written for this match (this console's tag %d)\n", end->player_standings[me & 3].x4, 0);
        record_log(end, 1);
        if (host) {
            /* the Loser stage selection rule's picker (vanilla's gm_80168710), kept by the host, who decides it */
            gm_80168710(end, gmVsMelee_GetVsData());
        }
    }
    if (lobby.valid) {
        memcpy(stars, lobby.stars, 4);
    }
    for (i = 0; i < 4; i++) {
        if (end->player_standings[i].pkind == Gm_PKind_Human) {
            stars[i] = (u8) lbTime_8000AF74(stars[i], end->player_standings[i].x20);
        }
    }
    if (match_auto && !gm_WasMatchCanceled(end->outcome)) {
        for (i = 0; i < 4; i++) {
            hc[i] = match_hc[i] >= 1 && match_hc[i] <= 9 ? match_hc[i] : 5;
        }
        fn_80169000(end, hc);
        for (i = 0; i < 4; i++) {
            u8 kind = end->player_standings[i].pkind;
            if (kind == 3) {
                hc[i] = 0;
                continue;
            }
            if ((kind == Gm_PKind_Human && i == me) || (kind == Gm_PKind_Cpu && host)) {
                s8* saved = gmMainLib_8015CE44(i, end->player_standings[i].x4);
                if (saved != NULL) {
                    *saved = (s8) hc[i];
                }
            }
        }
        logf_("Group: auto handicap after the match: %04x %04x (ports 1-2, 3-4)\n", hc[0] << 8 | hc[1], hc[2] << 8 | hc[3]);
    }
    memcpy(gmVsMelee_GetKOCounts(), stars, 4);
    mp_platform_group_results_end(stars, hc, host ? gmVsMelee_GetVsData()->winner : -1);
    match_auto = 0;
    lbCardGame_SetupArchive();
    if (prize_due) {
        prize_due = 0;
        gm_InitChallengerData(ChKind_None, 0, me & 3, 0x78, ChKind_None, 0);   /* as vanilla before its prize state */
        gm_SetNextGameModeStateId(prize_state);
        return;
    }
    gm_SetNextGameModeStateId(css_state);
}

/* The pads the results screen reads this frame (gmresultplayer.c, in a group). Vanilla's steps: 0 a wait (a press
 * skips it only after a cancelled match), 1 the intro, 2 waiting for any press, 3 the players' panels, where each
 * Human player's Start toggles their "ready" (with no Human player, anyone's Start ends it), 4 the end. A press that
 * moves the screen is never applied here: it goes to the host, and every console replays the lobby's state as presses
 * on the ports concerned, until its screen matches it. So all screens take the same steps, whenever the lobby's update
 * arrives. A press made while the host is still in the match (this console's game ended first) is kept and sent once
 * the lobby shows the host's results screen. This console's other buttons (a player's pages and scrolling) stay local, on its own port. The lobby back
 * in the character select (the host's screen ended) or a lost session plays the screen to its end. */
void mp_group_results_pads(const MpGroupResults* r, const MatchEnd* end, HSD_PadStatus* copy, HSD_PadStatus* master)
{
    MpGroupLobby lobby;
    int me = mp_platform_slippi_net_local_index() & 3;
    unsigned humans = 0, want_ready = 0;
    int want_advance = 0, over, here, inj = -1, i;
    u32 trig = HSD_PadCopyStatus[0].trigger;
    int canceled = gm_WasMatchCanceled(end->outcome);
    int ok = mp_platform_group_lobby_get(&lobby);

    for (i = 0; i < 4; i++) {
        if (end->player_standings[i].pkind == Gm_PKind_Human) {
            humans |= 1u << i;
            if (inj < 0) {
                inj = i;
            }
        }
    }
    if (inj < 0) {
        inj = me;
    }
    /* This console is busy while its results screen is up: the host refuses Start until every console is back on the
     * character select (its CSS clears the bit), so the lobby stays there until this screen has seen it. */
    if (ok && !(lobby.busy >> me & 1) && --busy_wait <= 0) {
        mp_platform_group_lobby_busy(1);
        busy_wait = 30;
    }
    /* The lobby counts only in a view that arrived after this console's match ended: an older one is from before the
     * host reached its results (a STAGE or MATCH phase, or the results phase the host passes through when a match is
     * tied, before its sudden death). Here: the host's results screen is up. Over: the host's screen ended (the lobby
     * is back in the character select). */
    here = ok && lobby.since_end && lobby.phase == 3;
    over = !ok || mp_platform_group_lost() || (lobby.phase == 0 && lobby.since_end);
    if (over) {
        want_ready = 0xF;
        want_advance = 2;
    } else if (here) {
        want_ready = lobby.ready;
        want_advance = lobby.advance;
    }

    for (i = 0; i < 4; i++) {
        memset(&copy[i], 0, sizeof copy[i]);
        memset(&master[i], 0, sizeof master[i]);
        copy[i].err = master[i].err = (humans >> i & 1) ? 0 : PAD_ERR_NO_CONTROLLER;
    }
    copy[me] = HSD_PadCopyStatus[0];   /* this console's only pad, on its own port */
    master[me] = HSD_PadMasterStatus[0];
    copy[me].trigger &= r->stage < 3 ? 0 : ~(u32) PAD_BUTTON_START;

    /* this console's press: to the host (kept until the host's results screen is up) */
    if (!over && trig != 0 && ((humans >> me & 1) || humans == 0)) {
        int advance = 0;
        if (r->stage == 0 && canceled && humans && (trig & PAD_BUTTON_START)) {
            advance = 1;
        } else if (r->stage == 2) {
            advance = 2;
        }
        if (advance > early_advance) {
            early_advance = advance;
            if (!here) {
                logf_("Group: results press kept until the host's results screen (step %d)\n", advance, 0);
            }
        }
        if (here && r->stage == 3 && r->panels_live && (trig & PAD_BUTTON_START)) {
            mp_platform_group_results_press(!(lobby.ready >> me & 1), 0);
        }
    }
    if (!over && here && early_advance > 0) {
        if (early_advance > lobby.advance) {
            mp_platform_group_results_press(lobby.ready >> me & 1, early_advance);
        }
        early_advance = 0;
    }

    if (r->stage != shown_step || r->ready != shown_ready) {
        shown_step = r->stage;
        shown_ready = r->ready;
        logf_("Group: results step %d, ready %x\n", r->stage, r->ready);
    }

    /* the lobby's state, as presses */
    if (r->stage == 0 && want_advance >= 1 && canceled && humans) {
        copy[inj].trigger |= PAD_BUTTON_START;
    } else if (r->stage == 2 && want_advance >= 2) {
        copy[inj].trigger |= PAD_BUTTON_A;
        copy[inj].err = master[inj].err = 0;
    } else if (r->stage == 3 && r->panels_live) {
        if (humans) {
            for (i = 0; i < 4; i++) {
                if ((humans >> i & 1) && (r->ready >> i & 1) != (want_ready >> i & 1)) {
                    copy[i].trigger |= PAD_BUTTON_START;
                }
            }
        } else if (want_ready & 0xF) {
            for (i = 0; !(want_ready >> i & 1); i++) {
            }
            copy[i].trigger |= PAD_BUTTON_START;
            copy[i].err = master[i].err = 0;
        }
    }
}

/* ---- the match ---- */

static unsigned cpu_applied;      /* ports already handed to a CPU this match */
static char tags[4][12];
static u8 tag_have[4];
static int tag_active;

int mp_group_frame_mask(unsigned mask, int frame, int normal, int local_index, const unsigned char* block)
{
    int i;
    (void) block;
    if (!normal) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (i == local_index || !(mask & (1u << i)) || (cpu_applied & (1u << i))) {
            continue;
        }
        cpu_applied |= 1u << i;
        if (Player_GetPlayerSlotType(i) != Gm_PKind_Human) {
            continue;   /* a CPU, or no fighter on that port (an empty door, out of a sudden death): nothing to take over */
        }
        Player_SetSlottype(i, Gm_PKind_Cpu);
        Player_SetPlayerAndEntityCpuType(i, 4);
        Player_SetPlayerAndEntityCpuLevel(i, 3);
        logf_("Group: port %d dropped; a level 3 CPU plays from frame %d\n", i + 1, frame);
    }
    if (mask & (1u << local_index)) {
        logf_("Group: the host made this console a CPU at frame %d\n", frame, 0);
        return 1;   /* the host is waiting for us no more: leave the match */
    }
    return 0;
}

/* The console's name over that port's damage display, as the game's text bytes (a UTF-8 name would be misread: kana
 * and accented letters go through the shared converter, port/3ds/name_text.c). */
int mp_group_hud_name(int port, char* name, int size)
{
    char raw[MP_GROUP_NAME_MAX];
    if (mp_platform_group_players() <= 0) {
        return -1;
    }
    name[0] = 0;
    raw[0] = 0;
    mp_platform_group_player_name(port, raw, sizeof raw);
    if (raw[0] != 0) {
        mp_platform_slippi_ui_sis(raw, name, size);
    }
    return name[0] != 0;
}

/* Every remote slot of the match block keeps tag id 0x78 (none), so nothing touches this console's save. A remote
 * player's tag is its 8 bytes of text from the lobby; ifnametag.c asks these two to draw it as vanilla does. */
int mp_group_tag_off(int slot)
{
    if (Player_GetNametagSlotID(slot) != 0x78) {
        return 0;
    }
    return !(tag_active && tag_have[slot & 3]);
}

char* mp_group_tag_text(int slot)
{
    if (Player_GetNametagSlotID(slot) == 0x78 && tag_active && tag_have[slot & 3]) {
        return tags[slot & 3];
    }
    return GetNameText(Player_GetNametagSlotID(slot));
}

/* The host's check of its own block against vanilla: the scene ran gmVsMelee_EnterVs from the host's VS data (its
 * rules menu, and doors mirrored from the lobby), so `vanilla` is what a Solo VS match would start with. The rules
 * bytes the block carries (0..0x37: kind, timer, stock flag, items, item mask, damage ratio, pause...) and each used
 * door's stocks and handicap must equal it; a difference is logged, byte by byte. Not run for a sudden death, which
 * vanilla sets up differently. */
void mp_group_check_block(const StartMeleeData* vanilla, const unsigned char* block)
{
    static const unsigned char player_bytes[] = { 2, 8, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F };
    const unsigned char* v = (const unsigned char*) vanilla;
    int i, k, diffs = 0;
    if ((block[0x13C] & 3) != 0 || block[0x13F] != 0) {
        return;   /* joiners have no vanilla copy of the host's rules; a sudden death is not built from the rules */
    }
    for (i = 0; i < 0x38; i++) {
        if (i == 0xE || i == 0xF || v[i] == block[i]) {
            continue;   /* the stage: the lobby's first pick, which this console's stage select may not have made */
        }
        logf_("Group: rules byte 0x%02x differs from vanilla (vanilla/block %04x)\n", i, v[i] << 8 | block[i]);
        diffs++;
    }
    for (i = 0; i < 4; i++) {
        const unsigned char* p = block + 0x60 + 0x24 * i;
        if (p[1] == 3) {
            continue;   /* no player on this port */
        }
        for (k = 0; k < (int) sizeof player_bytes; k++) {
            int at = 0x60 + 0x24 * i + player_bytes[k];
            if (v[at] != block[at]) {
                logf_("Group: player byte 0x%03x differs from vanilla (vanilla/block %04x)\n", at, v[at] << 8 | block[at]);
                diffs++;
            }
        }
    }
    logf_("Group: the match block's rules checked against vanilla: %d differences\n", diffs, 0);
}

/* The name of the console that played each port, as the match began (see mp_group_result_name). */
static char result_names[4][MP_GROUP_NAME_MAX];

int mp_group_result_name(int port, char* out, int len)
{
    if (len <= 0 || port < 0 || port > 3 || result_names[port][0] == 0 || mp_platform_group_players() <= 0) {
        return 0;
    }
    mp_platform_slippi_ui_sis(result_names[port], out, len);
    return out[0] != 0;
}

void mp_group_match_begin(StartMeleeData* data, int local_index, const unsigned char* block)
{
    MpGroupLobby lobby;
    int i, j;
    if (block[0x13F] == 1) {
        /* A sudden death: the tied match's block, set up as vanilla does from that match's result (the tied players
         * at 1 stock and 300 %, the others out; this console's copy of the result is every console's). The scene set
         * rules.x6 before the block replaced the data. */
        gm_SetupSuddenDeath(data, &sudden_end);
        data->rules.x6 = 1;
        in_sudden = 1;
        logf_("Group: sudden death starts\n", 0, 0);
    }
    cpu_applied = 0;
    tag_active = 0;
    memset(tag_have, 0, sizeof tag_have);
    if (!mp_platform_group_lobby_get(&lobby)) {
        return;
    }
    if (block[0x13F] != 1) {
        /* The results screen's names: every port that is a Human in this match, by the name of its console now. Later
         * roster changes (a console leaving, a new one taking its port) must not change them, and a player who drops
         * keeps theirs; a sudden death keeps the tied match's. */
        for (i = 0; i < 4; i++) {
            result_names[i][0] = 0;
            if (data->players[i].slot_type == Gm_PKind_Human) {
                mp_platform_group_player_name(i, result_names[i], sizeof result_names[i]);
                result_names[i][sizeof result_names[i] - 1] = 0;
            }
        }
        {
            char line[96];
            snprintf(line, sizeof line, "Group: results names: 1 '%s' 2 '%s' 3 '%s' 4 '%s'\n", result_names[0],
                     result_names[1], result_names[2], result_names[3]);
            mp_platform_log(line);
        }
        /* auto handicap after this match starts from the handicaps it is played with (the doors are frozen since the
         * stage select, so every console holds the same) */
        match_auto = lobby.rules.handicap == 1;
        for (i = 0; i < 4; i++) {
            match_hc[i] = lobby.door[i].handicap;
        }
    }
    /* this console's own tag keeps its real id (the shared block carries none) */
    data->players[local_index].nametag = gmVsMelee_GetVsData()->start.players[local_index].nametag;
    if (mp_platform_group_test_value(1) > 0) {
        data->players[local_index].nametag = (u8) (mp_platform_group_test_value(1) - 1);   /* tests: test_nametag */
    }
    for (i = 0; i < 4; i++) {
        const MpGroupDoor* door = &lobby.door[i];
        if (i == local_index || door->kind != 0 || door->tag[0] == 0) {
            continue;   /* only a Human door shows a tag */
        }
        for (j = 0; j < 8; j++) {
            tags[i][j] = door->tag[j] == '%' ? '?' : (char) door->tag[j];
        }
        tags[i][8] = 0;
        tag_have[i] = 1;
        tag_active = 1;
    }
}

void mp_group_match_exit(void)
{
    tag_active = 0;
    mp_platform_group_game_end();   /* the results screen follows; the host reopens the lobby when it ends */
}
