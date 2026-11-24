/* Group play, engine side (see group_engine.h): the scene flow of a group (lobby CSS -> stage select -> match -> back),
 * the CPU takeover of a dropped console's fighter, and remote players' name tags. The network and the lobby are
 * native (port/3ds/group, through group_bridge.h). */
#include <stdio.h>
#include <string.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/mn/mnname.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <group_bridge.h>
#include <group_engine.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>

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

/* ---- the scene flow ---- */

void mp_group_css_enter(GameModeState* state)
{
    gmVsMelee_EnterCss(state, gmVsMelee_GetVsData(), VS_MELEE);
}

void mp_group_css_exit(GameModeState* state, int sss_state)
{
    CSSData* css = gm_GetGameModeStateExitData(state);
    int leaving = css->pending_scene_change == 2;
    gmVsMelee_ExitCss(state, gmVsMelee_GetVsData());   /* a Back sends the game to the menus */
    if (leaving) {
        mp_platform_group_leave();
    } else {
        gm_SetNextGameModeStateId(sss_state);
    }
}

void mp_group_sss_enter(GameModeState* state)
{
    gmVsMelee_EnterSss(state, gmVsMelee_GetVsData());
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

/* After the match: back to the lobby, or, when the match was tied (as vanilla decides it), a sudden death. Every
 * console reaches the same decision from the same result; the host starts the sudden death game, everyone waits for
 * it. As in vanilla, a sudden death is never followed by another. */
void mp_group_vs_exit(GameModeState* state, int css_state, int sudden_state)
{
    MatchExitInfo* exit = gm_GetGameModeStateExitData(state);
    int was_sudden = in_sudden;
    gmVsMelee_ExitVs(state, (u8) css_state, (u8) css_state);
    gm_SetNextGameModeStateId(css_state);
    in_sudden = 0;
    if (was_sudden || !gm_MatchHasMultipleWinners(&exit->match_end)) {
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

int mp_group_hud_name(int port, char* name, int size)
{
    if (mp_platform_group_players() <= 0) {
        return -1;
    }
    name[0] = 0;
    mp_platform_group_player_name(port, name, size);
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
    /* this console's own tag keeps its real id (the shared block carries none) */
    data->players[local_index].nametag = gmVsMelee_GetVsData()->start.players[local_index].nametag;
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
    mp_platform_group_game_end();   /* the host reopens the doors and the stage select for everyone */
}
