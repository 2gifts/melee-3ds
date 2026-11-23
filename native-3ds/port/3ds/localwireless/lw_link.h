/* The link under the session (internal): up to four nodes, host = node 1. UDS (lw_link_uds.c) or the
 * loopback UDP stand-in (lw_link_udp.c). Datagrams may be lost or reordered; nothing here retries. */
#ifndef LW_LINK_H
#define LW_LINK_H
#include <stddef.h>
#include <stdint.h>

#define LW_LINK_WAITING 0
#define LW_LINK_READY 1       /* the network exists / the client has joined it */
#define LW_LINK_FAILED (-1)
#define LW_LINK_HOST_NODE 1
#define LW_LINK_NAME_MAX 12
#define LW_LINK_APPDATA_MAX 200   /* a UDS beacon carries up to 0xC8 bytes of application data */
#define LW_LINK_MODE_JOIN_FIRST 0 /* the `host` argument of open: join the first network found (fixed-roster sessions) */
#define LW_LINK_MODE_HOST 1
#define LW_LINK_MODE_BROWSE 2     /* scan and keep a list; join one with join() */

typedef struct {
    char host[LW_LINK_NAME_MAX];  /* the host console's name */
    int players, max;             /* consoles on the network / most it takes */
    int len;
    uint8_t data[LW_LINK_APPDATA_MAX];   /* the host's beacon blob */
} lw_net_info;

/* Each backend implements this set under its own prefix (uds_, udp_). */
#define LW_LINK_DECLARE(p) \
    int p##_open(int mode, int port, int max_nodes, char *err, int errlen); /* mode: LW_LINK_MODE_* */ \
    int p##_poll(char *err, int errlen);                   /* WAITING, READY or FAILED; refreshes nodes */ \
    unsigned p##_nodes(void);                              /* bit (n-1) set for every node n, this one included */ \
    int p##_my_node(void); \
    const char *p##_node_name(int node); \
    int p##_send(int node, const uint8_t *data, size_t len); /* a client can only reach node 1 */ \
    int p##_recv(uint8_t *buf, size_t cap, int *from);     /* one datagram, 0 when none; *from = source node */ \
    void p##_close(void); \
    /* browse mode: the list refreshes inside poll and keeps its order (a console that goes quiet is dropped after a few scans) */ \
    int p##_scan_count(void); \
    int p##_scan_get(int i, lw_net_info *out);             /* 0 ok, -1 no such entry */ \
    int p##_join(int i, char *err, int errlen);            /* 0: joined (the link is READY); -1: failed, still browsing */ \
    void p##_set_beacon(const uint8_t *data, int len);     /* host: what scans of this network show (<= LW_LINK_APPDATA_MAX) */ \
    void p##_eject(int node);                              /* host: remove a client; best effort */ \
    const char *p##_local_name(void);

LW_LINK_DECLARE(lw_uds)
LW_LINK_DECLARE(lw_udp)

#endif
