/* Slippi matchmaking client (port of Dolphin's SlippiMatchmaking, Direct mode).
 *
 * Wire behaviour kept from Dolphin:
 * - ENet host bound to 41000 + rand() % 10000 (or the forced port), 3 channels,
 *   connect to MM_HOST:43113 with 3 channels and connect data 0;
 * - one JSON text per reliable packet on channel 0;
 * - create-ticket {user{uid,playKey,connectCode,displayName}, search{mode,
 *   connectCode:[Shift-JIS bytes]}, appVersion, ipAddressLan "ip:port"};
 * - get-ticket-resp parsing and the LAN-vs-external address choice;
 * - the P2P connection uses the same local port (hole punching).
 * Difference: Dolphin destroys the MM host and creates a new one on the same
 * port for P2P (blocking up to 3 s for the MM disconnect). We keep the one
 * host (created with 10 peers instead of 1) and disconnect the MM peer while
 * the P2P handshake starts; the packets on the wire are the same.
 */
#include "slippi_internal.h"
#include "slippi_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mm_fail(const char *msg)
{
    g_slippi.mm_state = MM_ERROR;
    slippi_fail("%s", msg);
}

/* Dolphin getLocalAddress: connect a UDP socket toward the MM server and read
 * the local address the OS picked. Falls back to the platform host id. */
static uint32_t local_lan_address(const ENetAddress *mm)
{
    uint32_t ip = 0;
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s != ENET_SOCKET_NULL) {
        ENetAddress a;
        memset(&a, 0, sizeof(a));
        if (enet_socket_connect(s, mm) == 0 && enet_socket_get_address(s, &a) == 0) ip = a.host;
        enet_socket_destroy(s);
    }
    if (ip == 0) ip = sp_lan_ip_fallback();
    return ip;
}

static void send_create_ticket(void)
{
    slippi_state *g = &g_slippi;
    char lan[48] = "";
    if (g->cfg.lan_ip[0]) {
        snprintf(lan, sizeof(lan), "%s:%u", g->cfg.lan_ip, g->host_port);
    } else {
        uint32_t ip = local_lan_address(&g->mm_addr);
        if (ip) {
            char text[20];
            slippi_format_ip(ip, text, sizeof(text));
            snprintf(lan, sizeof(lan), "%s:%u", text, g->host_port);
        }
    }
    sp_log("mm: sending LAN address '%s'", lan);

    /* nlohmann::json::dump() order: object keys sorted, no whitespace. */
    sj_buf b;
    sj_buf_init(&b);
    int first = 1, inner;
    sj_raw(&b, "{");
    sj_key(&b, "appVersion", &first); sj_string(&b, g->cfg.app_version);
    sj_key(&b, "ipAddressLan", &first); sj_string(&b, lan);
    sj_key(&b, "search", &first);
    sj_raw(&b, "{");
    inner = 1;
    sj_key(&b, "connectCode", &inner);
    sj_raw(&b, "[");
    for (int i = 0; i < g->search_code_len; ++i) { if (i) sj_raw(&b, ","); sj_int(&b, g->search_code_sjis[i]); }
    sj_raw(&b, "]");
    sj_key(&b, "mode", &inner); sj_int(&b, 2); /* DIRECT */
    sj_raw(&b, "}");
    sj_key(&b, "type", &first); sj_string(&b, "create-ticket");
    sj_key(&b, "user", &first);
    sj_raw(&b, "{");
    inner = 1;
    sj_key(&b, "connectCode", &inner); sj_string(&b, g->user.connect_code);
    sj_key(&b, "displayName", &inner); sj_string(&b, g->user.display_name);
    sj_key(&b, "playKey", &inner); sj_string(&b, g->user.play_key);
    sj_key(&b, "uid", &inner); sj_string(&b, g->user.uid);
    sj_raw(&b, "}}");
    if (b.failed || !g->mm_peer) { sj_buf_free(&b); mm_fail("Out of memory"); return; }
    ENetPacket *p = enet_packet_create(b.data, b.length, ENET_PACKET_FLAG_RELIABLE);
    if (g->cfg.log_packets) sp_log("mm: create-ticket %u bytes (playKey omitted from log)", (unsigned)b.length);
    sj_buf_free(&b);
    if (!p || enet_peer_send(g->mm_peer, 0, p) != 0) { mm_fail("Failed to send create-ticket"); return; }
    enet_host_flush(g->host);
    g->mm_state = MM_WAIT_CREATE;
    g->mm_deadline_us = sp_time_us() + SLIPPI_MM_CREATE_TIMEOUT_MS * 1000ull;
    sp_log("mm: create-ticket sent (mode 2, code %s, appVersion %s)", g->search_code, g->cfg.app_version);
}

static void copy_str(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src ? src : ""); }

static void ip_only(const char *addr, char *out, size_t n)
{
    copy_str(out, n, addr);
    char *c = strchr(out, ':');
    if (c) *c = '\0';
}

/* handleMatchmaking: the part after a get-ticket-resp without error. */
static int ingest_ticket(const sj_node *resp)
{
    slippi_state *g = &g_slippi;
    slippi_match *m = &g->match;
    memset(m, 0, sizeof(*m));
    m->local_index = -1;
    copy_str(m->match_id, sizeof(m->match_id), sj_get_str(resp, "matchId", ""));
    const sj_node *players = sj_get(resp, "players");
    char local_ext[64] = "";
    if (players && players->type == SJ_ARRAY) {
        for (const sj_node *el = players->child; el; el = el->next) {
            ++m->player_count;
            if (sj_get_bool(el, "isLocalPlayer", 0)) {
                ip_only(sj_get_str(el, "ipAddress", "1.1.1.1:123"), local_ext, sizeof(local_ext));
                m->local_index = (int)sj_get_num(el, "port", 0) - 1;
            }
        }
        for (const sj_node *el = players->child; el; el = el->next) {
            if ((int)sj_get_num(el, "port", 0) - 1 == m->local_index) continue;
            const char *ext = sj_get_str(el, "ipAddress", "1.1.1.1:123");
            const char *lan = sj_get_str(el, "ipAddressLan", "1.1.1.1:123");
            char ext_ip[64];
            ip_only(ext, ext_ip, sizeof(ext_ip));
            sp_log("mm: opponent ipAddress %s ipAddressLan %s", ext, lan);
            if (strcmp(ext_ip, local_ext) != 0 || !lan[0]) copy_str(m->remote_addr, sizeof(m->remote_addr), ext);
            else copy_str(m->remote_addr, sizeof(m->remote_addr), lan);
            copy_str(m->remote_name, sizeof(m->remote_name), sj_get_str(el, "displayName", ""));
            copy_str(m->remote_code, sizeof(m->remote_code), sj_get_str(el, "connectCode", ""));
            copy_str(m->remote_uid, sizeof(m->remote_uid), sj_get_str(el, "uid", ""));
            break; /* 1v1 */
        }
    }
    m->is_host = sj_get_bool(resp, "isHost", 0);
    const sj_node *stages = sj_get(resp, "stages");
    if (stages && stages->type == SJ_ARRAY)
        for (const sj_node *s = stages->child; s && m->stage_count < (int)sizeof(m->stages); s = s->next)
            if (s->type == SJ_NUMBER) m->stages[m->stage_count++] = (uint8_t)s->number;
    if (!m->stage_count) {
        static const uint8_t defaults[] = {0x3, 0x8, 0x1C, 0x1F, 0x20, 0x2};
        m->stage_count = m->player_count == 2 ? 6 : 5;
        memcpy(m->stages, defaults, (size_t)m->stage_count);
    }
    m->items = (uint32_t)sj_get_num(resp, "items", 0);
    if (m->player_count != 2 || m->local_index < 0 || !m->remote_addr[0]) {
        sp_log("mm: unsupported match (%d players, local index %d)", m->player_count, m->local_index);
        return -1;
    }
    sp_log("mm: match %s: local port %d, decider %d, opponent '%s' (%s) at %s", m->match_id, m->local_index + 1, m->is_host,
           m->remote_name, m->remote_code, m->remote_addr);
    return 0;
}

static void handle_message(const char *data, size_t len)
{
    slippi_state *g = &g_slippi;
    sj_node *msg = sj_parse(data, len);
    if (!msg) { mm_fail("Invalid matchmaking response"); return; }
    const char *type = sj_get_str(msg, "type", "");
    const char *err = sj_get_str(msg, "error", "");
    if (g->mm_state == MM_WAIT_CREATE) {
        if (strcmp(type, "create-ticket-resp") != 0) { sp_log("mm: unexpected '%s' while joining", type); mm_fail("Invalid response when joining mm queue"); }
        else if (err[0]) { sp_log("mm: create-ticket error: %s", err); mm_fail(err); }
        else {
            g->mm_state = MM_SEARCH;
            g->mm_last_log_us = sp_time_us();
            sp_log("mm: ticket created, waiting for opponent %s", g->search_code);
        }
    } else if (g->mm_state == MM_SEARCH) {
        if (strcmp(type, "get-ticket-resp") != 0) { sp_log("mm: unexpected '%s' while searching", type); mm_fail("Invalid response when getting mm status"); }
        else if (err[0]) {
            const char *latest = sj_get_str(msg, "latestVersion", "");
            sp_log("mm: get-ticket error: %s%s%s", err, latest[0] ? "; latestVersion " : "", latest);
            mm_fail(err);
        } else if (ingest_ticket(msg) != 0) {
            mm_fail("Unsupported match from the mm server");
        } else {
            /* terminateMmConnection, without waiting for the server's reply. */
            enet_peer_disconnect(g->mm_peer, 0);
            g->mm_state = MM_DONE;
            slippi_p2p_start();
        }
    } else {
        sp_log("mm: ignoring '%s' in state %d", type, g->mm_state);
    }
    sj_free(msg);
}

void slippi_mm_start(void)
{
    slippi_state *g = &g_slippi;
    g->mm_state = MM_RESOLVE;
    g->mm_peer = NULL;
    slippi_set_status(SLIPPI_STATUS_SEARCHING);
}

void slippi_mm_close(void)
{
    slippi_state *g = &g_slippi;
    if (g->mm_peer && g->host) enet_peer_disconnect_now(g->mm_peer, 0);
    g->mm_peer = NULL;
    if (g->mm_state != MM_ERROR) g->mm_state = MM_NONE;
}

void slippi_mm_service_event(ENetEvent *ev)
{
    slippi_state *g = &g_slippi;
    switch (ev->type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (g->mm_state == MM_CONNECT) {
                sp_log("mm: connected to matchmaking server");
                send_create_ticket();
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (g->mm_state == MM_WAIT_CREATE || g->mm_state == MM_SEARCH)
                handle_message((const char *)ev->packet->data, ev->packet->dataLength);
            enet_packet_destroy(ev->packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            g->mm_peer = NULL;
            if (g->mm_state == MM_CONNECT) mm_fail("Failed to connect to mm server");
            else if (g->mm_state == MM_WAIT_CREATE || g->mm_state == MM_SEARCH) mm_fail("Lost connection to the mm server");
            break;
        default:
            break;
    }
}

void slippi_mm_tick(void)
{
    slippi_state *g = &g_slippi;
    uint64_t now = sp_time_us();
    switch (g->mm_state) {
        case MM_RESOLVE: {
            char host[128];
            int port = g->cfg.mm_port;
            snprintf(host, sizeof(host), "%s", g->cfg.mm_host);
            ENetAddress addr;
            memset(&addr, 0, sizeof(addr));
            /* DNS may block for seconds: never hold the state lock across it. */
            sp_unlock();
            int rc = enet_address_set_host(&addr, host);
            sp_lock();
            if (g->mm_state != MM_RESOLVE) return; /* cancelled meanwhile */
            if (rc != 0) { sp_log("mm: cannot resolve %s", host); mm_fail("Cannot resolve the matchmaking server"); return; }
            addr.port = (enet_uint16)port;
            g->mm_addr = addr;
            char ip[20];
            slippi_format_ip(addr.host, ip, sizeof(ip));
            sp_log("mm: %s resolved to %s:%d", host, ip, port);
            if (slippi_create_host() != 0) { mm_fail("Failed to create mm client"); return; }
            g->mm_peer = enet_host_connect(g->host, &g->mm_addr, 3, 0);
            if (!g->mm_peer) { mm_fail("Failed to start connection to mm server"); return; }
            enet_host_flush(g->host);
            g->mm_state = MM_CONNECT;
            g->mm_deadline_us = now + SLIPPI_MM_CONNECT_TIMEOUT_MS * 1000ull;
            break;
        }
        case MM_CONNECT:
            if (now > g->mm_deadline_us) { sp_log("mm: no answer from %s:%d", g->cfg.mm_host, g->cfg.mm_port); mm_fail("Failed to connect to mm server"); }
            break;
        case MM_WAIT_CREATE:
            if (now > g->mm_deadline_us) mm_fail("Failed to join mm queue");
            break;
        case MM_SEARCH:
            if (now - g->mm_last_log_us > 10000000ull) { g->mm_last_log_us = now; sp_log("mm: still waiting for %s", g->search_code); }
            break;
        default:
            break;
    }
}
