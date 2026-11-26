/* The UDS link (3DS, libctru uds.h, nwm::UDS) under the local wireless session (lw_link.h).
 *
 * The host creates a network for up to four consoles with a fixed passphrase; clients scan beacons for it and
 * join the one picked. One data frame carries one session packet on data channel 1. The 3DS system name is the player
 * name; other consoles' names are read from the node info. Node ids are whatever UDS assigns (1..16); the session maps
 * them to ports. Elsewhere this file only provides failing stubs. */
#include "localwireless.h"   /* LW_TEXT_WIRELESS_OFF */
#include "lw_link.h"
#include "lw_platform.h"
#include "../name_text.h"
#include <stdio.h>
#include <string.h>
#define LW_MAX_NODES 4   /* consoles in one network */

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
static char node_names[LW_LINK_MAX_NODE + 1][LW_LINK_NAME_MAX];
static uint64_t last_scan_us, last_status_us;
static u8 scan_buf[0x4000];
#define MAX_SCAN 8
#define SCAN_FORGET 3                      /* scans a network may miss before it leaves the list */
static int scan_n;
static struct { udsNetworkStruct net; lw_net_info info; int missed; } scan_list[MAX_SCAN];

/* The scan runs outside the session lock (lw_uds_scan_step), so the engine never waits for it: poll asks for one
 * (scan_due), the scan step does it and leaves its networks in scan_found, and the next poll merges them into
 * scan_list. uds_lock keeps every other UDS call that may come from another thread (joining, closing) from overlapping
 * a scan: they wait for its end. */
static LightLock uds_lock;
static int uds_lock_ready;
static volatile int scan_due, scan_done;
static udsNetworkScanInfo *scan_found;
static size_t scan_found_n;
static unsigned scan_logged;               /* scans timed in the log this session (the first few) */

static void uds_lock_take(void)
{
    if (!uds_lock_ready) { LightLock_Init(&uds_lock); uds_lock_ready = 1; }
    LightLock_Lock(&uds_lock);
}
static void uds_lock_give(void) { LightLock_Unlock(&uds_lock); }
static unsigned ms_since(uint64_t t0) { return (unsigned)((lw_platform_time_us() - t0) / 1000u); }

static void fmt_err(char *err, int errlen, const char *what, Result r)
{
    snprintf(err, (size_t)errlen, "%s (0x%08lX)", what, (unsigned long)r);
}

/* A node's name, whole: udsGetNodeInfoUsername writes at most 10 UTF-8 bytes (3 kana of a 10-kana name), so convert
 * the 10 UTF-16 units ourselves. "3DS" when it is empty. */
static void node_name(const udsNodeInfo *ni, char *out, int cap)
{
    if (name_from_utf16(ni->username, 10, out, cap, NAME_MAX_CHARS) == 0) snprintf(out, (size_t)cap, "3DS");
}

const char *lw_uds_local_name(void) { return local_name; }
unsigned lw_uds_nodes(void) { return node_mask; }
int lw_uds_my_node(void) { return my_node; }
const char *lw_uds_node_name(int node) { return node >= 1 && node <= LW_LINK_MAX_NODE ? node_names[node] : ""; }

int lw_uds_open(int host, int port, int max_nodes, const char *name, char *err, int errlen)
{
    (void)port;
    lw_uds_close();
    scan_n = 0;
    /* the name the session picked (group.ini name=, the Mii name, the system name) goes into this console's node info,
     * so every browser and lobby member sees it; NULL: libctru uses the system name */
    char given[LW_LINK_NAME_MAX];
    name_clean(name, given, sizeof(given), NAME_MAX_CHARS);
    uint64_t t0 = lw_platform_time_us();
    Result r = udsInit(SHAREDMEM, given[0] ? given : NULL);
    lw_platform_log("uds: udsInit took %u ms", ms_since(t0));
    if (R_FAILED(r)) {
        /* Most often wireless communication is switched off. The code stays in the log, not on the screen. */
        lw_platform_log("lw: udsInit failed (0x%08lX)", (unsigned long)r);
        snprintf(err, (size_t)errlen, "%s", LW_TEXT_WIRELESS_OFF);
        return -1;
    }
    up = 1;
    local_name[0] = 0;
    udsNodeInfo me;
    if (R_SUCCEEDED(udsGenerateNodeInfo(&me, given[0] ? given : NULL))) node_name(&me, local_name, sizeof(local_name));
    lw_platform_log("uds: init ok, this console is \"%s\"", local_name);
    my_node = 0;
    node_mask = 0;
    memset(node_names, 0, sizeof(node_names));
    last_scan_us = 0;
    last_status_us = 0;
    scan_due = scan_done = 0;
    scan_logged = 0;
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
    uds_lock_take();   /* after a scan in progress */
    uint64_t t0 = lw_platform_time_us();
    Result r = udsConnectNetwork(&netw, PASSPHRASE, strlen(PASSPHRASE), &bind_ctx, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, CHANNEL,
                                 UDS_DEFAULT_RECVBUFSIZE);
    uds_lock_give();
    lw_platform_log("uds: udsConnectNetwork took %u ms", ms_since(t0));
    if (R_FAILED(r)) { lw_platform_log("uds: join failed 0x%08lx", (unsigned long)r); if (err) fmt_err(err, errlen, "Could not join the game", r); return 0; }
    joined = 1;
    lw_platform_log("uds: joined a game");
    return 1;
}

/* A network's key: its host's MAC address, folded (FNV-1a), never 0. */
static uint32_t mac_key(const u8 *mac)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) h = (h ^ mac[i]) * 16777619u;
    return h ? h : 1;
}

/* The scan itself, without the session lock (the network thread, between its services; see scan_due above). It waits
 * for the wireless hardware, which takes a few hundred milliseconds. */
void lw_uds_scan_step(void)
{
    if (!scan_due || scan_done) return;
    udsNetworkScanInfo *nets = NULL;
    size_t total = 0;
    uds_lock_take();
    if (!up || hosting || joined) { uds_lock_give(); scan_due = 0; return; }
    uint64_t t0 = lw_platform_time_us();
    Result r = udsScanBeacons(scan_buf, sizeof(scan_buf), &nets, &total, WLANCOMM_ID, ID8, NULL, false);
    uds_lock_give();
    if (R_FAILED(r)) { lw_platform_log("uds: scan failed 0x%08lx", (unsigned long)r); free(nets); nets = NULL; total = 0; }
    if (scan_logged < 3) { ++scan_logged; lw_platform_log("uds: scan took %u ms, %u networks", ms_since(t0), (unsigned)total); }
    scan_found = nets;
    scan_found_n = total;
    scan_done = 1;
}

/* Drop a scan's networks that will not be merged. */
static void scan_discard(void)
{
    free(scan_found);
    scan_found = NULL;
    scan_found_n = 0;
    scan_done = scan_due = 0;
}

/* Merge the scan's networks (under the session lock): the list keeps its order, new networks go last, silent ones
 * leave after SCAN_FORGET scans. */
static void scan_merge(void)
{
    udsNetworkScanInfo *nets = scan_found;
    size_t total = scan_found_n;
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
        in->key = mac_key(n->host_macaddress);
        in->players = n->total_nodes;
        in->max = n->max_nodes;
        for (int j = 0; j < UDS_MAXNODES; ++j)
            if (nets[k].nodes[j].NetworkNodeID == LW_LINK_HOST_NODE) {
                node_name(&nets[k].nodes[j], in->host, sizeof(in->host));
            }
        if (!in->host[0]) snprintf(in->host, sizeof(in->host), "3DS");
        size_t got = 0;
        if (R_SUCCEEDED(udsGetNetworkStructApplicationData(n, in->data, sizeof(in->data), &got))) in->len = (int)got;
    }
    free(nets);
    scan_found = NULL;
    scan_found_n = 0;
    scan_done = scan_due = 0;
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
        if (scan_done) {
            scan_merge();
            last_scan_us = lw_platform_time_us();
        } else if (!scan_due && (now - last_scan_us >= SCAN_INTERVAL_US || !last_scan_us)) {
            scan_due = 1;   /* lw_uds_scan_step scans, outside the lock */
        }
        return LW_LINK_WAITING;
    }
    if (scan_done || scan_due) scan_discard();   /* joined meanwhile: that scan is of no use */
    if (now - last_status_us >= STATUS_INTERVAL_US || !last_status_us) {
        last_status_us = now;
        udsConnectionStatus cs;
        if (R_FAILED(udsGetConnectionStatus(&cs))) {
            node_mask = 0;
        } else {
            /* Node ids are 1-based bit positions in node_bitmask; the host is node 1. Every id is kept: UDS decides
             * the numbering, so a client may well be node 5 after others came and went. */
            node_mask = cs.node_bitmask & ((1u << LW_LINK_MAX_NODE) - 1);
            my_node = (int)cs.cur_NetworkNodeID;
            for (int n = 1; n <= LW_LINK_MAX_NODE; ++n) {
                if (!(node_mask & (1u << (n - 1)))) { node_names[n][0] = 0; continue; }
                if (node_names[n][0]) continue;
                udsNodeInfo ni;
                if (R_SUCCEEDED(udsGetNodeInformation((u16)n, &ni))) node_name(&ni, node_names[n], LW_LINK_NAME_MAX);
                else snprintf(node_names[n], LW_LINK_NAME_MAX, "3DS");
            }
        }
    }
    return LW_LINK_READY;
}

int lw_uds_send(int node, const uint8_t *data, size_t len)
{
    if (!up || node < 1 || node > LW_LINK_MAX_NODE || len > UDS_DATAFRAME_MAXSIZE) return -1;
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

int lw_uds_scan_count(void) { return !hosting && !joined ? scan_n : 0; }
int lw_uds_scan_get(int i, lw_net_info *out)
{
    if (hosting || joined || i < 0 || i >= scan_n) return -1;
    *out = scan_list[i].info;
    return 0;
}
int lw_uds_join(uint32_t key, char *err, int errlen)
{
    int i = 0;
    while (i < scan_n && scan_list[i].info.key != key) ++i;
    if (!up || hosting || joined || i >= scan_n) { snprintf(err, (size_t)errlen, "That game is gone"); return -1; }
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
    uds_lock_take();   /* after a scan in progress */
    uint64_t t0 = lw_platform_time_us();
    scan_discard();
    if (hosting) udsDestroyNetwork();
    else if (joined) udsDisconnectNetwork();
    if (hosting || joined) udsUnbind(&bind_ctx);
    hosting = joined = my_node = 0;
    node_mask = 0;
    scan_n = 0;
    udsExit();
    up = 0;
    uds_lock_give();
    lw_platform_log("uds: closed (%u ms)", ms_since(t0));
}
#else /* not a 3DS: no UDS */
int lw_uds_open(int host, int port, int max_nodes, const char *name, char *err, int errlen) { (void)host; (void)name; (void)port; (void)max_nodes; snprintf(err, (size_t)errlen, "UDS needs a 3DS"); return -1; }
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
int lw_uds_join(uint32_t key, char *e, int n) { (void)key; snprintf(e, (size_t)n, "UDS needs a 3DS"); return -1; }
void lw_uds_scan_step(void) {}
void lw_uds_set_beacon(const uint8_t *d, int n) { (void)d; (void)n; }
void lw_uds_eject(int node) { (void)node; }
#endif
