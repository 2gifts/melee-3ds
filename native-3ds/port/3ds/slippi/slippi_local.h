/* Local wireless transport (port/3ds/slippi/slippi_local_io.c): one datagram
 * per game packet between two consoles. On the 3DS this is UDS (nwm::UDS); the
 * PC tools build a loopback UDP stand-in so the netcode can be tested without
 * a radio. Datagrams may be lost or reordered; nothing here retries. */
#ifndef SLIPPI_LOCAL_H
#define SLIPPI_LOCAL_H
#include <stddef.h>
#include <stdint.h>

#define SLIPPI_IO_WAITING 0
#define SLIPPI_IO_CONNECTED 1
#define SLIPPI_IO_FAILED (-1)
#define SLIPPI_IO_PEER_LEFT (-2)

/* host = 1: create the network and wait; 0: scan for it and join. port: loopback
 * stand-in only. 0 on success; on failure err holds the text. */
int slippi_io_open(int host, int port, char *err, int errlen);
/* Polled: WAITING, CONNECTED (peer = the other console's name), FAILED (err) or PEER_LEFT. */
int slippi_io_state(char *peer, int peerlen, char *err, int errlen);
int slippi_io_send(const uint8_t *data, size_t len);
/* One received datagram into buf (at most cap bytes); its length, 0 when none. */
int slippi_io_recv(uint8_t *buf, size_t cap);
void slippi_io_close(void);
/* This console's name (the 3DS system name), "" before open. */
const char *slippi_io_local_name(void);

#endif
