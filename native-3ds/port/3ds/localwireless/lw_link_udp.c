/* Loopback UDP stand-in for the UDS link (lw_link.h): the PC test tools use it, and a development 3DS build can
 * too (use_udp), so several emulated consoles can play each other in automated tests (Azahar cannot join a UDS
 * room headless). Plain BSD sockets. A tiny hello/welcome/bye handshake assigns node ids; everything else is
 * passed through. Not part of a release 3DS build's behaviour (the session only picks it with use_udp). */
#include "lw_link.h"
#include "lw_platform.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_NONE INVALID_SOCKET
#define sock_close closesocket
typedef int sa_len_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_NONE (-1)
#define sock_close close
typedef socklen_t sa_len_t;
#endif

#define MAX_NODES 4
#define DEFAULT_PORT 47010
#define MSG_HELLO 0xF0   /* client -> host: name */
#define MSG_WELCOME 0xF1 /* host -> client: u8 node, host name */
#define MSG_BYE 0xF2     /* either way; a client's is told apart by its address */

static sock_t sock = SOCK_NONE;
static int is_host, joined, my_node, max_nodes_cfg;
#ifdef _WIN32
static int started_net;
#endif
static unsigned node_mask;
static struct sockaddr_in host_addr, client_addr[MAX_NODES + 1];
static char node_names[MAX_NODES + 1][LW_LINK_NAME_MAX];
static char local_name[32];
static uint64_t last_hello_us;

const char *lw_udp_local_name(void) { return local_name; }
unsigned lw_udp_nodes(void) { return node_mask; }
int lw_udp_my_node(void) { return my_node; }
const char *lw_udp_node_name(int node) { return node >= 1 && node <= MAX_NODES ? node_names[node] : ""; }

static int raw_send(const struct sockaddr_in *to, const uint8_t *d, size_t n)
{
    return (int)sendto(sock, (const char *)d, (int)n, 0, (const struct sockaddr *)to, sizeof(*to));
}

static void send_hello(void)
{
    uint8_t pkt[40];
    size_t n = strlen(local_name);
    pkt[0] = MSG_HELLO;
    memcpy(pkt + 1, local_name, n);
    raw_send(&host_addr, pkt, 1 + n);
}

static void send_welcome(const struct sockaddr_in *to, int node)
{
    uint8_t pkt[40];
    size_t n = strlen(local_name);
    pkt[0] = MSG_WELCOME;
    pkt[1] = (uint8_t)node;
    memcpy(pkt + 2, local_name, n);
    raw_send(to, pkt, 2 + n);
}

static int same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static int node_of(const struct sockaddr_in *a)
{
    for (int n = 2; n <= MAX_NODES; ++n)
        if ((node_mask & (1u << (n - 1))) && same_addr(&client_addr[n], a)) return n;
    return 0;
}

int lw_udp_open(int host, int port, int max_nodes, char *err, int errlen)
{
    lw_udp_close();
    if (lw_platform_net_init() != 0) { snprintf(err, (size_t)errlen, "sockets are unavailable"); return -1; }
#ifdef _WIN32
    { WSADATA d; if (WSAStartup(MAKEWORD(2, 2), &d) != 0) { snprintf(err, (size_t)errlen, "WSAStartup failed"); return -1; } started_net = 1; }
#endif
    if (port <= 0) port = DEFAULT_PORT;
    is_host = host;
    joined = my_node = 0;
    node_mask = 0;
    max_nodes_cfg = max_nodes < 2 ? 2 : max_nodes > MAX_NODES ? MAX_NODES : max_nodes;
    memset(node_names, 0, sizeof(node_names));
    snprintf(local_name, sizeof(local_name), "%s", host ? "PC HOST" : "PC JOINER");
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == SOCK_NONE) { snprintf(err, (size_t)errlen, "cannot create a socket"); return -1; }
    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port = htons((uint16_t)(host ? port : 0));
    if (bind(sock, (struct sockaddr *)&me, sizeof(me)) != 0) { snprintf(err, (size_t)errlen, "cannot bind port %d", port); lw_udp_close(); return -1; }
#ifdef _WIN32
    { u_long one = 1; ioctlsocket(sock, FIONBIO, &one); }
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    memset(&host_addr, 0, sizeof(host_addr));
    host_addr.sin_family = AF_INET;
    host_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    host_addr.sin_port = htons((uint16_t)port);
    if (host) {
        my_node = LW_LINK_HOST_NODE;
        node_mask = 1u << (LW_LINK_HOST_NODE - 1);
        snprintf(node_names[LW_LINK_HOST_NODE], LW_LINK_NAME_MAX, "%s", local_name);
    }
    last_hello_us = 0;
    lw_platform_log("loopback: %s on port %d for %d consoles", host ? "hosting" : "joining", port, max_nodes_cfg);
    return 0;
}

/* Reads datagrams until a game packet (returned in buf) or none. Control datagrams are handled here. */
static int pump(uint8_t *buf, size_t cap, int *from_node)
{
    for (int i = 0; i < 64; ++i) {
        struct sockaddr_in from;
        sa_len_t fl = sizeof(from);
        uint8_t tmp[1600];
        int n = (int)recvfrom(sock, (char *)tmp, sizeof(tmp), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) return 0;
        if (tmp[0] == MSG_HELLO && is_host) {
            int node = node_of(&from);
            if (!node) {
                for (int k = 2; k <= max_nodes_cfg && !node; ++k)
                    if (!(node_mask & (1u << (k - 1)))) node = k;
                if (!node) continue;   /* full */
                client_addr[node] = from;
                node_mask |= 1u << (node - 1);
                size_t l = (size_t)n - 1 < LW_LINK_NAME_MAX - 1 ? (size_t)n - 1 : LW_LINK_NAME_MAX - 1;
                memcpy(node_names[node], tmp + 1, l);
                node_names[node][l] = 0;
                lw_platform_log("loopback: node %d (%s) joined", node, node_names[node]);
            }
            send_welcome(&from, node);
        } else if (tmp[0] == MSG_WELCOME && !is_host) {
            if (!joined && n >= 2) {
                joined = 1;
                my_node = tmp[1];
                size_t l = (size_t)n - 2 < LW_LINK_NAME_MAX - 1 ? (size_t)n - 2 : LW_LINK_NAME_MAX - 1;
                memcpy(node_names[LW_LINK_HOST_NODE], tmp + 2, l);
                node_names[LW_LINK_HOST_NODE][l] = 0;
                snprintf(node_names[my_node], LW_LINK_NAME_MAX, "%s", local_name);
                node_mask = (1u << (LW_LINK_HOST_NODE - 1)) | (1u << (my_node - 1));
            }
        } else if (tmp[0] == MSG_BYE) {
            if (is_host) {
                int node = node_of(&from);
                if (node) { node_mask &= ~(1u << (node - 1)); node_names[node][0] = 0; }
            } else if (joined) {
                node_mask &= ~(1u << (LW_LINK_HOST_NODE - 1));
            }
        } else if (is_host) {
            int node = node_of(&from);
            if (node && (size_t)n <= cap) { memcpy(buf, tmp, (size_t)n); if (from_node) *from_node = node; return n; }
        } else if (joined && (size_t)n <= cap) {
            memcpy(buf, tmp, (size_t)n);
            if (from_node) *from_node = LW_LINK_HOST_NODE;
            return n;
        }
    }
    return 0;
}

int lw_udp_poll(char *err, int errlen)
{
    (void)err; (void)errlen;
    if (sock == SOCK_NONE) return LW_LINK_FAILED;
    if (!is_host && !joined) {
        uint64_t now = lw_platform_time_us();
        if (now - last_hello_us > 200000) { last_hello_us = now; send_hello(); }
        uint8_t scratch[1600];
        int from;
        pump(scratch, sizeof(scratch), &from);   /* the welcome; a game packet before it is dropped */
    }
    return (is_host || joined) ? LW_LINK_READY : LW_LINK_WAITING;
}

int lw_udp_send(int node, const uint8_t *data, size_t len)
{
    if (sock == SOCK_NONE || node < 1 || node > MAX_NODES) return -1;
    if (is_host) {
        if (node == LW_LINK_HOST_NODE || !(node_mask & (1u << (node - 1)))) return -1;
        return raw_send(&client_addr[node], data, len) < 0 ? -1 : 0;
    }
    if (!joined || node != LW_LINK_HOST_NODE) return -1;
    return raw_send(&host_addr, data, len) < 0 ? -1 : 0;
}

int lw_udp_recv(uint8_t *buf, size_t cap, int *from)
{
    if (sock == SOCK_NONE) return 0;
    int src = 0;
    int n = pump(buf, cap, &src);
    if (n > 0 && from) *from = src;
    return n;
}

void lw_udp_close(void)
{
    if (sock != SOCK_NONE) {
        uint8_t bye = MSG_BYE;
        if (is_host) {
            for (int n = 2; n <= MAX_NODES; ++n)
                if (node_mask & (1u << (n - 1))) for (int i = 0; i < 3; ++i) raw_send(&client_addr[n], &bye, 1);
        } else if (joined) {
            for (int i = 0; i < 3; ++i) raw_send(&host_addr, &bye, 1);
        }
        sock_close(sock);
        sock = SOCK_NONE;
    }
#ifdef _WIN32
    if (started_net) { WSACleanup(); started_net = 0; }
#endif
    joined = my_node = 0;
    node_mask = 0;
}
