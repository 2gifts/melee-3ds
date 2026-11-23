/* The UDS link (3DS, libctru uds.h, nwm::UDS) under the local wireless session (lw_link.h).
 *
 * The host creates a network for up to four consoles with a fixed passphrase; clients scan beacons for it and
 * join. One data frame carries one session packet on data channel 1. The 3DS system name is the player name;
 * other consoles' names are read from the node info. Elsewhere this file only provides failing stubs. */
#include "lw_link.h"
#include "lw_platform.h"
#include <stdio.h>
#include <string.h>
#define LW_MAX_NODES 4

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
#define STATUS_INTERVAL_US 50000ull

void lw_uds_close(void);
static int up, hosting, joined, my_node;
static unsigned node_mask;
static udsBindContext bind_ctx;
static udsNetworkStruct netw;
static char local_name[32];
static char node_names[LW_MAX_NODES + 1][LW_LINK_NAME_MAX];
static uint64_t last_scan_us, last_status_us;
static u8 scan_buf[0x4000];
#define MAX_SCAN 8
#define SCAN_FORGET 3                      /* scans a network may miss before it leaves the list */
static int browse, scan_n;
static struct { udsNetworkStruct net; lw_net_info info; int missed; } scan_list[MAX_SCAN];

static void fmt_err(char *err, int errlen, const char *what, Result r)
{
    snprintf(err, (size_t)errlen, "%s (0x%08lX)", what, (unsigned long)r);
}

const char *lw_uds_local_name(void) { return local_name; }
unsigned lw_uds_nodes(void) { return node_mask; }
int lw_uds_my_node(void) { return my_node; }
const char *lw_uds_node_name(int node) { return node >= 1 && node <= LW_MAX_NODES ? node_names[node] : ""; }

int lw_uds_open(int mode, int port, int max_nodes, char *err, int errlen)
{
    int host = mode == LW_LINK_MODE_HOST;
    browse = mode == LW_LINK_MODE_BROWSE;
    scan_n = 0;
    (void)port;
    lw_uds_close();
    Result r = udsInit(SHAREDMEM, NULL);
    if (R_FAILED(r)) { fmt_err(err, errlen, "Wireless is unavailable", r); return -1; }
    up = 1;
    local_name[0] = 0;
    udsNodeInfo me;
    if (R_SUCCEEDED(udsGenerateNodeInfo(&me, NULL))) udsGetNodeInfoUsername(&me, local_name);
    lw_platform_log("uds: init ok, this console is \"%s\"", local_name);
    my_node = 0;
    node_mask = 0;
    memset(node_names, 0, sizeof(node_names));
    last_scan_us = 0;
    last_status_us = 0;
    if (host) {
        if (max_nodes < 2) max_nodes = 2;
        if (max_nodes > LW_MAX_NODES) max_nodes = LW_MAX_NODES;
        udsGenerateDefaultNetworkStruct(&netw, WLANCOMM_ID, ID8, (u8)max_nodes);
        r = udsCreateNetwork(&netw, PASSPHRASE, strlen(PASSPHRASE), &bind_ctx, CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
        if (R_FAILED(r)) { fmt_err(err, errlen, "Could not create the game", r); lw_uds_close(); return -1; }
        hosting = 1;
        my_node = LW_LINK_HOST_NODE;
        lw_platform_log("uds: hosting for %d consoles", max_nodes);
    }
    return 0;
}

static int connect_to(const udsNetworkStruct *n, char *err, int errlen)
{
    netw = *n;
    Result r = udsConnectNetwork(&netw, PASSPHRASE, strlen(PASSPHRASE), &bind_ctx, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, CHANNEL,
                                 UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(r)) { lw_platform_log("uds: join failed 0x%08lx", (unsigned long)r); if (err) fmt_err(err, errlen, "Could not join the game", r); return 0; }
    joined = 1;
    lw_platform_log("uds: joined a game");
    return 1;
}

/* Scans once; joins the first game found. Returns 0 on failure (retried later). */
static int try_join(void)
{
    udsNetworkScanInfo *nets = NULL;
    size_t total = 0;
    Result r = udsScanBeacons(scan_buf, sizeof(scan_buf), &nets, &total, WLANCOMM_ID, ID8, NULL, false);
    if (R_FAILED(r) || total == 0) {
        if (R_FAILED(r)) lw_platform_log("uds: scan failed 0x%08lx", (unsigned long)r);
        free(nets);
        return 0;
    }
    udsNetworkStruct first = nets[0].network;
    free(nets);
    return connect_to(&first, NULL, 0);
}

/* Browse mode: one scan; the list keeps its order, new networks go last, silent ones leave after SCAN_FORGET scans. */
static void scan_refresh(void)
{
    udsNetworkScanInfo *nets = NULL;
    size_t total = 0;
    Result r = udsScanBeacons(scan_buf, sizeof(scan_buf), &nets, &total, WLANCOMM_ID, ID8, NULL, false);
    if (R_FAILED(r)) { lw_platform_log("uds: scan failed 0x%08lx", (unsigned long)r); total = 0; }
    int seen[MAX_SCAN] = {0};
    for (size_t k = 0; k < total; ++k) {
        const udsNetworkStruct *n = &nets[k].network;
        int at = -1;
        for (int i = 0; i < scan_n; ++i)
            if (!memcmp(scan_list[i].net.host_macaddress, n->host_macaddress, 6)) at = i;
        if (at < 0) {
            if (scan_n == MAX_SCAN) continue;
            at = scan_n++;
        }
        seen[at] = 1;
        scan_list[at].net = *n;
        scan_list[at].missed = 0;
        lw_net_info *in = &scan_list[at].info;
        memset(in, 0, sizeof(*in));
        in->players = n->total_nodes;
        in->max = n->max_nodes;
        for (int j = 0; j < UDS_MAXNODES; ++j)
            if (nets[k].nodes[j].NetworkNodeID == LW_LINK_HOST_NODE) {
                char nm[16] = "";
                udsGetNodeInfoUsername(&nets[k].nodes[j], nm);
                snprintf(in->host, sizeof(in->host), "%s", nm[0] ? nm : "3DS");
            }
        if (!in->host[0]) snprintf(in->host, sizeof(in->host), "3DS");
        size_t got = 0;
        if (R_SUCCEEDED(udsGetNetworkStructApplicationData(n, in->data, sizeof(in->data), &got))) in->len = (int)got;
    }
    free(nets);
    int out = 0;
    for (int i = 0; i < scan_n; ++i) {
        if (!seen[i] && ++scan_list[i].missed >= SCAN_FORGET) continue;
        scan_list[out++] = scan_list[i];
    }
    scan_n = out;
}

int lw_uds_poll(char *err, int errlen)
{
    (void)err; (void)errlen;
    if (!up) return LW_LINK_FAILED;
    uint64_t now = lw_platform_time_us();
    if (!hosting && !joined) {
        if (now - last_scan_us < SCAN_INTERVAL_US && last_scan_us) return LW_LINK_WAITING;
        last_scan_us = now;
        if (browse) { scan_refresh(); last_scan_us = lw_platform_time_us(); return LW_LINK_WAITING; }
        if (!try_join()) return LW_LINK_WAITING;
    }
    if (now - last_status_us >= STATUS_INTERVAL_US || !last_status_us) {
        last_status_us = now;
        udsConnectionStatus cs;
        if (R_FAILED(udsGetConnectionStatus(&cs))) {
            node_mask = 0;
        } else {
            /* Node ids are 1-based bit positions in node_bitmask; the host is node 1. */
            node_mask = cs.node_bitmask & ((1u << LW_MAX_NODES) - 1);
            my_node = (int)cs.cur_NetworkNodeID;
            for (int n = 1; n <= LW_MAX_NODES; ++n) {
                if (!(node_mask & (1u << (n - 1)))) { node_names[n][0] = 0; continue; }
                if (node_names[n][0]) continue;
                udsNodeInfo ni;
                char nm[16] = "";
                if (R_SUCCEEDED(udsGetNodeInformation((u16)n, &ni))) udsGetNodeInfoUsername(&ni, nm);
                snprintf(node_names[n], LW_LINK_NAME_MAX, "%s", nm[0] ? nm : "3DS");
            }
        }
    }
    return LW_LINK_READY;
}

int lw_uds_send(int node, const uint8_t *data, size_t len)
{
    if (!up || node < 1 || node > LW_MAX_NODES || len > UDS_DATAFRAME_MAXSIZE) return -1;
    Result r = udsSendTo((u16)node, CHANNEL, UDS_SENDFLAG_Default, data, len);
    /* 0xC86113F0: the frame was dropped (queue full); the netcode resends. */
    return (r < 0 && (u32)r != 0xC86113F0u) ? -1 : 0;
}

int lw_uds_recv(uint8_t *buf, size_t cap, int *from)
{
    if (!up || !(hosting || joined)) return 0;
    size_t n = 0;
    u16 src = 0;
    if (R_FAILED(udsPullPacket(&bind_ctx, buf, cap, &n, &src))) return 0;
    if (from) *from = (int)src;
    return (int)n;
}

int lw_uds_scan_count(void) { return browse && !joined ? scan_n : 0; }
int lw_uds_scan_get(int i, lw_net_info *out)
{
    if (!browse || joined || i < 0 || i >= scan_n) return -1;
    *out = scan_list[i].info;
    return 0;
}
int lw_uds_join(int i, char *err, int errlen)
{
    if (!up || !browse || joined || i < 0 || i >= scan_n) { snprintf(err, (size_t)errlen, "That game is gone"); return -1; }
    if (!connect_to(&scan_list[i].net, err, errlen)) return -1;
    last_status_us = 0;
    return 0;
}
void lw_uds_set_beacon(const uint8_t *data, int len)
{
    if (!up || !hosting) return;
    if (len > LW_LINK_APPDATA_MAX) len = LW_LINK_APPDATA_MAX;
    Result r = udsSetApplicationData(data, (size_t)len);
    if (R_FAILED(r)) lw_platform_log("uds: set appdata failed 0x%08lx", (unsigned long)r);
}
void lw_uds_eject(int node)
{
    if (up && hosting && node >= 2) udsEjectClient((u16)node);
}

void lw_uds_close(void)
{
    if (!up) return;
    if (hosting) udsDestroyNetwork();
    else if (joined) udsDisconnectNetwork();
    if (hosting || joined) udsUnbind(&bind_ctx);
    hosting = joined = my_node = 0;
    node_mask = 0;
    scan_n = 0;
    udsExit();
    up = 0;
    lw_platform_log("uds: closed");
}
#else /* not a 3DS: no UDS */
int lw_uds_open(int mode, int port, int max_nodes, char *err, int errlen) { (void)mode; (void)port; (void)max_nodes; snprintf(err, (size_t)errlen, "UDS needs a 3DS"); return -1; }
int lw_uds_poll(char *err, int errlen) { (void)err; (void)errlen; return LW_LINK_FAILED; }
unsigned lw_uds_nodes(void) { return 0; }
int lw_uds_my_node(void) { return 0; }
const char *lw_uds_node_name(int node) { (void)node; return ""; }
int lw_uds_send(int node, const uint8_t *d, size_t n) { (void)node; (void)d; (void)n; return -1; }
int lw_uds_recv(uint8_t *b, size_t c, int *f) { (void)b; (void)c; (void)f; return 0; }
void lw_uds_close(void) {}
const char *lw_uds_local_name(void) { return ""; }
int lw_uds_scan_count(void) { return 0; }
int lw_uds_scan_get(int i, lw_net_info *o) { (void)i; (void)o; return -1; }
int lw_uds_join(int i, char *e, int n) { (void)i; snprintf(e, (size_t)n, "UDS needs a 3DS"); return -1; }
void lw_uds_set_beacon(const uint8_t *d, int n) { (void)d; (void)n; }
void lw_uds_eject(int node) { (void)node; }
#endif
