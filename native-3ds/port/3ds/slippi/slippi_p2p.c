/* Slippi P2P netplay (port of Dolphin's SlippiNetplayClient for 1v1).
 *
 * Wire formats (SFML packet: big-endian integers, bool = u8):
 *   0x80 pad:   u8 id, s32 frame, u8 player_port, s32 checksum_frame, u32 checksum,
 *               N x 8-byte pads newest first (every frame not yet acked, <= 129)
 *               ENET_PACKET_FLAG_UNSEQUENCED on channel 1
 *   0x81 ack:   u8 id, s32 frame, u8 player_idx          UNSEQUENCED, channel 2
 *   0x82 sel:   u8 id, u8 char, u8 color, u8 char_sel, u8 player_idx, u16 stage,
 *               u8 stage_sel, u32 rng_offset, u8 team, u8 alt_stage_mode   RELIABLE ch 0
 *   0x84 chat:  u8 id, s32 message_id, u8 player_idx                       RELIABLE ch 0
 * Receive semantics, de-duplication of simultaneous connections, ping from acks
 * and the time-offset estimate follow SlippiNetplay.cpp line by line.
 */
#include "slippi_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int same_addr(const ENetPeer *a, const ENetPeer *b) { return a->address.host == b->address.host && a->address.port == b->address.port; }

static slippi_conn *find_conn(ENetPeer *peer)
{
    for (int i = 0; i < MAX_PEERS; ++i)
        if (g_slippi.conns[i].peer == peer) return &g_slippi.conns[i];
    return NULL;
}

static slippi_conn *add_conn(ENetPeer *peer)
{
    slippi_conn *c = find_conn(peer);
    if (c) { c->disconnected = 0; return c; }
    for (int i = 0; i < MAX_PEERS; ++i)
        if (!g_slippi.conns[i].peer) { g_slippi.conns[i].peer = peer; g_slippi.conns[i].disconnected = 0; return &g_slippi.conns[i]; }
    return NULL;
}

static int live_conn_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PEERS; ++i) if (g_slippi.conns[i].peer && !g_slippi.conns[i].disconnected) ++n;
    return n;
}

/* PlayerIdxFromPort: remote index for a global player port. */
static int remote_index(int port) { return port > g_slippi.match.local_index ? port - 1 : port; }

void slippi_p2p_clear(void)
{
    slippi_state *g = &g_slippi;
    memset(g->conns, 0, sizeof(g->conns));
    g->primary = NULL;
    g->p2p_connected = 0;
    g->disconnect_reason = 0;
    memset(&g->local_sel, 0, sizeof(g->local_sel));
    memset(&g->remote_sel, 0, sizeof(g->remote_sel));
    g->remote_sel_count = 0;
    g->chat_received = 0;
    g->ping_us = 0;
    g->offset_count = g->offset_idx = 0;
    slippi_p2p_reset_game();
    memset(g->remote, 0, sizeof(g->remote));
    g->remote_head_frame = 0;
    g->remote_checksum_frame = 0;
    g->remote_checksum = 0;
}

/* StartSlippiGame (minus match_info.Reset, done by the caller). */
void slippi_p2p_reset_game(void)
{
    slippi_state *g = &g_slippi;
    g->has_game_started = 0;
    g->local_count = 0;
    g->local_head = 0;
    g->last_queued_frame = 0;
    g->last_frame_timing.frame = 0;
    g->last_frame_timing.time_us = sp_time_us();
    g->last_frame_acked = 0;
    g->ack_first = g->ack_count = 0;
}

void slippi_p2p_start(void)
{
    slippi_state *g = &g_slippi;
    slippi_p2p_clear();
    if (slippi_parse_addr(g->match.remote_addr, &g->remote_addr) != 0) {
        slippi_fail("Bad opponent address '%s'", g->match.remote_addr);
        return;
    }
    /* SlippiNetplayClient(): player indices of the selections. */
    g->local_sel.player_idx = (uint8_t)g->match.local_index;
    g->remote_sel.player_idx = (uint8_t)(g->match.local_index == 0 ? 1 : 0);
    ENetPeer *peer = enet_host_connect(g->host, &g->remote_addr, 3, 0);
    if (!peer) { slippi_fail("Couldn't create peer"); return; }
    add_conn(peer);
    g->primary = peer;
    enet_host_flush(g->host);
    g->p2p_deadline_us = sp_time_us() + SLIPPI_P2P_TIMEOUT_MS * 1000ull;
    slippi_set_status(SLIPPI_STATUS_CONNECTING);
    sp_log("p2p: connecting to %s from local port %u (player index %d, decider %d)", g->match.remote_addr, g->host_port,
           g->match.local_index, g->match.is_host);
    slippi_early_events_replay();
}

/* Dolphin Send(): pads/acks unsequenced on channel 1, everything else reliable on 0. */
static void send_packet(const uint8_t *data, size_t len)
{
    slippi_state *g = &g_slippi;
    ENetPeer *target = g->primary;
    slippi_conn *c = target ? find_conn(target) : NULL;
    if (c && c->disconnected) {
        /* Dolphin skips a disconnected m_server entry; fall back to a live
         * duplicate connection to the same player instead of going silent. */
        target = NULL;
        for (int i = 0; i < MAX_PEERS; ++i)
            if (g->conns[i].peer && !g->conns[i].disconnected) { target = g->primary = g->conns[i].peer; break; }
    }
    if (!target) return;
    int pad = data[0] == SLIPPI_MSG_PAD || data[0] == SLIPPI_MSG_PAD_ACK;
    ENetPacket *p = enet_packet_create(data, len, pad ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE);
    if (p && enet_peer_send(target, pad ? 1 : 0, p) != 0) enet_packet_destroy(p);
}

static void write_selections(uint8_t out[14], const slippi_selections *s)
{
    out[0] = SLIPPI_MSG_MATCH_SELECTIONS;
    out[1] = s->character_id;
    out[2] = s->character_color;
    out[3] = s->is_character_selected;
    out[4] = s->player_idx;
    out[5] = (uint8_t)(s->stage_id >> 8);
    out[6] = (uint8_t)s->stage_id;
    out[7] = s->is_stage_selected;
    put32(out + 8, s->rng_offset);
    out[12] = s->team_id;
    out[13] = s->alt_stage_mode;
}

void slippi_p2p_send_selections(void)
{
    uint8_t pkt[14];
    write_selections(pkt, &g_slippi.local_sel);
    send_packet(pkt, sizeof(pkt));
    enet_host_flush(g_slippi.host);
    sp_log("p2p: sent selections char %u color %u sel %u stage %u sel %u rng 0x%x", pkt[1], pkt[2], pkt[3],
           g_slippi.local_sel.stage_id, pkt[7], (unsigned)g_slippi.local_sel.rng_offset);
}

void slippi_p2p_send_chat(int id)
{
    uint8_t pkt[6];
    pkt[0] = SLIPPI_MSG_CHAT;
    put32(pkt + 1, (uint32_t)id);
    pkt[5] = (uint8_t)g_slippi.match.local_index;
    send_packet(pkt, sizeof(pkt));
    enet_host_flush(g_slippi.host);
}

/* SendSlippiPad(nullptr) body: prune acked pads and send the rest. */
void slippi_p2p_send_pads(void)
{
    slippi_state *g = &g_slippi;
    if (g->status != SLIPPI_STATUS_CONNECTED || !g->local_count) return;
    int32_t min_ack = INT_MAX;
    if (live_conn_count() > 0) min_ack = g->last_frame_acked;  /* player_active */
    int32_t newest = g->local[g->local_head].frame;
    if (min_ack < newest - SLIPPI_LOCAL_BACKLOG) min_ack = newest - SLIPPI_LOCAL_BACKLOG;
    while (g->local_count) {
        int oldest = (g->local_head - g->local_count + 1 + LOCAL_RING) % LOCAL_RING;
        if (g->local[oldest].frame >= min_ack) break;
        --g->local_count;
    }
    if (!g->local_count) return;

    uint8_t pkt[SLIPPI_PAD_HEADER + (SLIPPI_LOCAL_BACKLOG + 2) * SLIPPI_PAD_DATA_SIZE];
    pkt[0] = SLIPPI_MSG_PAD;
    put32(pkt + 1, (uint32_t)newest);
    pkt[5] = (uint8_t)g->match.local_index;
    put32(pkt + 6, (uint32_t)g->local[g->local_head].checksum_frame);
    put32(pkt + 10, g->local[g->local_head].checksum);
    size_t len = SLIPPI_PAD_HEADER;
    for (int i = 0; i < g->local_count && len + SLIPPI_PAD_DATA_SIZE <= sizeof(pkt); ++i) {
        int idx = (g->local_head - i + LOCAL_RING) % LOCAL_RING;
        memcpy(pkt + len, g->local[idx].pad, SLIPPI_PAD_DATA_SIZE);
        len += SLIPPI_PAD_DATA_SIZE;
    }
    if (g->cfg.test_drop_pct && (int)(sp_random() % 100) < g->cfg.test_drop_pct)
        ++g->stats[SLIPPI_STAT_DROPPED_TEST];   /* test hook: pretend the datagram was lost */
    else {
        send_packet(pkt, len);
        enet_host_flush(g->host);
    }
    ++g->stats[SLIPPI_STAT_PAD_PACKETS_SENT];
    uint64_t t = sp_time_us();
    g->last_send_us = t;
    g->has_game_started = 1;
    g->last_frame_timing.frame = newest;
    g->last_frame_timing.time_us = t;
    /* ack_timers.Push */
    int slot = (g->ack_first + g->ack_count) % ACK_TIMER_RING;
    if (g->ack_count == ACK_TIMER_RING) g->ack_first = (g->ack_first + 1) % ACK_TIMER_RING;
    else ++g->ack_count;
    g->ack_timers[slot].frame = newest;
    g->ack_timers[slot].time_us = t;
    if (g->cfg.log_packets) sp_log("p2p: pad packet frame %d, %d pads", (int)newest, g->local_count);
}

void slippi_p2p_disconnect(void)
{
    slippi_state *g = &g_slippi;
    for (int i = 0; i < MAX_PEERS; ++i)
        if (g->conns[i].peer && !g->conns[i].disconnected) {
            enet_peer_disconnect(g->conns[i].peer, g->disconnect_reason);
            g->conns[i].disconnected = 1;
        }
    if (g->host) enet_host_flush(g->host);
}

int32_t slippi_p2p_time_offset(void)
{
    slippi_state *g = &g_slippi;
    if (!g->offset_count || live_conn_count() == 0) return 0;
    int32_t buf[SLIPPI_LOCKSTEP_INTERVAL];
    int n = g->offset_count;
    memcpy(buf, g->offsets, sizeof(int32_t) * (size_t)n);
    for (int i = 1; i < n; ++i) { /* insertion sort, n <= 30 */
        int32_t v = buf[i];
        int j = i - 1;
        while (j >= 0 && buf[j] > v) { buf[j + 1] = buf[j]; --j; }
        buf[j + 1] = v;
    }
    int off = (int)((1.0f / 3.0f) * n), end = n - off;
    int count = end - off;
    if (count <= 0) return 0;
    int sum = 0;
    for (int k = off; k < end; ++k) sum += buf[k];
    return sum / count;
}

static void on_pad(const uint8_t *d, size_t len, ENetPeer *peer)
{
    slippi_state *g = &g_slippi;
    uint64_t now = sp_time_us();
    if (len < SLIPPI_PAD_HEADER) { sp_log("p2p: pad packet too small (%u)", (unsigned)len); return; }
    int32_t frame = (int32_t)get32(d + 1);
    int port = d[5];
    int32_t checksum_frame = (int32_t)get32(d + 6);
    uint32_t checksum = get32(d + 10);
    if (remote_index(port) != 0) { sp_log("p2p: pad packet with invalid player port %d", port); return; }

    /* Several live connections to the same player: the lower port keeps this one. */
    slippi_conn *self = find_conn(peer);
    int live = 0;
    for (int i = 0; i < MAX_PEERS; ++i)
        if (g->conns[i].peer && !g->conns[i].disconnected && same_addr(g->conns[i].peer, peer)) ++live;
    if (self && !self->disconnected && live > 1 && g->match.local_index < port) {
        g->primary = peer;
        sp_log("p2p: multiple connections to the opponent; keeping one, disconnecting %d", live - 1);
        for (int i = 0; i < MAX_PEERS; ++i) {
            slippi_conn *c = &g->conns[i];
            if (!c->peer || c->peer == peer || c->disconnected || !same_addr(c->peer, peer)) continue;
            enet_peer_disconnect(c->peer, 0);
            c->disconnected = 1;
        }
    }

    /* Time offset sample (CalcTimeOffsetUs input). */
    slippi_timing timing = g->last_frame_timing;
    if (!g->has_game_started) { timing.frame = 0; timing.time_us = now; }
    int64_t opponent_send_time_us = (int64_t)now - (int64_t)(g->ping_us / 2);
    int64_t frame_diff_offset_us = (int64_t)SLIPPI_FRAME_US * (timing.frame - frame);
    int64_t time_offset_us = opponent_send_time_us - (int64_t)timing.time_us + frame_diff_offset_us;
    g->offsets[g->offset_idx] = (int32_t)time_offset_us;
    if (g->offset_count < SLIPPI_LOCKSTEP_INTERVAL) ++g->offset_count;
    g->offset_idx = (g->offset_idx + 1) % SLIPPI_LOCKSTEP_INTERVAL;

    int64_t inputs_to_copy = (int64_t)frame - g->remote_head_frame;
    if (SLIPPI_PAD_HEADER + inputs_to_copy * SLIPPI_PAD_DATA_SIZE > (int64_t)len) {
        sp_log("p2p: pad packet too small for %lld inputs (%u bytes)", (long long)inputs_to_copy, (unsigned)len);
        return;
    }
    if (inputs_to_copy > SLIPPI_MAX_PAD_FRAMES_IN) { sp_log("p2p: pad packet with too many frames (%lld)", (long long)inputs_to_copy); return; }
    ++g->stats[SLIPPI_STAT_PAD_PACKETS_RECEIVED];
    g->last_receive_us = now;
    if (inputs_to_copy > 1 && g->remote_head_frame > 0) ++g->stats[SLIPPI_STAT_PAD_GAPS];
    if (inputs_to_copy <= 0) ++g->stats[SLIPPI_STAT_PAD_PACKETS_STALE];
    for (int64_t i = inputs_to_copy - 1; i >= 0; --i) {
        int32_t f = (int32_t)(frame - i);
        int slot = f & (REMOTE_RING - 1);
        g->remote[slot].frame = f;
        memcpy(g->remote[slot].pad, d + SLIPPI_PAD_HEADER + i * SLIPPI_PAD_DATA_SIZE, SLIPPI_PAD_DATA_SIZE);
    }
    if (inputs_to_copy > 0) g->remote_head_frame = frame;
    g->remote_checksum_frame = checksum_frame;
    g->remote_checksum = checksum;

    if (inputs_to_copy > 0) {
        uint8_t ack[6];
        ack[0] = SLIPPI_MSG_PAD_ACK;
        put32(ack + 1, (uint32_t)frame);
        ack[5] = (uint8_t)g->match.local_index;
        ENetPacket *p = enet_packet_create(ack, sizeof(ack), ENET_PACKET_FLAG_UNSEQUENCED);
        if (p && enet_peer_send(peer, 2, p) != 0) enet_packet_destroy(p);
        ++g->stats[SLIPPI_STAT_ACKS_SENT];
    }
}

static void on_ack(const uint8_t *d, size_t len)
{
    slippi_state *g = &g_slippi;
    if (len < 6) { sp_log("p2p: ack packet too small"); return; }
    int32_t frame = (int32_t)get32(d + 1);
    if (remote_index(d[5]) != 0) { sp_log("p2p: ack with invalid player port %d", d[5]); return; }
    ++g->stats[SLIPPI_STAT_ACKS_RECEIVED];
    if (frame > g->last_frame_acked) g->last_frame_acked = frame;
    while (g->ack_count && g->ack_timers[g->ack_first].frame < frame) {
        g->ack_first = (g->ack_first + 1) % ACK_TIMER_RING;
        --g->ack_count;
    }
    if (!g->ack_count || g->ack_timers[g->ack_first].frame != frame) return;
    uint64_t send_time = g->ack_timers[g->ack_first].time_us;
    g->ack_first = (g->ack_first + 1) % ACK_TIMER_RING;
    --g->ack_count;
    g->ping_us = sp_time_us() - send_time;
}

static void on_selections(const uint8_t *d, size_t len)
{
    slippi_state *g = &g_slippi;
    if (len < 14) { sp_log("p2p: invalid selections packet (%u bytes)", (unsigned)len); return; }
    slippi_selections s;
    s.character_id = d[1];
    s.character_color = d[2];
    s.is_character_selected = d[3];
    s.player_idx = d[4];
    s.stage_id = (uint16_t)((d[5] << 8) | d[6]);
    s.is_stage_selected = d[7];
    s.rng_offset = get32(d + 8);
    s.team_id = d[12];
    s.alt_stage_mode = d[13];
    if (remote_index(s.player_idx) != 0) { sp_log("p2p: selections with invalid player idx %u", s.player_idx); return; }
    /* SlippiPlayerSelections::Merge */
    slippi_selections *r = &g->remote_sel;
    r->rng_offset = s.rng_offset;
    if (s.is_stage_selected) { r->stage_id = s.stage_id; r->is_stage_selected = 1; r->alt_stage_mode = s.alt_stage_mode; }
    if (s.is_character_selected) { r->character_id = s.character_id; r->character_color = s.character_color; r->team_id = s.team_id; r->is_character_selected = 1; }
    /* New game: forget the old remote inputs. */
    g->has_game_started = 0;
    memset(g->remote, 0, sizeof(g->remote));
    g->remote_head_frame = 0;
    ++g->remote_sel_count;
    sp_log("p2p: received selections from player %u: char %u color %u sel %u stage %u sel %u rng 0x%x team %u alt %u", s.player_idx,
           s.character_id, s.character_color, s.is_character_selected, s.stage_id, s.is_stage_selected, (unsigned)s.rng_offset,
           s.team_id, s.alt_stage_mode);
}

static void on_chat(const uint8_t *d, size_t len)
{
    slippi_state *g = &g_slippi;
    if (len < 6) { sp_log("p2p: chat packet too small"); return; }
    int id = (int)get32(d + 1);
    static const int allowed[] = {136, 129, 130, 132, 34, 40, 33, 36, 72, 66, 68, 65, 24, 18, 20, 17, SLIPPI_CHAT_DISABLED};
    int ok = 0;
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) if (allowed[i] == id) ok = 1;
    if (!ok) { sp_log("p2p: invalid chat message %d", id); return; }
    sp_log("p2p: chat message %d from player %u", id, d[5]);
    if (g->cfg.chat_enabled) { g->chat_received = id; return; }
    /* Chat disabled: answer like Dolphin's GetSlippiRemoteChatMessage(false). */
    if (id > 0 && id != SLIPPI_CHAT_DISABLED) slippi_p2p_send_chat(SLIPPI_CHAT_DISABLED);
}

static void on_data(const uint8_t *d, size_t len, ENetPeer *peer)
{
    if (!len) { sp_log("p2p: empty packet"); return; }
    switch (d[0]) {
        case SLIPPI_MSG_PAD: on_pad(d, len, peer); break;
        case SLIPPI_MSG_PAD_ACK: on_ack(d, len); break;
        case SLIPPI_MSG_MATCH_SELECTIONS: on_selections(d, len); break;
        case SLIPPI_MSG_CHAT: on_chat(d, len); break;
        case SLIPPI_MSG_CONN_SELECTED: break;                 /* unused by Dolphin */
        case SLIPPI_MSG_COMPLETE_STEP: case SLIPPI_MSG_SYNCED_STATE: break; /* ranked only */
        default: sp_log("p2p: unknown message 0x%02x (%u bytes)", d[0], (unsigned)len); break;
    }
}

static void became_connected(void)
{
    slippi_state *g = &g_slippi;
    slippi_set_status(SLIPPI_STATUS_CONNECTED);
    g->last_receive_us = sp_time_us();
    char ip[20];
    slippi_format_ip(g->primary->address.host, ip, sizeof(ip));
    sp_log("p2p: connected to '%s' (%s) at %s:%u; we are player index %d, decider %d", g->match.remote_name, g->match.remote_code, ip,
           g->primary->address.port, g->match.local_index, g->match.is_host);
    /* Dolphin sends its current selections (random stage, not selected) right after connecting. */
    if (g->match.stage_count) g->local_sel.stage_id = g->match.stages[sp_random() % (uint32_t)g->match.stage_count];
    slippi_p2p_send_selections();
}

void slippi_p2p_event(ENetEvent *ev)
{
    slippi_state *g = &g_slippi;
    ENetPeer *peer = ev->peer;
    switch (ev->type) {
        case ENET_EVENT_TYPE_RECEIVE:
            if (peer) on_data(ev->packet->data, ev->packet->dataLength, peer);
            enet_packet_destroy(ev->packet);
            break;
        case ENET_EVENT_TYPE_CONNECT: {
            if (!peer) break;
            add_conn(peer);
            if (g->status == SLIPPI_STATUS_CONNECTING) {
                if (g->p2p_connected) {
                    if (same_addr(peer, g->primary)) g->primary = peer; /* "already connected" */
                } else if (peer->address.host == g->remote_addr.host) {
                    /* Host only: tolerate NATs that change the port (Dolphin). */
                    g->primary = peer;
                    g->p2p_connected = 1;
                    became_connected();
                }
            } else {
                sp_log("p2p: late connection from port %u", peer->address.port);
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            if (!peer) break;
            slippi_conn *c = find_conn(peer);
            if (c) c->disconnected = 1;
            int all_for_key = 1;
            for (int i = 0; i < MAX_PEERS; ++i)
                if (g->conns[i].peer && !g->conns[i].disconnected && same_addr(g->conns[i].peer, peer)) all_for_key = 0;
            int connected_client = g->primary && same_addr(peer, g->primary);
            if (connected_client && ev->data != 0) g->disconnect_reason = ev->data;
            sp_log("p2p: disconnect from port %u (reason %u, all peers gone %d, connected client %d)", peer->address.port,
                   (unsigned)ev->data, all_for_key, connected_client);
            if (connected_client && all_for_key && live_conn_count() == 0 && g->status == SLIPPI_STATUS_CONNECTED) {
                slippi_set_status(SLIPPI_STATUS_DISCONNECTED);
                snprintf(g->error, sizeof(g->error), "Opponent disconnected%s", ev->data == 1 ? " (poor performance)" : "");
            }
            break;
        }
        default:
            break;
    }
}

void slippi_p2p_tick(void)
{
    slippi_state *g = &g_slippi;
    uint64_t now = sp_time_us();
    if (g->status == SLIPPI_STATUS_CONNECTING && now > g->p2p_deadline_us) {
        sp_log("p2p: connection to %s timed out after %d ms", g->match.remote_addr, SLIPPI_P2P_TIMEOUT_MS);
        slippi_p2p_disconnect();
        if (g->cfg.mm_retries < 0 || g->retries < g->cfg.mm_retries) {
            ++g->retries;
            sp_log("p2p: connection attempt failed, searching again (attempt %d)", g->retries + 1);
            slippi_destroy_host();
            slippi_mm_start();
        } else {
            slippi_fail("Could not connect to the opponent");
        }
        return;
    }
    if (g->status == SLIPPI_STATUS_CONNECTED && g->cfg.auto_resend && g->local_count &&
        g->local[g->local_head].frame > g->last_frame_acked && now - g->last_send_us >= (uint64_t)SLIPPI_FRAME_US) {
        ++g->stats[SLIPPI_STAT_RESENDS];
        slippi_p2p_send_pads();
    }
}
