/* Group play's network side (see group_session.h): the localwireless module carrying a Melee lobby and lockstep
 * matches. The module moves bytes: one 8-byte pad per console per frame, the lobby blob (group_lobby.c), door / Start /
 * stage commands, and the match block as the start blob. Everything Melee-specific stays here and in group_lobby.c. */
#include "group_session.h"
#include "group_bridge.h"
#include "localwireless.h"
#include "lw_platform.h"
#include "../name_text.h"
#include "slippi_plat.h"   /* the port's platform layer (time, lock, thread, log, data folder); nothing else of Slippi */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __3DS__
#include <3ds.h>                 /* frd (the Mii name), cfgu (the system name) */
#elif !defined(_WIN32)
#include <unistd.h>              /* gethostname */
#endif

/* What the localwireless module needs from its host project. */
uint64_t lw_platform_time_us(void) { return sp_time_us(); }
uint32_t lw_platform_random(void) { return sp_random(); }
int lw_platform_net_init(void) { return sp_net_init(); }
void lw_platform_log(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sp_log("%s", line);
}

#define PAD_BYTES 8
#define FRAME_US 16683
enum { ST_IDLE = 0, ST_SEARCHING = 1, ST_CONNECTED = 3, ST_FAILED = 4, ST_DISCONNECTED = 5 };   /* slippi_net_bridge.h */

static struct {
    int open, status, host, automatic, my_port;
    char error[160];
    group_config cfg;
    int thread_running;
    volatile int thread_stop;
    /* the lobby: the host's state (host) and what everyone shows */
    lobby_host host_lobby;
    lobby_state view;
    int view_valid;
    unsigned view_version;
    int view_since_end;            /* a lobby snapshot arrived after this console's match ended (the host's own: always) */
    int late;                      /* this console arrived during a match it is not in: it watches until the lobby reopens
                                    * (also through the results, even on the port of a player who dropped out of it) */
    int view_held;                 /* tests: a joiner keeps its lobby view as it is (group_test_hold_view) */
    int match_pending;            /* host: the stage was picked; start the game once the module is free */
    int sudden_pending;           /* host: the last match was tied; start its sudden death once the module is free */
    unsigned char last_block[LOBBY_BLOCK_SIZE];   /* host: the block of the last game started */
    int last_block_ok;
    /* automatic sessions (tests) */
    int auto_joined, auto_started, auto_stage;
    /* the lockstep frame driver */
    int last_queued_frame, stall_frames;
    uint64_t stall_since_us;
    /* the host's Mii for the beacon (read once per session) and the beacon last sent */
    uint8_t mii[LOBBY_MII_BYTES];
    int has_mii;
    uint8_t beacon[LOBBY_BEACON_MAX];
    int beacon_len;
} G;

/* ---- settings ---- */

int group_ini_int(const char *text, const char *key, int def)
{
    size_t kl = strlen(key);
    for (const char *line = text; line && *line;) {
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') ++line;
        if (!strncmp(line, key, kl) && line[kl] == '=') return atoi(line + kl + 1);
        line = strchr(line, '\n');
    }
    return def;
}

void group_ini_str(const char *text, const char *key, char *out, int len)
{
    size_t kl = strlen(key);
    if (len <= 0) return;
    out[0] = 0;
    for (const char *line = text; line && *line;) {
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') ++line;
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            const char *v = line + kl + 1;
            int n = 0;
            while (v[n] && v[n] != '\r' && v[n] != '\n' && n < len - 1) ++n;
            while (n > 0 && (v[n] & 0xC0) == 0x80 && v[n] != '\r' && v[n] != '\n') --n;   /* not inside a character */
            memcpy(out, v, (size_t)n);
            out[n] = 0;
            return;
        }
        line = strchr(line, '\n');
    }
}

void group_config_load(group_config *c)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/group.ini", sp_data_dir());
    char *t = sp_read_file(path, NULL);
    const char *s = t ? t : "";
    c->menu = group_ini_int(s, "menu", 0);
    c->top_menu = group_ini_int(s, "top_menu", 0);
    c->delay = group_ini_int(s, "delay", 2);
    if (c->delay < 1) c->delay = 1;
    if (c->delay > 15) c->delay = 15;
    c->timeout_ms = group_ini_int(s, "timeout_ms", 2000);
    if (c->timeout_ms < 200) c->timeout_ms = 200;
    c->udp = group_ini_int(s, "udp", 0);
    c->port = group_ini_int(s, "port", 0);
    c->drop_pct = group_ini_int(s, "drop_pct", 0);
    c->net_thread = group_ini_int(s, "net_thread", 1);
    c->net_thread_core = group_ini_int(s, "net_thread_core", -2);
    c->auto_players = group_ini_int(s, "auto_players", 2);
    c->test_wireless_off = group_ini_int(s, "test_wireless_off", 0);
    c->test_screen_name_fail = group_ini_int(s, "test_screen_name_fail", 0);
    {
        char raw[64];
        group_ini_str(s, "test_screen_name", raw, sizeof(raw));
        name_clean(raw, c->test_screen_name, sizeof(c->test_screen_name), NAME_MAX_CHARS);
    }
    {
        char raw[64];
        group_ini_str(s, "name", raw, sizeof(raw));
        name_clean(raw, c->name, sizeof(c->name), NAME_MAX_CHARS);
    }
    c->test_mii_fail = group_ini_int(s, "test_mii_fail", 0);
    c->test_mii_corrupt = group_ini_int(s, "test_mii_corrupt", 0);
    {
        char hex[2 * LOBBY_MII_BYTES + 2];
        group_ini_str(s, "test_mii", hex, sizeof(hex));
        c->test_mii_ok = (int)strlen(hex) == 2 * LOBBY_MII_BYTES;
        for (int i = 0; c->test_mii_ok && i < LOBBY_MII_BYTES; ++i) {
            unsigned v;
            if (sscanf(hex + 2 * i, "%2x", &v) != 1) c->test_mii_ok = 0;
            c->test_mii[i] = (uint8_t)v;
        }
    }
    free(t);
}

/* ---- this console's name ---- */

/* The Mii name (the friend list's screen name). 1 with a name, 0 when there is none or the call failed. */
static int mii_name(const group_config *c, char *out, int cap)
{
    out[0] = 0;
    if (c->test_screen_name_fail) return 0;
    if (c->test_screen_name[0]) { snprintf(out, (size_t)cap, "%s", c->test_screen_name); return 1; }
#ifdef __3DS__
    MiiScreenName n;
    memset(n, 0, sizeof(n));
    if (R_FAILED(frdInit(true))) return 0;
    Result r = FRD_GetMyScreenName(&n);
    frdExit();
    if (R_FAILED(r)) return 0;
    return name_from_utf16(n, NAME_MAX_CHARS, out, cap, NAME_MAX_CHARS) > 0;
#else
    return 0;                                     /* the PC tools have no Friends service */
#endif
}

/* The system's user name (3DS: the config block 0x000A0000; PC: the host name). */
static int system_name(char *out, int cap)
{
    out[0] = 0;
#ifdef __3DS__
    u16 blk[14];
    memset(blk, 0, sizeof(blk));
    if (R_FAILED(cfguInit())) return 0;
    Result r = CFGU_GetConfigInfoBlk2(0x1C, 0x000A0000, blk);
    cfguExit();
    if (R_FAILED(r)) return 0;
    return name_from_utf16(blk, NAME_MAX_CHARS, out, cap, NAME_MAX_CHARS) > 0;
#elif defined(_WIN32)
    return 0;
#else
    char h[256];
    if (gethostname(h, sizeof(h)) != 0) return 0;
    h[sizeof(h) - 1] = 0;
    return name_clean(h, out, cap, NAME_MAX_CHARS) > 0;
#endif
}

/* This console's name, the first of: group.ini name= (tests), the Mii name, the system name, "3DS". Both links show the
 * same one (UDS: in the node info; the loopback link: in its handshake). */
static void pick_name(const group_config *c, char *out, int cap)
{
    const char *from;
    if (c->name[0]) { snprintf(out, (size_t)cap, "%s", c->name); from = "ini"; }
    else if (mii_name(c, out, cap)) from = "Mii";
    else if (system_name(out, cap)) from = "system";
    else { snprintf(out, (size_t)cap, "3DS"); from = "default"; }
    sp_log("group: name from %s \"%s\"", from, out);
}

/* ---- this console's Mii ---- */

/* The host's Mii as CFLStoreData, for the beacon (browsers draw its head). 1 with a valid one. */
static int my_mii(const group_config *c, uint8_t *out)
{
    memset(out, 0, LOBBY_MII_BYTES);
    if (c->test_mii_fail) { sp_log("group: Mii skipped (test_mii_fail)"); return 0; }
    if (c->test_mii_ok) memcpy(out, c->test_mii, LOBBY_MII_BYTES);
#ifdef __3DS__
    else {
        FriendMii m;
        _Static_assert(sizeof(m) == LOBBY_MII_BYTES, "FriendMii is CFLStoreData");
        memset(&m, 0, sizeof(m));
        if (R_FAILED(frdInit(true))) { sp_log("group: no Mii (frd)"); return 0; }
        Result r = FRD_GetMyMii(&m);
        frdExit();
        if (R_FAILED(r)) { sp_log("group: no Mii (FRD_GetMyMii 0x%08lx)", (unsigned long)r); return 0; }
        memcpy(out, &m, LOBBY_MII_BYTES);
    }
#else
    else return 0;                                /* the PC tools have no Friends service */
#endif
    /* a Mii whose CRC fails (or an empty one) counts as none: no head anywhere, the Host button just says HOST GAME
     * (owner, 2026-11-25); it is not resealed */
    if (!lobby_mii_ok(out)) { sp_log("group: no Mii (empty, or its CRC fails)"); return 0; }
    {
        char hex[2 * LOBBY_MII_BYTES + 1];
        for (int i = 0; i < LOBBY_MII_BYTES; ++i) snprintf(hex + 2 * i, 3, "%02x", out[i]);
        sp_log("group: Mii for the beacon %s", hex);
    }
    return 1;
}

/* ---- the lobby (lock held) ---- */

/* The host's lobby state goes to every console, and into the beacon (so a console browsing sees who is here). */
static void lobby_publish(void)
{
    uint8_t b[LW_MAX_LOBBY];
    int n = lobby_encode(&G.host_lobby.st, b, sizeof(b));
    if (n <= 0) return;
    lw_host_set_lobby(b, n);
    uint8_t beacon[LOBBY_BEACON_MAX];
    uint8_t mii[LOBBY_MII_BYTES];
    memcpy(mii, G.mii, sizeof(mii));
    if (G.cfg.test_mii_corrupt) mii[0x20] ^= 0x5A;   /* tests: after the CRC, so browsers must turn it down */
    int bn = lobby_encode_beacon(&G.host_lobby.st, G.has_mii ? mii : NULL, beacon, sizeof(beacon));
    if (bn > 0 && (bn != G.beacon_len || memcmp(beacon, G.beacon, (size_t)bn))) {   /* only when it changes */
        memcpy(G.beacon, beacon, (size_t)bn);
        G.beacon_len = bn;
        lw_host_set_beacon(beacon, bn);
    }
    G.view = G.host_lobby.st;
    G.view_valid = 1;
}

/* Host: the names of the consoles in the match that just began, so the lobby keeps the name of one that leaves. */
static void note_match_names(void)
{
    for (int i = 0; i < LOBBY_SLOTS; ++i)
        lobby_host_name(&G.host_lobby, i, (G.host_lobby.st.played >> i & 1) ? lw_player_name(i) : "");
}

/* Host: the stage is picked, so build the block from the lobby and start the game (once the module is free). */
static int lobby_start_match(void)
{
    unsigned char blk[LOBBY_BLOCK_SIZE];
    lobby_build_block(blk, &G.host_lobby.st, sp_random() & 0xFFFF, G.cfg.delay);
    if (lw_host_start(blk, sizeof(blk)) != 0) return 0;        /* the last game is still ending; try again */
    memcpy(G.last_block, blk, sizeof(blk));
    G.last_block_ok = 1;
    lobby_host_match_began(&G.host_lobby, 0);
    note_match_names();
    sp_log("group: starting a match: stage 0x%02x, %d consoles present", G.host_lobby.st.stage, __builtin_popcount(lw_present()));
    lobby_publish();
    return 1;
}

/* Host: a sudden death after a tie: the last game's block marked LOBBY_BLOCK_SUDDEN (every console turns it into the
 * sudden death setup from its own copy of the tied result, which lockstep makes identical), a new rng offset, and only
 * the consoles that played that game. */
static int sudden_start(void)
{
    unsigned char blk[LOBBY_BLOCK_SIZE];
    unsigned rng = sp_random() & 0xFFFF;
    memcpy(blk, G.last_block, sizeof(blk));
    blk[LOBBY_BLOCK_SUDDEN] = 1;
    blk[0x138] = (unsigned char)(rng >> 24); blk[0x139] = (unsigned char)(rng >> 16);
    blk[0x13A] = (unsigned char)(rng >> 8); blk[0x13B] = (unsigned char)rng;
    if (lw_host_start_again(blk, sizeof(blk)) != 0) return 0;  /* the last game is still ending; try again */
    memcpy(G.last_block, blk, sizeof(blk));
    lobby_host_match_began(&G.host_lobby, 1);
    note_match_names();
    G.host_lobby.st.mode = 1;
    sp_log("group: starting a sudden death");
    lobby_publish();
    return 1;
}

static void host_apply(int flags)
{
    if (flags & LOBBY_START_MATCH) G.match_pending = 1;
    if (flags & LOBBY_CHANGED) lobby_publish();
}

/* base: the door's kind as this console last had it, plus 1 (0 not known), see lobby_cmd_door */
static void set_door_base(int port, const lobby_slot *l, int base)
{
    if (G.host) {
        int from = G.my_port < 0 ? 0 : G.my_port;
        int r = lobby_host_edit_door(&G.host_lobby, from, port, l, base);
        if (!r && l->kind != G.host_lobby.st.slot[port].kind)
            sp_log("group: door %d kind change refused (in use on another console, or an older view)", port + 1);
        host_apply(r);
        return;
    }
    uint8_t c[LW_MAX_COMMAND];
    lw_send_command(c, lobby_cmd_door(c, port, l, base));
}

static void set_door(int port, const lobby_slot *l) { set_door_base(port, l, 0); }

/* An automatic session's host starts by itself once auto_players consoles are here and every door is ready. */
static void auto_host(void)
{
    lobby_state *s = &G.host_lobby.st;
    if (G.auto_started) return;
    if (s->phase == LOBBY_CSS && __builtin_popcount(lw_present()) >= G.cfg.auto_players && lobby_can_start(s)) {
        int all = 1;
        for (int i = 0; i < LOBBY_SLOTS; ++i)
            if ((lw_present() >> i & 1) && s->slot[i].chosen != 1) all = 0;
        if (all) host_apply(lobby_host_start(&G.host_lobby, 0));
    }
    if (s->phase == LOBBY_STAGE && !s->stage_picked) {
        host_apply(lobby_host_pick_stage(&G.host_lobby, G.auto_stage, 0));
        G.auto_started = 1;
    }
}

static void lobby_service(void)
{
    if (G.host) {
        int flags = lobby_host_sync(&G.host_lobby, lw_present());
        uint8_t cmd[LW_MAX_COMMAND];
        int from, n;
        while ((n = lw_poll_command(&from, cmd, sizeof(cmd))) > 0) {
            int r = lobby_host_command(&G.host_lobby, from, cmd, n);
            if (!r && n >= 17 && cmd[0] == LCMD_DOOR && cmd[1] < LOBBY_SLOTS && cmd[2] != G.host_lobby.st.slot[cmd[1]].kind &&
                G.host_lobby.st.phase == LOBBY_CSS)
                sp_log("group: door %d kind change from port %d refused (in use on another console, or an older view)",
                       cmd[1] + 1, from + 1);
            flags |= r;
        }
        host_apply(flags);
        if (G.automatic) auto_host();
        if (G.sudden_pending) {
            G.match_pending = 0;                           /* nothing else starts before the sudden death */
            if (sudden_start()) G.sudden_pending = 0;
        } else if (G.match_pending && lobby_start_match()) {
            G.match_pending = 0;
        }
        if (!G.view_valid) lobby_publish();
    } else if (!(G.view_held && G.view_valid)) {
        unsigned v = lw_lobby_version();
        if (v != G.view_version || !G.view_valid) {
            uint8_t b[LW_MAX_LOBBY];
            int n = lw_get_lobby(b, sizeof(b));
            if (n > 0 && lobby_decode(&G.view, b, n) == 0) G.view_valid = 1;
            if (v != G.view_version) G.view_since_end = 1;
            G.view_version = v;
        }
    }
}

/* ---- the session ---- */

static void fail(int status, const char *text)
{
    G.status = status;
    snprintf(G.error, sizeof(G.error), "%s", text && text[0] ? text : "Local wireless failed");
    sp_log("group: %s", G.error);
}

static void service(void)
{
    if (!G.open || (G.status != ST_SEARCHING && G.status != ST_CONNECTED)) return;
    int st = lw_poll();
    if (st == LW_FAILED) { fail(ST_FAILED, lw_error()); return; }
    if (st == LW_DISCONNECTED) { fail(ST_DISCONNECTED, lw_error()); return; }
    if (st == LW_SEARCHING && G.automatic && !G.host && !G.auto_joined && lw_browsing() && lw_scan_count() > 0) {
        lw_net n;
        if (lw_scan_get(0, &n) == 0 && lw_join(n.key) == 0) {
            G.auto_joined = 1;
            sp_log("group: joining the game of \"%s\"", n.host);
        }
    }
    if (st != LW_CONNECTED) return;
    if (G.status == ST_SEARCHING) {
        G.my_port = lw_my_index();
        G.status = ST_CONNECTED;
        sp_log("group: in the lobby on port %d%s", G.my_port + 1, G.host ? " (host)" : "");
    }
    lobby_service();
}

static void worker(void *arg)
{
    (void)arg;
    while (!G.thread_stop) {
        sp_lock();
        service();
        sp_unlock();
        sp_sleep_us(2000);
    }
}

/* Called without the lock: the worker needs it to finish its last service. */
static void stop_thread(void)
{
    if (G.thread_running) {
        G.thread_stop = 1;
        sp_thread_join();
        G.thread_running = 0;
    }
}

int group_session_start(int host, int automatic)
{
    char err[160] = "";
    group_session_stop();
    sp_lock_init();
    sp_lock();
    memset(&G, 0, sizeof(G));
    group_config_load(&G.cfg);
    G.host = host;
    G.automatic = automatic;
    G.my_port = host ? 0 : -1;
    G.auto_stage = 0x1F;                      /* Battlefield, unless the host's engine says otherwise */
    lw_config c;
    memset(&c, 0, sizeof(c));
    c.host = host;
    c.input_size = PAD_BYTES;
    c.frame_us = FRAME_US;
    c.timeout_ms = G.cfg.timeout_ms;
    c.use_udp = G.cfg.udp;
#ifndef __3DS__
    c.use_udp = 1;                            /* PC tools: there is no UDS */
#endif
    c.port = G.cfg.port;
    c.test_drop_pct = G.cfg.drop_pct;
    c.test_wireless_off = G.cfg.test_wireless_off;
    pick_name(&G.cfg, c.name, sizeof(c.name));
    if (host) lobby_host_init(&G.host_lobby, 1);
    G.has_mii = my_mii(&G.cfg, G.mii);            /* a host sends it; a console browsing shows it on its Host button */
    if (G.has_mii && G.cfg.test_mii_corrupt) sp_log("group: Mii corrupted in the beacon for the test (test_mii_corrupt)");
    sp_log("group: %s (delay %d)", host ? "hosting a game" : "looking for games", G.cfg.delay);
    if (lw_open(&c, err, sizeof(err)) != 0) {
        fail(ST_FAILED, err[0] ? err : "Local wireless is unavailable");
        sp_unlock();
        return -1;
    }
    G.open = 1;
    G.status = ST_SEARCHING;
    int want_thread = G.cfg.net_thread, core = G.cfg.net_thread_core;
    sp_unlock();
    if (want_thread) {
        G.thread_stop = 0;
        if (sp_thread_start(worker, NULL, core) == 0) G.thread_running = 1;
        else sp_log("group: no network thread; servicing from the engine's polls");
    }
    return 0;
}

void group_session_stop(void)
{
    stop_thread();
    sp_lock_init();
    sp_lock();
    if (G.open) {
        lw_close();
        sp_log("group: session closed");
    }
    G.open = 0;
    G.view_valid = 0;
    G.match_pending = 0;
    if (G.status == ST_SEARCHING || G.status == ST_CONNECTED) G.status = ST_IDLE;
    sp_unlock();
    sp_log_drain();
}

int group_session_open(void) { return G.open; }

int group_my_mii(uint8_t *out)
{
    sp_lock();
    int has = G.open && G.has_mii;
    if (has) memcpy(out, G.mii, LOBBY_MII_BYTES);
    sp_unlock();
    return has;
}
int group_is_host(void) { return G.host; }
int group_my_port(void) { return G.my_port < 0 ? 0 : G.my_port; }

/* ---- browsing ---- */

int group_scan(group_game *out, int max)
{
    int n = 0;
    sp_lock();
    if (G.open && !G.host) {
        int count = lw_scan_count();
        for (int i = 0; i < count && n < max; ++i) {
            lw_net net;
            if (lw_scan_get(i, &net) != 0) break;
            group_game *g = &out[n++];
            memset(g, 0, sizeof(*g));
            g->key = net.key;
            snprintf(g->host, sizeof(g->host), "%s", net.host);
            g->players = net.players;
            g->max = net.max;
            if (lobby_decode_beacon(&g->beacon, g->mii, &g->has_mii, net.data, net.len) != 0) {
                memset(&g->beacon, 0, sizeof(g->beacon));
                for (int k = 0; k < LOBBY_SLOTS; ++k) g->beacon.slot[k].kind = LOBBY_EMPTY;
            }
            g->mii_crc = g->has_mii ? (uint16_t)(g->mii[LOBBY_MII_BYTES - 2] << 8 | g->mii[LOBBY_MII_BYTES - 1]) : 0;
        }
    }
    sp_unlock();
    return n;
}

int group_join(uint32_t key)
{
    sp_lock();
    int r = G.open && !G.host ? lw_join(key) : -1;
    if (r != 0) snprintf(G.error, sizeof(G.error), "%s", lw_error()[0] ? lw_error() : "Could not join the game");
    sp_unlock();
    return r;
}

/* ---- the lobby, natively ---- */

void group_test_hold_view(int hold)
{
    sp_lock();
    G.view_held = hold;
    sp_unlock();
}

int group_view_since_game_end(void)
{
    sp_lock();
    int r = G.open && (G.host || G.view_since_end);
    sp_unlock();
    return r;
}

int group_lobby_view(lobby_state *out)
{
    sp_lock();
    int ok = G.open && G.view_valid;
    if (ok && out) *out = G.view;
    sp_unlock();
    return ok;
}

unsigned group_present(void) { sp_lock(); unsigned p = G.open ? lw_present() : 0; sp_unlock(); return p; }

/* A match runs that this console is not in, or the results screen of one is up (lock held). */
static int watching_locked(void)
{
    if (!G.open || G.status != ST_CONNECTED) return 0;
    if (lw_match_running() && !lw_in_match()) { G.late = 1; return 1; }
    if (G.late) {
        if (!G.view_valid || G.view.phase != LOBBY_CSS) return 1;   /* the match is over, its results not yet */
        G.late = 0;
    }
    return G.view_valid && G.view.phase == LOBBY_RESULTS && G.my_port >= 0 && !(G.view.played >> G.my_port & 1);
}

int group_watching(void)
{
    sp_lock();
    int w = watching_locked();
    sp_unlock();
    return w;
}

#define STATUS_US 250000ull                    /* how often the match status goes out */

void group_match_status(const lobby_status *st)
{
    static uint64_t last_us;
    uint64_t now = sp_time_us();
    if (!G.open || !G.host || now - last_us < STATUS_US) return;   /* cheap checks first: called every frame */
    sp_lock();
    if (G.open && G.host && G.status == ST_CONNECTED && lw_match_running()) {
        last_us = now;
        host_apply(lobby_host_set_status(&G.host_lobby, st));
    }
    sp_unlock();
}
int group_signal(int port) { sp_lock(); int s = G.open ? lw_signal(port) : 0; sp_unlock(); return s; }

void group_name(int port, char *out, int len)
{
    if (len <= 0) return;
    sp_lock();
    snprintf(out, (size_t)len, "%s", G.open ? lw_player_name(port) : "");
    sp_unlock();
}

/* ---- the lobby, for the engine ---- */

int group_lobby_get(MpGroupLobby *out)
{
    memset(out, 0, sizeof(*out));
    sp_lock();
    if (!G.open || !G.view_valid) { sp_unlock(); return 0; }
    const lobby_state *v = &G.view;
    out->valid = 1;
    out->me = (unsigned char)(G.my_port < 0 ? 0 : G.my_port);
    out->host = (unsigned char)G.host;
    out->present = (unsigned char)lw_present();
    out->busy = v->busy;
    out->in_use = v->in_use;
    memcpy(out->busy_why, v->busy_why, sizeof(out->busy_why));
    out->watching = (unsigned char)watching_locked();
    out->played = v->played; out->ready = v->ready; out->advance = v->advance;
    out->since_end = (unsigned char)(G.host || G.view_since_end);
    memcpy(out->stars, v->stars, sizeof(out->stars));
    out->phase = v->phase; out->starter = v->starter; out->stage_picked = v->stage_picked; out->mode = v->mode;
    out->stage_hi = (unsigned char)(v->stage >> 8); out->stage_lo = (unsigned char)v->stage; out->alt_stage = v->alt_stage;
    out->rules.time_mode = v->rules.time_mode; out->rules.stocks = v->rules.stocks; out->rules.minutes = v->rules.minutes;
    out->rules.items = v->rules.items; out->rules.teams = v->rules.teams; out->rules.friendly_fire = v->rules.friendly_fire;
    out->rules.melee_valid = v->rules.melee_valid;
    memcpy(out->rules.melee, v->rules.melee, sizeof(out->rules.melee));
    out->rules.mode = v->rules.mode; out->rules.time_limit = v->rules.time_limit;
    out->rules.stock_count = v->rules.stock_count; out->rules.stock_time_limit = v->rules.stock_time_limit;
    out->rules.handicap = v->rules.handicap;
    for (int i = 0; i < 4; ++i) {
        const lobby_slot *l = &v->slot[i];
        MpGroupDoor *d = &out->door[i];
        out->signal[i] = (unsigned char)lw_signal(i);
        d->kind = l->kind; d->fighter = l->fighter; d->color = l->color; d->team = l->team; d->level = l->level; d->chosen = l->chosen;
        memcpy(d->tag, l->tag, LOBBY_TAG_BYTES);
        d->handicap = l->handicap;
    }
    sp_unlock();
    return 1;
}

void group_lobby_set_door(int port, const MpGroupDoor *d)
{
    lobby_slot l;
    memset(&l, 0, sizeof(l));
    l.kind = d->kind; l.fighter = d->fighter; l.color = d->color; l.team = d->team; l.level = d->level; l.chosen = d->chosen;
    memcpy(l.tag, d->tag, LOBBY_TAG_BYTES);
    l.handicap = d->handicap;
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) set_door_base(port, &l, d->spare);   /* spare: the base kind + 1 */
    sp_unlock();
}

void group_lobby_start(void)
{
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) {
        if (G.host) host_apply(lobby_host_start(&G.host_lobby, 0));
        else { uint8_t c[LW_MAX_COMMAND]; lw_send_command(c, lobby_cmd_start(c)); }
    }
    sp_unlock();
}

void group_lobby_stage(int stage, int alt_stage)
{
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) {
        if (G.host) host_apply(lobby_host_pick_stage(&G.host_lobby, stage, alt_stage));
        else { uint8_t c[LW_MAX_COMMAND]; lw_send_command(c, lobby_cmd_stage(c, stage, alt_stage)); }
    }
    sp_unlock();
}

void group_lobby_back(void)
{
    uint8_t c[LW_MAX_COMMAND];
    int n = lobby_cmd_back(c);
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) {
        if (G.host) host_apply(lobby_host_command(&G.host_lobby, 0, c, n));
        else lw_send_command(c, n);
    }
    sp_unlock();
}

void group_lobby_busy(int busy)
{
    uint8_t c[LW_MAX_COMMAND];
    int n = lobby_cmd_busy(c, busy & 0xF, (busy >> 4) & 0xF);   /* bits 0..3: why (LOBBY_BUSY_*); 4..7: the doors in use */
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) {
        if (G.host) host_apply(lobby_host_command(&G.host_lobby, G.my_port < 0 ? 0 : G.my_port, c, n));
        else lw_send_command(c, n);
    }
    sp_unlock();
}

void group_lobby_rules(const MpGroupRules *r)
{
    lobby_rules x;
    memset(&x, 0, sizeof(x));
    x.time_mode = r->time_mode; x.stocks = r->stocks; x.minutes = r->minutes; x.items = r->items; x.teams = r->teams;
    x.friendly_fire = r->friendly_fire;
    x.melee_valid = r->melee_valid ? 1 : 0;
    memcpy(x.melee, r->melee, LOBBY_MELEE_RULES);
    x.mode = r->mode; x.time_limit = r->time_limit; x.stock_count = r->stock_count;
    x.stock_time_limit = r->stock_time_limit; x.handicap = r->handicap;
    sp_lock();
    if (G.open && G.host) host_apply(lobby_host_set_rules(&G.host_lobby, &x));   /* only the host sets the rules */
    sp_unlock();
}

/* Built with --no-door-names (MP_GROUP_NO_DOOR_NAMES): the character select draws nothing under its doors. */
int group_door_names(void)
{
#ifdef MP_GROUP_NO_DOOR_NAMES
    return 0;
#else
    return 1;
#endif
}

void group_menu_sticks(int x, int y, signed char *out)
{
    uint8_t mine[2] = {(uint8_t)(int8_t)x, (uint8_t)(int8_t)y}, v[LW_MAX_MENU];
    memset(out, 0, 8);
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) {
        lw_menu_input_set(mine, 2);
        for (int k = 0; k < 4; ++k)
            if (lw_menu_input_get(k, v)) { out[2 * k] = (signed char)v[0]; out[2 * k + 1] = (signed char)v[1]; }
    }
    sp_unlock();
}

int group_players(void) { sp_lock(); int n = G.open ? lw_players() : 0; sp_unlock(); return n; }

int group_player_name(int port, char *buf, int len)
{
    if (len <= 0) return 0;
    group_name(port, buf, len);
    return (int)strlen(buf);
}

/* The match is over on this console: its results screen follows (group_results_end reopens the lobby). */
void group_game_end(void)
{
    sp_lock();
    if (G.open) {
        lw_game_end();
        if (G.host && lobby_host_results_begin(&G.host_lobby)) lobby_publish();
        /* A joiner's lobby view may still be from before the match (snapshots are not sent during it, and a newer one
         * replaces any it missed): only one that arrives from now on tells where the host is. Snapshots arrive in
         * order, and the host published its match phase before this match began, so none older comes after. */
        G.view_since_end = G.host;
        G.last_queued_frame = 0;
        G.stall_frames = 0;
    }
    sp_unlock();
}

void group_results_press(int ready, int advance)
{
    sp_lock();
    if (G.open && G.status == ST_CONNECTED) {
        if (G.host) host_apply(lobby_host_results(&G.host_lobby, G.my_port < 0 ? 0 : G.my_port, ready, advance));
        else { uint8_t c[LW_MAX_COMMAND]; lw_send_command(c, lobby_cmd_results(c, ready, advance)); }
    }
    sp_unlock();
}

void group_results_end(const unsigned char *stars, const unsigned char *handicap)
{
    sp_lock();
    if (G.open && G.host) {
        host_apply(lobby_host_results_over(&G.host_lobby, stars, handicap));
        sp_log("group: the results are over: stars %u/%u/%u/%u", stars[0], stars[1], stars[2], stars[3]);
    }
    sp_unlock();
}

void group_sudden_death(void)
{
    sp_lock();
    if (G.open && G.host && G.last_block_ok) {
        G.sudden_pending = 1;
        sp_log("group: the match was tied: a sudden death follows");
    }
    sp_unlock();
}

/* ---- the online bridge ---- */

int group_net_start(const char *role)
{
    int host = role && !strcmp(role, "@host");
    if (!role || (!host && strcmp(role, "@join"))) {
        snprintf(G.error, sizeof(G.error), "opponent= must be @host or @join");
        G.status = ST_FAILED;
        return -2;
    }
    return group_session_start(host, 1) == 0 ? 0 : -3;
}

int group_net_status(void) { return G.status; }
const char *group_net_error(void) { return G.error; }

void group_net_poll(void)
{
    if (!G.thread_running) {
        sp_lock();
        service();
        sp_unlock();
    }
    sp_log_drain();
}

void group_net_stop(void) { group_session_stop(); }
int group_net_local_index(void) { return group_my_port(); }

/* Automatic sessions: this console takes its door with the engine's configured fighter (a different one per port, so
 * the fighters are not all identical) and the host remembers the stage. */
void group_net_set_selections(int char_id, int color, int stage_id, int stage_selected)
{
    sp_lock();
    if (G.open && G.status == ST_CONNECTED && G.automatic) {
        lobby_slot s;
        memset(&s, 0, sizeof(s));
        s.kind = LOBBY_HUMAN;
        s.fighter = (uint8_t)((char_id + group_my_port()) % 26);
        s.color = (uint8_t)color;
        s.chosen = 1;
        s.level = 3;
        if (G.host && stage_selected) G.auto_stage = stage_id;
        set_door(group_my_port(), &s);
    }
    sp_unlock();
}

int group_net_remote_ready(void) { return G.status == ST_CONNECTED; }

int group_net_match_block(unsigned char *out)
{
    unsigned char blk[LOBBY_BLOCK_SIZE];
    sp_lock();
    int ok = G.open && G.status == ST_CONNECTED && lw_get_start(blk, sizeof(blk)) == (int)sizeof(blk);
    if (ok && out) {
        memcpy(out, blk, sizeof(blk));
        out[0x13C] = (unsigned char)group_my_port();
        sp_log("group: match block: %d consoles, stage 0x%02x%02x, fighters %u/%u/%u/%u, types %u/%u/%u/%u, delay %u%s", lw_players(),
               blk[0xE], blk[0xF], blk[0x60], blk[0x84], blk[0xA8], blk[0xCC], blk[0x61], blk[0x85], blk[0xA9], blk[0xCD], blk[0x13D],
               blk[LOBBY_BLOCK_SUDDEN] ? ", sudden death" : "");
        sp_log("group: match rules %02x %02x %02x %02x, teams %u, items %02x, time limit %u s, damage ratio %02x%02x", blk[0], blk[1],
               blk[2], blk[3], blk[8], blk[0xB], (unsigned)blk[0x12] << 8 | blk[0x13], blk[0x30], blk[0x31]);
    }
    sp_unlock();
    return ok;
}

static void begin_game_locked(void)
{
    lw_game_begin();
    G.last_queued_frame = 0;
    G.stall_frames = 0;
}

void group_net_new_game(void)
{
    sp_lock();
    if (G.open) begin_game_locked();
    sp_unlock();
}

/* Our pad goes to the host; the answer is the host's bundle for this frame (see group_bridge.h). */
int group_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum, const unsigned char *local_pad12,
                          unsigned char *out)
{
    sp_lock();
    if (!G.open || G.status != ST_CONNECTED) { sp_unlock(); return 3; }
    if (frame == 1 && G.last_queued_frame != 1 + delay) begin_game_locked();   /* a new game: frame 1 again */
    if (frame + delay > G.last_queued_frame) {
        /* zero pads for the delay frames at the start (Dolphin handleSendInputs) */
        if (G.last_queued_frame == 0)
            for (int f = 1; f < frame + delay; ++f) lw_submit(f, NULL, 0, 0);
        lw_submit(frame + delay, local_pad12, finalized_frame, checksum);
        G.last_queued_frame = frame + delay;
    }
    uint8_t in[LW_MAX_PLAYERS][LW_MAX_INPUT], mask = 0;
    if (!lw_bundle(frame, in, &mask)) {
        if (G.stall_frames++ == 0) G.stall_since_us = sp_time_us();
        /* backstop, past the host's own drop timeout */
        if (sp_time_us() - G.stall_since_us > 5000000ull + (uint64_t)G.cfg.timeout_ms * 1000ull) {
            sp_log("group: no inputs from the host for too long (frame %d)", frame);
            fail(ST_DISCONNECTED, "The host stopped responding");   /* the same cause as the module's message */
            G.stall_frames = 0;
            sp_unlock();
            return 3;
        }
        sp_unlock();
        return 2;
    }
    G.stall_frames = 0;
    if (out) {
        memset(out, 0, 50);
        for (int i = 0; i < 4; ++i) memcpy(out + 12 * i, in[i], PAD_BYTES);
        out[48] = mask;
        out[49] = (unsigned char)lw_players();
    }
    sp_unlock();
    return 1;
}

int group_net_remote_checksum_frame(void) { sp_lock(); int f = lw_remote_checksum(NULL); sp_unlock(); return f; }
int group_net_remote_checksum(int *unused) { (void)unused; return group_net_remote_checksum_frame(); }
unsigned group_net_remote_checksum_value(void) { uint32_t ck = 0; sp_lock(); lw_remote_checksum(&ck); sp_unlock(); return ck; }
int group_net_ping_ms(void) { sp_lock(); int p = lw_ping_us() / 1000; sp_unlock(); return p; }
int group_net_time_offset_us(void) { return 0; }   /* the star paces everyone by the slowest console; no time sync */
int group_net_stat(int which) { sp_lock(); int v = (int)lw_stat(which); sp_unlock(); return v; }
