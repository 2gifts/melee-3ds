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

/* Each backend implements this set under its own prefix (uds_, udp_). */
#define LW_LINK_DECLARE(p) \
    int p##_open(int host, int port, int max_nodes, char *err, int errlen); \
    int p##_poll(char *err, int errlen);                   /* WAITING, READY or FAILED; refreshes nodes */ \
    unsigned p##_nodes(void);                              /* bit (n-1) set for every node n, this one included */ \
    int p##_my_node(void); \
    const char *p##_node_name(int node); \
    int p##_send(int node, const uint8_t *data, size_t len); /* a client can only reach node 1 */ \
    int p##_recv(uint8_t *buf, size_t cap, int *from);     /* one datagram, 0 when none; *from = source node */ \
    void p##_close(void); \
    const char *p##_local_name(void);

LW_LINK_DECLARE(lw_uds)
LW_LINK_DECLARE(lw_udp)

#endif
