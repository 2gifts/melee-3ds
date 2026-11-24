/* PC console for group play tests: the 3DS's group code (port/3ds/group, the localwireless module) over the loopback
 * link, with deterministic test pads instead of a game. Several of these play each other (tools/group/group_test.py),
 * or one plays an emulated 3DS (tools/group/group_lobby_test.py).
 *
 * Usage: group_client --dir DIR   (DIR holds group.ini: the session's own keys, see group_session.h, plus)
 *   role=host|join      host the lobby, or join the first game found
 *   via_menu=1          meet through the Melee -> Group pages (group_menu.c) instead (needs menu=1)
 *   test_players=N      host: wait for N consoles before filling the doors (default 2)
 *   test_games=N        games played back to back (default 1)
 *   test_frames=N       frames per game (default 600)
 *   test_character=N    this console's fighter is N + its port (mod 26)
 *   test_crash_frame=F  vanish without a goodbye at frame F of the first game
 *   test_late=1         join only once the host's match is running: watch its status (the Match in progress page's
 *                       data) until it ends, then play the next games (test_games counts only those)
 *   test_linger_ms=N    stay in the lobby N ms after the last game before closing it
 *   test_full=1         expect the lobby to be full: see it listed as full, try to join anyway, and pass when refused
 * A host publishes made-up match status (timer, stocks, damage) during its games, as a real host's bottom screen does.
 * Every console prints "bundle hash" per game; consoles that stayed must agree. Exit 0 = PASS. */
#include "group_bridge.h"
#include "group_menu.h"
#include "group_session.h"
#include "localwireless.h"
#include "slippi_plat.h"
#include "slippi_testpattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define FRAME_US 16683

static struct { int host, via_menu, players, games, frames, character, crash_frame, delay, late, full, linger_ms; } T;

static const char *status_name(int s)
{
    static const char *names[] = {"idle", "searching", "connecting", "connected", "failed", "disconnected"};
    return s >= 0 && s <= 5 ? names[s] : "?";
}

/* Waits until cond() or timeout, servicing the network. 1 = condition met. */
static int wait_for(int (*cond)(void), int timeout_ms)
{
    static int last = -1;
    uint64_t end = sp_time_us() + (uint64_t)timeout_ms * 1000;
    while (sp_time_us() < end) {
        group_net_poll();
        int st = group_net_status();
        if (st != last) { sp_log("group-test: status %s", status_name(st)); last = st; }
        if (st == 4 || st == 5) return 0;
        if (cond()) return 1;
        sp_sleep_us(5000);
    }
    return 0;
}

static int connected(void) { return group_net_status() == 3; }

static int joined;
static int join_first(void)
{
    if (group_net_status() == 3) return 1;
    group_game g;
    if (!joined && group_scan(&g, 1) > 0 && (!T.late || g.lobby.phase == LOBBY_MATCH)) {
        sp_log("group-test: found a game by '%s' with %d consoles; joining", g.host, g.players);
        if (group_join(g.key) == 0) joined = 1;
    }
    return 0;
}

/* Melee -> Group: Group, then Host or the first game in the list. */
static int via_menu(void)
{
    uint64_t end = sp_time_us() + 60000000;
    int asked = 0, requested = 0;
    if (!group_open()) { sp_log("group-test: FAIL the group menu is off (menu=1)"); return -1; }
    while (sp_time_us() < end) {
        const GroupView *v = group_view();
        if (v->page == GROUP_PAGE_ASK && !asked) { group_pick_group(); asked = 1; }
        else if (v->page == GROUP_PAGE_LIST && !requested) {
            if (T.host) { group_host(); requested = 1; }
            else if (v->count > 0) {
                sp_log("group-test: the list shows '%s', %d consoles", v->row[0].host, v->row[0].players);
                group_join_row(0);
                requested = 1;
            }
        }
        int r = group_poll(0);
        if (r == 2) return 0;
        if (r != 0) { sp_log("group-test: FAIL the group menu returned %d", r); return -1; }
        if (v->error[0]) { sp_log("group-test: FAIL group menu: %s", v->error); return -1; }
        sp_sleep_us(5000);
    }
    sp_log("group-test: FAIL the group menu timed out");
    return -1;
}

/* A console that joined during a match: the host's status must arrive and move until the match is over. */
static int watch_match(void)
{
    int updates = 0, last = -1, max_seconds = 0;
    uint64_t end = sp_time_us() + 120000000;
    if (!group_watching()) { sp_log("group-test: FAIL joined late but no match is running here"); return 0; }
    while (sp_time_us() < end && group_watching()) {
        lobby_state v;
        group_net_poll();
        if (group_lobby_view(&v) && v.status.valid && (int)v.status.seconds != last) {
            last = v.status.seconds;
            ++updates;
            if (last > max_seconds) max_seconds = last;
        }
        sp_sleep_us(10000);
    }
    sp_log("group-test: watched the match in progress: %d status updates, timer up to %d s", updates, max_seconds);
    if (updates < 3) { sp_log("group-test: FAIL too few status updates while watching"); return 0; }
    return 1;
}

/* A fifth console: the game shows as full (4 of 4) and a join is refused, cleanly. 0 = as expected. */
static int expect_full(void)
{
    group_game g;
    uint64_t end = sp_time_us() + 60000000;
    int seen = 0;
    while (sp_time_us() < end) {
        group_net_poll();
        if (group_scan(&g, 1) > 0 && g.max > 0 && g.players >= g.max) { seen = 1; break; }
        sp_sleep_us(10000);
    }
    if (!seen) { sp_log("group-test: FAIL the full game was never listed as full"); return 1; }
    sp_log("group-test: the game is listed as full (%d of %d)", g.players, g.max);
    if (group_join(g.key) != 0) { sp_log("group-test: refused at once: %s", group_net_error()); return 0; }
    end = sp_time_us() + 30000000;
    while (sp_time_us() < end) {
        group_net_poll();
        int st = group_net_status();
        if (st == 3) { sp_log("group-test: FAIL a fifth console got into the lobby"); return 1; }
        if (st == 4 || st == 5) { sp_log("group-test: refused: %s", group_net_error()); return 0; }
        sp_sleep_us(10000);
    }
    sp_log("group-test: FAIL the join neither failed nor succeeded");
    return 1;
}

static int all_here(void) { return __builtin_popcount(group_present()) >= T.players; }
/* Joining opens no door, so a console still on its way to opening its own would only sit the match out: the host
 * waits, as a person would, for every console here to have a fighter in its door. */
static int doors_ready(void)
{
    lobby_state v;
    if (!group_lobby_view(&v) || !lobby_can_start(&v)) return 0;
    for (int i = 0; i < LOBBY_SLOTS; ++i)
        if ((group_present() >> i & 1) && v.slot[i].kind != LOBBY_HUMAN) return 0;
    return 1;
}
static int in_stage(void) { lobby_state v; return group_lobby_view(&v) && v.phase == LOBBY_STAGE; }
static unsigned char block[LOBBY_BLOCK_SIZE];
static int block_ready(void) { return group_net_match_block(block); }

/* Every console takes its own door, the host adds a CPU on a free port, the host presses Start and picks. */
static int lobby_dance(int me, int game)
{
    if (game == 1) {
        if (T.host && !wait_for(all_here, 30000)) { sp_log("group-test: FAIL the other consoles never arrived"); return 0; }
        MpGroupDoor d;
        memset(&d, 0, sizeof(d));
        d.kind = 0; d.fighter = (unsigned char)((T.character + me) % 26); d.color = (unsigned char)me; d.chosen = 1; d.level = 3;
        group_lobby_set_door(me, &d);
        if (T.host && T.players < 4) {            /* a CPU on the first free port: no console there */
            MpGroupDoor c = d;
            c.kind = 1; c.fighter = 0x0A; c.color = 0; c.level = 7;
            group_lobby_set_door(T.players, &c);
        }
    }
    if (T.host) {
        if (!wait_for(doors_ready, 30000)) { sp_log("group-test: FAIL the doors never filled"); return 0; }
        group_lobby_start();
        if (!wait_for(in_stage, 10000)) { sp_log("group-test: FAIL no stage select"); return 0; }
        group_lobby_stage(0x1F, 0);
    }
    if (!wait_for(block_ready, T.host ? 30000 : 600000)) { sp_log("group-test: FAIL no match block (game %d): %s", game, group_net_error()); return 0; }
    sp_log("group-test: match block ready: stage 0x%02x%02x slots %u/%u/%u/%u types %u/%u/%u/%u local %u delay %u", block[0xE], block[0xF],
           block[0x60], block[0x84], block[0xA8], block[0xCC], block[0x61], block[0x85], block[0xA9], block[0xCD], block[0x13C], block[0x13D]);
    return 1;
}

/* One game of test pads. 0 = every check passed. */
static int play_game(int me, int game)
{
    const int delay = block[0x13D] ? block[0x13D] : T.delay, frames = T.frames;
    const int nplayers = group_players();
    int frame = 1, skips = 0, bad_pads = 0, bad_checksums = 0, checked_checksums = 0, last_checked_cf = 0, result = 0;
    uint32_t bundle_hash = 2166136261u;
    unsigned seen_cpu = 0;
    uint64_t start = sp_time_us(), next = start;
    group_net_new_game();
    while (frame <= frames) {
        uint64_t now = sp_time_us();
        if (now < next) { group_net_poll(); sp_sleep_us((uint32_t)(next - now > 2000 ? 2000 : next - now)); continue; }
        next += FRAME_US;
        uint8_t local[12] = {0}, remote[64];
        slippi_test_pad(me, frame + delay, local);
        int finalized = frame - 1;
        int rc = group_net_send_inputs(frame, delay, finalized, slippi_test_checksum(finalized), local, remote);
        if (rc == 3) { sp_log("group-test: disconnected at frame %d: %s", frame, group_net_error()); result = -1; break; }
        if (rc == 2) { ++skips; continue; }
        if (T.host) {
            /* what a real host's bottom screen passes on: a timer, and stocks and damage per port */
            lobby_status st;
            memset(&st, 0, sizeof(st));
            st.timer = 1;
            st.seconds = (uint16_t)(480 - frame / 60);
            for (int i = 0; i < 4; ++i) {
                st.port[i].kind = (uint8_t)(i < nplayers ? 0 : 3);
                st.port[i].stocks = (uint8_t)(4 - frame / 200 % 4);
                st.port[i].damage = (uint16_t)((frame + 37 * i) % 300);
            }
            group_match_status(&st);
        }
        unsigned mask = remote[48];
        for (int i = 0; i < nplayers; ++i) {
            if (mask & (1u << i)) {
                if (!(seen_cpu & (1u << i))) { seen_cpu |= 1u << i; sp_log("group-test: port %d is a CPU from frame %d", i + 1, frame); }
                static const uint8_t zero[8];
                if (memcmp(remote + 12 * i, zero, 8)) ++bad_pads;
            } else if (!slippi_test_pad_ok(i, frame, remote + 12 * i)) {
                if (bad_pads++ < 5) sp_log("group-test: bundle pad mismatch for port %d at frame %d", i + 1, frame);
            }
        }
        if ((seen_cpu & ~mask) != 0) { ++bad_pads; sp_log("group-test: CPU bit cleared at frame %d", frame); }
        uint8_t tmp[37];
        memcpy(tmp, &frame, 4);
        tmp[4] = (uint8_t)mask;
        for (int i = 0; i < 4; ++i) memcpy(tmp + 5 + 8 * i, remote + 12 * i, 8);
        for (int i = 0; i < 37; ++i) bundle_hash = (bundle_hash ^ tmp[i]) * 16777619u;
        int cf = group_net_remote_checksum_frame();
        if (cf > 0 && cf != last_checked_cf) {
            last_checked_cf = cf;
            ++checked_checksums;
            if (group_net_remote_checksum_value() != slippi_test_checksum(cf)) ++bad_checksums;
        }
        if (game == 1 && T.crash_frame && frame == T.crash_frame) {
            sp_log("group-test: vanishing at frame %d", frame);
#ifndef _WIN32
            _exit(0);
#endif
        }
        ++frame;
    }
    int done = frame - 1;
    uint64_t linger = sp_time_us() + 1500000;   /* keep servicing so the last acks and pads arrive */
    while (sp_time_us() < linger) { group_net_poll(); sp_sleep_us(5000); }
    double seconds = (double)(sp_time_us() - start) / 1e6;
    sp_log("group-test: %d/%d frames in %.2f s, %d skips, pad mismatches %d, checksums %d checked %d bad, packets tx %d rx %d resends %d "
           "test-dropped %d", done, frames, seconds, skips, bad_pads, checked_checksums, bad_checksums, group_net_stat(LW_STAT_PACKETS_SENT),
           group_net_stat(LW_STAT_PACKETS_RECEIVED), group_net_stat(LW_STAT_RESENDS), group_net_stat(LW_STAT_DROPPED_TEST));
    sp_log("group-test: bundle hash %08x, %d consoles, cpu mask %x", (unsigned)bundle_hash, nplayers, seen_cpu);
    /* a console alone in its match (a host with CPUs) has nobody's checksum to check */
    int pass = result == 0 && done == frames && bad_pads == 0 && bad_checksums == 0 && (checked_checksums > 0 || nplayers <= 1);
    sp_log("group-test: game %d %s", game, pass ? "PASS" : "FAIL");
    return pass ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *dir = ".";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    sp_set_data_dir(dir);
    sp_lock_init();
    char path[512];
    snprintf(path, sizeof(path), "%s/group.ini", dir);
    char *ini = sp_read_file(path, NULL);
    const char *s = ini ? ini : "";
    T.host = strstr(s, "role=host") != NULL;
    T.via_menu = group_ini_int(s, "via_menu", 0);
    T.players = group_ini_int(s, "test_players", 2);
    T.games = group_ini_int(s, "test_games", 1);
    T.frames = group_ini_int(s, "test_frames", 600);
    T.character = group_ini_int(s, "test_character", 2);
    T.crash_frame = group_ini_int(s, "test_crash_frame", 0);
    T.delay = group_ini_int(s, "delay", 2);
    T.late = group_ini_int(s, "test_late", 0);
    T.full = group_ini_int(s, "test_full", 0);
    T.linger_ms = group_ini_int(s, "test_linger_ms", 0);
    free(ini);

    sp_log("group-test: starting as %s (data dir %s)", T.host ? "host" : "joiner", dir);
    if (T.via_menu ? via_menu() != 0 : group_session_start(T.host, 0) != 0) {
        sp_log("group-test: FAIL start: %s", group_net_error());
        group_session_stop();
        return 1;
    }
    if (T.full) {
        int r = expect_full();
        sp_log("group-test: %s", r ? "FAIL" : "PASS");
        group_session_stop();
        return r;
    }
    if (!wait_for(T.via_menu || T.host ? connected : join_first, 120000)) {
        sp_log("group-test: FAIL no connection: %s", group_net_error());
        group_session_stop();
        return 1;
    }
    int me = group_net_local_index();
    sp_log("group-test: in the lobby on port %d", me + 1);
    int failed = 0;
    if (T.late && !watch_match()) failed = 1;
    for (int game = 1; game <= T.games && !failed; ++game) {
        if (!lobby_dance(me, game)) { failed = 1; break; }
        if (play_game(me, game) != 0) { failed = 1; break; }
        group_game_end();
        sp_log("group-test: back in the lobby");
    }
    for (uint64_t end = sp_time_us() + (uint64_t)T.linger_ms * 1000; sp_time_us() < end;) {
        group_net_poll();
        sp_sleep_us(5000);
    }
    sp_log("group-test: %s", failed ? "FAIL" : "PASS");
    group_session_stop();
    return failed ? 1 : 0;
}
