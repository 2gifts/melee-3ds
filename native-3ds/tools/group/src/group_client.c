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
 *   test_results_hold_ms=N    on the results screen, press ready only after N ms (the others must wait for it)
 *   test_results_linger_ms=N  stay on the results screen (busy) N ms after the lobby is back in the character select,
 *                       as a console whose screen ends later; a host tries Start meanwhile, which must be refused
 *   test_results_leave=1|2    leave on the first results screen without pressing ready: 1 a goodbye, 2 vanish
 *   test_after_lost=1   joiner (via_menu): after its games, wait for the session to end under it (group_lost), reopen
 *                       the Host/Join page as the VS menu does, and log the note it shows ("group-test: lost note: ...")
 *   test_vanish_ms=N    host: vanish without a goodbye N ms after its last game
 *   test_expect_error=1 via_menu: the Group page is expected to fail with "Please enable wireless and try again"
 *                       (test_wireless_off=1): log it, try Group once more, and pass if it fails the same way
 *   test_wait_each=1    host: before every game (not only the first), wait for test_players consoles
 *   test_zelda=1        host: its match status says its own port plays Zelda (18) for the first half of each game and
 *                       Sheik (19) after it, as a real host's does when Zelda transforms
 *   test_stale_view=1   joiner: in game 1, keep the lobby view from before the host's Start until 1.5 s after the
 *                       match ends (as if every snapshot in between was lost), so the results screen starts on a
 *                       character select view from before the match, which must not count as the results' end
 *   test_control=1      host: take this console's door, then do what DIR/control.txt says, one line at a time as the
 *                       test appends them (see control()), logging "group-test: control N done: LINE" for each:
 *                       door PORT KIND FIGHTER LEVEL CHOSEN HANDICAP, rules HANDICAP MODE TIME STOCKS, busy B (bit 0
 *                       busy, bits 4..7 the doors this console uses, as group_lobby_busy takes them),
 *                       stick X Y (this console's C-stick, -127..127, held until changed), start (Start on the CSS),
 *                       back (B on the stage select), sticks (log every port's C-stick as read here), dump, quit
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

static struct {
    int host, via_menu, players, games, frames, character, crash_frame, delay, late, full, linger_ms, control;
    int results_hold_ms, results_linger_ms, results_leave, stale_view;
    int after_lost, vanish_ms, expect_error, wait_each, zelda;
} T;

/* A clock every console on this machine shares (CLOCK_MONOTONIC), so a test can order events across their logs. */
static unsigned long long now_ms(void) { return (unsigned long long)(sp_time_us() / 1000); }

static const char *status_name(int s)
{
    static const char *names[] = {"idle", "searching", "connecting", "connected", "failed", "disconnected"};
    return s >= 0 && s <= 5 ? names[s] : "?";
}

/* Logs the lobby's busy bits when they change ("group-test: busy now B"), so a test can see another console's busy
 * state (its name tag list, name entry, rules menu or a held slider) arrive here. */
static void log_busy(void)
{
    static int last = 0;
    lobby_state v;
    if (group_lobby_view(&v) && v.busy != last) {
        last = v.busy;
        sp_log("group-test: busy now %x", last);
    }
}

/* Waits until cond() or timeout, servicing the network. 1 = condition met. */
static int wait_for(int (*cond)(void), int timeout_ms)
{
    static int last = -1;
    uint64_t end = sp_time_us() + (uint64_t)timeout_ms * 1000;
    while (sp_time_us() < end) {
        group_net_poll();
        group_system_frame();                  /* HOME and sleep, as the game's frame does */
        int st = group_net_status();
        if (st != last) { sp_log("group-test: status %s", status_name(st)); last = st; }
        if (st == 4 || st == 5) return 0;
        log_busy();
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
    if (!joined && group_scan(&g, 1) > 0 && (!T.late || g.beacon.phase == LOBBY_MATCH)) {
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
        if (v->error[0]) {
            sp_log("group-test: menu error: %s", v->error);
            if (!T.expect_error) { sp_log("group-test: FAIL group menu: %s", v->error); return -1; }
            if (strcmp(v->error, LW_TEXT_WIRELESS_OFF) || v->page != GROUP_PAGE_ASK || group_active()) {
                sp_log("group-test: FAIL an unexpected error or page (page %d, session %d)", v->page, group_active());
                return -1;
            }
            if (T.expect_error++ >= 2) return 1;   /* failed twice the same way: as expected */
            group_pick_group();                   /* Group again: a retry, as a player would after fixing it */
            group_poll(0);
            continue;
        }
        sp_sleep_us(5000);
    }
    sp_log("group-test: FAIL the group menu timed out");
    return -1;
}

/* A console that joined during a match: the host's status must arrive and move until the match is over. */
static int watch_match(void)
{
    int updates = 0, last = -1, max_seconds = 0, saw_results = 0;
    uint64_t end = sp_time_us() + 120000000;
    lobby_state v;
    if (!group_watching()) { sp_log("group-test: FAIL joined late but no match is running here"); return 0; }
    while (sp_time_us() < end && group_watching()) {
        group_net_poll();
        if (group_lobby_view(&v) && v.status.valid && (int)v.status.seconds != last) {
            last = v.status.seconds;
            ++updates;
            if (last > max_seconds) max_seconds = last;
        }
        if (group_lobby_view(&v) && v.phase == LOBBY_RESULTS) saw_results = 1;
        sp_sleep_us(10000);
    }
    sp_log("group-test: watched the match in progress: %d status updates, timer up to %d s", updates, max_seconds);
    if (updates < 3) { sp_log("group-test: FAIL too few status updates while watching"); return 0; }
    /* the results that follow are the players' own: this console watches until the lobby reopens */
    if (!group_lobby_view(&v) || v.phase != LOBBY_CSS) { sp_log("group-test: FAIL stopped watching in phase %u", v.phase); return 0; }
    sp_log("group-test: held through the results (%s) until the lobby reopened at %llu", saw_results ? "seen" : "not seen", now_ms());
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

/* The lobby as this console sees it, on one log line (the test reads ports, busy bits and doors from it). */
static void dump_lobby(void)
{
    lobby_state v;
    if (!group_lobby_view(&v)) { sp_log("group-test: lobby none"); return; }
    char doors[160] = "";
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        char one[40];
        snprintf(one, sizeof(one), " %d:%u/%u/%u/%u/%u", i, v.slot[i].kind, v.slot[i].fighter, v.slot[i].level,
                 v.slot[i].chosen, v.slot[i].handicap);
        strncat(doors, one, sizeof(doors) - strlen(doors) - 1);
    }
    sp_log("group-test: lobby present %x busy %x phase %u handicap %u in_use %x doors%s", group_present(), v.busy, v.phase,
           v.rules.handicap, v.in_use, doors);
}

/* A host driven by the test, a line at a time from DIR/control.txt (the test appends; each line is done once):
 *   door P KIND FIGHTER LEVEL CHOSEN HANDICAP   set door P (0..3; kind 0 Human, 1 CPU, 2 closed)
 *   rules HANDICAP MODE MINUTES STOCKS          the host's rules (handicap 0 off, 1 auto, 2 fixed; mode 0 time, 1 stock)
 *   busy B                                      this console's busy bit (as its name tag list or rules menu would)
 *   dump                                        log the lobby (dump_lobby)
 *   quit                                        close the session and exit 0
 * Gives up (exit 1) after 20 minutes without a quit. */
static int control(const char *dir, int me)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/control.txt", dir);
    int done = 0;
    MpGroupDoor d;
    memset(&d, 0, sizeof(d));
    d.kind = 0; d.fighter = (unsigned char)((T.character + me) % 26); d.chosen = 1; d.level = 3;
    group_lobby_set_door(me, &d);
    uint64_t end = sp_time_us() + 1200000000ull;
    int sx = 0, sy = 0;
    while (sp_time_us() < end) {
        signed char sticks[8];
        group_net_poll();
        group_menu_sticks(sx, sy, sticks);      /* as a menu does each frame */
        if (group_net_status() == 4 || group_net_status() == 5) { sp_log("group-test: FAIL the session ended"); return 1; }
        char *text = sp_read_file(path, NULL);
        int line = 0;
        for (char *s = text, *nl; s && *s; s = nl ? nl + 1 : s + strlen(s)) {
            nl = strchr(s, '\n');
            if (!nl) break;                     /* a line still being written */
            *nl = 0;
            if (line++ < done) continue;
            int a[6] = {0};
            if (sscanf(s, "door %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6) {
                MpGroupDoor x;
                memset(&x, 0, sizeof(x));
                x.kind = (unsigned char)a[1]; x.fighter = (unsigned char)a[2]; x.level = (unsigned char)a[3];
                x.chosen = (unsigned char)a[4]; x.handicap = (unsigned char)a[5];
                group_lobby_set_door(a[0], &x);
            } else if (sscanf(s, "rules %d %d %d %d", &a[0], &a[1], &a[2], &a[3]) == 4) {
                MpGroupRules r;
                memset(&r, 0, sizeof(r));
                r.handicap = (unsigned char)a[0]; r.mode = (unsigned char)a[1]; r.time_limit = (unsigned char)a[2];
                r.stock_count = (unsigned char)a[3]; r.time_mode = a[1] == 0; r.minutes = r.time_limit;
                r.stocks = r.stock_count; r.melee_valid = 1;
                group_lobby_rules(&r);
            } else if (sscanf(s, "busy %d", &a[0]) == 1) {
                group_lobby_busy(a[0]);
            } else if (sscanf(s, "stick %d %d", &a[0], &a[1]) == 2) {
                sx = a[0]; sy = a[1];
            } else if (!strcmp(s, "start")) {
                group_lobby_start();
            } else if (!strcmp(s, "back")) {
                group_lobby_back();
            } else if (!strcmp(s, "sticks")) {
                sp_log("group-test: sticks %d,%d %d,%d %d,%d %d,%d", sticks[0], sticks[1], sticks[2], sticks[3], sticks[4],
                       sticks[5], sticks[6], sticks[7]);
            } else if (!strcmp(s, "dump")) {
                dump_lobby();
            } else if (!strcmp(s, "quit")) {
                sp_log("group-test: control %d done: quit", done + 1);
                free(text);
                return 0;
            } else {
                sp_log("group-test: control: unknown line '%s'", s);
            }
            sp_log("group-test: control %d done: %s", ++done, s);
        }
        free(text);
        sp_sleep_us(20000);
    }
    sp_log("group-test: FAIL control timed out");
    return 1;
}

/* After a match: its results screen, as a player who presses Start at once. Every console waits until each console
 * that played is ready; then the host reopens the lobby (with the stars unchanged: this test has no match result) and
 * a joiner waits for that. While it is up this console is busy, as in the game, so the host cannot start the next
 * match before every console is back. 0 = back in the lobby. */
static int results_screen(int me, int game)
{
    uint64_t end = sp_time_us() + 60000000ull, hold = 0, release = 0;
    int pressed = 0;
    group_lobby_busy(1);                        /* as the game does: no Start until this console is back */
    if (game == 1 && T.stale_view && !T.host) {
        lobby_state v;
        int ok = group_lobby_view(&v);
        sp_log("group-test: stale view at the results: phase %u, after the match %d", ok ? v.phase : 255u,
               group_view_since_game_end());
        release = sp_time_us() + 1500000ull;
    }
    while (sp_time_us() < end) {
        lobby_state v;
        group_net_poll();
        if (release && sp_time_us() >= release) {
            release = 0;
            group_test_hold_view(0);
            sp_log("group-test: the lobby view is let through again at %llu", now_ms());
        }
        if (group_net_status() == 4 || group_net_status() == 5) { sp_log("group-test: FAIL the session ended in the results"); return 1; }
        if (group_lobby_view(&v)) {
            /* back in the character select: over (in a view from after this console's match: an older one, or a
             * stage select or match phase, is from before the host reached its results) */
            if (v.phase == LOBBY_CSS && group_view_since_game_end()) {
                sp_log("group-test: the results are over at %llu (phase %u, stars %u/%u/%u/%u)", now_ms(), v.phase, v.stars[0],
                       v.stars[1], v.stars[2], v.stars[3]);
                for (uint64_t t = sp_time_us() + (uint64_t)T.results_linger_ms * 1000; sp_time_us() < t;) {
                    group_net_poll();
                    sp_sleep_us(5000);
                }
                group_lobby_busy(0);
                return 0;
            }
            if (v.phase == LOBBY_RESULTS && group_view_since_game_end()) {
                if (game == 1 && T.results_leave) {
                    sp_log("group-test: leaving on the results screen at %llu (%s)", now_ms(), T.results_leave == 2 ? "vanish" : "goodbye");
#ifndef _WIN32
                    if (T.results_leave == 2) _exit(0);
#endif
                    group_session_stop();
                    return 2;
                }
                if (!hold) hold = sp_time_us() + (uint64_t)T.results_hold_ms * 1000;
                if (!pressed && (v.played >> me & 1) && sp_time_us() >= hold) {
                    group_results_press(1, 2);
                    pressed = 1;
                    sp_log("group-test: ready on the results screen at %llu", now_ms());
                }
                if (group_is_host() && (v.ready & v.played) == v.played) group_results_end(v.stars, NULL);
            }
        }
        sp_sleep_us(10000);
    }
    sp_log("group-test: FAIL the results screen did not end");
    return 1;
}

static int nobody_busy(void) { lobby_state v; return group_lobby_view(&v) && v.busy == 0; }
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
        if (T.stale_view && !T.host) {
            group_test_hold_view(1);           /* the view as it is now: the character select, before Start */
            sp_log("group-test: holding the lobby view (phase CSS) until after the match");
        }
        if (T.host && T.players < 4) {            /* a CPU on the first free port: no console there */
            MpGroupDoor c = d;
            c.kind = 1; c.fighter = 0x0A; c.color = 0; c.level = 7;
            group_lobby_set_door(T.players, &c);
        }
    }
    if (T.host && game > 1 && T.wait_each && !wait_for(all_here, 60000)) { sp_log("group-test: FAIL the consoles for game %d never arrived", game); return 0; }
    if (T.host) {
        lobby_state v;
        if (game > 1 && group_lobby_view(&v) && v.busy) {
            /* a console is still on its results screen: Start must not take the lobby to the stage select */
            group_lobby_start();
            for (uint64_t t = sp_time_us() + 200000; sp_time_us() < t;) { group_net_poll(); sp_sleep_us(5000); }
            if (group_lobby_view(&v) && v.phase != LOBBY_CSS) { sp_log("group-test: FAIL Start taken while busy"); return 0; }
            sp_log("group-test: Start refused while consoles %x are busy", v.busy);
        }
        if (!wait_for(doors_ready, 30000)) { sp_log("group-test: FAIL the doors never filled"); return 0; }
        group_lobby_start();
        if (!wait_for(in_stage, 10000)) { sp_log("group-test: FAIL no stage select"); return 0; }
        group_lobby_stage(0x1F, 0);
    }
    if (!wait_for(block_ready, T.host ? 30000 : 600000)) { sp_log("group-test: FAIL no match block (game %d): %s", game, group_net_error()); return 0; }
    sp_log("group-test: match block %d at %llu", game, now_ms());
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
                st.port[i].fighter = (uint8_t)((T.character + i) % 26);
            }
            if (T.zelda) {
                st.port[me].fighter = (uint8_t)(frame < frames / 2 ? 18 : 19);
                static int said;
                if (frame >= frames / 2 && said != game) { said = game; sp_log("group-test: Zelda becomes Sheik at %llu", now_ms()); }
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
    T.control = group_ini_int(s, "test_control", 0);
    T.results_hold_ms = group_ini_int(s, "test_results_hold_ms", 0);
    T.results_linger_ms = group_ini_int(s, "test_results_linger_ms", 0);
    T.results_leave = group_ini_int(s, "test_results_leave", 0);
    T.stale_view = group_ini_int(s, "test_stale_view", 0);
    T.after_lost = group_ini_int(s, "test_after_lost", 0);
    T.vanish_ms = group_ini_int(s, "test_vanish_ms", 0);
    T.expect_error = group_ini_int(s, "test_expect_error", 0);
    T.wait_each = group_ini_int(s, "test_wait_each", 0);
    T.zelda = group_ini_int(s, "test_zelda", 0);
    free(ini);

    sp_log("group-test: starting as %s (data dir %s)", T.host ? "host" : "joiner", dir);
    if (T.via_menu && T.expect_error) {
        int r = via_menu();
        sp_log("group-test: %s", r == 1 ? "PASS (wireless off, refused twice)" : "FAIL");
        group_session_stop();
        return r == 1 ? 0 : 1;
    }
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
    if (!T.host && !T.late) {
        /* the doors as the host has them when this console arrives (a console coming back must see the host's door
         * for its port, not one left over from an earlier visit) */
        /* (one that arrives on another's results screen takes its door once the character select is back: the host
         * takes no door edits before then, and a person would pick again there) */
        static lobby_state first;
        uint64_t end = sp_time_us() + 30000000ull;
        while (sp_time_us() < end && !(group_lobby_view(&first) && first.phase == LOBBY_CSS)) { group_net_poll(); sp_sleep_us(5000); }
        dump_lobby();
    }
    int failed = 0;
    if (T.control) {
        failed = control(dir, me);
        sp_log("group-test: %s", failed ? "FAIL" : "PASS");
        group_session_stop();
        return failed;
    }
    if (T.late && !watch_match()) failed = 1;
    for (int game = 1; game <= T.games && !failed; ++game) {
        if (!lobby_dance(me, game)) { failed = 1; break; }
        if (play_game(me, game) != 0) { failed = 1; break; }
        group_game_end();
        int r = results_screen(me, game);
        if (r == 2) { sp_log("group-test: PASS (left on the results screen)"); return 0; }
        if (r != 0) { failed = 1; break; }
        sp_log("group-test: back in the lobby at %llu", now_ms());
    }
    /* the others may still be on their results screens: closing the lobby now would end their session there */
    if (T.host && !failed && !wait_for(nobody_busy, 20000)) sp_log("group-test: consoles still busy at the end");
    if (T.host && T.vanish_ms) {
        for (uint64_t end = sp_time_us() + (uint64_t)T.vanish_ms * 1000; sp_time_us() < end;) { group_net_poll(); sp_sleep_us(5000); }
        sp_log("group-test: vanishing after the last game at %llu", now_ms());
#ifndef _WIN32
        _exit(0);
#endif
    }
    for (uint64_t end = sp_time_us() + (uint64_t)T.linger_ms * 1000; sp_time_us() < end;) {
        group_net_poll();
        if (group_lost()) {   /* only a session opened from the menu reports this */
            sp_log("group-test: FAIL the session was lost while lingering: %s", group_net_error());
            failed = 1;
            break;
        }
        sp_sleep_us(5000);
    }
    if (T.after_lost && !failed) {
        uint64_t end = sp_time_us() + 30000000ull;
        while (sp_time_us() < end && !group_lost()) { group_net_poll(); group_system_frame(); sp_sleep_us(5000); }
        if (!group_lost()) { sp_log("group-test: FAIL the session was never lost"); failed = 1; }
        else {
            group_leave();          /* as the engine does when the lost session takes it back to the VS menu */
            group_system_frame();   /* a frame on the VS menu before the page reopens */
            group_reopen();
            group_poll(0);
            sp_log("group-test: lost note: %s", group_view()->error);
            sp_log("group-test: page after the loss: %d", group_view()->page);
        }
    }
    sp_log("group-test: %s", failed ? "FAIL" : "PASS");
    group_session_stop();
    return failed ? 1 : 0;
}
