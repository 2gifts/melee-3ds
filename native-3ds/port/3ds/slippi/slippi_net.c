/* Slippi network client: lifecycle, service loop, network thread and the
 * native API (slippi_net.h). Matchmaking is in slippi_mm.c, the P2P protocol
 * in slippi_p2p.c and the engine bridge in slippi_bridge.c.
 *
 * Partly a port of Project Slippi's Dolphin (GPL-2.0-or-later) and of Melee
 * Unlocked's port of it (GPL-3.0-or-later); this fork is GPL-3.0. */
#include "slippi_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

slippi_state g_slippi;
static int enet_ready, lock_ready, thread_running;
static volatile int thread_stop;

void slippi_set_status(int status)
{
    if (g_slippi.status != status) sp_log("status %d -> %d", g_slippi.status, status);
    g_slippi.status = status;
}

void slippi_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_slippi.error, sizeof(g_slippi.error), fmt, ap);
    va_end(ap);
    sp_log("error: %s", g_slippi.error);
    slippi_set_status(SLIPPI_STATUS_FAILED);
}

int slippi_create_host(void)
{
    slippi_state *g = &g_slippi;
    slippi_destroy_host();
    for (int retry = 0; !g->host && retry < 15; ++retry) {
        g->host_port = (uint16_t)(g->cfg.local_port > 0 ? g->cfg.local_port : 41000 + (int)(sp_random() % 10000));
        ENetAddress addr;
        memset(&addr, 0, sizeof(addr));
        addr.host = ENET_HOST_ANY;
        addr.port = g->host_port;
        /* Dolphin: MM host (1 peer) then P2P host (10 peers) on the same port; one host here. */
        g->host = enet_host_create(&addr, 10, 3, 0, 0);
    }
    if (!g->host) { sp_log("cannot create an ENet host"); return -1; }
    sp_log("ENet host bound to local port %u", g->host_port);
    return 0;
}

/* Dolphin creates its P2P host only after matchmaking, so an opponent that
 * hears about the match first gets no answer and retries. Here one ENet host
 * serves matchmaking and P2P, so ENet accepts that connection and its
 * selections at once; they are held until the match reply arrives. */
#define EARLY_EVENTS 32
static ENetEvent early[EARLY_EVENTS];
static int early_count;

static void early_event_keep(ENetEvent *ev)
{
    if (early_count == EARLY_EVENTS) {
        sp_log("p2p: early event queue full; dropping event %d", ev->type);
        if (ev->type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev->packet);
        return;
    }
    if (ev->type == ENET_EVENT_TYPE_CONNECT && ev->peer)
        sp_log("p2p: connection from port %u before our match reply; holding it", ev->peer->address.port);
    early[early_count++] = *ev;
}

void slippi_early_events_clear(void)
{
    for (int i = 0; i < early_count; ++i)
        if (early[i].type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(early[i].packet);
    early_count = 0;
}

/* Lock held; called by slippi_p2p_start once the opponent is known. */
void slippi_early_events_replay(void)
{
    slippi_state *g = &g_slippi;
    int n = early_count, used = 0;
    early_count = 0;
    for (int i = 0; i < n; ++i) {
        ENetEvent *ev = &early[i];
        /* A peer that has gone since is reset and its slot may be reused
         * (even by our own new connection): its disconnect is not replayed,
         * nor a connect for a peer that is no longer connected. */
        int stale = ev->type == ENET_EVENT_TYPE_DISCONNECT ||
                    (ev->type == ENET_EVENT_TYPE_CONNECT && ev->peer && ev->peer->state != ENET_PEER_STATE_CONNECTED);
        if (!stale && g->host && ev->peer && ev->peer->address.host == g->remote_addr.host &&
            (g->status == SLIPPI_STATUS_CONNECTING || g->status == SLIPPI_STATUS_CONNECTED)) {
            slippi_p2p_event(ev);   /* destroys a received packet */
            ++used;
        } else if (ev->type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(ev->packet);
        }
    }
    if (n) sp_log("p2p: replayed %d of %d events that arrived before our match reply", used, n);
}

void slippi_destroy_host(void)
{
    slippi_state *g = &g_slippi;
    slippi_early_events_clear();
    if (!g->host) return;
    enet_host_flush(g->host);
    enet_host_destroy(g->host);
    g->host = NULL;
    g->mm_peer = NULL;
    g->primary = NULL;
    memset(g->conns, 0, sizeof(g->conns));
}

/* One service pass, lock held. */
static void service_locked(void)
{
    slippi_state *g = &g_slippi;
    if (!g->initialized) return;
    if (g->mm_state == MM_RESOLVE) slippi_mm_tick();
    if (!g->host) return;
    ENetEvent ev;
    for (int i = 0; i < 64; ++i) {
        int rc = enet_host_service(g->host, &ev, 0);
        if (rc <= 0) {
            if (rc < 0) sp_log("enet_host_service error");
            break;
        }
        /* Connection events, and anything before the match: rare, and the
         * evidence when a first connection goes wrong. */
        if (g->status != SLIPPI_STATUS_CONNECTED && !(ev.type == ENET_EVENT_TYPE_RECEIVE && ev.peer == g->mm_peer))
            sp_log("net: event %d slot %d port %u connectID %u status %d mm %d len %u", ev.type, ev.peer ? (int)(ev.peer - g->host->peers) : -1,
                   ev.peer ? ev.peer->address.port : 0, ev.peer ? (unsigned)ev.peer->connectID : 0, g->status, ev.peer && ev.peer == g->mm_peer,
                   ev.type == ENET_EVENT_TYPE_RECEIVE ? (unsigned)ev.packet->dataLength : 0);
        if (ev.peer && ev.peer == g->mm_peer) slippi_mm_service_event(&ev);
        else if (g->status == SLIPPI_STATUS_CONNECTING || g->status == SLIPPI_STATUS_CONNECTED) slippi_p2p_event(&ev);
        else if (g->status == SLIPPI_STATUS_SEARCHING && ev.peer) early_event_keep(&ev);
        else if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        if (!g->host) return;
    }
    slippi_mm_tick();
    slippi_p2p_tick();
    if (g->host) enet_host_flush(g->host);
}

static void worker_main(void *arg)
{
    (void)arg;
    while (!thread_stop) {
        sp_lock();
        service_locked();
        int interval = g_slippi.cfg.net_thread_interval_us;
        sp_unlock();
        sp_sleep_us((uint32_t)(interval > 0 ? interval : 2000));
    }
}

int slippi_init(void)
{
    slippi_state *g = &g_slippi;
    if (!lock_ready) { sp_lock_init(); lock_ready = 1; }
    sp_lock();
    if (g->initialized) { sp_unlock(); return 0; }
    memset(g, 0, sizeof(*g));
    g->status = SLIPPI_STATUS_UNINITIALIZED;
    slippi_config_defaults(&g->cfg);
    char path[256];
    snprintf(path, sizeof(path), "%s/config.ini", sp_data_dir());
    if (slippi_config_load(&g->cfg, path) == 0) sp_log("config: %s loaded", path);
    else sp_log("config: %s not found, using defaults", path);
    snprintf(path, sizeof(path), "%s/user.json", sp_data_dir());
    char err[160];
    if (slippi_user_load(&g->user, path, err, sizeof(err)) != 0) {
        snprintf(g->error, sizeof(g->error), "%s", err);
        sp_log("error: %s", err);
        sp_unlock();
        return -1;
    }
    sp_log("user: %s (%s), uid %.6s..., play key %u chars", g->user.display_name, g->user.connect_code, g->user.uid,
           (unsigned)strlen(g->user.play_key));
    if (sp_net_init() != 0) { snprintf(g->error, sizeof(g->error), "Network unavailable (socInit failed)"); sp_unlock(); return -2; }
    int wifi = sp_wifi_status();
    if (wifi == 0) sp_log("warning: Wi-Fi reports no connection");
    if (!enet_ready) {
        if (enet_initialize() != 0) { snprintf(g->error, sizeof(g->error), "enet_initialize failed"); sp_unlock(); return -3; }
        enet_ready = 1;
    }
    g->initialized = 1;
    g->status = SLIPPI_STATUS_IDLE;
    sp_log("ready: mm %s:%d, appVersion %s, delay %d, thread %d", g->cfg.mm_host, g->cfg.mm_port, g->cfg.app_version, g->cfg.delay,
           g->cfg.net_thread);
    int start_thread = g->cfg.net_thread && !thread_running;
    int core = g->cfg.net_thread_core;
    sp_unlock();
    if (start_thread) {
        thread_stop = 0;
        if (sp_thread_start(worker_main, NULL, core) == 0) thread_running = 1;
        else sp_log("network thread unavailable; servicing from slippi_poll only");
    }
    return 0;
}

void slippi_shutdown(void)
{
    slippi_state *g = &g_slippi;
    if (!lock_ready) return;
    sp_lock();
    int was = g->initialized;
    if (was && g->host) {
        slippi_p2p_disconnect();
        slippi_mm_close();
        enet_host_flush(g->host);
    }
    sp_unlock();
    if (thread_running) {
        thread_stop = 1;
        sp_thread_join();
        thread_running = 0;
    }
    sp_lock();
    if (was) {
        /* Give the graceful disconnects a moment to leave (ENet sends them on service). */
        if (g->host) {
            ENetEvent ev;
            uint64_t end = sp_time_us() + 200000;
            while (sp_time_us() < end) {
                int rc = enet_host_service(g->host, &ev, 0);
                if (rc > 0 && ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
                if (rc <= 0) sp_sleep_us(5000);
            }
        }
        slippi_destroy_host();
        g->initialized = 0;
        g->status = SLIPPI_STATUS_UNINITIALIZED;
        sp_log("shut down");
    }
    sp_unlock();
    sp_log_drain();
    if (was) {
        if (enet_ready) { enet_deinitialize(); enet_ready = 0; }
        sp_net_exit();
    }
}

int slippi_find_match(const char *code)
{
    slippi_state *g = &g_slippi;
    if (!lock_ready) return -1;
    sp_lock();
    if (!g->initialized) { sp_unlock(); return -1; }
    const char *c = code && code[0] ? code : g->cfg.opponent;
    if (!c[0]) { snprintf(g->error, sizeof(g->error), "No opponent connect code"); g->status = SLIPPI_STATUS_FAILED; sp_unlock(); return -2; }
    slippi_p2p_disconnect();
    slippi_mm_close();
    slippi_destroy_host();
    memset(&g->match, 0, sizeof(g->match));
    g->match.local_index = -1;
    slippi_p2p_clear();
    memset(g->stats, 0, sizeof(g->stats));
    snprintf(g->search_code, sizeof(g->search_code), "%s", c);
    for (char *p = g->search_code; *p; ++p) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    g->search_code_len = slippi_code_to_sjis(g->search_code, g->cfg.code_fullwidth, g->search_code_sjis);
    g->retries = 0;
    g->error[0] = '\0';
    sp_log("find match: Direct, opponent %s (%s encoding, %d bytes)", g->search_code, g->cfg.code_fullwidth ? "full-width" : "ascii",
           g->search_code_len);
    slippi_mm_start();
    sp_unlock();
    return 0;
}

void slippi_poll(void)
{
    if (!lock_ready) return;
    sp_lock();
    service_locked();
    sp_unlock();
    sp_log_drain();
}

int slippi_status(void)
{
    if (!lock_ready) return SLIPPI_STATUS_UNINITIALIZED;
    sp_log_drain();
    return g_slippi.status;
}

int slippi_error(char *buf, int len)
{
    if (!buf || len <= 0) return 0;
    if (!lock_ready) { snprintf(buf, (size_t)len, "not initialised"); return (int)strlen(buf); }
    sp_lock();
    snprintf(buf, (size_t)len, "%s", g_slippi.error);
    sp_unlock();
    return (int)strlen(buf);
}

void slippi_disconnect(void)
{
    if (!lock_ready) return;
    sp_lock();
    slippi_state *g = &g_slippi;
    if (g->initialized) {
        slippi_p2p_disconnect();
        slippi_mm_close();
        if (g->status != SLIPPI_STATUS_FAILED) g->status = SLIPPI_STATUS_IDLE;
        g->mm_state = MM_NONE;
        sp_log("disconnect requested");
    }
    sp_unlock();
}

int slippi_is_decider(void) { return g_slippi.match.is_host; }
int slippi_local_index(void) { return g_slippi.match.local_index; }

int slippi_info(int what, char *buf, int len)
{
    if (!buf || len <= 0) return 0;
    const char *s = "";
    if (lock_ready) sp_lock();
    switch (what) {
        case SLIPPI_INFO_NAME: s = g_slippi.match.remote_name; break;
        case SLIPPI_INFO_CODE: s = g_slippi.match.remote_code; break;
        case SLIPPI_INFO_UID: s = g_slippi.match.remote_uid; break;
        case SLIPPI_INFO_MATCH_ID: s = g_slippi.match.match_id; break;
        case SLIPPI_INFO_LOCAL_NAME: s = g_slippi.user.display_name; break;
        case SLIPPI_INFO_LOCAL_CODE: s = g_slippi.user.connect_code; break;
        default: break;
    }
    snprintf(buf, (size_t)len, "%s", s);
    if (lock_ready) sp_unlock();
    return (int)strlen(buf);
}

int slippi_mm_stages(unsigned char *out, int max)
{
    int n = g_slippi.match.stage_count < max ? g_slippi.match.stage_count : max;
    if (out && n > 0) memcpy(out, g_slippi.match.stages, (size_t)n);
    return n;
}

static void selections_bytes(const slippi_selections *s, unsigned char out[13])
{
    out[0] = s->character_id;
    out[1] = s->character_color;
    out[2] = s->is_character_selected;
    out[3] = s->player_idx;
    out[4] = (unsigned char)(s->stage_id >> 8);
    out[5] = (unsigned char)s->stage_id;
    out[6] = s->is_stage_selected;
    out[7] = (unsigned char)(s->rng_offset >> 24);
    out[8] = (unsigned char)(s->rng_offset >> 16);
    out[9] = (unsigned char)(s->rng_offset >> 8);
    out[10] = (unsigned char)s->rng_offset;
    out[11] = s->team_id;
    out[12] = s->alt_stage_mode;
}

void slippi_set_selections(int character, int color, int stage, int flags)
{
    if (!lock_ready) return;
    sp_lock();
    slippi_state *g = &g_slippi;
    /* CEXISlippi::setMatchSelections + SlippiPlayerSelections::Merge */
    slippi_selections *l = &g->local_sel;
    l->rng_offset = sp_random() % 0xFFFF;
    if (stage >= 0) { l->stage_id = (uint16_t)stage; l->is_stage_selected = 1; l->alt_stage_mode = (uint8_t)(flags >> 8); }
    if (character >= 0) { l->character_id = (uint8_t)character; l->character_color = (uint8_t)color; l->team_id = (uint8_t)flags; l->is_character_selected = 1; }
    l->player_idx = (uint8_t)(g->match.local_index < 0 ? 0 : g->match.local_index);
    if (g->status == SLIPPI_STATUS_CONNECTED) slippi_p2p_send_selections();
    sp_unlock();
}

int slippi_remote_selections(unsigned char out[13])
{
    if (!lock_ready) return 0;
    sp_lock();
    if (out) selections_bytes(&g_slippi.remote_sel, out);
    int n = g_slippi.remote_sel_count;
    sp_unlock();
    return n;
}

int slippi_local_selections(unsigned char out[13])
{
    if (!lock_ready) return 0;
    sp_lock();
    if (out) selections_bytes(&g_slippi.local_sel, out);
    sp_unlock();
    return g_slippi.local_sel.is_character_selected;
}

unsigned slippi_rng_offset(void)
{
    return g_slippi.match.is_host ? g_slippi.local_sel.rng_offset : g_slippi.remote_sel.rng_offset;
}

void slippi_start_game_locked(void)
{
    slippi_p2p_reset_game();
    /* match_info.Reset(): selections are re-sent for every game. */
    slippi_selections *l = &g_slippi.local_sel, *r = &g_slippi.remote_sel;
    l->character_id = l->character_color = l->is_character_selected = l->team_id = 0;
    l->stage_id = 0; l->is_stage_selected = 0; l->rng_offset = 0;
    r->character_id = r->character_color = r->is_character_selected = r->team_id = 0;
    r->stage_id = 0; r->is_stage_selected = 0; r->rng_offset = 0;
}

void slippi_start_game(void)
{
    if (!lock_ready) return;
    sp_lock();
    slippi_start_game_locked();
    sp_unlock();
}

/* Queue one pad at the front (newest). Lock held. */
static void queue_pad(int32_t frame, const unsigned char *pad, int32_t checksum_frame, uint32_t checksum)
{
    slippi_state *g = &g_slippi;
    g->local_head = (g->local_head + 1) % LOCAL_RING;
    if (g->local_count < LOCAL_RING) ++g->local_count;
    g->local[g->local_head].frame = frame;
    g->local[g->local_head].checksum_frame = checksum_frame;
    g->local[g->local_head].checksum = checksum;
    if (pad) memcpy(g->local[g->local_head].pad, pad, SLIPPI_PAD_DATA_SIZE);
    else memset(g->local[g->local_head].pad, 0, SLIPPI_PAD_DATA_SIZE);
    g->last_queued_frame = frame;
}

int slippi_send_pad_locked(int frame, const unsigned char *pad, int checksum_frame, unsigned checksum)
{
    slippi_state *g = &g_slippi;
    if (g->status != SLIPPI_STATUS_CONNECTED) return -1;
    if (pad && frame > g->last_queued_frame) {
        /* Dolphin handleSendInputs: zero pads for the delay frames at the start. */
        if (g->last_queued_frame == 0)
            for (int f = 1; f < frame; ++f) { queue_pad(f, NULL, 0, 0); slippi_p2p_send_pads(); }
        queue_pad(frame, pad, checksum_frame, checksum);
    }
    slippi_p2p_send_pads();
    return 0;
}

int slippi_send_pad(int frame, const unsigned char *pad, int checksum_frame, unsigned checksum)
{
    if (!lock_ready) return -1;
    sp_lock();
    int rc = slippi_send_pad_locked(frame, pad, checksum_frame, checksum);
    sp_unlock();
    return rc;
}

int slippi_remote_pad_locked(int frame, unsigned char *out)
{
    slippi_state *g = &g_slippi;
    if (frame <= 0) return -1;
    if (frame > g->remote_head_frame) return 0;
    int slot = frame & (REMOTE_RING - 1);
    if (g->remote[slot].frame != frame) return -1;
    if (out) memcpy(out, g->remote[slot].pad, SLIPPI_PAD_DATA_SIZE);
    return 1;
}

int slippi_remote_pad(int frame, unsigned char *out)
{
    if (!lock_ready) return -1;
    sp_lock();
    int rc = slippi_remote_pad_locked(frame, out);
    sp_unlock();
    return rc;
}

int slippi_latest_remote_frame(void) { return g_slippi.remote_head_frame; }
int slippi_remote_checksum_frame(void) { return g_slippi.remote_checksum_frame; }
unsigned slippi_remote_checksum(void) { return g_slippi.remote_checksum; }

int slippi_time_offset_us(void)
{
    if (!lock_ready) return 0;
    sp_lock();
    int v = slippi_p2p_time_offset();
    sp_unlock();
    return v;
}

int slippi_ping_us(void) { return (int)g_slippi.ping_us; }

int slippi_chat_poll(void)
{
    if (!lock_ready) return 0;
    sp_lock();
    int id = g_slippi.chat_received;
    g_slippi.chat_received = 0;
    sp_unlock();
    return id;
}

void slippi_send_chat(int message_id)
{
    if (!lock_ready) return;
    sp_lock();
    if (g_slippi.status == SLIPPI_STATUS_CONNECTED) slippi_p2p_send_chat(message_id);
    sp_unlock();
}

int slippi_stat(int which)
{
    slippi_state *g = &g_slippi;
    if (which < 0 || which >= SLIPPI_STAT_COUNT) return 0;
    if (!lock_ready) return 0;
    sp_lock();
    int v = (int)g->stats[which];
    ENetPeer *p = g->primary;
    switch (which) {
        case SLIPPI_STAT_ENET_RTT_MS: v = p ? (int)p->roundTripTime : 0; break;
        case SLIPPI_STAT_ENET_LOSS: v = p ? (int)p->packetLoss : 0; break;   /* ENET_PEER_PACKET_LOSS_SCALE = 65536 */
        case SLIPPI_STAT_PEERS: { v = 0; for (int i = 0; i < MAX_PEERS; ++i) if (g->conns[i].peer && !g->conns[i].disconnected) ++v; break; }
        case SLIPPI_STAT_LAST_RECEIVE_MS: v = g->last_receive_us ? (int)((sp_time_us() - g->last_receive_us) / 1000) : -1; break;
        case SLIPPI_STAT_LOCAL_QUEUE: v = g->local_count; break;
        default: break;
    }
    sp_unlock();
    return v;
}
