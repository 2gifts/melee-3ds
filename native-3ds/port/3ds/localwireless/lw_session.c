/* The local wireless session: roster, setup/start blobs, per-frame bundles, drop detection (see localwireless.h).
 *
 * Packets (big-endian). Everything that must arrive is re-sent until acknowledged; UDS delivers a unicast frame with
 * 802.11 retransmission, but a lost frame is lost.
 *   0x10 pad        client->host: u8 id, s32 newest_frame, u8 player, s32 checksum_frame, u32 checksum, then
 *                   input_size bytes per frame, newest first, for every frame the host has not acked (<= backlog)
 *   0x11 pad ack    host->client: u8 id, s32 frame
 *   0x12 setup      client->host: u8 id, u8 player, u8 len, blob       (re-sent every 200 ms until the start arrives)
 *   0x13 bundle     host->client: u8 id, u8 game, s32 first_frame, u8 count, s32 checksum_frame, u32 checksum,
 *                   then per frame, oldest first: u8 cpu_mask, input_size bytes x 4 players
 *   0x14 bundle ack client->host: u8 id, u8 game, s32 newest contiguous frame
 *   0x15 start      host->client: u8 id, u8 game, u16 len, blob        (re-sent every 200 ms until the client plays)
 *   0x16 keep-alive both ways: u8 id (when nothing else went out for 100 ms)
 *   0x17 roster     host->client: u8 id, u8 players, u8 game, 4 x (u8 node, 12 name bytes)  (every 200 ms until acked)
 */
#include "localwireless.h"
#include "lw_link.h"
#include "lw_platform.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { MSG_PAD = 0x10, MSG_PAD_ACK, MSG_SETUP, MSG_BUNDLE, MSG_BUNDLE_ACK, MSG_START, MSG_KEEPALIVE, MSG_ROSTER };

#define RING 512                 /* frames kept per player / bundles (power of two) */
#define LOCAL_RING 256
#define ACK_RING 512
#define BACKLOG 128              /* client keeps (and the host accepts) at most this many un-acked frames */
#define MAX_PACKET 1400
#define RESEND_US 200000ull
#define KEEPALIVE_US 100000ull
#define BUNDLE_HEADER 15

typedef struct {
    int32_t frame;
    uint8_t cpu_mask;
    uint8_t in[LW_MAX_PLAYERS][LW_MAX_INPUT];
} bundle_t;

typedef struct {
    int node;
    char name[LW_NAME_MAX];
    int cpu;                             /* dropped: a CPU from the next bundle on */
    int roster_acked, started;           /* host: heard from it / it has sent a pad (so it has the start blob) */
    int32_t head;                        /* host: newest input frame received (own input for player 0) */
    int32_t acked;                       /* host: newest bundle frame acked */
    int32_t sent_head;                   /* host: newest bundle frame sent */
    uint64_t heard_us, sent_us, roster_us, start_us;
    struct { int32_t frame; uint8_t in[LW_MAX_INPUT]; } ring[RING];
    uint8_t setup[LW_MAX_SETUP];
    int setup_len;
} player_t;

static struct {
    lw_config cfg;
    int open, use_udp, status;
    char error[160];
    int is_host, players, my_idx;
    uint64_t timeout_us, frame_us;
    uint8_t game;
    player_t pl[LW_MAX_PLAYERS];
    bundle_t bundles[RING];
    int32_t bundle_head;
    int started;                         /* a game has begun (first bundle) */
    uint8_t start[LW_MAX_START];
    int start_len, start_valid, start_sending;
    int start_seen;                      /* client: some start blob was accepted, its game id is in `game` */
    uint8_t my_setup[LW_MAX_SETUP];
    int my_setup_len, setup_pending;
    uint64_t setup_us;
    /* client: our own input ring, acks and ping */
    struct { int32_t frame, ck_frame; uint32_t ck; uint8_t in[LW_MAX_INPUT]; } local[LOCAL_RING];
    int local_count, local_head;
    int32_t last_queued, last_acked;
    uint64_t last_send_us, keepalive_us, host_heard_us;
    struct { int32_t frame; uint64_t t; } ack_t[ACK_RING];
    int ack_first, ack_count;
    uint64_t ping_us;
    int32_t ck_frame;
    uint32_t ck;
    unsigned stats[LW_STAT_COUNT];
} S;

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int popcount(unsigned v) { int n = 0; for (; v; v &= v - 1) ++n; return n; }

/* ---- the link, by backend ---- */

static int link_open(int host, int port, int max_nodes, char *err, int errlen)
{
    return S.use_udp ? lw_udp_open(host, port, max_nodes, err, errlen) : lw_uds_open(host, port, max_nodes, err, errlen);
}
static int link_poll(char *err, int errlen) { return S.use_udp ? lw_udp_poll(err, errlen) : lw_uds_poll(err, errlen); }
static unsigned link_nodes(void) { return S.use_udp ? lw_udp_nodes() : lw_uds_nodes(); }
static int link_my_node(void) { return S.use_udp ? lw_udp_my_node() : lw_uds_my_node(); }
static const char *link_node_name(int n) { return S.use_udp ? lw_udp_node_name(n) : lw_uds_node_name(n); }
static int link_recv(uint8_t *b, size_t c, int *f) { return S.use_udp ? lw_udp_recv(b, c, f) : lw_uds_recv(b, c, f); }
static void link_close(void) { if (S.use_udp) lw_udp_close(); else lw_uds_close(); }

static void send_to(int node, const uint8_t *d, size_t n)
{
    if (S.cfg.test_drop_pct && (int)(lw_platform_random() % 100) < S.cfg.test_drop_pct) { ++S.stats[LW_STAT_DROPPED_TEST]; return; }
    if ((S.use_udp ? lw_udp_send(node, d, n) : lw_uds_send(node, d, n)) == 0) ++S.stats[LW_STAT_PACKETS_SENT];
}

static void fail(int status, const char *fmt, const char *arg)
{
    S.status = status;
    snprintf(S.error, sizeof(S.error), fmt, arg);
    lw_platform_log("lw: %s", S.error);
}

/* ---- open / close ---- */

int lw_open(const lw_config *cfg, char *err, int errlen)
{
    lw_close();
    memset(&S, 0, sizeof(S));
    S.cfg = *cfg;
    if (S.cfg.input_size < 1 || S.cfg.input_size > LW_MAX_INPUT) { snprintf(err, (size_t)errlen, "bad input size"); return -1; }
    S.is_host = cfg->host != 0;
    S.use_udp = cfg->use_udp != 0;
    S.timeout_us = (uint64_t)(cfg->timeout_ms > 0 ? cfg->timeout_ms : 2000) * 1000ull;
    S.frame_us = (uint64_t)(cfg->frame_us > 0 ? cfg->frame_us : 16683);
    S.players = S.is_host ? (cfg->players < 2 ? 2 : cfg->players > LW_MAX_PLAYERS ? LW_MAX_PLAYERS : cfg->players) : 0;
    S.my_idx = S.is_host ? 0 : -1;
    if (link_open(S.is_host, cfg->port, S.is_host ? S.players : LW_MAX_PLAYERS, err, errlen) != 0) return -1;
    S.open = 1;
    S.status = LW_SEARCHING;
    return 0;
}

void lw_close(void)
{
    if (!S.open) return;
    link_close();
    S.open = 0;
    if (S.status == LW_SEARCHING || S.status == LW_CONNECTED) S.status = LW_IDLE;
}

int lw_status(void) { return S.status; }
const char *lw_error(void) { return S.error; }
int lw_players(void) { return S.players; }
int lw_my_index(void) { return S.my_idx; }
const char *lw_player_name(int idx) { return idx >= 0 && idx < S.players ? S.pl[idx].name : ""; }
const char *lw_local_name(void) { return !S.open ? "" : S.use_udp ? lw_udp_local_name() : lw_uds_local_name(); }
int lw_ping_us(void) { return (int)S.ping_us; }
unsigned lw_stat(int what) { return what >= 0 && what < LW_STAT_COUNT ? S.stats[what] : 0; }

int32_t lw_remote_checksum(uint32_t *checksum)
{
    if (checksum) *checksum = S.ck;
    return S.ck_frame;
}

/* ---- host: who is in the match ---- */

static void host_assign_players(void)
{
    unsigned nodes = link_nodes();
    int idx = 0;
    for (int n = 1; n <= 4 && idx < S.players; ++n) {
        if (!(nodes & (1u << (n - 1)))) continue;
        player_t *p = &S.pl[idx];
        memset(p, 0, sizeof(*p));
        p->node = n;
        snprintf(p->name, sizeof(p->name), "%s", link_node_name(n));
        p->heard_us = lw_platform_time_us();
        lw_platform_log("lw: player %d is node %d (%s)", idx + 1, n, p->name);
        ++idx;
    }
    S.game = (uint8_t)(S.game + 1);
}

static void drop_player(int idx, const char *why)
{
    player_t *p = &S.pl[idx];
    if (p->cpu) return;
    p->cpu = 1;
    lw_platform_log("lw: player %d (%s) %s; becomes a CPU from frame %d", idx + 1, p->name, why, (int)S.bundle_head + 1);
}

/* ---- host: releasing bundles ---- */

static void host_release(void)
{
    int is = S.cfg.input_size;
    for (;;) {
        int32_t f = S.bundle_head + 1;
        if (S.pl[0].head < f) return;                         /* our own input is not queued yet */
        for (int i = 1; i < S.players; ++i)
            if (!S.pl[i].cpu && S.pl[i].head < f) return;
        bundle_t *b = &S.bundles[f & (RING - 1)];
        memset(b, 0, sizeof(*b));
        b->frame = f;
        for (int i = 0; i < S.players; ++i) {
            if (S.pl[i].cpu) b->cpu_mask |= (uint8_t)(1u << i);
            else memcpy(b->in[i], S.pl[i].ring[f & (RING - 1)].in, (size_t)is);
        }
        S.bundle_head = f;
        S.started = 1;
    }
}

static void send_keepalive(int node)
{
    uint8_t pkt[1] = {MSG_KEEPALIVE};
    send_to(node, pkt, sizeof(pkt));
}

static void send_bundles(int idx, uint64_t now)
{
    int is = S.cfg.input_size, per = 1 + 4 * is;
    player_t *p = &S.pl[idx];
    int32_t first = p->acked + 1;
    if (first < 1) first = 1;
    int count = (int)(S.bundle_head - first + 1);
    int cap = (MAX_PACKET - BUNDLE_HEADER) / per;
    if (count <= 0) return;
    if (count > cap) count = cap;
    uint8_t pkt[MAX_PACKET];
    pkt[0] = MSG_BUNDLE;
    pkt[1] = S.game;
    put32(pkt + 2, (uint32_t)first);
    pkt[6] = (uint8_t)count;
    int32_t ckf = 0;
    uint32_t ck = 0;
    if (S.local_count) { ckf = S.local[S.local_head].ck_frame; ck = S.local[S.local_head].ck; }   /* the host's own */
    put32(pkt + 7, (uint32_t)ckf);
    put32(pkt + 11, ck);
    size_t len = BUNDLE_HEADER;
    for (int i = 0; i < count; ++i) {
        const bundle_t *b = &S.bundles[(first + i) & (RING - 1)];
        pkt[len++] = b->cpu_mask;
        for (int k = 0; k < 4; ++k) { memcpy(pkt + len, b->in[k], (size_t)is); len += (size_t)is; }
    }
    send_to(p->node, pkt, len);
    p->sent_us = now;
    p->sent_head = S.bundle_head;
}

static void host_tick(uint64_t now)
{
    unsigned nodes = link_nodes();
    for (int i = 1; i < S.players; ++i) {
        player_t *p = &S.pl[i];
        if (p->cpu) continue;
        int gone = !(nodes & (1u << (p->node - 1)));
        int silent = now - p->heard_us > S.timeout_us;
        if (!gone && !silent) continue;
        if (!S.started && !S.start_sending) {
            /* Before the match: the lobby just lost a player. */
            char who[LW_NAME_MAX + 8];
            snprintf(who, sizeof(who), "%s", p->name[0] ? p->name : "A player");
            fail(LW_DISCONNECTED, "%s left", who);
            return;
        }
        drop_player(i, gone ? "left the network" : "stopped responding");
    }
    host_release();

    for (int i = 1; i < S.players; ++i) {
        player_t *p = &S.pl[i];
        if (p->cpu) continue;
        if (!p->roster_acked) {
            if (now - p->roster_us >= RESEND_US) {
                p->roster_us = now;
                uint8_t pkt[3 + 4 * 13];
                pkt[0] = MSG_ROSTER;
                pkt[1] = (uint8_t)S.players;
                pkt[2] = S.game;
                for (int k = 0; k < 4; ++k) {
                    pkt[3 + k * 13] = k < S.players ? (uint8_t)S.pl[k].node : 0;
                    memset(pkt + 4 + k * 13, 0, 12);
                    if (k < S.players) memcpy(pkt + 4 + k * 13, S.pl[k].name, 11);
                }
                send_to(p->node, pkt, sizeof(pkt));
            }
            continue;
        }
        if (S.start_sending && !p->started && now - p->start_us >= RESEND_US) {
            p->start_us = now;
            uint8_t pkt[4 + LW_MAX_START];
            pkt[0] = MSG_START;
            pkt[1] = S.game;
            pkt[2] = (uint8_t)(S.start_len >> 8);
            pkt[3] = (uint8_t)S.start_len;
            memcpy(pkt + 4, S.start, (size_t)S.start_len);
            send_to(p->node, pkt, (size_t)(4 + S.start_len));
        }
        if (S.bundle_head > 0 && p->acked < S.bundle_head) {
            if (p->sent_head < S.bundle_head || now - p->sent_us >= S.frame_us) {
                if (p->sent_head >= S.bundle_head) ++S.stats[LW_STAT_RESENDS];
                send_bundles(i, now);
            }
        } else if (now - p->sent_us >= KEEPALIVE_US) {
            p->sent_us = now;
            send_keepalive(p->node);
        }
    }
}

/* ---- client ---- */

static void client_send_pads(void);

static void client_tick(uint64_t now)
{
    int host_here = (link_nodes() & (1u << (LW_LINK_HOST_NODE - 1))) != 0;
    if (S.status == LW_CONNECTED && (!host_here || now - S.host_heard_us > S.timeout_us)) {
        if (host_here) fail(LW_DISCONNECTED, "%s", "The host stopped responding");
        else fail(LW_DISCONNECTED, "%s", "The host left");
        return;
    }
    if (now - S.keepalive_us >= KEEPALIVE_US) {
        S.keepalive_us = now;
        send_keepalive(LW_LINK_HOST_NODE);
    }
    if (S.setup_pending && now - S.setup_us >= RESEND_US) {
        S.setup_us = now;
        uint8_t pkt[3 + LW_MAX_SETUP];
        pkt[0] = MSG_SETUP;
        pkt[1] = (uint8_t)S.my_idx;
        pkt[2] = (uint8_t)S.my_setup_len;
        memcpy(pkt + 3, S.my_setup, (size_t)S.my_setup_len);
        send_to(LW_LINK_HOST_NODE, pkt, (size_t)(3 + S.my_setup_len));
    }
    if (S.local_count && S.local[S.local_head].frame > S.last_acked && now - S.last_send_us >= S.frame_us) {
        ++S.stats[LW_STAT_RESENDS];
        client_send_pads();
    }
}

/* Prune what the host has acked and send the rest (newest first). */
static void client_send_pads(void)
{
    int is = S.cfg.input_size;
    if (S.status != LW_CONNECTED || !S.local_count) return;
    int32_t newest = S.local[S.local_head].frame;
    int32_t min_ack = S.last_acked;
    int max_frames = (MAX_PACKET - 14) / is;
    if (max_frames > BACKLOG) max_frames = BACKLOG;
    if (min_ack < newest - max_frames) min_ack = newest - max_frames;
    while (S.local_count) {
        int oldest = (S.local_head - S.local_count + 1 + LOCAL_RING) % LOCAL_RING;
        if (S.local[oldest].frame >= min_ack) break;
        --S.local_count;
    }
    if (!S.local_count) return;
    uint8_t pkt[MAX_PACKET];
    pkt[0] = MSG_PAD;
    put32(pkt + 1, (uint32_t)newest);
    pkt[5] = (uint8_t)S.my_idx;
    put32(pkt + 6, (uint32_t)S.local[S.local_head].ck_frame);
    put32(pkt + 10, S.local[S.local_head].ck);
    size_t len = 14;
    for (int i = 0; i < S.local_count && len + (size_t)is <= sizeof(pkt); ++i) {
        int idx = (S.local_head - i + LOCAL_RING) % LOCAL_RING;
        memcpy(pkt + len, S.local[idx].in, (size_t)is);
        len += (size_t)is;
    }
    send_to(LW_LINK_HOST_NODE, pkt, len);
    uint64_t t = lw_platform_time_us();
    S.last_send_us = t;
    int slot = (S.ack_first + S.ack_count) % ACK_RING;
    if (S.ack_count == ACK_RING) S.ack_first = (S.ack_first + 1) % ACK_RING;
    else ++S.ack_count;
    S.ack_t[slot].frame = newest;
    S.ack_t[slot].t = t;
}

/* ---- receiving ---- */

static void on_roster(const uint8_t *d, size_t len)
{
    if (len < 3 + 4 * 13 || S.status == LW_CONNECTED) return;
    int me = link_my_node(), n = d[1], idx = -1;
    if (n < 2 || n > 4) return;
    for (int k = 0; k < n; ++k)
        if (d[3 + k * 13] == me) idx = k;
    if (idx < 1) { lw_platform_log("lw: roster without this console (node %d)", me); return; }
    S.players = n;
    S.game = d[2];
    S.my_idx = idx;
    for (int k = 0; k < n; ++k) {
        S.pl[k].node = d[3 + k * 13];
        memcpy(S.pl[k].name, d + 4 + k * 13, 11);
        S.pl[k].name[11] = 0;
    }
    S.host_heard_us = lw_platform_time_us();
    S.status = LW_CONNECTED;
    lw_platform_log("lw: joined as player %d of %d (host \"%s\")", idx + 1, n, S.pl[0].name);
}

static void on_start(const uint8_t *d, size_t len)
{
    if (len < 4 || S.status != LW_CONNECTED) return;
    int n = (d[2] << 8) | d[3];
    if (n > LW_MAX_START || len < (size_t)(4 + n)) return;
    if (S.start_seen && d[1] == S.game) return;               /* a repeat of the one we have */
    S.game = d[1];
    memcpy(S.start, d + 4, (size_t)n);
    S.start_len = n;
    S.start_valid = S.start_seen = 1;
    memset(S.bundles, 0, sizeof(S.bundles));                  /* a new game: frames restart at 1 */
    S.bundle_head = 0;
    S.ck_frame = 0;
    S.setup_pending = 0;
    lw_platform_log("lw: received the start blob (%d bytes, game %u)", n, S.game);
}

static void on_bundle(const uint8_t *d, size_t len)
{
    int is = S.cfg.input_size, per = 1 + 4 * is;
    if (len < BUNDLE_HEADER || !S.start_seen || d[1] != S.game) return;
    int32_t first = (int32_t)get32(d + 2);
    int count = d[6];
    if (len < BUNDLE_HEADER + (size_t)count * (size_t)per) { lw_platform_log("lw: bundle packet too small"); return; }
    ++S.stats[LW_STAT_PACKETS_RECEIVED];
    int advanced = 0;
    for (int i = 0; i < count; ++i) {
        int32_t f = first + i;
        if (f <= S.bundle_head) continue;
        if (f != S.bundle_head + 1) { ++S.stats[LW_STAT_GAPS]; break; }
        bundle_t *b = &S.bundles[f & (RING - 1)];
        const uint8_t *src = d + BUNDLE_HEADER + i * per;
        memset(b, 0, sizeof(*b));
        b->frame = f;
        b->cpu_mask = src[0];
        for (int k = 0; k < 4; ++k) memcpy(b->in[k], src + 1 + k * is, (size_t)is);
        S.bundle_head = f;
        advanced = 1;
    }
    if (!advanced) ++S.stats[LW_STAT_PACKETS_STALE];
    S.ck_frame = (int32_t)get32(d + 7);
    S.ck = get32(d + 11);
    uint8_t ack[6];
    ack[0] = MSG_BUNDLE_ACK;
    ack[1] = S.game;
    put32(ack + 2, (uint32_t)S.bundle_head);
    send_to(LW_LINK_HOST_NODE, ack, sizeof(ack));
    ++S.stats[LW_STAT_ACKS_SENT];
}

static void on_pad_ack(const uint8_t *d, size_t len)
{
    if (len < 5) return;
    int32_t frame = (int32_t)get32(d + 1);
    ++S.stats[LW_STAT_ACKS_RECEIVED];
    if (frame > S.last_acked) S.last_acked = frame;
    while (S.ack_count && S.ack_t[S.ack_first].frame < frame) { S.ack_first = (S.ack_first + 1) % ACK_RING; --S.ack_count; }
    if (!S.ack_count || S.ack_t[S.ack_first].frame != frame) return;
    S.ping_us = lw_platform_time_us() - S.ack_t[S.ack_first].t;
    S.ack_first = (S.ack_first + 1) % ACK_RING;
    --S.ack_count;
}

static void on_pad(int idx, const uint8_t *d, size_t len)
{
    int is = S.cfg.input_size;
    player_t *p = &S.pl[idx];
    if (len < 14 || p->cpu) return;
    int32_t frame = (int32_t)get32(d + 1);
    if (d[5] != idx) { lw_platform_log("lw: pad packet with port %d from player %d", d[5], idx + 1); return; }
    int64_t n = (int64_t)frame - p->head;
    if (14 + n * is > (int64_t)len) { lw_platform_log("lw: pad packet too small for %lld inputs", (long long)n); return; }
    if (n > BACKLOG) { lw_platform_log("lw: pad packet with too many frames (%lld)", (long long)n); return; }
    ++S.stats[LW_STAT_PACKETS_RECEIVED];
    p->started = 1;
    if (n > 1 && p->head > 0) ++S.stats[LW_STAT_GAPS];
    if (n <= 0) ++S.stats[LW_STAT_PACKETS_STALE];
    for (int64_t i = n - 1; i >= 0; --i) {
        int32_t f = (int32_t)(frame - i);
        int slot = f & (RING - 1);
        p->ring[slot].frame = f;
        memcpy(p->ring[slot].in, d + 14 + i * is, (size_t)is);
    }
    if (n > 0) {
        p->head = frame;
        S.ck_frame = (int32_t)get32(d + 6);
        S.ck = get32(d + 10);
        uint8_t ack[5];
        ack[0] = MSG_PAD_ACK;
        put32(ack + 1, (uint32_t)frame);
        send_to(p->node, ack, sizeof(ack));
        ++S.stats[LW_STAT_ACKS_SENT];
        host_release();
    }
}

static void on_data(const uint8_t *d, size_t len, int from)
{
    if (!len) return;
    uint64_t now = lw_platform_time_us();
    if (S.is_host) {
        int idx = -1;
        for (int i = 1; i < S.players; ++i)
            if (S.pl[i].node == from) idx = i;
        if (idx < 1 || S.status != LW_CONNECTED) return;
        player_t *p = &S.pl[idx];
        if (p->cpu) return;                    /* dropped: nothing it says counts any more */
        p->heard_us = now;
        switch (d[0]) {
            case MSG_PAD: p->roster_acked = 1; on_pad(idx, d, len); break;
            case MSG_SETUP:
                p->roster_acked = 1;
                if (len >= 3 && d[1] == idx && d[2] <= LW_MAX_SETUP && len >= (size_t)(3 + d[2])) {
                    memcpy(p->setup, d + 3, d[2]);
                    p->setup_len = d[2];
                }
                break;
            case MSG_BUNDLE_ACK:
                if (len >= 6 && d[1] == S.game) {
                    int32_t f = (int32_t)get32(d + 2);
                    if (f > p->acked && f <= S.bundle_head) p->acked = f;
                }
                p->roster_acked = 1;
                break;
            case MSG_KEEPALIVE: p->roster_acked = 1; break;
            default: break;
        }
        return;
    }
    if (from != LW_LINK_HOST_NODE) return;
    S.host_heard_us = now;
    switch (d[0]) {
        case MSG_ROSTER: on_roster(d, len); break;
        case MSG_START: on_start(d, len); break;
        case MSG_BUNDLE: on_bundle(d, len); break;
        case MSG_PAD_ACK: on_pad_ack(d, len); break;
        default: break;
    }
}

int lw_poll(void)
{
    if (!S.open || (S.status != LW_SEARCHING && S.status != LW_CONNECTED)) return S.status;
    char err[160] = "";
    int st = link_poll(err, sizeof(err));
    if (st == LW_LINK_FAILED) { fail(LW_FAILED, "%s", err[0] ? err : "Local wireless failed"); return S.status; }
    if (st != LW_LINK_READY) return S.status;
    uint8_t buf[1500];
    for (int i = 0; i < 64; ++i) {
        int from = 0;
        int n = link_recv(buf, sizeof(buf), &from);
        if (n <= 0) break;
        on_data(buf, (size_t)n, from);
    }
    uint64_t now = lw_platform_time_us();
    if (S.status == LW_SEARCHING && S.is_host) {
        if (popcount(link_nodes()) >= S.players) {
            host_assign_players();
            S.status = LW_CONNECTED;
            lw_platform_log("lw: %d consoles are here; we are player 1 (host)", S.players);
        }
        return S.status;
    }
    if (S.status == LW_CONNECTED) {
        if (S.is_host) host_tick(now);
        else client_tick(now);
    } else if (S.status == LW_SEARCHING && !S.is_host && S.my_idx < 0) {
        /* waiting for the roster: nothing to send, the host speaks first */
    }
    return S.status;
}

/* ---- the game's view: before the match ---- */

void lw_set_setup(const uint8_t *blob, int len)
{
    if (len < 0) len = 0;
    if (len > LW_MAX_SETUP) len = LW_MAX_SETUP;
    if (S.is_host) {
        memcpy(S.pl[0].setup, blob, (size_t)len);
        S.pl[0].setup_len = len;
        return;
    }
    memcpy(S.my_setup, blob, (size_t)len);
    S.my_setup_len = len;
    S.setup_pending = 1;
    S.setup_us = 0;               /* sent on the next poll */
}

int lw_get_setup(int idx, uint8_t *out, int cap)
{
    if (!S.is_host || idx < 0 || idx >= S.players) return 0;
    int n = S.pl[idx].setup_len < cap ? S.pl[idx].setup_len : cap;
    if (n > 0 && out) memcpy(out, S.pl[idx].setup, (size_t)n);
    return n;
}

int lw_host_start(const uint8_t *blob, int len)
{
    if (!S.is_host || S.status != LW_CONNECTED || len < 0 || len > LW_MAX_START) return -1;
    memcpy(S.start, blob, (size_t)len);
    S.start_len = len;
    S.start_valid = S.start_sending = 1;
    S.game = (uint8_t)(S.game + 1);
    /* A new game: frames restart at 1, nobody has sent an input for it yet. */
    memset(S.bundles, 0, sizeof(S.bundles));
    S.bundle_head = 0;
    S.started = 0;
    S.ck_frame = 0;
    for (int i = 0; i < S.players; ++i) {
        player_t *p = &S.pl[i];
        memset(p->ring, 0, sizeof(p->ring));
        p->head = p->acked = p->sent_head = 0;
        p->started = (i == 0);
        p->start_us = p->sent_us = 0;
        p->heard_us = lw_platform_time_us();
        if (i > 0) p->cpu = 0;
    }
    S.last_queued = 0;
    return 0;
}

int lw_get_start(uint8_t *out, int cap)
{
    if (!S.start_valid || S.start_len > cap) return 0;
    if (out) memcpy(out, S.start, (size_t)S.start_len);
    return S.start_len;
}

void lw_game_begin(void)
{
    S.start_valid = 0;                 /* consumed; the host keeps sending its copy until every client plays */
    if (S.is_host) {
        for (int i = 0; i < S.players; ++i) S.pl[i].setup_len = 0;
    }
    S.local_count = 0;
    S.local_head = 0;
    S.last_queued = 0;
    S.last_acked = 0;
    S.ack_first = S.ack_count = 0;
    S.last_send_us = lw_platform_time_us();
}

/* ---- the game's view: during the match ---- */

void lw_submit(int32_t frame, const uint8_t *input, int32_t ck_frame, uint32_t ck)
{
    int is = S.cfg.input_size;
    if (S.status != LW_CONNECTED || frame <= S.last_queued) return;
    S.last_queued = frame;
    if (S.is_host) {
        player_t *me = &S.pl[0];
        int slot = frame & (RING - 1);
        me->ring[slot].frame = frame;
        if (input) memcpy(me->ring[slot].in, input, (size_t)is);
        else memset(me->ring[slot].in, 0, (size_t)is);
        me->head = frame;
        /* the host's own checksum rides in its bundles */
        S.local_head = (S.local_head + 1) % LOCAL_RING;
        if (S.local_count < LOCAL_RING) ++S.local_count;
        S.local[S.local_head].frame = frame;
        S.local[S.local_head].ck_frame = ck_frame;
        S.local[S.local_head].ck = ck;
        host_release();
        return;
    }
    S.local_head = (S.local_head + 1) % LOCAL_RING;
    if (S.local_count < LOCAL_RING) ++S.local_count;
    S.local[S.local_head].frame = frame;
    S.local[S.local_head].ck_frame = ck_frame;
    S.local[S.local_head].ck = ck;
    if (input) memcpy(S.local[S.local_head].in, input, (size_t)is);
    else memset(S.local[S.local_head].in, 0, (size_t)is);
    client_send_pads();
}

int lw_bundle(int32_t frame, uint8_t inputs[LW_MAX_PLAYERS][LW_MAX_INPUT], uint8_t *cpu_mask)
{
    if (frame <= 0 || frame > S.bundle_head) return 0;
    const bundle_t *b = &S.bundles[frame & (RING - 1)];
    if (b->frame != frame) return 0;
    if (inputs) memcpy(inputs, b->in, sizeof(b->in));
    if (cpu_mask) *cpu_mask = b->cpu_mask;
    return 1;
}
