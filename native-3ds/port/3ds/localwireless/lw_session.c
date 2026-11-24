/* The local wireless session: the lobby, start blobs, per-frame bundles, drop detection (see localwireless.h).
 *
 * Packets (big-endian). Everything that must arrive is re-sent until acknowledged; UDS delivers a unicast frame with
 * 802.11 retransmission, but a lost frame is lost.
 *   0x10 pad        client->host: u8 id, s32 newest_frame, u8 port | game << 2, s32 checksum_frame, u32 checksum, then
 *                   input_size bytes per frame, newest first, for every frame the host has not acked (<= backlog)
 *   0x11 pad ack    host->client: u8 id, s32 frame, u8 game
 *   0x13 bundle     host->client: u8 id, u8 game, s32 first_frame, u8 count, s32 checksum_frame, u32 checksum,
 *                   then per frame, oldest first: u8 cpu_mask, input_size bytes x 4 ports
 *   0x14 bundle ack client->host: u8 id, u8 game, s32 newest contiguous frame
 *   0x15 start      host->client: u8 id, u8 game, u16 len, blob        (re-sent every 200 ms until the client plays)
 *   0x16 keep-alive both ways: u8 id (when nothing else went out for 100 ms)
 *   0x18 lobby      host->client: u8 id, u8 seq, u8 flags (bit0: a game is running), u8 present mask, 4 x u8 signal,
 *                   4 x u8 link node, 4 x 12 name bytes, u8 len, blob   (on every change, then every 200 ms until acked)
 *   0x19 lobby ack  client->host: u8 id, u8 seq
 *   0x1A command    client->host: u8 id, u8 seq, u8 len, blob  (one in flight, re-sent every 100 ms until acked)
 *   0x1B command ack host->client: u8 id, u8 seq
 *
 * Ports and link nodes: the host gives each arriving node the lowest free port and keeps the node -> port table; the
 * lobby packet carries it, so a client finds its own port by its node id. Nothing assumes how the link numbers nodes.
 */
#include "localwireless.h"
#include "lw_link.h"
#include "lw_platform.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { MSG_PAD = 0x10, MSG_PAD_ACK = 0x11, MSG_BUNDLE = 0x13, MSG_BUNDLE_ACK = 0x14, MSG_START = 0x15, MSG_KEEPALIVE = 0x16,
       MSG_LOBBY = 0x18, MSG_LOBBY_ACK = 0x19, MSG_CMD = 0x1A, MSG_CMD_ACK = 0x1B };

#define RING 512                 /* frames kept per player / bundles (power of two) */
#define LOCAL_RING 256
#define ACK_RING 512
#define BACKLOG 128              /* client keeps (and the host accepts) at most this many un-acked frames */
#define MAX_PACKET 1400
#define RESEND_US 200000ull
#define KEEPALIVE_US 100000ull
#define BUNDLE_HEADER 15
#define LOBBY_HEADER 61
#define CMD_RING 8               /* client: queued commands */
#define HOST_CMD_RING 32         /* host: commands not yet read by the game */
#define CMD_RESEND_US 100000ull
#define ENDING_MAX_US 3000000ull /* host: longest wait for the last bundles to be acked after a game */
#define JOIN_TIMEOUT_US 5000000ull

#ifdef LW_NO_UDP
#define LINK(fn, args) lw_uds_##fn args
#else
#define LINK(fn, args) (S.use_udp ? lw_udp_##fn args : lw_uds_##fn args)
#endif

typedef struct {
    int32_t frame;
    uint8_t cpu_mask;
    uint8_t in[LW_MAX_PLAYERS][LW_MAX_INPUT];
} bundle_t;

typedef struct {
    int node;                            /* the link node on this port */
    char name[LW_NAME_MAX];
    int cpu;                             /* host: dropped out of the running game, a CPU from the next bundle on */
    int known;                           /* host: it has acked a lobby (so it can take part in the next game) */
    int started;                         /* host: it has sent a pad (so it has the start blob) */
    int present, in_match;               /* here / in the running game */
    int lobby_valid;                     /* host: lobby_acked is meaningful */
    uint8_t lobby_acked, lobby_sent;     /* host: newest snapshot the console acked / that we sent it */
    uint8_t cmd_last;                    /* host: newest command seq accepted from it */
    uint64_t lobby_us;
    int32_t head;                        /* host: newest input frame received (own input for port 0) */
    int32_t acked;                       /* host: newest bundle frame acked */
    int32_t sent_head;                   /* host: newest bundle frame sent */
    uint64_t heard_us, sent_us, start_us;
    struct { int32_t frame; uint8_t in[LW_MAX_INPUT]; } ring[RING];
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
    uint8_t start[LW_MAX_START];
    int start_len, start_valid, start_sending;
    int start_seen;                      /* client: some start blob was accepted, its game id is in `game` */
    /* client: our own input ring, acks and ping (the host keeps its checksums here too) */
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
    /* lobby */
    int browsing, joining;
    int in_play;                         /* this console began a game (lw_game_begin) and has not ended it */
    uint64_t join_us;                    /* client: when lw_join started */
    unsigned present;                    /* consoles here, by port */
    uint32_t ignored;                    /* host: link nodes we stopped listening to until they leave the link */
    uint8_t lob_seq, lob_signal[LW_MAX_PLAYERS];
    uint8_t lob_blob[LW_MAX_LOBBY];
    int lob_len, lob_seen, lob_match;    /* lob_seen: client has a snapshot */
    unsigned lob_version;
    int match_active, ending;            /* host: a game runs / its last bundles are still going out */
    unsigned last_members;               /* host: ports that took part in the last game to the end (lw_host_start_again) */
    int last_ended;                      /* client: id of the game we last ended (-1 none), so a stale start is ignored */
    uint64_t ending_us, signal_us;
    struct { uint8_t seq, len, data[LW_MAX_COMMAND]; } cmd[CMD_RING];   /* client */
    int cmd_count, cmd_head;
    uint8_t cmd_next;
    uint64_t cmd_us;
    struct { int from, len; uint8_t data[LW_MAX_COMMAND]; } hcmd[HOST_CMD_RING];   /* host */
    int hcmd_count, hcmd_head;
} S;

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t node_bit(int node) { return node >= 1 && node <= LW_LINK_MAX_NODE ? 1u << (node - 1) : 0; }

static void send_to(int node, const uint8_t *d, size_t n)
{
    if (S.cfg.test_drop_pct && (int)(lw_platform_random() % 100) < S.cfg.test_drop_pct) { ++S.stats[LW_STAT_DROPPED_TEST]; return; }
    if (LINK(send, (node, d, n)) == 0) ++S.stats[LW_STAT_PACKETS_SENT];
}

static void fail(int status, const char *fmt, const char *arg)
{
    S.status = status;
    snprintf(S.error, sizeof(S.error), fmt, arg);
    lw_platform_log("lw: %s", S.error);
}

static int top_of(unsigned present)
{
    int top = 0;
    for (int k = 0; k < LW_MAX_PLAYERS; ++k) if (present & (1u << k)) top = k + 1;
    return top;
}

/* ---- open / close ---- */

int lw_open(const lw_config *cfg, char *err, int errlen)
{
    lw_close();
    memset(&S, 0, sizeof(S));
    S.cfg = *cfg;
    if (S.cfg.input_size < 1 || S.cfg.input_size > LW_MAX_INPUT) { snprintf(err, (size_t)errlen, "bad input size"); return -1; }
#ifdef LW_NO_UDP
    if (cfg->use_udp) { snprintf(err, (size_t)errlen, "the loopback link is not in this build"); return -1; }
#endif
    S.is_host = cfg->host != 0;
    S.use_udp = cfg->use_udp != 0;
    S.timeout_us = (uint64_t)(cfg->timeout_ms > 0 ? cfg->timeout_ms : 2000) * 1000ull;
    S.frame_us = (uint64_t)(cfg->frame_us > 0 ? cfg->frame_us : 16683);
    S.players = S.is_host ? 1 : 0;
    S.my_idx = S.is_host ? 0 : -1;
    S.last_ended = -1;
    S.lob_seq = 1;
    S.cmd_next = 1;
    S.browsing = !S.is_host;
    if (LINK(open, (S.is_host, cfg->port, LW_MAX_PLAYERS, err, errlen)) != 0) return -1;
    S.open = 1;
    S.status = LW_SEARCHING;
    return 0;
}

void lw_close(void)
{
    if (!S.open) return;
    LINK(close, ());
    S.open = 0;
    if (S.status == LW_SEARCHING || S.status == LW_CONNECTED) S.status = LW_IDLE;
}

int lw_status(void) { return S.status; }
const char *lw_error(void) { return S.error; }
int lw_players(void) { return S.players; }
unsigned lw_present(void) { return S.present; }
int lw_my_index(void) { return S.my_idx; }
const char *lw_player_name(int idx) { return idx >= 0 && idx < LW_MAX_PLAYERS && (S.present & (1u << idx)) ? S.pl[idx].name : ""; }
const char *lw_local_name(void) { return !S.open ? "" : LINK(local_name, ()); }
int lw_ping_us(void) { return (int)S.ping_us; }
unsigned lw_stat(int what) { return what >= 0 && what < LW_STAT_COUNT ? S.stats[what] : 0; }

int32_t lw_remote_checksum(uint32_t *checksum)
{
    if (checksum) *checksum = S.ck;
    return S.ck_frame;
}

/* ---- host: who is here ---- */

static void drop_player(int idx, const char *why)
{
    player_t *p = &S.pl[idx];
    if (p->cpu) return;
    p->cpu = 1;
    lw_platform_log("lw: port %d (%s) %s; a CPU from frame %d", idx + 1, p->name, why, (int)S.bundle_head + 1);
}

/* The console that is in the running game (and so gets no lobby until it ends). */
static int in_game(const player_t *p) { return S.match_active && p->in_match && !p->cpu; }

static void lobby_bump(void)
{
    if (!++S.lob_seq) ++S.lob_seq;           /* 0 never names a snapshot */
    ++S.lob_version;
}

static int port_of_node(int node)
{
    for (int i = 1; i < LW_MAX_PLAYERS; ++i)
        if ((S.present & (1u << i)) && S.pl[i].node == node) return i;
    return -1;
}

static int signal_level(uint64_t age)
{
    return age <= 300000ull ? 3 : age <= 700000ull ? 2 : age <= 1500000ull ? 1 : 0;
}

/* Forget the commands a port's console queued and the game has not read yet: commands are filed by port, so once that
 * console has gone they would otherwise count as the next console's on the same port. */
static void drop_commands_of(int idx)
{
    int kept = 0;
    for (int k = 0; k < S.hcmd_count; ++k) {
        int at = (S.hcmd_head + k) % HOST_CMD_RING;
        if (S.hcmd[at].from == idx) continue;
        int to = (S.hcmd_head + kept++) % HOST_CMD_RING;
        if (to != at) S.hcmd[to] = S.hcmd[at];
    }
    S.hcmd_count = kept;
}

/* Consoles that arrived on or left the link join or leave the roster. */
static void host_sync_roster(uint64_t now)
{
    unsigned nodes = LINK(nodes, ());
    S.ignored &= nodes;
    if (!S.pl[0].name[0]) snprintf(S.pl[0].name, sizeof(S.pl[0].name), "%s", LINK(node_name, (LW_LINK_HOST_NODE)));
    for (int i = 1; i < LW_MAX_PLAYERS; ++i) {
        player_t *p = &S.pl[i];
        if (!(S.present & (1u << i))) continue;
        uint32_t bit = node_bit(p->node);
        if ((nodes & bit) && !(S.ignored & bit)) continue;
        if (in_game(p)) drop_player(i, "left the network");
        S.present &= ~(1u << i);
        p->present = 0;
        p->name[0] = 0;
        drop_commands_of(i);
        lobby_bump();
        lw_platform_log("lw: port %d (node %d) left the lobby", i + 1, p->node);
    }
    for (int n = 1; n <= LW_LINK_MAX_NODE; ++n) {
        uint32_t bit = node_bit(n);
        if (n == LW_LINK_HOST_NODE || !(nodes & bit) || (S.ignored & bit) || port_of_node(n) >= 0) continue;
        int idx = -1;
        for (int i = 1; i < LW_MAX_PLAYERS && idx < 0; ++i) if (!(S.present & (1u << i))) idx = i;
        if (idx < 0) {                       /* full: the link let a fifth console in */
            lw_platform_log("lw: node %d arrived but the lobby is full", n);
            S.ignored |= bit;
            LINK(eject, (n));
            continue;
        }
        player_t *p = &S.pl[idx];
        int was = S.match_active && p->in_match, cpu = p->cpu;
        memset(p, 0, sizeof(*p));
        p->node = n;
        p->present = 1;
        p->in_match = was;                   /* a port whose console dropped out of this game stays a CPU in it */
        p->cpu = was ? cpu : 0;
        snprintf(p->name, sizeof(p->name), "%s", LINK(node_name, (n)));
        p->heard_us = now;
        S.present |= 1u << idx;
        lobby_bump();
        lw_platform_log("lw: node %d (%s) joined the lobby on port %d%s", n, p->name, idx + 1, S.match_active ? " during a game" : "");
    }
    S.players = top_of(S.present);
    if (now - S.signal_us >= 100000ull) {
        S.signal_us = now;
        for (int k = 0; k < LW_MAX_PLAYERS; ++k) {
            uint8_t v = !(S.present & (1u << k)) ? 0 : k == 0 ? 3 : (uint8_t)signal_level(now - S.pl[k].heard_us);
            if (v != S.lob_signal[k]) { S.lob_signal[k] = v; lobby_bump(); }
        }
    }
}

/* ---- host: releasing bundles ---- */

static void host_release(void)
{
    int is = S.cfg.input_size;
    for (;;) {
        int32_t f = S.bundle_head + 1;
        if (S.pl[0].head < f) return;                         /* our own input is not queued yet */
        for (int i = 1; i < LW_MAX_PLAYERS; ++i)
            if (S.pl[i].in_match && !S.pl[i].cpu && S.pl[i].head < f) return;
        bundle_t *b = &S.bundles[f & (RING - 1)];
        memset(b, 0, sizeof(*b));
        b->frame = f;
        for (int i = 0; i < LW_MAX_PLAYERS; ++i) {
            if (S.pl[i].cpu || !S.pl[i].in_match) b->cpu_mask |= (uint8_t)(1u << i);
            else memcpy(b->in[i], S.pl[i].ring[f & (RING - 1)].in, (size_t)is);
        }
        S.bundle_head = f;
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

static void send_lobby(int idx, uint64_t now)
{
    player_t *p = &S.pl[idx];
    uint8_t pkt[LOBBY_HEADER + LW_MAX_LOBBY];
    pkt[0] = MSG_LOBBY;
    pkt[1] = S.lob_seq;
    pkt[2] = (uint8_t)(S.match_active ? 1 : 0);
    pkt[3] = (uint8_t)S.present;
    for (int k = 0; k < 4; ++k) {
        int here = (S.present >> k) & 1;
        pkt[4 + k] = S.lob_signal[k];
        pkt[8 + k] = here ? (uint8_t)S.pl[k].node : 0;
        memset(pkt + 12 + k * 12, 0, 12);
        if (here) memcpy(pkt + 12 + k * 12, S.pl[k].name, 11);
    }
    pkt[60] = (uint8_t)S.lob_len;
    memcpy(pkt + LOBBY_HEADER, S.lob_blob, (size_t)S.lob_len);
    send_to(p->node, pkt, (size_t)(LOBBY_HEADER + S.lob_len));
    p->lobby_us = now;
    p->lobby_sent = S.lob_seq;
}

static void host_finish_game(void)
{
    S.match_active = S.ending = S.start_sending = 0;
    S.start_valid = 0;
    S.last_members = 0;
    for (int i = 0; i < LW_MAX_PLAYERS; ++i) {
        if (S.pl[i].present && S.pl[i].in_match && !S.pl[i].cpu) S.last_members |= 1u << i;
        S.pl[i].in_match = S.pl[i].cpu = 0;
    }
    lobby_bump();
    lw_platform_log("lw: back in the lobby");
}

static void host_tick(uint64_t now)
{
    host_sync_roster(now);
    for (int i = 1; i < LW_MAX_PLAYERS; ++i) {
        player_t *p = &S.pl[i];
        if (!p->present || now - p->heard_us <= S.timeout_us) continue;
        if (in_game(p)) drop_player(i, "stopped responding");
        S.ignored |= node_bit(p->node);      /* silent: let go of it until it leaves the link */
        LINK(eject, (p->node));
    }
    if (S.ending) {
        int done = now - S.ending_us > ENDING_MAX_US;
        if (!done) {
            done = 1;
            for (int i = 1; i < LW_MAX_PLAYERS; ++i)
                if (S.pl[i].present && in_game(&S.pl[i]) && S.pl[i].acked < S.bundle_head) done = 0;
        }
        if (done) host_finish_game();
    }
    host_release();

    for (int i = 1; i < LW_MAX_PLAYERS; ++i) {
        player_t *p = &S.pl[i];
        if (!p->present) continue;
        if (!in_game(p)) {
            /* The lobby: on every change, then until it is acked. */
            int fresh = p->lobby_sent != S.lob_seq;
            int acked = p->lobby_valid && p->lobby_acked == S.lob_seq;
            if (fresh || (!acked && now - p->lobby_us >= RESEND_US)) {
                send_lobby(i, now);
            } else if (acked && now - p->sent_us >= KEEPALIVE_US && now - p->lobby_us >= KEEPALIVE_US) {
                p->sent_us = now;
                send_keepalive(p->node);
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
    int host_here = (LINK(nodes, ()) & node_bit(LW_LINK_HOST_NODE)) != 0;
    if (!host_here || now - S.host_heard_us > S.timeout_us) {
        if (host_here) fail(LW_DISCONNECTED, "%s", "The host stopped responding");
        else fail(LW_DISCONNECTED, "%s", "The host left");
        return;
    }
    if (now - S.keepalive_us >= KEEPALIVE_US) {
        S.keepalive_us = now;
        send_keepalive(LW_LINK_HOST_NODE);
    }
    if (S.cmd_count && now - S.cmd_us >= CMD_RESEND_US) {
        S.cmd_us = now;
        uint8_t pkt[3 + LW_MAX_COMMAND];
        pkt[0] = MSG_CMD;
        pkt[1] = S.cmd[S.cmd_head].seq;
        pkt[2] = S.cmd[S.cmd_head].len;
        memcpy(pkt + 3, S.cmd[S.cmd_head].data, S.cmd[S.cmd_head].len);
        send_to(LW_LINK_HOST_NODE, pkt, (size_t)(3 + pkt[2]));
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
    pkt[5] = (uint8_t)(S.my_idx | (S.game & 0x3F) << 2);       /* the game too: a late repeat of an older game's pads must not count */
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

static void on_start(const uint8_t *d, size_t len)
{
    if (len < 4 || S.status != LW_CONNECTED) return;
    int n = (d[2] << 8) | d[3];
    if (n > LW_MAX_START || len < (size_t)(4 + n)) return;
    if (S.start_seen && d[1] == S.game) return;               /* a repeat of the one we have */
    if (d[1] == S.last_ended) return;                          /* a late repeat of a game that is over */
    if (S.in_play) return;                                     /* the host got ahead of us: the next game waits until this one is over here */
    S.game = d[1];
    memcpy(S.start, d + 4, (size_t)n);
    S.start_len = n;
    S.start_valid = S.start_seen = 1;
    memset(S.bundles, 0, sizeof(S.bundles));                  /* a new game: frames restart at 1 */
    S.bundle_head = 0;
    S.ck_frame = 0;
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
    if (len < 6 || d[5] != S.game) return;                      /* an ack of another game */
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
    if ((d[5] >> 2) != (S.game & 0x3F)) return;                  /* a pad of another game */
    if ((d[5] & 3) != idx) { lw_platform_log("lw: pad packet with port %d from port %d", d[5] & 3, idx + 1); return; }
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
        uint8_t ack[6];
        ack[0] = MSG_PAD_ACK;
        put32(ack + 1, (uint32_t)frame);
        ack[5] = S.game;
        send_to(p->node, ack, sizeof(ack));
        ++S.stats[LW_STAT_ACKS_SENT];
        host_release();
    }
}

static void on_lobby(const uint8_t *d, size_t len)
{
    if (len < LOBBY_HEADER || d[60] > LW_MAX_LOBBY || len < (size_t)(LOBBY_HEADER + d[60])) return;
    int me = LINK(my_node, ()), idx = -1;
    unsigned present = d[3] & 0xF;
    for (int k = 1; k < LW_MAX_PLAYERS; ++k)
        if ((present & (1u << k)) && d[8 + k] == me) idx = k;
    if (idx < 1) return;                                                         /* not (yet) a member */
    uint8_t ack[2] = {MSG_LOBBY_ACK, d[1]};
    send_to(LW_LINK_HOST_NODE, ack, sizeof(ack));
    if (S.lob_seen && (int8_t)(uint8_t)(d[1] - S.lob_seq) <= 0) return;          /* a repeat or an old one */
    S.lob_seen = 1;
    S.lob_seq = d[1];
    S.present = present;
    S.lob_match = d[2] & 1;
    for (int k = 0; k < 4; ++k) {
        S.lob_signal[k] = d[4 + k];
        S.pl[k].node = d[8 + k];
        memcpy(S.pl[k].name, d + 12 + k * 12, 11);
        S.pl[k].name[11] = 0;
        S.pl[k].present = (present >> k) & 1;
    }
    S.players = top_of(present);
    S.lob_len = d[60];
    memcpy(S.lob_blob, d + LOBBY_HEADER, d[60]);
    ++S.lob_version;
    if (S.status != LW_CONNECTED) {
        S.my_idx = idx;
        S.host_heard_us = lw_platform_time_us();
        S.status = LW_CONNECTED;
        S.joining = 0;
        lw_platform_log("lw: joined the lobby on port %d (node %d, host \"%s\")", idx + 1, me, S.pl[0].name);
    }
}

static void on_cmd_ack(const uint8_t *d, size_t len)
{
    if (len >= 2 && S.cmd_count && S.cmd[S.cmd_head].seq == d[1]) {
        S.cmd_head = (S.cmd_head + 1) % CMD_RING;
        --S.cmd_count;
        S.cmd_us = 0;                                                            /* the next one goes at once */
    }
}

static void on_cmd(int idx, const uint8_t *d, size_t len)
{
    if (len < 3 || d[2] > LW_MAX_COMMAND || len < (size_t)(3 + d[2])) return;
    player_t *p = &S.pl[idx];
    int8_t diff = (int8_t)(uint8_t)(d[1] - p->cmd_last);
    if (diff == 1) {
        if (S.hcmd_count == HOST_CMD_RING) return;                               /* no room yet: not acked, it will resend */
        int slot = (S.hcmd_head + S.hcmd_count++) % HOST_CMD_RING;
        S.hcmd[slot].from = idx;
        S.hcmd[slot].len = d[2];
        memcpy(S.hcmd[slot].data, d + 3, d[2]);
        p->cmd_last = d[1];
    } else if (diff > 1) {
        return;
    }
    uint8_t ack[2] = {MSG_CMD_ACK, d[1]};
    send_to(p->node, ack, sizeof(ack));
}

static void on_data(const uint8_t *d, size_t len, int from)
{
    if (!len) return;
    uint64_t now = lw_platform_time_us();
    if (S.is_host) {
        int idx = port_of_node(from);
        if (idx < 1 || S.status != LW_CONNECTED) return;
        player_t *p = &S.pl[idx];
        p->heard_us = now;
        switch (d[0]) {
            case MSG_LOBBY_ACK:
                if (len >= 2) { p->lobby_acked = d[1]; p->lobby_valid = 1; p->known = 1; }
                return;
            case MSG_CMD: p->known = 1; on_cmd(idx, d, len); return;          /* it only sends one once it has the lobby */
            case MSG_KEEPALIVE: return;
            default: break;
        }
        if (!p->in_match || p->cpu) return;    /* not in this game, or dropped: nothing it says counts */
        switch (d[0]) {
            case MSG_PAD: on_pad(idx, d, len); break;
            case MSG_BUNDLE_ACK:
                if (len >= 6 && d[1] == S.game) {
                    int32_t f = (int32_t)get32(d + 2);
                    if (f > p->acked && f <= S.bundle_head) p->acked = f;
                }
                break;
            default: break;
        }
        return;
    }
    if (from != LW_LINK_HOST_NODE) return;
    S.host_heard_us = now;
    switch (d[0]) {
        case MSG_LOBBY: on_lobby(d, len); break;
        case MSG_CMD_ACK: on_cmd_ack(d, len); break;
        case MSG_START: on_start(d, len); break;
        case MSG_BUNDLE: on_bundle(d, len); break;
        case MSG_PAD_ACK: on_pad_ack(d, len); break;
        default: break;
    }
}

int lw_poll(void)
{
    if (!S.open || (S.status != LW_SEARCHING && S.status != LW_CONNECTED)) return S.status;
    uint64_t now = lw_platform_time_us();
    /* Joining has a deadline whatever the link says: a link can be up while the host never lets us into its lobby. */
    if (S.joining && S.status == LW_SEARCHING && now - S.join_us > JOIN_TIMEOUT_US) {
        fail(LW_FAILED, "%s", "Could not join the game");
        return S.status;
    }
    char err[160] = "";
    int st = LINK(poll, (err, sizeof(err)));
    if (st == LW_LINK_FAILED) { fail(LW_FAILED, "%s", err[0] ? err : "Local wireless failed"); return S.status; }
    if (st != LW_LINK_READY) return S.status;
    uint8_t buf[1500];
    for (int i = 0; i < 64; ++i) {
        int from = 0;
        int n = LINK(recv, (buf, sizeof(buf), &from));
        if (n <= 0) break;
        on_data(buf, (size_t)n, from);
    }
    now = lw_platform_time_us();
    if (S.status == LW_SEARCHING && S.is_host) {
        memset(&S.pl[0], 0, sizeof(S.pl[0]));
        S.pl[0].node = LW_LINK_HOST_NODE;
        S.pl[0].present = 1;
        S.present = 1;
        S.players = 1;
        S.lob_signal[0] = 3;
        snprintf(S.pl[0].name, sizeof(S.pl[0].name), "%s", LINK(node_name, (LW_LINK_HOST_NODE)));
        S.status = LW_CONNECTED;
        lw_platform_log("lw: lobby open; we are the host");
        return S.status;
    }
    if (S.status == LW_CONNECTED) {
        if (S.is_host) host_tick(now);
        else client_tick(now);
    } else if (S.status == LW_SEARCHING && !S.is_host && now - S.keepalive_us >= KEEPALIVE_US) {
        /* waiting for the first lobby snapshot: let the host know we are alive, or it lets go of us */
        S.keepalive_us = now;
        send_keepalive(LW_LINK_HOST_NODE);
    }
    return S.status;
}

/* ---- starting a game ---- */

static int host_start(const uint8_t *blob, int len, unsigned members)
{
    if (!S.is_host || S.status != LW_CONNECTED || len < 0 || len > LW_MAX_START) return -1;
    if (S.ending) return -2;                      /* the last game's final bundles are still going out */
    memcpy(S.start, blob, (size_t)len);
    S.start_len = len;
    S.start_valid = S.start_sending = 1;
    S.game = (uint8_t)(S.game + 1);
    /* A new game: frames restart at 1, nobody has sent an input for it yet. */
    memset(S.bundles, 0, sizeof(S.bundles));
    S.bundle_head = 0;
    S.ck_frame = 0;
    S.match_active = 1;
    for (int i = 0; i < LW_MAX_PLAYERS; ++i) {
        player_t *p = &S.pl[i];
        p->in_match = p->present && (i == 0 || p->known) && (members >> i & 1);   /* a console still joining cannot start this game */
        memset(p->ring, 0, sizeof(p->ring));
        p->head = p->acked = p->sent_head = 0;
        p->started = (i == 0);
        p->start_us = p->sent_us = 0;
        p->heard_us = lw_platform_time_us();
        p->cpu = 0;
    }
    S.last_queued = 0;
    lobby_bump();
    return 0;
}

int lw_host_start(const uint8_t *blob, int len) { return host_start(blob, len, 0xFu); }
int lw_host_start_again(const uint8_t *blob, int len) { return host_start(blob, len, S.last_members | 1u); }

int lw_get_start(uint8_t *out, int cap)
{
    if (!S.start_valid || S.start_len > cap) return 0;
    if (out) memcpy(out, S.start, (size_t)S.start_len);
    return S.start_len;
}

void lw_game_begin(void)
{
    S.in_play = 1;
    S.start_valid = 0;                 /* consumed; the host keeps sending its copy until every client plays */
    S.local_count = 0;
    S.local_head = 0;
    S.last_queued = 0;
    S.last_acked = 0;
    S.ack_first = S.ack_count = 0;
    S.last_send_us = lw_platform_time_us();
}

/* ---- during the game ---- */

void lw_submit(int32_t frame, const uint8_t *input, int32_t ck_frame, uint32_t ck)
{
    int is = S.cfg.input_size;
    if (S.status != LW_CONNECTED || frame <= S.last_queued) return;
    S.last_queued = frame;
    S.local_head = (S.local_head + 1) % LOCAL_RING;
    if (S.local_count < LOCAL_RING) ++S.local_count;
    S.local[S.local_head].frame = frame;
    S.local[S.local_head].ck_frame = ck_frame;
    S.local[S.local_head].ck = ck;
    if (S.is_host) {
        /* the host's own input goes straight into its ring; its checksum rides in its bundles */
        player_t *me = &S.pl[0];
        int slot = frame & (RING - 1);
        me->ring[slot].frame = frame;
        if (input) memcpy(me->ring[slot].in, input, (size_t)is);
        else memset(me->ring[slot].in, 0, (size_t)is);
        me->head = frame;
        host_release();
        return;
    }
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

/* ---- the lobby ---- */

int lw_signal(int idx) { return idx >= 0 && idx < LW_MAX_PLAYERS && (S.present & (1u << idx)) ? S.lob_signal[idx] : 0; }
unsigned lw_lobby_version(void) { return S.lob_version; }
int lw_match_running(void) { return S.is_host ? S.match_active : S.lob_match; }
int lw_in_match(void) { return S.is_host ? (S.match_active && S.pl[0].in_match) : S.start_seen; }

void lw_host_set_lobby(const uint8_t *blob, int len)
{
    if (!S.is_host) return;
    if (len < 0) len = 0;
    if (len > LW_MAX_LOBBY) len = LW_MAX_LOBBY;
    if (len == S.lob_len && (!len || !memcmp(blob, S.lob_blob, (size_t)len))) return;
    memcpy(S.lob_blob, blob, (size_t)len);
    S.lob_len = len;
    lobby_bump();
}

int lw_get_lobby(uint8_t *out, int cap)
{
    if (S.lob_len > cap) return 0;
    if (S.lob_len && out) memcpy(out, S.lob_blob, (size_t)S.lob_len);
    return S.lob_len;
}

int lw_send_command(const uint8_t *cmd, int len)
{
    if (S.is_host || len < 1 || len > LW_MAX_COMMAND || S.cmd_count == CMD_RING) return -1;
    int slot = (S.cmd_head + S.cmd_count++) % CMD_RING;
    S.cmd[slot].seq = S.cmd_next++;
    S.cmd[slot].len = (uint8_t)len;
    memcpy(S.cmd[slot].data, cmd, (size_t)len);
    if (S.cmd_count == 1) S.cmd_us = 0;
    return 0;
}

int lw_poll_command(int *from, uint8_t *out, int cap)
{
    if (!S.hcmd_count) return 0;
    int n = S.hcmd[S.hcmd_head].len;
    if (n > cap) n = cap;
    if (from) *from = S.hcmd[S.hcmd_head].from;
    if (out) memcpy(out, S.hcmd[S.hcmd_head].data, (size_t)n);
    S.hcmd_head = (S.hcmd_head + 1) % HOST_CMD_RING;
    --S.hcmd_count;
    return n;
}

void lw_game_end(void)
{
    if (!S.open) return;
    S.in_play = 0;
    if (S.is_host) {
        if (S.match_active && !S.ending) { S.ending = 1; S.ending_us = lw_platform_time_us(); }
        return;
    }
    S.last_ended = S.game;
    S.start_seen = S.start_valid = 0;
    S.local_count = S.local_head = 0;
    S.last_queued = S.last_acked = 0;
    S.ack_first = S.ack_count = 0;
    S.bundle_head = 0;
}

/* ---- browsing ---- */

int lw_browsing(void) { return S.open && S.browsing; }

int lw_scan_count(void)
{
    if (!S.open || !S.browsing) return 0;
    return LINK(scan_count, ());
}

int lw_scan_get(int i, lw_net *out)
{
    lw_net_info in;
    if (!S.open || !S.browsing) return -1;
    if (LINK(scan_get, (i, &in)) != 0) return -1;
    memset(out, 0, sizeof(*out));
    out->key = in.key;
    memcpy(out->host, in.host, sizeof(out->host) < sizeof(in.host) ? sizeof(out->host) : sizeof(in.host));
    out->host[LW_NAME_MAX - 1] = 0;
    out->players = in.players;
    out->max = in.max;
    out->len = in.len > LW_MAX_BEACON ? LW_MAX_BEACON : in.len;
    memcpy(out->data, in.data, (size_t)out->len);
    return 0;
}

int lw_join(uint32_t key)
{
    char err[160] = "";
    if (!S.open || !S.browsing) return -1;
    if (LINK(join, (key, err, sizeof(err))) != 0) {
        snprintf(S.error, sizeof(S.error), "%s", err[0] ? err : "Could not join the game");
        return -1;
    }
    S.browsing = 0;
    S.joining = 1;
    S.join_us = lw_platform_time_us();
    return 0;
}

void lw_host_set_beacon(const uint8_t *blob, int len)
{
    if (!S.open || !S.is_host) return;
    if (len < 0) len = 0;
    LINK(set_beacon, (blob, len));
}
