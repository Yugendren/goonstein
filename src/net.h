// UDP transport. Plain sockets through net_sys.h (BSD on unix, Winsock on Windows), no
// dependencies, no encryption.
//
// One socket per process. A NetPeer is one remote address with a sequence number, a 32-bit ack
// history and a small reliable channel; unreliable payloads (inputs, snapshots) ride the same
// packets. Everything is little-endian and written through the byte cursor below so the wire
// format does not depend on struct padding.
//
// Packet = header, then the reliable blocks that are still unacked, then one unreliable payload.
//
//   u16 magic  0x484E ('H','N')
//   u8  proto  NET_PROTO
//   u8  ptype  payload type (NPT_*), NPT_NONE when the packet only carries reliable data
//   u16 seq        this packet's sequence number
//   u16 ack        the highest sequence number seen from the other side
//   u32 ack_bits   bit i set = ack-1-i was also received
//   u16 rel_ack    highest contiguous reliable id received from the other side
//   u8  nrel       number of reliable blocks that follow
//   u8  pad
//   nrel x { u16 id, u16 len, u8 body[len] }
//   payload bytes to the end of the packet
#pragma once
#include "net_sys.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_PROTO        1
#define NET_MAGIC        0x484Eu
#define NET_HEADER       16
#define NET_MAX_PACKET   1200
#define NET_REL_MAX      32     // reliable messages in flight, per peer
#define NET_REL_BODY     64     // bytes in one reliable message
#define NET_REL_PER_PKT  4      // reliable blocks resent per outgoing packet

// Payload types (the unreliable part of a packet).
enum { NPT_NONE = 0, NPT_INPUT = 1, NPT_SNAPSHOT = 2 };

typedef struct NetAddr { uint32_t ip; uint16_t port; } NetAddr;   // IPv4, host byte order

typedef struct NetSocket {
    netsys_socket fd;
    bool open;
    float loss;              // HOLLOW_NET_LOSS: fraction of received packets dropped on the floor
    uint32_t rng;
    // process-wide counters
    uint64_t bytes_in, bytes_out;
    uint32_t packets_in, packets_out, dropped_in;
    uint8_t  scratch[NET_MAX_PACKET];    // the datagram net_recv last read
} NetSocket;

typedef struct NetPeer {
    bool     used;
    NetAddr  addr;
    uint16_t local_seq;      // next sequence number we will send
    uint16_t remote_seq;     // highest sequence seen from them
    uint32_t ack_bits;       // history behind remote_seq
    bool     got_any;
    double   last_recv, last_send;
    float    rtt;            // smoothed, seconds
    double   sent_at[256];   // send time by seq & 255, for rtt
    // reliable out: ids are dense from rel_base; a slot is freed when the peer acks past it
    struct { bool used; uint16_t id, len; uint8_t body[NET_REL_BODY]; } rel_out[NET_REL_MAX];
    uint16_t rel_next_id;    // id the next queued reliable message gets
    uint16_t rel_acked;      // highest contiguous id they have acknowledged
    uint16_t rel_expect;     // next reliable id we want to deliver in order
    // reliable in: out-of-order arrivals parked until the gap fills
    struct { bool used; uint16_t id, len; uint8_t body[NET_REL_BODY]; } rel_in[NET_REL_MAX];
    // per-peer counters (reset by net_peer_stats_reset)
    uint64_t bytes_in, bytes_out; uint32_t packets_in, packets_out;
} NetPeer;

// ---------------------------------------------------------------- byte cursor
// Bounds-checked little-endian reader/writer. `err` sticks once anything overflows, so callers
// can write a whole packet and test once at the end.
typedef struct NetBuf { uint8_t *p; size_t len, cap; size_t rd; bool err; } NetBuf;

void nb_init_write(NetBuf *b, void *mem, size_t cap);
void nb_init_read(NetBuf *b, const void *mem, size_t len);
void nb_u8(NetBuf *b, uint8_t v);
void nb_u16(NetBuf *b, uint16_t v);
void nb_u32(NetBuf *b, uint32_t v);
void nb_i16(NetBuf *b, int16_t v);
void nb_i8(NetBuf *b, int8_t v);
void nb_f32(NetBuf *b, float v);
void nb_bytes(NetBuf *b, const void *src, size_t n);
uint8_t  rb_u8(NetBuf *b);
uint16_t rb_u16(NetBuf *b);
uint32_t rb_u32(NetBuf *b);
int16_t  rb_i16(NetBuf *b);
int8_t   rb_i8(NetBuf *b);
float    rb_f32(NetBuf *b);
void     rb_bytes(NetBuf *b, void *dst, size_t n);
size_t   rb_left(const NetBuf *b);

// ---------------------------------------------------------------- socket
bool net_open(NetSocket *s, uint16_t port);      // port 0 = any free port
void net_close(NetSocket *s);
bool net_resolve(const char *host_port, NetAddr *out);   // "1.2.3.4:7777" or "localhost:7777"
int  net_addr_str(const NetAddr *a, char *out, size_t n);
bool net_addr_eq(const NetAddr *a, const NetAddr *b);

// ---------------------------------------------------------------- peers
void net_peer_init(NetPeer *p, const NetAddr *addr);
void net_peer_reset(NetPeer *p);
// Queue a reliable message. Returns false if the outbox is full (the peer is not acking).
bool net_reliable_send(NetPeer *p, const void *body, uint16_t len);

// Build and send one packet: header + pending reliable blocks + `payload` (may be NULL).
// `now` is seconds, monotonic. Returns bytes sent, 0 on failure.
int  net_send(NetSocket *s, NetPeer *p, uint8_t ptype, const void *payload, int payload_len, double now);
// Send a raw packet with no peer state (used to answer an unknown address, e.g. a reject).
int  net_send_raw(NetSocket *s, const NetAddr *to, uint8_t ptype, const void *payload, int payload_len);

// One received datagram, already parsed. `payload` points into the socket's scratch buffer and
// stays valid until the next net_recv.
typedef struct NetPacket {
    NetAddr  addr;
    int      size;                        // whole datagram, for byte counting
    uint8_t  ptype;
    uint16_t seq, ack, rel_ack; uint32_t ack_bits;
    const uint8_t *payload; int payload_len;
    // reliable blocks carried by this packet, exactly as they arrived
    struct { uint16_t id, len; const uint8_t *body; } raw_rel[NET_REL_PER_PKT]; int nraw;
    // filled by net_peer_absorb: the reliable messages that are now deliverable, in order
    struct { uint16_t len; uint8_t body[NET_REL_BODY]; } rel[NET_REL_MAX]; int nrel;
} NetPacket;

// Read one datagram. Returns 1 on a packet, 0 when nothing is waiting, -1 on a malformed or
// deliberately dropped (HOLLOW_NET_LOSS) packet — keep calling until it returns 0.
// The caller demuxes on out->addr, then calls net_peer_absorb to update that peer's state.
int  net_recv(NetSocket *s, NetPacket *out);
// Fold a received packet into a peer: acks, rtt, reliable reassembly. Fills pk->rel / pk->nrel.
void net_peer_absorb(NetPeer *p, NetPacket *pk, double now);

float net_peer_loss(const NetPeer *p);   // fraction of the last 32 packets we sent that were not acked
