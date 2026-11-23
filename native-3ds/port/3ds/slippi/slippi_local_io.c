/* The link under local wireless play (slippi_local.h).
 *
 * 3DS: UDS (libctru uds.h, nwm::UDS). The host creates a 2-node network with a
 * fixed passphrase; the client scans beacons for it and joins. One data frame
 * carries one game packet on data channel 1. The 3DS system name is the player
 * name; the host's is read from the node info.
 *
 * PC: a loopback UDP stand-in (ENet's socket calls) with a tiny hello/bye
 * handshake, so the netcode and packet-loss handling are testable with no
 * radio. Both builds deliver unreliable, possibly reordered datagrams. */
#include "slippi_internal.h"
#include "slippi_local.h"
#include <stdio.h>
#include <string.h>

#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>

#define WLANCOMM_ID 0x00A5D5E1u   /* distinct from the spike app's 0x00A5D500 */
#define ID8 2
#define CHANNEL 1
#define PASSPHRASE "melee3ds-local"
#define SHAREDMEM 0x3000
#define SCAN_INTERVAL_US 1500000ull
#define STATUS_INTERVAL_US 100000ull

static int up, hosting, joined, was_connected, peer_id;
static udsBindContext bind_ctx;
static udsNetworkStruct netw;
static char local_name[32];
static uint64_t last_scan_us, last_status_us;
static u8 scan_buf[0x4000];

static void fmt_err(char *err, int errlen, const char *what, Result r)
{
    snprintf(err, (size_t)errlen, "%s (0x%08lX)", what, (unsigned long)r);
}

const char *slippi_io_local_name(void) { return local_name; }

int slippi_io_open(int host, int port, char *err, int errlen)
{
    (void)port;
    slippi_io_close();
    Result r = udsInit(SHAREDMEM, NULL);
    if (R_FAILED(r)) { fmt_err(err, errlen, "Wireless is unavailable", r); return -1; }
    up = 1;
    local_name[0] = 0;
    udsNodeInfo me;
    if (R_SUCCEEDED(udsGenerateNodeInfo(&me, NULL))) udsGetNodeInfoUsername(&me, local_name);
    sp_log("uds: init ok, this console is \"%s\"", local_name);
    was_connected = 0;
    peer_id = 0;
    last_scan_us = 0;
    last_status_us = 0;
    if (host) {
        udsGenerateDefaultNetworkStruct(&netw, WLANCOMM_ID, ID8, 2);
        r = udsCreateNetwork(&netw, PASSPHRASE, strlen(PASSPHRASE), &bind_ctx, CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
        if (R_FAILED(r)) { fmt_err(err, errlen, "Could not create the game", r); slippi_io_close(); return -1; }
        hosting = 1;
        sp_log("uds: hosting");
    }
    return 0;
}

/* Scans once; joins the first game found. Returns 0 on failure (retried later). */
static int try_join(void)
{
    udsNetworkScanInfo *nets = NULL;
    size_t total = 0;
    Result r = udsScanBeacons(scan_buf, sizeof(scan_buf), &nets, &total, WLANCOMM_ID, ID8, NULL, false);
    if (R_FAILED(r) || total == 0) {
        if (R_FAILED(r)) sp_log("uds: scan failed 0x%08lx", (unsigned long)r);
        free(nets);
        return 0;
    }
    netw = nets[0].network;
    free(nets);
    r = udsConnectNetwork(&netw, PASSPHRASE, strlen(PASSPHRASE), &bind_ctx, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, CHANNEL,
                          UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(r)) { sp_log("uds: join failed 0x%08lx", (unsigned long)r); return 0; }
    joined = 1;
    sp_log("uds: joined a game");
    return 1;
}

int slippi_io_state(char *peer, int peerlen, char *err, int errlen)
{
    (void)err; (void)errlen;
    if (!up) return SLIPPI_IO_FAILED;
    uint64_t now = sp_time_us();
    if (!hosting && !joined) {
        if (now - last_scan_us < SCAN_INTERVAL_US && last_scan_us) return SLIPPI_IO_WAITING;
        last_scan_us = now;
        if (!try_join()) return SLIPPI_IO_WAITING;
    }
    if (now - last_status_us < STATUS_INTERVAL_US && last_status_us && !was_connected) return SLIPPI_IO_WAITING;
    last_status_us = now;
    udsConnectionStatus cs;
    if (R_FAILED(udsGetConnectionStatus(&cs))) return was_connected ? SLIPPI_IO_PEER_LEFT : SLIPPI_IO_WAITING;
    /* Node ids are 1-based bit positions in node_bitmask; the host is node 1. */
    int other = 0;
    for (int i = 0; i < UDS_MAXNODES; ++i)
        if ((cs.node_bitmask & (1u << i)) && (i + 1) != cs.cur_NetworkNodeID) { other = i + 1; break; }
    if (!other) return was_connected ? SLIPPI_IO_PEER_LEFT : SLIPPI_IO_WAITING;
    if (!was_connected) {
        udsNodeInfo ni;
        peer[0] = 0;
        if (R_SUCCEEDED(udsGetNodeInformation((u16)other, &ni))) udsGetNodeInfoUsername(&ni, peer);
        if (peerlen > 0) peer[peerlen - 1] = 0;
        peer_id = other;
        was_connected = 1;
        sp_log("uds: node %d (%s) is connected; we are node %d", other, peer, (int)cs.cur_NetworkNodeID);
    }
    return SLIPPI_IO_CONNECTED;
}

int slippi_io_send(const uint8_t *data, size_t len)
{
    if (!up || !peer_id || len > UDS_DATAFRAME_MAXSIZE) return -1;
    Result r = udsSendTo((u16)peer_id, CHANNEL, UDS_SENDFLAG_Default, data, len);
    /* 0xC86113F0: the frame was dropped (queue full); the netcode resends. */
    return (r < 0 && (u32)r != 0xC86113F0u) ? -1 : 0;
}

int slippi_io_recv(uint8_t *buf, size_t cap)
{
    if (!up || !(hosting || joined)) return 0;
    size_t n = 0;
    u16 from = 0;
    if (R_FAILED(udsPullPacket(&bind_ctx, buf, cap, &n, &from))) return 0;
    return (int)n;
}

void slippi_io_close(void)
{
    if (!up) return;
    if (hosting) udsDestroyNetwork();
    else if (joined) udsDisconnectNetwork();
    if (hosting || joined) udsUnbind(&bind_ctx);
    hosting = joined = was_connected = peer_id = 0;
    udsExit();
    up = 0;
    sp_log("uds: closed");
}

#else /* PC: loopback UDP */

#include <enet/enet.h>

#define DEFAULT_PORT 47010
#define MSG_HELLO 0xF0   /* client -> host: name */
#define MSG_WELCOME 0xF1 /* host -> client: name */
#define MSG_BYE 0xF2

static ENetSocket sock = ENET_SOCKET_NULL;
static int is_host, have_peer, was_connected, peer_gone, enet_up;
static ENetAddress peer_addr, host_addr;
static char local_name[32], peer_name[32];
static uint64_t last_hello_us;

const char *slippi_io_local_name(void) { return local_name; }

static int raw_send(const ENetAddress *to, const uint8_t *d, size_t n)
{
    ENetBuffer b;
    b.data = (void *)d;
    b.dataLength = n;
    return enet_socket_send(sock, to, &b, 1);
}

static void send_named(const ENetAddress *to, uint8_t type)
{
    uint8_t pkt[40];
    size_t n = strlen(local_name);
    pkt[0] = type;
    memcpy(pkt + 1, local_name, n);
    raw_send(to, pkt, 1 + n);
}

int slippi_io_open(int host, int port, char *err, int errlen)
{
    slippi_io_close();
    if (enet_initialize() != 0) { snprintf(err, (size_t)errlen, "enet_initialize failed"); return -1; }
    enet_up = 1;
    if (port <= 0) port = DEFAULT_PORT;
    is_host = host;
    have_peer = was_connected = peer_gone = 0;
    snprintf(local_name, sizeof(local_name), "%s", host ? "PC HOST" : "PC JOINER");
    sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (sock == ENET_SOCKET_NULL) { snprintf(err, (size_t)errlen, "cannot create a socket"); slippi_io_close(); return -1; }
    ENetAddress me;
    memset(&me, 0, sizeof(me));
    me.host = ENET_HOST_ANY;
    me.port = (uint16_t)(host ? port : 0);
    if (enet_socket_bind(sock, &me) != 0) { snprintf(err, (size_t)errlen, "cannot bind port %d", port); slippi_io_close(); return -1; }
    enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
    enet_address_set_host(&host_addr, "127.0.0.1");
    host_addr.port = (uint16_t)port;
    last_hello_us = 0;
    sp_log("loopback: %s on port %d", host ? "hosting" : "joining", port);
    return 0;
}

/* Reads datagrams until a game packet (returned in buf) or none. Control
 * datagrams are handled here. */
static int pump(uint8_t *buf, size_t cap)
{
    for (int i = 0; i < 64; ++i) {
        ENetAddress from;
        ENetBuffer b;
        uint8_t tmp[1600];
        b.data = tmp;
        b.dataLength = sizeof(tmp);
        int n = enet_socket_receive(sock, &from, &b, 1);
        if (n <= 0) return 0;
        if (tmp[0] == MSG_HELLO && is_host) {
            if (!have_peer) {
                peer_addr = from;
                have_peer = 1;
                size_t l = (size_t)n - 1 < sizeof(peer_name) - 1 ? (size_t)n - 1 : sizeof(peer_name) - 1;
                memcpy(peer_name, tmp + 1, l);
                peer_name[l] = 0;
            }
            send_named(&from, MSG_WELCOME);
        } else if (tmp[0] == MSG_WELCOME && !is_host) {
            if (!have_peer) {
                peer_addr = from;
                have_peer = 1;
                size_t l = (size_t)n - 1 < sizeof(peer_name) - 1 ? (size_t)n - 1 : sizeof(peer_name) - 1;
                memcpy(peer_name, tmp + 1, l);
                peer_name[l] = 0;
            }
        } else if (tmp[0] == MSG_BYE) {
            if (have_peer) peer_gone = 1;
        } else if (have_peer && (size_t)n <= cap) {
            memcpy(buf, tmp, (size_t)n);
            return n;
        }
    }
    return 0;
}

int slippi_io_state(char *peer, int peerlen, char *err, int errlen)
{
    (void)err; (void)errlen;
    if (sock == ENET_SOCKET_NULL) return SLIPPI_IO_FAILED;
    if (!was_connected) {
        uint64_t now = sp_time_us();
        if (!is_host && now - last_hello_us > 200000) { last_hello_us = now; send_named(&host_addr, MSG_HELLO); }
        uint8_t scratch[1600];
        if (pump(scratch, sizeof(scratch)) > 0) { /* a game packet before the handshake finished: drop it */ }
        if (!have_peer) return SLIPPI_IO_WAITING;
        was_connected = 1;
        snprintf(peer, (size_t)peerlen, "%s", peer_name);
        sp_log("loopback: connected to '%s'", peer_name);
        return SLIPPI_IO_CONNECTED;
    }
    return peer_gone ? SLIPPI_IO_PEER_LEFT : SLIPPI_IO_CONNECTED;
}

int slippi_io_send(const uint8_t *data, size_t len)
{
    if (sock == ENET_SOCKET_NULL || !have_peer) return -1;
    return raw_send(&peer_addr, data, len) < 0 ? -1 : 0;
}

int slippi_io_recv(uint8_t *buf, size_t cap)
{
    if (sock == ENET_SOCKET_NULL) return 0;
    return pump(buf, cap);
}

void slippi_io_close(void)
{
    if (sock != ENET_SOCKET_NULL) {
        if (have_peer) {
            uint8_t bye = MSG_BYE;
            for (int i = 0; i < 3; ++i) raw_send(&peer_addr, &bye, 1);
        }
        enet_socket_destroy(sock);
        sock = ENET_SOCKET_NULL;
    }
    if (enet_up) { enet_deinitialize(); enet_up = 0; }
    have_peer = was_connected = peer_gone = 0;
}

#endif
