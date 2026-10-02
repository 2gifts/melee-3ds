/* Fake Slippi peer for local tests: behaves like Slippi Dolphin on the wire.
 *
 * Uses the same client library as the 3DS (port/3ds/slippi) for matchmaking
 * and the P2P packets, and drives it like CEXISlippi does for a rollback
 * client: one frame per 16683 us, inputs sent tagged frame+delay (zero pads
 * for 1..delay), predicted play up to ROLLBACK_MAX_FRAMES (7) ahead of the
 * newest remote input, the 7 s stall disconnect, Dolphin's early time-sync
 * halts (frames <= 120, ahead > 10 ms) and its finalized-frame checksum in
 * every pad packet. Every received remote pad is checked against the shared
 * test pattern (slippi_testpattern.h).
 *
 * Usage: slippi_fake_peer --dir DIR [--frames N] [--character C] [--linger S]
 *   DIR holds user.json (dummy credentials) and config.ini (opponent=, delay=,
 *   mm_host=127.0.0.1, test_drop_pct=, ...).
 */
#include "slippi_internal.h"
#include "slippi_testpattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROLLBACK_MAX_FRAMES 7

static uint32_t crc32_bytes(const unsigned char *p, int n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

int main(int argc, char **argv)
{
    const char *dir = ".";
    int frames = 600, character = 0x14, linger_s = 3;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--character") && i + 1 < argc) character = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--linger") && i + 1 < argc) linger_s = atoi(argv[++i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    sp_set_data_dir(dir);
    if (slippi_init() != 0) { char e[160]; slippi_error(e, sizeof(e)); sp_log("peer: init failed: %s", e); return 1; }
    if (!strcmp(g_slippi.cfg.mm_host, SLIPPI_MM_HOST_PROD)) { sp_log("peer: refusing the real matchmaking server; set mm_host"); return 1; }
    const int delay = g_slippi.cfg.delay;
    if (slippi_find_match(NULL) != 0) return 1;

    uint64_t t0 = sp_time_us();
    while (slippi_status() != SLIPPI_STATUS_CONNECTED) {
        slippi_poll();
        int st = slippi_status();
        if (st == SLIPPI_STATUS_FAILED) { char e[160]; slippi_error(e, sizeof(e)); sp_log("peer: FAIL %s", e); return 1; }
        if (sp_time_us() - t0 > 180000000ull) { sp_log("peer: FAIL timeout waiting for a match"); return 1; }
        sp_sleep_us(5000);
    }
    int mm_index = slippi_local_index(), remote_port = mm_index == 0 ? 1 : 0;
    char name[64], code[32];
    slippi_info(SLIPPI_INFO_NAME, name, sizeof(name));
    slippi_info(SLIPPI_INFO_CODE, code, sizeof(code));
    sp_log("peer: connected to '%s' (%s); MM index %d, decider %d, delay %d", name, code, mm_index, slippi_is_decider(), delay);

    /* Lock in (Dolphin setMatchSelections): decider picks Final Destination. */
    slippi_net_set_selections(character, 0, 0x20, slippi_is_decider());
    unsigned char block[0x140];
    t0 = sp_time_us();
    while (!slippi_net_match_block(block)) {
        slippi_poll();
        if (slippi_status() != SLIPPI_STATUS_CONNECTED || sp_time_us() - t0 > 30000000ull) { sp_log("peer: FAIL no remote selections"); return 1; }
        sp_sleep_us(5000);
    }
    sp_log("peer: match block crc32 0x%08x (rng 0x%02x%02x%02x%02x, stage 0x%02x%02x)", crc32_bytes(block, 0x138), block[0x138],
           block[0x139], block[0x13A], block[0x13B], block[0xE], block[0xF]);

    slippi_start_game();
    int frame = 1, stall = 0, halts = 0, rollback_halts = 0, time_sync_halts = 0, frames_to_skip = 0, skipping = 0;
    int verified = 0, bad = 0, max_ahead = 0;
    long long ping_sum = 0, ping_n = 0;
    uint64_t start = sp_time_us(), next = start;
    int result = 0;
    while (frame <= frames) {
        uint64_t now = sp_time_us();
        if (now < next) { slippi_poll(); sp_sleep_us((uint32_t)(next - now > 1000 ? 1000 : next - now)); continue; }
        next += SLIPPI_FRAME_US;
        slippi_poll();
        if (slippi_status() != SLIPPI_STATUS_CONNECTED) { sp_log("peer: disconnected at frame %d", frame); result = 1; break; }
        /* Verify every remote pad that has arrived. */
        int latest = slippi_latest_remote_frame();
        while (verified < latest) {
            unsigned char pad[8];
            int f = verified + 1;
            if (slippi_remote_pad(f, pad) != 1 || !slippi_test_pad_ok(remote_port, f, pad)) {
                if (bad++ < 5) sp_log("peer: bad remote pad for frame %d", f);
            }
            verified = f;
        }
        int finalized = latest < frame - 1 ? latest : frame - 1;
        /* shouldSkipOnlineFrame */
        int skip = 0;
        if (latest - finalized < frame - finalized - ROLLBACK_MAX_FRAMES) {
            skip = 1;
            ++rollback_halts;
            if (++stall > 60 * 7) { sp_log("peer: 7 s stall, disconnecting"); slippi_disconnect(); result = 1; break; }
        } else {
            stall = 0;
            if (frame % 30 == 0 && !skipping && frame <= 120) {
                int offset = slippi_time_offset_us();
                if (offset > 10000) {
                    skipping = 1;
                    frames_to_skip = (offset - 10000) / SLIPPI_FRAME_US + 1;
                    if (frames_to_skip > 5) frames_to_skip = 5;
                    sp_log("peer: halting on frame %d due to time sync, offset %d us, %d frames", frame, offset, frames_to_skip);
                }
            }
            if (frames_to_skip > 0) { --frames_to_skip; skip = 1; ++time_sync_halts; }
            else skipping = 0;
        }
        if (skip) {
            ++halts;
            slippi_send_pad(0, NULL, 0, 0);   /* SendSlippiPad(nullptr) */
            continue;
        }
        unsigned char pad[8];
        slippi_test_pad(mm_index, frame + delay, pad);
        slippi_send_pad(frame + delay, pad, finalized, slippi_test_checksum(finalized));
        if (frame - latest > max_ahead) max_ahead = frame - latest;
        int ping = slippi_ping_us();
        if (ping > 0) { ping_sum += ping; ++ping_n; }
        if (frame % 60 == 0)
            sp_log("peer: frame %d: latest remote %d (ahead %d), ping %.1f ms, offset %d us, halts %d, rx %d gaps %d stale %d, remote checksum %d/0x%08x %s",
                   frame, latest, frame - latest, ping / 1000.0, slippi_time_offset_us(), halts, slippi_stat(SLIPPI_STAT_PAD_PACKETS_RECEIVED),
                   slippi_stat(SLIPPI_STAT_PAD_GAPS), slippi_stat(SLIPPI_STAT_PAD_PACKETS_STALE), slippi_remote_checksum_frame(),
                   slippi_remote_checksum(),
                   slippi_remote_checksum_frame() <= 0 || slippi_remote_checksum() == slippi_test_checksum(slippi_remote_checksum_frame()) ? "ok" : "BAD");
        ++frame;
    }
    double secs = (double)(sp_time_us() - start) / 1e6;
    /* Keep acking/re-sending until the other side has everything. */
    uint64_t end = sp_time_us() + (uint64_t)linger_s * 1000000;
    while (sp_time_us() < end) {
        slippi_poll();
        int latest = slippi_latest_remote_frame();
        while (verified < latest) {
            unsigned char pad[8];
            int f = verified + 1;
            if (slippi_remote_pad(f, pad) != 1 || !slippi_test_pad_ok(remote_port, f, pad)) ++bad;
            verified = f;
        }
        sp_sleep_us(2000);
    }
    sp_log("peer: %d frames in %.2f s; verified %d remote pads, %d bad; halts %d (rollback limit %d, time sync %d); max frames ahead %d",
           frame - 1, secs, verified, bad, halts, rollback_halts, time_sync_halts, max_ahead);
    sp_log("peer: ping avg %.1f ms; pad packets tx %d rx %d stale %d gaps %d; acks tx %d rx %d; resends %d; test-dropped %d",
           ping_n ? ping_sum / 1000.0 / ping_n : 0.0, slippi_stat(SLIPPI_STAT_PAD_PACKETS_SENT), slippi_stat(SLIPPI_STAT_PAD_PACKETS_RECEIVED),
           slippi_stat(SLIPPI_STAT_PAD_PACKETS_STALE), slippi_stat(SLIPPI_STAT_PAD_GAPS), slippi_stat(SLIPPI_STAT_ACKS_SENT),
           slippi_stat(SLIPPI_STAT_ACKS_RECEIVED), slippi_stat(SLIPPI_STAT_RESENDS), slippi_stat(SLIPPI_STAT_DROPPED_TEST));
    int pass = !result && bad == 0 && verified >= frames;
    sp_log("peer: %s", pass ? "PASS" : "FAIL");
    slippi_shutdown();
    return pass ? 0 : 1;
}
