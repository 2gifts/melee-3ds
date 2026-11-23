/* Test client for the local wireless module alone (port/3ds/localwireless), no game: a host or a joiner that
 * exchanges deterministic inputs through the session and checks every bundle. Prints a hash of all bundles, which
 * must be identical on every console that stayed in the match.
 *
 *   lw_client --host --players 4 --frames 600 [--delay 2] [--port N] [--drop PCT] [--timeout-ms MS] [--crash FRAME]
 *   lw_client --join ...
 * Exit 0 = every bundle matched the pattern. A console that vanishes (--crash) exits 0 without a goodbye. */
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

static void pattern(int player, int frame, uint8_t out[8])
{
    uint32_t x = (uint32_t)frame * 2654435761u ^ (uint32_t)(player + 1) * 0x9E3779B9u;
    for (int i = 0; i < 8; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; out[i] = (uint8_t)x; }
}

int main(int argc, char **argv)
{
    lw_config c;
    memset(&c, 0, sizeof(c));
    c.players = 2;
    c.input_size = 8;
    c.use_udp = 1;
    c.port = 47020;
    int frames = 600, delay = 2, crash = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--host")) c.host = 1;
        else if (!strcmp(argv[i], "--join")) c.host = 0;
        else if (i + 1 < argc && !strcmp(argv[i], "--players")) c.players = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--delay")) delay = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--port")) c.port = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--drop")) c.test_drop_pct = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--timeout-ms")) c.timeout_ms = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--crash")) crash = atoi(argv[++i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    t0 = now_us();
    char err[160];
    if (lw_open(&c, err, sizeof(err)) != 0) { printf("FAIL open: %s\n", err); return 1; }
    while (lw_poll() == LW_SEARCHING) {
        if (now_us() - t0 > 60000000ull) { printf("FAIL: nobody joined\n"); return 1; }
        sleep_us(2000);
    }
    if (lw_status() != LW_CONNECTED) { printf("FAIL: %s\n", lw_error()); return 1; }
    int me = lw_my_index(), n = lw_players();
    printf("player %d of %d\n", me + 1, n);

    /* Setup and start: each console's one-byte setup is its index + 100; the host starts with a recognisable blob. */
    uint8_t setup = (uint8_t)(me + 100), blob[64], got[64];
    for (int i = 0; i < 64; ++i) blob[i] = (uint8_t)(i * 7 + 1);
    lw_set_setup(&setup, 1);
    for (;;) {
        lw_poll();
        if (lw_status() != LW_CONNECTED) { printf("FAIL: %s\n", lw_error()); return 1; }
        if (c.host) {
            int ready = 1;
            for (int i = 0; i < n; ++i) { uint8_t b[LW_MAX_SETUP]; if (lw_get_setup(i, b, sizeof b) != 1 || b[0] != i + 100) ready = 0; }
            if (ready) { lw_host_start(blob, 64); break; }
        } else if (lw_get_start(got, sizeof got) == 64) {
            if (memcmp(got, blob, 64)) { printf("FAIL: start blob differs\n"); return 1; }
            break;
        }
        if (now_us() - t0 > 60000000ull) { printf("FAIL: no start\n"); return 1; }
        sleep_us(2000);
    }
    lw_game_begin();

    uint32_t hash = 2166136261u;
    unsigned seen_cpu = 0;
    int bad = 0;
    for (int f = 1; f <= delay; ++f) lw_submit(f, NULL, 0, 0);
    uint64_t next = now_us();
    for (int frame = 1; frame <= frames; ++frame) {
        uint8_t mine[8];
        pattern(me, frame + delay, mine);
        lw_submit(frame + delay, mine, frame - 1, (uint32_t)frame * 31u);
        uint8_t in[LW_MAX_PLAYERS][LW_MAX_INPUT], mask = 0;
        uint64_t wait_from = now_us();
        while (!lw_bundle(frame, in, &mask)) {
            lw_poll();
            if (lw_status() != LW_CONNECTED) { printf("FAIL at frame %d: %s\n", frame, lw_error()); return 1; }
            if (now_us() - wait_from > 30000000ull) { printf("FAIL: no bundle for frame %d\n", frame); return 1; }
            sleep_us(500);
        }
        for (int i = 0; i < n; ++i) {
            uint8_t want[8], zero[8] = {0};
            pattern(i, frame, want);
            if (mask & (1u << i)) {
                if (!(seen_cpu & (1u << i))) { seen_cpu |= 1u << i; printf("player %d is a CPU from frame %d\n", i + 1, frame); }
                if (memcmp(in[i], zero, 8)) ++bad;
            } else if (memcmp(in[i], want, 8) && !(frame <= delay && !memcmp(in[i], zero, 8))) {
                if (bad++ < 5) printf("mismatch: player %d frame %d\n", i + 1, frame);
            }
        }
        if (seen_cpu & ~(unsigned)mask) { ++bad; printf("CPU bit cleared at frame %d\n", frame); }
        uint8_t tmp[5 + 32];
        memcpy(tmp, &frame, 4);
        tmp[4] = mask;
        for (int i = 0; i < 4; ++i) memcpy(tmp + 5 + 8 * i, in[i], 8);
        for (int i = 0; i < 37; ++i) hash = (hash ^ tmp[i]) * 16777619u;
        if (crash && frame == crash) { printf("vanishing at frame %d\n", frame); _exit(0); }
        next += 16683;
        uint64_t t = now_us();
        if (next > t) sleep_us((unsigned)(next - t));
        lw_poll();
    }
    uint64_t linger = now_us() + 1500000;
    while (now_us() < linger) { lw_poll(); sleep_us(2000); }
    printf("bundle hash %08x, %d consoles, cpu mask %x, bad %d\n", (unsigned)hash, n, seen_cpu, bad);
    printf(bad ? "FAIL\n" : "PASS\n");
    lw_close();
    return bad ? 1 : 0;
}
