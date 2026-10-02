/* Fake Slippi matchmaking server for local tests (never talks to Slippi).
 *
 * Speaks the protocol of mm.slippi.gg as Slippi Dolphin uses it: ENet over
 * UDP (3 channels), one JSON text per reliable packet on channel 0.
 *   client -> create-ticket {appVersion, ipAddressLan, search{connectCode:[SJIS bytes], mode},
 *                            type, user{uid, playKey, connectCode, displayName}}
 *   server -> create-ticket-resp {type[, error]}
 *   server -> get-ticket-resp {type, matchId, isHost, stages, players[{uid, displayName,
 *             connectCode, port, isLocalPlayer, isBot, chatMessages, ipAddress, ipAddressLan}]}
 * Two Direct tickets pair when each one searches for the other's connect code.
 * The first ticket of a pair gets port 1 and isHost (configurable).
 *
 * Usage: slippi_fake_mm [--port 43113] [--second-is-host] [--error TEXT]
 *                       [--min-version X.Y.Z] [--external-ip A.B.C.D] [--once]
 */
#include "slippi_internal.h"
#include "slippi_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ENetPeer *peer;
    int active;
    char uid[64], name[64], code[32], search[32], lan[64], ext[64], version[32];
    int mode;
} ticket;

#define MAX_TICKETS 32
static ticket tickets[MAX_TICKETS];
static int second_is_host, once, matches;
static const char *forced_error, *min_version, *external_ip;

static void send_json(ENetPeer *peer, sj_buf *b)
{
    ENetPacket *p = enet_packet_create(b->data, b->length, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, p);
}

static int version_less(const char *a, const char *b)
{
    int x[3] = {0}, y[3] = {0};
    sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; ++i) if (x[i] != y[i]) return x[i] < y[i];
    return 0;
}

static void write_player(sj_buf *b, const ticket *t, int port, int local)
{
    static const char *chat[16] = {"ggs", "one more", "brb", "good luck", "well played", "that was fun", "thanks", "too good",
                                   "sorry", "my b", "lol", "wow", "gotta go", "one sec", "let's play again later", "bad connection"};
    int f = 1;
    sj_raw(b, "{");
    sj_key(b, "chatMessages", &f);
    sj_raw(b, "[");
    for (int i = 0; i < 16; ++i) { if (i) sj_raw(b, ","); sj_string(b, chat[i]); }
    sj_raw(b, "]");
    sj_key(b, "connectCode", &f); sj_string(b, t->code);
    sj_key(b, "displayName", &f); sj_string(b, t->name);
    sj_key(b, "ipAddress", &f); sj_string(b, t->ext);
    sj_key(b, "ipAddressLan", &f); sj_string(b, t->lan);
    sj_key(b, "isBot", &f); sj_raw(b, "false");
    sj_key(b, "isLocalPlayer", &f); sj_raw(b, local ? "true" : "false");
    sj_key(b, "port", &f); sj_int(b, port);
    sj_key(b, "uid", &f); sj_string(b, t->uid);
    sj_raw(b, "}");
}

static void send_match(ticket *first, ticket *second)
{
    ticket *p1 = second_is_host ? second : first, *p2 = second_is_host ? first : second;
    char match_id[96];
    snprintf(match_id, sizeof(match_id), "mode.direct-fake-%llu-%d", (unsigned long long)(sp_time_us() / 1000), ++matches);
    for (int side = 0; side < 2; ++side) {
        ticket *me = side ? p2 : p1;
        sj_buf b;
        sj_buf_init(&b);
        int f = 1;
        sj_raw(&b, "{");
        sj_key(&b, "isHost", &f); sj_raw(&b, side == 0 ? "true" : "false");
        sj_key(&b, "matchId", &f); sj_string(&b, match_id);
        sj_key(&b, "players", &f);
        sj_raw(&b, "[");
        write_player(&b, p1, 1, side == 0);
        sj_raw(&b, ",");
        write_player(&b, p2, 2, side == 1);
        sj_raw(&b, "]");
        sj_key(&b, "stages", &f); sj_raw(&b, "[2,3,8,28,31,32]");
        sj_key(&b, "type", &f); sj_string(&b, "get-ticket-resp");
        sj_raw(&b, "}");
        send_json(me->peer, &b);
        sp_log("mm: -> %s get-ticket-resp (%u bytes): %s", me->code, (unsigned)b.length, b.data);
        sj_buf_free(&b);
    }
    sp_log("mm: MATCHED %s (port 1, host) vs %s (port 2), %s", p1->code, p2->code, match_id);
    first->active = second->active = 0;
}

static void handle(ENetPeer *peer, const char *data, size_t len)
{
    sj_node *msg = sj_parse(data, len);
    if (!msg) { sp_log("mm: unparseable message (%u bytes)", (unsigned)len); return; }
    const char *type = sj_get_str(msg, "type", "");
    if (strcmp(type, "create-ticket") != 0) { sp_log("mm: ignoring message type '%s'", type); sj_free(msg); return; }
    const sj_node *user = sj_get(msg, "user"), *search = sj_get(msg, "search");
    const sj_node *codebuf = sj_get(search, "connectCode");
    ticket t;
    memset(&t, 0, sizeof(t));
    t.peer = peer;
    snprintf(t.uid, sizeof(t.uid), "%s", sj_get_str(user, "uid", ""));
    snprintf(t.name, sizeof(t.name), "%s", sj_get_str(user, "displayName", ""));
    snprintf(t.code, sizeof(t.code), "%s", sj_get_str(user, "connectCode", ""));
    snprintf(t.lan, sizeof(t.lan), "%s", sj_get_str(msg, "ipAddressLan", ""));
    snprintf(t.version, sizeof(t.version), "%s", sj_get_str(msg, "appVersion", ""));
    const char *play_key = sj_get_str(user, "playKey", "");
    t.mode = (int)sj_get_num(search, "mode", -1);
    uint8_t sjis[32];
    int n = 0;
    char bytes[160] = "";
    if (codebuf && codebuf->type == SJ_ARRAY)
        for (const sj_node *c = codebuf->child; c && n < (int)sizeof(sjis); c = c->next) {
            sjis[n++] = (uint8_t)c->number;
            size_t l = strlen(bytes);
            snprintf(bytes + l, sizeof(bytes) - l, "%s%u", n > 1 ? "," : "", (unsigned)(uint8_t)c->number);
        }
    slippi_sjis_to_ascii(sjis, n, t.search, sizeof(t.search));
    char ip[20];
    slippi_format_ip(peer->address.host, ip, sizeof(ip));
    snprintf(t.ext, sizeof(t.ext), "%s:%u", external_ip ? external_ip : ip, peer->address.port);
    sp_log("mm: <- create-ticket from %s: user uid=%s connectCode=%s displayName=%s playKey=<%u chars, not logged>; search mode=%d "
           "connectCode=[%s] (\"%s\"); appVersion=%s; ipAddressLan=%s; observed %s:%u",
           t.ext, t.uid, t.code, t.name, (unsigned)strlen(play_key), t.mode, bytes, t.search, t.version, t.lan, ip, peer->address.port);
    int ok = user && search && codebuf && codebuf->type == SJ_ARRAY && t.uid[0] && play_key[0] && t.code[0] && t.mode == 2 &&
             sj_get(msg, "appVersion") && sj_get(msg, "ipAddressLan");
    sj_buf b;
    sj_buf_init(&b);
    int f = 1;
    sj_raw(&b, "{");
    const char *err = forced_error;
    if (!ok) err = "Malformed create-ticket (fake server)";
    if (!err && min_version && version_less(t.version, min_version)) err = "Your version of Slippi is out of date (fake server)";
    if (err) { sj_key(&b, "error", &f); sj_string(&b, err); }
    sj_key(&b, "type", &f); sj_string(&b, "create-ticket-resp");
    sj_raw(&b, "}");
    send_json(peer, &b);
    sp_log("mm: -> create-ticket-resp %s", b.data);
    sj_buf_free(&b);
    sj_free(msg);
    if (err) return;

    /* Replace an older ticket from the same peer/user, then try to pair. */
    for (int i = 0; i < MAX_TICKETS; ++i)
        if (tickets[i].active && (tickets[i].peer == peer || !strcmp(tickets[i].uid, t.uid))) tickets[i].active = 0;
    for (int i = 0; i < MAX_TICKETS; ++i) {
        ticket *o = &tickets[i];
        if (!o->active) continue;
        if (!strcmp(o->code, t.search) && !strcmp(o->search, t.code)) {
            ticket mine = t;
            send_match(o, &mine);
            return;
        }
    }
    for (int i = 0; i < MAX_TICKETS; ++i)
        if (!tickets[i].active) { tickets[i] = t; tickets[i].active = 1; sp_log("mm: %s waiting for %s", t.code, t.search); return; }
    sp_log("mm: ticket table full");
}

int main(int argc, char **argv)
{
    int port = SLIPPI_MM_PORT_DEFAULT;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--second-is-host")) second_is_host = 1;
        else if (!strcmp(argv[i], "--error") && i + 1 < argc) forced_error = argv[++i];
        else if (!strcmp(argv[i], "--min-version") && i + 1 < argc) min_version = argv[++i];
        else if (!strcmp(argv[i], "--external-ip") && i + 1 < argc) external_ip = argv[++i];
        else if (!strcmp(argv[i], "--once")) once = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (enet_initialize() != 0) { sp_log("enet_initialize failed"); return 1; }
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16)port;
    ENetHost *host = enet_host_create(&addr, 64, 3, 0, 0);
    if (!host) { sp_log("cannot bind UDP port %d", port); return 1; }
    sp_log("fake Slippi matchmaking server on UDP %d (ENet %u.%u.%u)", port, ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH);
    uint64_t done_at = 0;
    for (;;) {
        ENetEvent ev;
        int rc = enet_host_service(host, &ev, 50);
        if (rc > 0) {
            char ip[20];
            slippi_format_ip(ev.peer->address.host, ip, sizeof(ip));
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT: sp_log("mm: connect from %s:%u (connect data %u)", ip, ev.peer->address.port, (unsigned)ev.data); break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (ev.channelID != 0 || !(ev.packet->flags & ENET_PACKET_FLAG_RELIABLE))
                        sp_log("mm: note: message on channel %u flags 0x%x", ev.channelID, (unsigned)ev.packet->flags);
                    handle(ev.peer, (const char *)ev.packet->data, ev.packet->dataLength);
                    enet_packet_destroy(ev.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    sp_log("mm: disconnect from %s:%u", ip, ev.peer->address.port);
                    for (int i = 0; i < MAX_TICKETS; ++i) if (tickets[i].peer == ev.peer) tickets[i].active = 0;
                    break;
                default: break;
            }
        }
        if (once && matches && !done_at) done_at = sp_time_us() + 5000000;
        if (done_at && sp_time_us() > done_at) break;
    }
    enet_host_destroy(host);
    enet_deinitialize();
    return 0;
}
