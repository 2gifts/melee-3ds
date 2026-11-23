/* Network self-test (config.ini selftest=1). Runs the engine bridge API the
 * way the lockstep frame driver will: matchmake (Direct), exchange selections,
 * build the match block, then N frames of deterministic pads at 59.94 Hz with
 * SKIP retries, verifying every remote pad and checksum. Logs ping, time
 * offset, stalls and loss. Platform-neutral: the 3DS boot path and the PC
 * tool slippi_client.exe run the same code.
 *
 * Safety: refuses to queue on the production matchmaking server unless
 * selftest_allow_real_mm=1, so a test config can never reach mm.slippi.gg. */
#include "slippi_internal.h"
#include "slippi_testpattern.h"
#include "slippi_group.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int slippi_selftest_requested(void)
{
    slippi_config cfg;
    char path[256];
    slippi_config_defaults(&cfg);
    snprintf(path, sizeof(path), "%s/config.ini", sp_data_dir());
    if (slippi_config_load(&cfg, path) != 0) return 0;
    return cfg.selftest ? (cfg.selftest_exit ? 2 : 1) : 0;
}

static const char *status_name(int s)
{
    static const char *names[] = {"idle", "matchmaking", "connecting", "connected", "failed", "disconnected"};
    return s >= 0 && s <= 5 ? names[s] : "?";
}

/* Waits until cond() or timeout, servicing the network. 1 = condition met. */
static int wait_for(int (*cond)(void), int timeout_ms, int *last_status)
{
    uint64_t end = sp_time_us() + (uint64_t)timeout_ms * 1000;
    while (sp_time_us() < end) {
        if (!sp_app_running()) return 0;
        slippi_net_poll();
        int st = slippi_net_status();
        if (st != *last_status) { sp_log("selftest: status %s", status_name(st)); *last_status = st; }
        if (st == 4 || st == 5) return 0;
        if (cond()) return 1;
        sp_sleep_us(5000);
    }
    return 0;
}

static int is_connected(void) { return slippi_net_status() == 3; }
static unsigned char block[0x140];
static int block_ready(void) { return slippi_net_match_block(block); }

/* ---- a group lobby (local_lobby=1): the consoles meet, fill their doors, start and pick a stage ---- */

static int joined;
static int lobby_connected(void)
{
    if (slippi_net_status() == 3) return 1;
    sp_lock();
    if (!g_slippi.match.is_host && !joined && slippi_local_scan_count() > 0) {
        char host[16];
        int players = 0;
        slippi_local_scan_get(0, host, sizeof(host), &players, NULL);
        sp_log("selftest: found a game by '%s' with %d consoles; joining", host, players);
        if (slippi_local_join(0) == 0) joined = 1;
    }
    sp_unlock();
    return 0;
}

/* The Melee -> Group menu (slippi_group.c) instead of opponent=@host/@join: Group, then Host or the first game found. */
static int group_menu(int host)
{
    uint64_t end = sp_time_us() + 60000000;
    int asked = 0, requested = 0;
    if (!slippi_group_open()) { sp_log("selftest: FAIL the group menu is off (local_group=1)"); return -1; }
    while (sp_time_us() < end && sp_app_running()) {
        const SlippiGroupView *v = slippi_group_view();
        int r;
        if (v->page == GROUP_PAGE_ASK && !asked) { slippi_group_pick_group(); asked = 1; }
        else if (v->page == GROUP_PAGE_LIST && !requested) {
            if (host) { slippi_group_host(); requested = 1; }
            else if (v->count > 0) {
                sp_log("selftest: the list shows '%s', %d consoles", v->row[0].host, v->row[0].players);
                slippi_group_join(0);
                requested = 1;
            }
        }
        r = slippi_group_poll(0);
        if (r == 2) return 0;
        if (r != 0) { sp_log("selftest: FAIL the group menu returned %d", r); return -1; }
        if (v->error[0]) { sp_log("selftest: FAIL group menu: %s", v->error); return -1; }
        sp_sleep_us(5000);
    }
    sp_log("selftest: FAIL the group menu timed out");
    return -1;
}

static int lobby_all_here(void)
{
    sp_lock();
    int n = __builtin_popcount(slippi_local_present());
    sp_unlock();
    return n >= g_slippi.cfg.local_players;
}
static int lobby_doors_ready(void)
{
    lobby_state v;
    sp_lock();
    int ok = slippi_local_lobby_view(&v) && lobby_can_start(&v);
    sp_unlock();
    return ok;
}
static int lobby_in_stage(void)
{
    lobby_state v;
    sp_lock();
    int ok = slippi_local_lobby_view(&v) && v.phase == LOBBY_STAGE;
    sp_unlock();
    return ok;
}

/* Every console takes its own door, the host adds a CPU on a free port, someone presses Start and the host picks. */
static int lobby_dance(const slippi_config *cfg, int local_index, int game, int *last)
{
    sp_lock();
    int host = g_slippi.match.is_host;
    sp_unlock();
    if (game == 1) {
        if (host && !wait_for(lobby_all_here, 30000, last)) { sp_log("selftest: FAIL the other consoles never arrived"); return 0; }
        lobby_slot s;
        memset(&s, 0, sizeof(s));
        s.kind = LOBBY_HUMAN; s.fighter = (uint8_t)((cfg->selftest_character + local_index) % 26); s.color = (uint8_t)local_index; s.chosen = 1; s.level = 3;
        sp_lock();
        slippi_local_set_door(local_index, &s);
        sp_unlock();
        if (host && cfg->local_players < 4) {            /* a CPU on the first free port: no console there */
            lobby_slot c = s;
            c.kind = LOBBY_CPU; c.fighter = 0x0A; c.color = 0; c.level = 7;
            sp_lock();
            slippi_local_set_door(cfg->local_players, &c);
            sp_unlock();
        }
    }
    if (host) {
        if (!wait_for(lobby_doors_ready, 30000, last)) { sp_log("selftest: FAIL the doors never filled"); return 0; }
        sp_lock();
        slippi_local_press_start();
        sp_unlock();
        if (!wait_for(lobby_in_stage, 10000, last)) { sp_log("selftest: FAIL no stage select"); return 0; }
        sp_lock();
        slippi_local_pick_stage(0x1F, 0);
        sp_unlock();
    }
    if (!wait_for(block_ready, host ? 30000 : 600000, last)) { sp_log("selftest: FAIL no match block (game %d): %s", game, slippi_net_error()); return 0; }
    sp_log("selftest: match block ready: stage 0x%02x%02x slots %u/%u/%u/%u types %u/%u/%u/%u local %u delay %u", block[0xE], block[0xF],
           block[0x60], block[0x84], block[0xA8], block[0xCC], block[0x61], block[0x85], block[0xA9], block[0xCD], block[0x13C], block[0x13D]);
    return 1;
}

/* One game of frames from slippi_net_new_game() to the last checked bundle. 0 = every check passed. */
static int play_game(const slippi_config cfg, int mm_index, int game)
{
    slippi_net_new_game();
    const int delay = cfg.delay, frames = cfg.selftest_frames > 0 ? cfg.selftest_frames : 600;
    const int remote_port = mm_index == 0 ? 1 : 0;
    int last = slippi_net_status();
    const int nplayers = slippi_net_local_players();
    (void)last;
    int frame = 1, ticks = 0, skips = 0, bad_pads = 0, bad_checksums = 0, checked_checksums = 0, worst_skip_run = 0, skip_run = 0;
    int last_checked_cf = 0;
    uint32_t bundle_hash = 2166136261u;
    unsigned seen_cpu = 0;
    long long ping_sum = 0, ping_n = 0, ping_max = 0;
    int result = 0;
    uint64_t start = sp_time_us(), next = start;
    while (frame <= frames) {
        if (!sp_app_running()) { result = -1; break; }
        uint64_t now = sp_time_us();
        if (now < next) { slippi_net_poll(); sp_sleep_us((uint32_t)(next - now > 2000 ? 2000 : next - now)); continue; }
        next += SLIPPI_FRAME_US;
        ++ticks;
        uint8_t local[12] = {0}, remote[64];
        slippi_test_pad(mm_index, frame + delay, local);
        int finalized = frame - 1;
        int rc = slippi_net_send_inputs(frame, delay, finalized, slippi_test_checksum(finalized), local, remote);
        if (rc == 3) { sp_log("selftest: disconnected at frame %d: %s", frame, slippi_net_error()); result = -1; break; }
        if (rc == 2) {
            ++skips;
            if (++skip_run > worst_skip_run) worst_skip_run = skip_run;
            continue;
        }
        skip_run = 0;
        if (g_slippi.local_mode) {
            unsigned mask = remote[48];
            for (int i = 0; i < nplayers; ++i) {
                if (mask & (1u << i)) {
                    if (!(seen_cpu & (1u << i))) { seen_cpu |= 1u << i; sp_log("selftest: player %d is a CPU from frame %d", i + 1, frame); }
                    static const uint8_t zero[8];
                    if (memcmp(remote + 12 * i, zero, 8)) ++bad_pads;
                } else if (!slippi_test_pad_ok(i, frame, remote + 12 * i)) {
                    if (bad_pads++ < 5) sp_log("selftest: bundle pad mismatch for player %d at frame %d", i + 1, frame);
                }
            }
            if ((seen_cpu & ~mask) != 0) { ++bad_pads; sp_log("selftest: CPU bit cleared at frame %d", frame); }
            uint8_t tmp[56];
            memcpy(tmp, &frame, 4);
            tmp[4] = (uint8_t)mask;
            for (int i = 0; i < 4; ++i) memcpy(tmp + 5 + 8 * i, remote + 12 * i, 8);
            for (int i = 0; i < 37; ++i) bundle_hash = (bundle_hash ^ tmp[i]) * 16777619u;
        } else if (!slippi_test_pad_ok(remote_port, frame, remote)) {
            if (bad_pads++ < 5)
                sp_log("selftest: remote pad mismatch at frame %d: %02x %02x %02x %02x %02x %02x %02x %02x", frame, remote[0], remote[1],
                       remote[2], remote[3], remote[4], remote[5], remote[6], remote[7]);
        }
        int cf = slippi_net_remote_checksum_frame();
        if (cf > 0 && cf != last_checked_cf) {
            last_checked_cf = cf;
            ++checked_checksums;
            if (slippi_net_remote_checksum_value() != slippi_test_checksum(cf)) ++bad_checksums;
        }
        int ping = slippi_ping_us();
        if (ping > 0) { ping_sum += ping; ++ping_n; if (ping > ping_max) ping_max = ping; }
        if (frame % 60 == 0)
            sp_log("selftest: frame %d tick %d: ping %.1f ms, offset %d us, skips %d, latest remote %d, rx %d stale %d gaps %d, tx %d resend %d, "
                   "ENet rtt %d ms loss %d/65536",
                   frame, ticks, ping / 1000.0, slippi_time_offset_us(), skips, slippi_latest_remote_frame(),
                   slippi_stat(SLIPPI_STAT_PAD_PACKETS_RECEIVED), slippi_stat(SLIPPI_STAT_PAD_PACKETS_STALE), slippi_stat(SLIPPI_STAT_PAD_GAPS),
                   slippi_stat(SLIPPI_STAT_PAD_PACKETS_SENT), slippi_stat(SLIPPI_STAT_RESENDS), slippi_stat(SLIPPI_STAT_ENET_RTT_MS),
                   slippi_stat(SLIPPI_STAT_ENET_LOSS));
        if (cfg.selftest_crash_frame && frame == cfg.selftest_crash_frame) {
            sp_log("selftest: vanishing at frame %d", frame);
#ifndef _WIN32
            _exit(0);
#endif
        }
        ++frame;
    }
    double seconds = (double)(sp_time_us() - start) / 1e6;
    int done = frame - 1;
    /* Keep servicing briefly so the last acks/pads reach the peer. */
    uint64_t linger = sp_time_us() + 1500000;
    while (sp_time_us() < linger) { slippi_net_poll(); sp_sleep_us(5000); }
    sp_log("selftest: %d/%d frames in %.2f s (%.2f fps), %d ticks, %d skips (longest run %d), remote pad mismatches %d, checksums %d checked %d bad",
           done, frames, seconds, done / (seconds > 0 ? seconds : 1), ticks, skips, worst_skip_run, bad_pads, checked_checksums, bad_checksums);
    sp_log("selftest: ping avg %.1f ms max %.1f ms, time offset %d us, pad packets tx %d rx %d stale %d gaps %d resends %d test-dropped %d, "
           "acks tx %d rx %d",
           ping_n ? ping_sum / 1000.0 / ping_n : 0.0, ping_max / 1000.0, slippi_time_offset_us(), slippi_stat(SLIPPI_STAT_PAD_PACKETS_SENT),
           slippi_stat(SLIPPI_STAT_PAD_PACKETS_RECEIVED), slippi_stat(SLIPPI_STAT_PAD_PACKETS_STALE), slippi_stat(SLIPPI_STAT_PAD_GAPS),
           slippi_stat(SLIPPI_STAT_RESENDS), slippi_stat(SLIPPI_STAT_DROPPED_TEST), slippi_stat(SLIPPI_STAT_ACKS_SENT),
           slippi_stat(SLIPPI_STAT_ACKS_RECEIVED));
    if (g_slippi.local_mode) sp_log("selftest: bundle hash %08x, %d consoles, cpu mask %x, block crc local", (unsigned)bundle_hash, nplayers, seen_cpu);
    int pass = result == 0 && done == frames && bad_pads == 0 && bad_checksums == 0 && checked_checksums > 0;
    sp_log("selftest: game %d %s", game, pass ? "PASS" : "FAIL");
    return pass ? 0 : -1;
}

int slippi_selftest_run(void)
{
    sp_log("selftest: starting (data dir %s)", sp_data_dir());
    if (slippi_init() != 0) { sp_log("selftest: FAIL init: %s", slippi_net_error()); return -1; }
    slippi_config cfg = g_slippi.cfg;
    if (!g_slippi.local_mode && !strcmp(cfg.mm_host, SLIPPI_MM_HOST_PROD) && !cfg.selftest_allow_real_mm) {
        sp_log("selftest: refusing to use the real matchmaking server %s; set mm_host to the fake server", cfg.mm_host);
        slippi_net_stop();
        return -1;
    }
    sp_log("selftest: Wi-Fi status %d, LAN fallback address 0x%08x", sp_wifi_status(), (unsigned)sp_lan_ip_fallback());
    uint64_t t0 = sp_time_us();
    if (cfg.local_group ? group_menu(g_slippi.match.is_host || !strcmp(cfg.opponent, "@host")) != 0 : slippi_net_start(NULL) != 0) { sp_log("selftest: FAIL start: %s", slippi_net_error()); slippi_net_stop(); return -1; }
    int last = -1;
    if (!wait_for(g_slippi.cfg.local_lobby ? lobby_connected : is_connected, 120000, &last)) {
        sp_log("selftest: FAIL no connection: %s", slippi_net_error());
        slippi_net_stop();
        return -1;
    }
    char name[64], code[32];
    slippi_info(SLIPPI_INFO_NAME, name, sizeof(name));
    slippi_info(SLIPPI_INFO_CODE, code, sizeof(code));
    int local_index = slippi_net_local_index();
    int mm_index = g_slippi.local_mode ? local_index : slippi_local_index();
    sp_log("selftest: connected in %.1f s to '%s' (%s); game index %d, MM index %d", (double)(sp_time_us() - t0) / 1e6, name, code,
           local_index, mm_index);

    if (!g_slippi.cfg.local_lobby) {
        /* Decider picks Battlefield; the other side leaves the stage open. */
        slippi_net_set_selections((cfg.selftest_character + local_index) % 26, local_index, 0x1F, local_index == 0);
        if (!wait_for(block_ready, 20000, &last)) {
            sp_log("selftest: FAIL no remote selections: %s", slippi_net_error());
            slippi_net_stop();
            return -1;
        }
        unsigned rng = ((unsigned)block[0x138] << 24) | ((unsigned)block[0x139] << 16) | ((unsigned)block[0x13A] << 8) | block[0x13B];
        sp_log("selftest: match block ready: stage 0x%02x%02x p1 %u/%u p2 %u/%u rng 0x%08x local %u delay %u", block[0xE], block[0xF],
               block[0x60], block[0x63], block[0x84], block[0x87], rng, block[0x13C], block[0x13D]);
    }

    int games = cfg.selftest_games > 0 ? cfg.selftest_games : 1, failed = 0;
    for (int game = 1; game <= games && !failed; ++game) {
        if (g_slippi.cfg.local_lobby && !lobby_dance(&cfg, local_index, game, &last)) { failed = 1; break; }
        if (play_game(cfg, mm_index, game) != 0) { failed = 1; break; }
        if (g_slippi.cfg.local_lobby) {
            slippi_net_game_end();
            sp_log("selftest: back in the lobby");
        }
    }
    sp_log("selftest: %s", failed ? "FAIL" : "PASS");
    slippi_net_stop();
    return failed ? -1 : 0;
}
