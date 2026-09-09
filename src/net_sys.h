// net_sys.h - the one place that knows the difference between Winsock and BSD sockets.
//
// Header-only, no .c file, no dependency on SDL. Include it instead of <sys/socket.h> and friends:
//
//     #include "net_sys.h"
//
// and then use plain socket(), bind(), sendto(), recvfrom(), setsockopt(), getaddrinfo(),
// inet_pton(), htons()/ntohl() as normal - those spellings are identical on both platforms.
// Only the six things that genuinely differ are wrapped:
//
//     netsys_socket            the handle type          (int on unix, SOCKET on Windows)
//     NETSYS_INVALID_SOCKET    the "no socket" value    (-1 on unix, INVALID_SOCKET on Windows)
//     netsys_init/quit         WSAStartup / WSACleanup  (no-ops on unix)
//     netsys_close             close() / closesocket()
//     netsys_set_nonblocking   O_NONBLOCK / FIONBIO
//     netsys_errno + friends   errno / WSAGetLastError()
//
// Rules for the caller:
//   * store handles in `netsys_socket`, never in `int` - SOCKET is a 64-bit UINT_PTR on Win64 and
//     an int would silently truncate it;
//   * test with netsys_valid(s), never with `s >= 0` - SOCKET is unsigned;
//   * after a failing recvfrom/sendto read the code once with netsys_errno() and classify it with
//     netsys_would_block() / netsys_is_conn_reset(); errno itself is meaningless on Windows;
//   * call netsys_init() before the first socket and netsys_quit() at shutdown (both are
//     reference counted and safe to nest).
//
// Windows link requirement: ws2_32 (CMakeLists.txt already links it on WIN32).

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------- platform includes and types

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>   // must precede windows.h
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>

typedef SOCKET   netsys_socket;
typedef int      netsys_socklen;   // Windows takes int* where unix takes socklen_t*
typedef int      netsys_ssize;     // send/recv return int on Windows, ssize_t on unix
#define NETSYS_INVALID_SOCKET INVALID_SOCKET
#define NETSYS_SOCKET_ERROR   SOCKET_ERROR

#else   // ---------------------------------------------------------------- unix (macOS, Linux)

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

typedef int       netsys_socket;
typedef socklen_t netsys_socklen;
typedef ssize_t   netsys_ssize;
#define NETSYS_INVALID_SOCKET (-1)
#define NETSYS_SOCKET_ERROR   (-1)

#endif

// setsockopt/getsockopt take `const char *` on Windows and `const void *` on unix. Pass option
// values through this cast so one call site compiles on both.
#if defined(_WIN32)
#define NETSYS_OPTVAL(p)  ((const char *)(p))
#define NETSYS_OPTVAL_RW(p) ((char *)(p))
#else
#define NETSYS_OPTVAL(p)  ((const void *)(p))
#define NETSYS_OPTVAL_RW(p) ((void *)(p))
#endif

// ---------------------------------------------------------------- library init

// WSAStartup on Windows, nothing on unix. Reference counted: N calls to netsys_init need N calls
// to netsys_quit. Returns false only if Winsock itself refuses to start.
#if defined(_WIN32)
static inline int *netsys__refs(void) { static int refs = 0; return &refs; }
#endif

static inline bool netsys_init(void) {
#if defined(_WIN32)
    int *refs = netsys__refs();
    if (*refs == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    }
    (*refs)++;
#endif
    return true;
}

static inline void netsys_quit(void) {
#if defined(_WIN32)
    int *refs = netsys__refs();
    if (*refs > 0 && --(*refs) == 0) WSACleanup();
#endif
}

// ---------------------------------------------------------------- handles

static inline bool netsys_valid(netsys_socket s) {
    return s != NETSYS_INVALID_SOCKET;
}

static inline void netsys_close(netsys_socket s) {
    if (!netsys_valid(s)) return;
#if defined(_WIN32)
    closesocket(s);
#else
    close(s);
#endif
}

// Non-blocking mode. Returns false and leaves the socket alone on failure.
static inline bool netsys_set_nonblocking(netsys_socket s) {
#if defined(_WIN32)
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// ---------------------------------------------------------------- errors

// The last socket error for this thread. Read it immediately after the failing call.
static inline int netsys_errno(void) {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

// "no datagram was waiting" - the normal, boring result of a non-blocking recvfrom.
static inline bool netsys_would_block(int err) {
#if defined(_WIN32)
    return err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

// Windows quirk: after a sendto whose destination replied ICMP port-unreachable, the NEXT
// recvfrom on that UDP socket fails with WSAECONNRESET instead of returning a packet. On unix
// that never happens for unconnected UDP. Treat it like "would block" - or, better, call
// netsys_udp_ignore_conn_reset() once after bind and never see it. ECONNREFUSED is the Linux
// equivalent when the socket has been connect()ed.
static inline bool netsys_is_conn_reset(int err) {
#if defined(_WIN32)
    return err == WSAECONNRESET || err == WSAENETRESET;
#else
    return err == ECONNREFUSED || err == ECONNRESET;
#endif
}

// A signal that the peer is unreachable rather than a bug on our side.
static inline bool netsys_is_unreachable(int err) {
#if defined(_WIN32)
    return err == WSAEHOSTUNREACH || err == WSAENETUNREACH || err == WSAEHOSTDOWN;
#else
    return err == EHOSTUNREACH || err == ENETUNREACH || err == EHOSTDOWN;
#endif
}

// Human-readable message for a code from netsys_errno(). Writes into the caller's buffer so it is
// thread safe; returns buf for convenient use inside a log call.
static inline const char *netsys_strerror(int err, char *buf, size_t n) {
    if (!buf || n == 0) return "";
#if defined(_WIN32)
    DWORD w = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                             (DWORD)err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             buf, (DWORD)n, NULL);
    if (w == 0) snprintf(buf, n, "winsock error %d", err);
    else {   // strip the trailing CRLF FormatMessage insists on
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
    }
#else
    snprintf(buf, n, "%s", strerror(err));
#endif
    return buf;
}

// ---------------------------------------------------------------- UDP behaviour fixes

// Windows only: stop the WSAECONNRESET-on-recvfrom behaviour described above. No-op elsewhere.
// Call once, after socket() and before or after bind(). Returns true if the socket is now sane.
static inline bool netsys_udp_ignore_conn_reset(netsys_socket s) {
#if defined(_WIN32)
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
    BOOL off = FALSE; DWORD out = 0;
    return WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &out, NULL, NULL) == 0;
#else
    (void)s;
    return true;
#endif
}

// ---------------------------------------------------------------- optional helpers

// Wait up to timeout_ms for the socket to have a datagram ready. 0 polls, <0 blocks forever.
// Returns 1 = readable, 0 = timed out, -1 = error. Only needed for a dedicated network thread;
// the game loop just polls recvfrom every tick.
static inline int netsys_wait_readable(netsys_socket s, int timeout_ms) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }
#if defined(_WIN32)
    int n = select(0, &r, NULL, NULL, ptv);   // first argument ignored on Windows
#else
    int n = select(s + 1, &r, NULL, NULL, ptv);
#endif
    return n < 0 ? -1 : (n > 0 ? 1 : 0);
}

// Open a bound, non-blocking IPv4 UDP socket with the usual options, or NETSYS_INVALID_SOCKET.
// port 0 = let the OS choose (clients). Convenience only; the caller may do this by hand.
static inline netsys_socket netsys_udp_open(unsigned short port, int rcvbuf, int sndbuf) {
    netsys_socket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (!netsys_valid(s)) return NETSYS_INVALID_SOCKET;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, NETSYS_OPTVAL(&one), sizeof one);
    if (rcvbuf > 0) setsockopt(s, SOL_SOCKET, SO_RCVBUF, NETSYS_OPTVAL(&rcvbuf), sizeof rcvbuf);
    if (sndbuf > 0) setsockopt(s, SOL_SOCKET, SO_SNDBUF, NETSYS_OPTVAL(&sndbuf), sizeof sndbuf);
    if (!netsys_set_nonblocking(s)) { netsys_close(s); return NETSYS_INVALID_SOCKET; }
    netsys_udp_ignore_conn_reset(s);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&sa, (netsys_socklen)sizeof sa) == NETSYS_SOCKET_ERROR) {
        netsys_close(s);
        return NETSYS_INVALID_SOCKET;
    }
    return s;
}

// The port the OS actually gave us (useful after netsys_udp_open(0, ...)). 0 on failure.
static inline unsigned short netsys_local_port(netsys_socket s) {
    struct sockaddr_in sa;
    netsys_socklen len = (netsys_socklen)sizeof sa;
    memset(&sa, 0, sizeof sa);
    if (getsockname(s, (struct sockaddr *)&sa, &len) == NETSYS_SOCKET_ERROR) return 0;
    return ntohs(sa.sin_port);
}
