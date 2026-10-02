/**
 @file  ctru.c
 @brief ENet platform functions for the Nintendo 3DS (libctru soc:U).

 Written for the Melee 3DS port's Slippi experiment, modelled on ENet's own
 unix.c (MIT, see LICENSE). Differences from unix.c:
 - soc:U has no sendmsg/recvmsg, so scatter/gather buffers are joined into one
   datagram for sendto() and received with recvfrom() into the first buffer
   (ENet always receives into a single buffer).
 - time comes from the monotonic ARM11 system tick, not gettimeofday().
 - the socket buffer options are clamped: soc:U carves socket buffers out of
   the socInit() context, and ENet's 256 KiB defaults would not fit.
 - name lookup uses gethostbyname()/inet_aton(); there is no reverse lookup.
 The caller must have run socInit() before enet_initialize().
*/
#ifdef __3DS__

#include <3ds.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

/* Socket buffers requested from soc:U (ENet asks for 256 KiB each). */
#define ENET_CTRU_MAX_SOCKET_BUFFER (32 * 1024)

#ifndef SOMAXCONN
#define SOMAXCONN 8
#endif

static enet_uint32 timeBase = 0;

int
enet_initialize (void)
{
    return 0;
}

void
enet_deinitialize (void)
{
}

enet_uint32
enet_host_random_seed (void)
{
    u64 tick = svcGetSystemTick ();
    return (enet_uint32) (tick ^ (tick >> 32));
}

static enet_uint32
ctru_milliseconds (void)
{
    u64 tick = svcGetSystemTick ();
    u64 seconds = tick / SYSCLOCK_ARM11;
    u64 rest = tick % SYSCLOCK_ARM11;
    return (enet_uint32) (seconds * 1000 + rest * 1000 / SYSCLOCK_ARM11);
}

enet_uint32
enet_time_get (void)
{
    return ctru_milliseconds () - timeBase;
}

void
enet_time_set (enet_uint32 newTimeBase)
{
    timeBase = ctru_milliseconds () - newTimeBase;
}

int
enet_address_set_host (ENetAddress * address, const char * name)
{
    struct in_addr literal;
    struct hostent * hostEntry;

    /* Dotted quads need no DNS round trip. */
    if (inet_aton (name, & literal))
    {
        address -> host = literal.s_addr;
        return 0;
    }

    hostEntry = gethostbyname (name);
    if (hostEntry != NULL && hostEntry -> h_addrtype == AF_INET && hostEntry -> h_addr_list [0] != NULL)
    {
        memcpy (& address -> host, hostEntry -> h_addr_list [0], sizeof (enet_uint32));
        return 0;
    }

    return -1;
}

int
enet_address_get_host_ip (const ENetAddress * address, char * name, size_t nameLength)
{
    const unsigned char * b = (const unsigned char *) & address -> host;
    char text [16];
    size_t length;
    int i, n = 0;

    for (i = 0; i < 4; ++ i)
    {
        unsigned v = b [i];
        if (i) text [n ++] = '.';
        if (v >= 100) text [n ++] = (char) ('0' + v / 100);
        if (v >= 10) text [n ++] = (char) ('0' + (v / 10) % 10);
        text [n ++] = (char) ('0' + v % 10);
    }
    text [n] = '\0';
    length = (size_t) n;
    if (length >= nameLength)
      return -1;
    memcpy (name, text, length + 1);
    return 0;
}

int
enet_address_get_host (const ENetAddress * address, char * name, size_t nameLength)
{
    return enet_address_get_host_ip (address, name, nameLength);
}

int
enet_socket_bind (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;

    memset (& sin, 0, sizeof (struct sockaddr_in));

    sin.sin_family = AF_INET;

    if (address != NULL)
    {
       sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
       sin.sin_addr.s_addr = address -> host;
    }
    else
    {
       sin.sin_port = 0;
       sin.sin_addr.s_addr = INADDR_ANY;
    }

    return bind (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
}

int
enet_socket_get_address (ENetSocket socket, ENetAddress * address)
{
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);

    memset (& sin, 0, sizeof (sin));
    if (getsockname (socket, (struct sockaddr *) & sin, & sinLength) == -1)
      return -1;

    address -> host = (enet_uint32) sin.sin_addr.s_addr;
    address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);

    return 0;
}

int
enet_socket_listen (ENetSocket socket, int backlog)
{
    return listen (socket, backlog < 0 ? SOMAXCONN : backlog);
}

ENetSocket
enet_socket_create (ENetSocketType type)
{
    return socket (AF_INET, type == ENET_SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
}

int
enet_socket_set_option (ENetSocket socket, ENetSocketOption option, int value)
{
    int result = -1;
    switch (option)
    {
        case ENET_SOCKOPT_NONBLOCK:
        {
            int flags = fcntl (socket, F_GETFL, 0);
            if (flags == -1)
              flags = 0;
            result = fcntl (socket, F_SETFL, value ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
            break;
        }

        case ENET_SOCKOPT_BROADCAST:
            /* SO_BROADCAST is a no-op define on soc:U; broadcast is never used here. */
            result = 0;
            break;

        case ENET_SOCKOPT_REUSEADDR:
            result = setsockopt (socket, SOL_SOCKET, SO_REUSEADDR, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_RCVBUF:
            if (value > ENET_CTRU_MAX_SOCKET_BUFFER)
              value = ENET_CTRU_MAX_SOCKET_BUFFER;
            result = setsockopt (socket, SOL_SOCKET, SO_RCVBUF, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_SNDBUF:
            if (value > ENET_CTRU_MAX_SOCKET_BUFFER)
              value = ENET_CTRU_MAX_SOCKET_BUFFER;
            result = setsockopt (socket, SOL_SOCKET, SO_SNDBUF, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_RCVTIMEO:
        case ENET_SOCKOPT_SNDTIMEO:
        case ENET_SOCKOPT_NODELAY:
            /* Only used by TCP sockets, which ENet never creates for hosts. */
            result = 0;
            break;

        default:
            break;
    }
    return result == -1 ? -1 : 0;
}

int
enet_socket_get_option (ENetSocket socket, ENetSocketOption option, int * value)
{
    int result = -1;
    socklen_t len;
    switch (option)
    {
        case ENET_SOCKOPT_ERROR:
            len = sizeof (int);
            result = getsockopt (socket, SOL_SOCKET, SO_ERROR, value, & len);
            break;

        default:
            break;
    }
    return result == -1 ? -1 : 0;
}

int
enet_socket_connect (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;
    int result;

    memset (& sin, 0, sizeof (struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
    sin.sin_addr.s_addr = address -> host;

    result = connect (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    if (result == -1 && errno == EINPROGRESS)
      return 0;

    return result;
}

ENetSocket
enet_socket_accept (ENetSocket socket, ENetAddress * address)
{
    int result;
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);

    result = accept (socket,
                     address != NULL ? (struct sockaddr *) & sin : NULL,
                     address != NULL ? & sinLength : NULL);

    if (result == -1)
      return ENET_SOCKET_NULL;

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return result;
}

int
enet_socket_shutdown (ENetSocket socket, ENetSocketShutdown how)
{
    return shutdown (socket, (int) how);
}

void
enet_socket_destroy (ENetSocket socket)
{
    if (socket != -1)
      closesocket (socket);
}

int
enet_socket_send (ENetSocket socket,
                  const ENetAddress * address,
                  const ENetBuffer * buffers,
                  size_t bufferCount)
{
    /* ENet never builds a datagram larger than its maximum MTU. */
    unsigned char datagram [ENET_PROTOCOL_MAXIMUM_MTU];
    struct sockaddr_in sin;
    size_t length = 0, i;
    int sentLength;

    for (i = 0; i < bufferCount; ++ i)
    {
        if (length + buffers [i].dataLength > sizeof (datagram))
          return -1;
        memcpy (datagram + length, buffers [i].data, buffers [i].dataLength);
        length += buffers [i].dataLength;
    }

    if (address != NULL)
    {
        memset (& sin, 0, sizeof (struct sockaddr_in));

        sin.sin_family = AF_INET;
        sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
        sin.sin_addr.s_addr = address -> host;

        sentLength = sendto (socket, datagram, length, 0, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    }
    else
        sentLength = send (socket, datagram, length, 0);

    if (sentLength == -1)
    {
       if (errno == EWOULDBLOCK || errno == EAGAIN)
         return 0;

       return -1;
    }

    return sentLength;
}

int
enet_socket_receive (ENetSocket socket,
                     ENetAddress * address,
                     ENetBuffer * buffers,
                     size_t bufferCount)
{
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);
    int recvLength;

    if (bufferCount < 1)
      return -1;

    memset (& sin, 0, sizeof (sin));
    recvLength = recvfrom (socket, buffers [0].data, buffers [0].dataLength, 0,
                           address != NULL ? (struct sockaddr *) & sin : NULL,
                           address != NULL ? & sinLength : NULL);

    if (recvLength == -1)
    {
       if (errno == EWOULDBLOCK || errno == EAGAIN)
         return 0;

       return -1;
    }

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return recvLength;
}

int
enet_socketset_select (ENetSocket maxSocket, ENetSocketSet * readSet, ENetSocketSet * writeSet, enet_uint32 timeout)
{
    struct timeval timeVal;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    return select (maxSocket + 1, readSet, writeSet, NULL, & timeVal);
}

int
enet_socket_wait (ENetSocket socket, enet_uint32 * condition, enet_uint32 timeout)
{
    struct pollfd pollSocket;
    int pollCount;

    pollSocket.fd = socket;
    pollSocket.events = 0;
    pollSocket.revents = 0;

    if (* condition & ENET_SOCKET_WAIT_SEND)
      pollSocket.events |= POLLOUT;

    if (* condition & ENET_SOCKET_WAIT_RECEIVE)
      pollSocket.events |= POLLIN;

    pollCount = poll (& pollSocket, 1, (int) timeout);

    if (pollCount < 0)
    {
        if (errno == EINTR && * condition & ENET_SOCKET_WAIT_INTERRUPT)
        {
            * condition = ENET_SOCKET_WAIT_INTERRUPT;

            return 0;
        }

        return -1;
    }

    * condition = ENET_SOCKET_WAIT_NONE;

    if (pollCount == 0)
      return 0;

    if (pollSocket.revents & POLLOUT)
      * condition |= ENET_SOCKET_WAIT_SEND;

    if (pollSocket.revents & POLLIN)
      * condition |= ENET_SOCKET_WAIT_RECEIVE;

    return 0;
}

#endif /* __3DS__ */
