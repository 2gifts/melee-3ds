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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int slippi_selftest_run(void)
{
    sp_log("selftest: starting (data dir %s)", sp_data_dir());
    if (slippi_init() != 0) { sp_log("selftest: FAIL init: %s", slippi_net_error()); return -1; }
    slippi_config cfg = g_slippi.cfg;
    if (!strcmp(cfg.mm_host, SLIPPI_MM_HOST_PROD) && !cfg.selftest_allow_real_mm) {
        sp_log("selftest: refusing to use the real matchmaking server %s; set mm_host to the fake server", cfg.mm_host);
        slippi_net_stop();
        return -1;
    }
    sp_log("selftest: Wi-Fi status %d, LAN fallback address 0x%08x", sp_wifi_status(), (unsigned)sp_lan_ip_fallback());
    uint64_t t0 = sp_time_us();
    if (slippi_net_start(NULL) != 0) { sp_log("selftest: FAIL start: %s", slippi_net_error()); slippi_net_stop(); return -1; }
    int last = -1;
    if (!wait_for(is_connected, 120000, &last)) {
        sp_log("selftest: FAIL no connection: %s", slippi_net_error());
        slippi_net_stop();
        return -1;
    }
    char name[64], code[32];
    slippi_info(SLIPPI_INFO_NAME, name, sizeof(name));
    slippi_info(SLIPPI_INFO_CODE, code, sizeof(code));
    int local_index = slippi_net_local_index();
    int mm_index = slippi_local_index();
    sp_log("selftest: connected in %.1f s to '%s' (%s); game index %d, MM index %d", (double)(sp_time_us() - t0) / 1e6, name, code,
           local_index, mm_index);

    /* Decider picks Battlefield; the other side leaves the stage open. */
    slippi_net_set_selections(cfg.selftest_character, local_index, 0x1F, local_index == 0);
    if (!wait_for(block_ready, 20000, &last)) {
        sp_log("selftest: FAIL no remote selections: %s", slippi_net_error());
        slippi_net_stop();
        return -1;
    }
    unsigned rng = ((unsigned)block[0x138] << 24) | ((unsigned)block[0x139] << 16) | ((unsigned)block[0x13A] << 8) | block[0x13B];
    sp_log("selftest: match block ready: stage 0x%02x%02x p1 %u/%u p2 %u/%u rng 0x%08x local %u delay %u", block[0xE], block[0xF],
           block[0x60], block[0x63], block[0x84], block[0x87], rng, block[0x13C], block[0x13D]);

    slippi_net_new_game();
    const int delay = cfg.delay, frames = cfg.selftest_frames > 0 ? cfg.selftest_frames : 600;
    const int remote_port = mm_index == 0 ? 1 : 0;
    int frame = 1, ticks = 0, skips = 0, bad_pads = 0, bad_checksums = 0, checked_checksums = 0, worst_skip_run = 0, skip_run = 0;
    int last_checked_cf = 0;
    long long ping_sum = 0, ping_n = 0, ping_max = 0;
    int result = 0;
    uint64_t start = sp_time_us(), next = start;
    while (frame <= frames) {
        if (!sp_app_running()) { result = -1; break; }
        uint64_t now = sp_time_us();
        if (now < next) { slippi_net_poll(); sp_sleep_us((uint32_t)(next - now > 2000 ? 2000 : next - now)); continue; }
        next += SLIPPI_FRAME_US;
        ++ticks;
        uint8_t local[12] = {0}, remote[12];
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
        if (!slippi_test_pad_ok(remote_port, frame, remote)) {
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
    int pass = result == 0 && done == frames && bad_pads == 0 && bad_checksums == 0 && checked_checksums > 0;
    sp_log("selftest: %s", pass ? "PASS" : "FAIL");
    slippi_net_stop();
    return pass ? 0 : -1;
}
