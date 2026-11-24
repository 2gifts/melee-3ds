/* Test client for the local wireless lobby, no game: a host or a joiner that sits in the lobby, sends
 * commands, and plays lockstep matches when the host starts them. Used by lw_lobby_test.py, which starts several of them
 * at different times, makes some leave or vanish, and compares what they print.
 *
 *   lw_lobby_client --host --port N [--games G] [--again] [--start-at MS] [--frames F] [--run-ms MS] [--drop PCT]
 *   lw_lobby_client --join --port N [--want-tag T] [--join-at MS] [--leave-at MS] [--crash-at MS] [--drop PCT]
 *   either with [--menu V@MS ...] [--menu-log]
 * A joiner browses and joins the first game listed (with --want-tag, the first whose beacon starts with T).
 *
 * The host sets a lobby blob (a counter and a pattern of it) every 100 ms, starts game 1 at --start-at and each next game
 * 500 ms after the previous one is over, then freezes the blob (counter 0xFFFFFFFF) at --run-ms and waits for the others.
 * Everybody sends the host a numbered command every 400 ms until it sees the frozen blob.
 * Prints (compared by the test): MATCH <game> <hash> ..., CMDS <last>, FINAL <present mask>, LATE ...
 * Menu input: --menu V@MS makes this console's value V (its first byte) from MS on, set every loop as a game's menu
 * would each frame (up to 8 of them). --menu-log prints "MENU <port> <value> <clock ms>" whenever the value this console
 * reads for a port changes (the clock is CLOCK_MONOTONIC, shared by every console on this machine), and "MENUTX <game>
 * <packets>": the menu packets this console sent while that game ran. */
#include "localwireless.h"
#include "lw_platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
static uint64_t now_us(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (uint64_t)(c.QuadPart * 1000000ull / f.QuadPart); }
static void sleep_us(unsigned us) { Sleep(us / 1000 ? us / 1000 : 1); }
#else
#include <unistd.h>
static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000; }
static void sleep_us(unsigned us) { usleep(us); }
#endif

static uint64_t t0;
uint64_t lw_platform_time_us(void) { return now_us(); }
uint32_t lw_platform_random(void) { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x ^ (uint32_t)now_us(); }
int lw_platform_net_init(void) { return 0; }
void lw_platform_log(const char *fmt, ...)
{
    va_list ap;
    printf("[%8.3f] ", (double)(now_us() - t0) / 1e6);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static unsigned ms(void) { return (unsigned)((now_us() - t0) / 1000); }

static void pattern(int player, int frame, uint8_t out[8])
{
    uint32_t x = (uint32_t)frame * 2654435761u ^ (uint32_t)(player + 1) * 0x9E3779B9u;
    for (int i = 0; i < 8; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; out[i] = (uint8_t)x; }
}

#define BLOB 40
static void make_blob(uint8_t *b, uint32_t counter)
{
    b[0] = (uint8_t)(counter >> 24); b[1] = (uint8_t)(counter >> 16); b[2] = (uint8_t)(counter >> 8); b[3] = (uint8_t)counter;
    uint32_t x = counter * 2654435761u + 7;
    for (int i = 4; i < BLOB; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[i] = (uint8_t)x; }
}
static int check_blob(const uint8_t *b, uint32_t *counter)
{
    uint32_t c = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3], k = 0;
    uint8_t want[BLOB];
    make_blob(want, c);
    *counter = c;
    for (int i = 0; i < BLOB; ++i) k |= (uint32_t)(want[i] ^ b[i]);
    return k == 0;
}

int main(int argc, char **argv)
{
    lw_config c;
    memset(&c, 0, sizeof(c));
    c.input_size = 8;
    c.use_udp = 1;
    c.port = 47030;
    int games = 1, frames = 200, delay = 2, bad = 0;   /* clients take the frame count from the start blob */
    int want_tag = -1, beacon_tag = -1, again = 0;   /* again: games after the first with lw_host_start_again */
    unsigned start_at = 3000, run_ms = 12000, join_at = 0, leave_at = 0, crash_at = 0;
    int menu_n = 0, menu_val[8], menu_log = 0, menu_seen[LW_MAX_PLAYERS];
    unsigned menu_at[8], menu_tx_at_begin = 0;
    for (int k = 0; k < LW_MAX_PLAYERS; ++k) menu_seen[k] = -1;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--host")) c.host = 1;
        else if (!strcmp(argv[i], "--join")) c.host = 0;
        else if (i + 1 < argc && !strcmp(argv[i], "--want-tag")) want_tag = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--beacon-tag")) beacon_tag = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--port")) c.port = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--drop")) c.test_drop_pct = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--games")) games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--again")) again = 1;
        else if (i + 1 < argc && !strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--start-at")) start_at = (unsigned)atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--run-ms")) run_ms = (unsigned)atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--join-at")) join_at = (unsigned)atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--leave-at")) leave_at = (unsigned)atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--crash-at")) crash_at = (unsigned)atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--menu") && menu_n < 8) {
            const char *v = argv[++i], *at = strchr(v, '@');
            menu_val[menu_n] = atoi(v);
            menu_at[menu_n++] = at ? (unsigned)atoi(at + 1) : 0;
        } else if (!strcmp(argv[i], "--menu-log")) menu_log = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    t0 = now_us();
    while (ms() < join_at) sleep_us(2000);
    char err[160];
    if (lw_open(&c, err, sizeof(err)) != 0) { printf("FAIL open: %s\n", err); return 1; }

    int connected = 0, me = -1;
    uint32_t counter = 0, seen_counter = 0, cmd_n = 0, hcmd_last[LW_MAX_PLAYERS], seen_frozen = 0, snapshots = 0, last_version = 0;
    uint64_t frozen_at = 0, next_cmd = 0, next_blob = 0;
    int game_no = 0, games_started = 0;
    uint64_t next_start = 0;
    memset(hcmd_last, 0xFF, sizeof(hcmd_last));   /* none yet */
    /* the running game */
    int playing = 0, frame = 0, game_id = 0;
    uint32_t hash = 0;
    uint64_t next_frame = 0;
    int saw_running_late = 0;
    char frozen_names[96] = "";
    unsigned frozen_present = 0;

    for (;;) {
        uint64_t now = now_us();
        unsigned t = ms();
        if (crash_at && t >= crash_at) { printf("vanishing at %u ms (clock %llu)\n", t, (unsigned long long)(now / 1000)); fflush(stdout); _exit(0); }
        if (leave_at && t >= leave_at) { printf("leaving at %u ms (clock %llu)\n", t, (unsigned long long)(now / 1000)); lw_close(); return 0; }
        int st = lw_poll();
        if (st == LW_SEARCHING) {
            if (lw_browsing()) {
                static unsigned last_print;
                int n = lw_scan_count();
                for (int k = 0; k < n; ++k) {
                    lw_net net;
                    if (lw_scan_get(k, &net) != 0) continue;
                    if (want_tag < 0 || (net.len >= 2 && net.data[0] == (uint8_t)want_tag)) {
                        printf("SCAN: %d games; joining #%d \"%s\" tag %d, %d of %d consoles, beacon check %d\n", n, k, net.host, net.data[0], net.players, net.max, net.data[1]);
                        if (want_tag >= 0 && net.data[1] != (uint8_t)(net.data[0] * 3 + 1)) { printf("FAIL: beacon torn\n"); return 1; }
                        if (lw_join(net.key) != 0) printf("join failed: %s\n", lw_error());
                        break;
                    }
                }
                if (ms() - last_print > 2000) { last_print = ms(); printf("SCAN: %d games listed\n", n); }
            }
            if (t > 30000) { printf("FAIL: never joined\n"); return 1; }
            sleep_us(2000);
            continue;
        }
        if (st != LW_CONNECTED) {
            if (seen_frozen) break;            /* the host closed the lobby after the end */
            printf("FAIL: %s\n", lw_error());
            return 1;
        }
        if (!connected) {
            connected = 1;
            me = lw_my_index();
            printf("console index %d, present %x, host %s (clock %llu)\n", me, lw_present(), lw_player_name(0),
                   (unsigned long long)(now / 1000));
        }
        /* menu input: this console's scheduled value, set each loop as a menu does each frame; what it reads, logged */
        {
            int v = -1;
            for (int k = 0; k < menu_n; ++k) if (t >= menu_at[k]) v = menu_val[k];
            if (v >= 0) { uint8_t d[1] = {(uint8_t)v}; lw_menu_input_set(d, 1); }
            if (menu_log) {
                for (int k = 0; k < LW_MAX_PLAYERS; ++k) {
                    uint8_t got[LW_MAX_MENU];
                    lw_menu_input_get(k, got);
                    if (got[0] != menu_seen[k]) {
                        menu_seen[k] = got[0];
                        printf("MENU %d %d %llu\n", k, got[0], (unsigned long long)(now_us() / 1000));
                    }
                }
            }
        }
        uint8_t blob[LW_MAX_LOBBY];

        if (c.host) {
            if (beacon_tag >= 0) {
                uint8_t bc[2] = {(uint8_t)beacon_tag, (uint8_t)(beacon_tag * 3 + 1)};
                lw_host_set_beacon(bc, 2);
            }
            if (!playing) {
                if (now >= next_blob) {
                    next_blob = now + 100000;
                    uint8_t b[BLOB];
                    if (t >= run_ms) { if (!frozen_at) { frozen_at = now; frozen_present = lw_present(); } counter = 0xFFFFFFFFu; }
                    else ++counter;
                    make_blob(b, counter);
                    lw_host_set_lobby(b, BLOB);
                }
                if (!frozen_at && games_started < games && !lw_match_running() && t >= start_at && now >= next_start) {
                    uint8_t sb[16];
                    memset(sb, 0x5A, sizeof(sb));
                    sb[0] = (uint8_t)(games_started + 1);
                    sb[2] = (uint8_t)(frames >> 8);
                    sb[3] = (uint8_t)frames;
                    int r = again && games_started > 0 ? lw_host_start_again(sb, sizeof(sb)) : lw_host_start(sb, sizeof(sb));
                    if (r == 0) {
                        ++games_started;
                        printf("host starts game %d with present %x\n", games_started, lw_present());
                    }
                }
            }
            uint8_t cmd[LW_MAX_COMMAND];
            int from;
            int n;
            while ((n = lw_poll_command(&from, cmd, sizeof(cmd))) > 0) {
                uint32_t k = ((uint32_t)cmd[1] << 8) | cmd[2];
                if (n != 3 || cmd[0] != from) { printf("FAIL: odd command from %d\n", from); ++bad; continue; }
                if (k == 0) hcmd_last[from] = 0;                              /* a new console in that slot */
                else if (hcmd_last[from] + 1 == k) hcmd_last[from] = k;
                else { printf("FAIL: command %u from %d after %u\n", k, from, hcmd_last[from]); ++bad; }
            }
            if (frozen_at && now - frozen_at > 3500000ull) break;
        } else {
            if (lw_lobby_version() != last_version && !playing) {
                last_version = lw_lobby_version();
                int len = lw_get_lobby(blob, sizeof(blob));
                if (len == BLOB) {
                    uint32_t k;
                    ++snapshots;
                    if (!check_blob(blob, &k)) { printf("FAIL: torn lobby blob\n"); ++bad; }
                    else if (k < seen_counter && k != 0xFFFFFFFFu) { printf("FAIL: lobby went back from %u to %u\n", seen_counter, k); ++bad; }
                    else {
                        seen_counter = k;
                        if (k == 0xFFFFFFFFu && !seen_frozen) {
                            seen_frozen = 1;
                            frozen_at = now;
                            frozen_present = lw_present();
                        }
                    }
                }
                unsigned pr = lw_present();
                if (!(pr & (1u << me))) { printf("FAIL: we are not in the present mask %x\n", pr); ++bad; }
                for (int k = 0; k < LW_MAX_PLAYERS; ++k)
                    if (((pr >> k) & 1) != (lw_player_name(k)[0] != 0) || (!((pr >> k) & 1) && lw_signal(k)) || lw_signal(k) > 3) {
                        printf("FAIL: roster mismatch at %d (present %x, name \"%s\", signal %d)\n", k, pr, lw_player_name(k), lw_signal(k));
                        ++bad;
                    }
                if (lw_match_running() && !lw_in_match() && !saw_running_late) { saw_running_late = 1; printf("LATE: sees a game in progress\n"); }
            }
            if (seen_frozen && now - frozen_at > 2500000ull) break;
            if (!seen_frozen && now >= next_cmd && !playing) {
                uint8_t cmd[3] = {(uint8_t)me, (uint8_t)(cmd_n >> 8), (uint8_t)cmd_n};
                if (lw_send_command(cmd, 3) == 0) ++cmd_n;
                next_cmd = now + 400000;
            }
        }

        /* a game: the host has started one for us */
        if (!playing) {
            uint8_t sb[LW_MAX_START];
            int n = lw_get_start(sb, sizeof(sb));
            if (n == 16 && sb[1] == 0x5A) {
                game_id = sb[0];
                frames = (sb[2] << 8) | sb[3];
                lw_game_begin();
                menu_tx_at_begin = lw_stat(LW_STAT_MENU_SENT);
                playing = 1;
                frame = 1;
                hash = 2166136261u ^ (uint32_t)game_id;
                for (int f = 1; f <= delay; ++f) lw_submit(f, NULL, 0, 0);
                next_frame = now;
                printf("game %d begins, present %x\n", game_id, lw_present());
            }
        } else if (now >= next_frame) {
            uint8_t in[LW_MAX_PLAYERS][LW_MAX_INPUT], mask = 0;
            if (!lw_bundle(frame, in, &mask)) {
                /* not here yet; submit our input for frame + delay once */
            }
            static int submitted_for = 0;
            if (submitted_for != frame + delay + game_id * 100000) {
                uint8_t mine[8];
                pattern(me, frame + delay, mine);
                lw_submit(frame + delay, mine, frame - 1, (uint32_t)frame * 31u);
                submitted_for = frame + delay + game_id * 100000;
            }
            if (lw_bundle(frame, in, &mask)) {
                for (int i = 0; i < LW_MAX_PLAYERS; ++i) {
                    uint8_t want[8], zero[8] = {0};
                    pattern(i, frame, want);
                    if (mask & (1u << i)) { if (memcmp(in[i], zero, 8)) { ++bad; printf("FAIL: input from a CPU slot %d\n", i); } }
                    else if (memcmp(in[i], want, 8) && !(frame <= delay && !memcmp(in[i], zero, 8))) {
                        if (bad++ < 5) printf("mismatch: player %d frame %d\n", i, frame);
                    }
                }
                uint8_t tmp[5 + 32];
                memcpy(tmp, &frame, 4);
                tmp[4] = mask;
                for (int i = 0; i < 4; ++i) memcpy(tmp + 5 + 8 * i, in[i], 8);
                for (int i = 0; i < 37; ++i) hash = (hash ^ tmp[i]) * 16777619u;
                ++frame;
                next_frame += 16683;
                if (next_frame < now) next_frame = now;
                if (frame > frames) {
                    printf("MATCH %d %08x\n", game_id, (unsigned)hash);
                    if (menu_log) printf("MENUTX %d %u\n", game_id, lw_stat(LW_STAT_MENU_SENT) - menu_tx_at_begin);
                    lw_game_end();
                    playing = 0;
                    next_start = now + 500000ull;
                    seen_counter = 0;
                    last_version = 0;
                }
            }
        }
        sleep_us(500);
    }
    if (c.host) {
        for (int i = 1; i < LW_MAX_PLAYERS; ++i) if (hcmd_last[i] != 0xFFFFFFFFu) printf("CMDS %d %u\n", i, hcmd_last[i]);
        printf("FINAL %x\n", frozen_present);
    } else {
        printf("CMDS %d %u\n", me, cmd_n ? cmd_n - 1 : 0);
        printf("FINAL %x\n", frozen_present);
        printf("snapshots %u\n", snapshots);
    }
    (void)frozen_names;
    (void)game_no;
    printf(bad ? "FAIL\n" : "PASS\n");
    lw_close();
    return bad ? 1 : 0;
}
