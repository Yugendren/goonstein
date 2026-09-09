// UDP transport: sockets, sequencing, acks, and a small reliable channel. See net.h for the
// wire format. Nothing in here knows what a player is.
#include "net.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ---------------------------------------------------------------- byte cursor

void nb_init_write(NetBuf *b, void *mem, size_t cap) { b->p = mem; b->len = 0; b->cap = cap; b->rd = 0; b->err = false; }
void nb_init_read(NetBuf *b, const void *mem, size_t len) { b->p = (uint8_t *)(uintptr_t)mem; b->len = len; b->cap = len; b->rd = 0; b->err = false; }

static void put(NetBuf *b, const void *src, size_t n) {
    if (b->err || b->len + n > b->cap) { b->err = true; return; }
    memcpy(b->p + b->len, src, n); b->len += n;
}
void nb_u8(NetBuf *b, uint8_t v)   { put(b, &v, 1); }
void nb_i8(NetBuf *b, int8_t v)    { put(b, &v, 1); }
void nb_u16(NetBuf *b, uint16_t v) { uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(b, t, 2); }
void nb_i16(NetBuf *b, int16_t v)  { nb_u16(b, (uint16_t)v); }
void nb_u32(NetBuf *b, uint32_t v) { uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; put(b, t, 4); }
void nb_f32(NetBuf *b, float v)    { uint32_t u; memcpy(&u, &v, 4); nb_u32(b, u); }
void nb_bytes(NetBuf *b, const void *src, size_t n) { put(b, src, n); }

static bool take(NetBuf *b, void *dst, size_t n) {
    if (b->err || b->rd + n > b->len) { b->err = true; if (dst) memset(dst, 0, n); return false; }
    if (dst) memcpy(dst, b->p + b->rd, n);
    b->rd += n; return true;
}
uint8_t  rb_u8(NetBuf *b)  { uint8_t v = 0; take(b, &v, 1); return v; }
int8_t   rb_i8(NetBuf *b)  { return (int8_t)rb_u8(b); }
uint16_t rb_u16(NetBuf *b) { uint8_t t[2] = {0,0}; take(b, t, 2); return (uint16_t)(t[0] | (t[1] << 8)); }
int16_t  rb_i16(NetBuf *b) { return (int16_t)rb_u16(b); }
uint32_t rb_u32(NetBuf *b) { uint8_t t[4] = {0,0,0,0}; take(b, t, 4); return (uint32_t)t[0] | ((uint32_t)t[1] << 8) | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24); }
float    rb_f32(NetBuf *b) { uint32_t u = rb_u32(b); float v; memcpy(&v, &u, 4); return v; }
void     rb_bytes(NetBuf *b, void *dst, size_t n) { take(b, dst, n); }
size_t   rb_left(const NetBuf *b) { return b->len > b->rd ? b->len - b->rd : 0; }

// ---------------------------------------------------------------- socket

bool net_open(NetSocket *s, uint16_t port) {
    memset(s, 0, sizeof *s);
    s->fd = NETSYS_INVALID_SOCKET;
    if (!netsys_init()) return false;
    s->fd = netsys_udp_open(port, 256 * 1024, 256 * 1024);
    if (!netsys_valid(s->fd)) { netsys_quit(); return false; }
    s->open = true;
    s->rng = 0x2545F491u ^ ((uint32_t)netsys_local_port(s->fd) << 16) ^ (uint32_t)time(NULL);
    const char *loss = getenv("HOLLOW_NET_LOSS");
    s->loss = loss ? (float)atof(loss) : 0.0f;
    return true;
}

void net_close(NetSocket *s) {
    if (!s->open) return;
    netsys_close(s->fd); netsys_quit();
    s->fd = NETSYS_INVALID_SOCKET; s->open = false;
}

bool net_resolve(const char *host_port, NetAddr *out) {
    char host[128]; snprintf(host, sizeof host, "%s", host_port);
    char *colon = strrchr(host, ':');
    if (!colon) return false;
    *colon = 0;
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) return false;
    struct in_addr ia;
    if (inet_pton(AF_INET, host, &ia) == 1) { out->ip = ntohl(ia.s_addr); out->port = (uint16_t)port; return true; }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    struct sockaddr_in *sa = (struct sockaddr_in *)(void *)res->ai_addr;
    out->ip = ntohl(sa->sin_addr.s_addr); out->port = (uint16_t)port;
    freeaddrinfo(res);
    return true;
}

int net_addr_str(const NetAddr *a, char *out, size_t n) {
    return snprintf(out, n, "%u.%u.%u.%u:%u", (a->ip >> 24) & 255u, (a->ip >> 16) & 255u, (a->ip >> 8) & 255u, a->ip & 255u, a->port);
}
bool net_addr_eq(const NetAddr *a, const NetAddr *b) { return a->ip == b->ip && a->port == b->port; }

// ---------------------------------------------------------------- peers

void net_peer_init(NetPeer *p, const NetAddr *addr) {
    memset(p, 0, sizeof *p);
    p->used = true; p->addr = *addr; p->rtt = 0.05f;
}
void net_peer_reset(NetPeer *p) { memset(p, 0, sizeof *p); }

// Reliable slots are indexed by id % NET_REL_MAX, so the outbox is full when the slot the next id
// would take is still waiting for its ack.
bool net_reliable_send(NetPeer *p, const void *body, uint16_t len) {
    if (len > NET_REL_BODY) return false;
    int k = p->rel_next_id % NET_REL_MAX;
    if (p->rel_out[k].used) return false;
    p->rel_out[k].used = true; p->rel_out[k].id = p->rel_next_id; p->rel_out[k].len = len;
    memcpy(p->rel_out[k].body, body, len);
    p->rel_next_id++;
    return true;
}

// Sequence comparison that survives the 16-bit wrap.
static bool seq_newer(uint16_t a, uint16_t b) { return (a > b && (uint16_t)(a - b) <= 32768u) || (b > a && (uint16_t)(b - a) > 32768u); }

static int sendto_addr(NetSocket *s, const NetAddr *to, const void *buf, int len) {
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(to->ip); sa.sin_port = htons(to->port);
    netsys_ssize n = sendto(s->fd, (const char *)buf, (size_t)len, 0, (struct sockaddr *)&sa, (netsys_socklen)sizeof sa);
    if (n == NETSYS_SOCKET_ERROR) return 0;
    s->bytes_out += (uint64_t)n; s->packets_out++;
    return (int)n;
}

static void write_header(NetBuf *b, uint8_t ptype, uint16_t seq, uint16_t ack, uint32_t ack_bits, uint16_t rel_ack, uint8_t nrel) {
    nb_u16(b, NET_MAGIC); nb_u8(b, NET_PROTO); nb_u8(b, ptype);
    nb_u16(b, seq); nb_u16(b, ack); nb_u32(b, ack_bits);
    nb_u16(b, rel_ack); nb_u8(b, nrel); nb_u8(b, 0);
}

int net_send(NetSocket *s, NetPeer *p, uint8_t ptype, const void *payload, int payload_len, double now) {
    if (!s->open || !p->used) return 0;
    uint8_t pkt[NET_MAX_PACKET]; NetBuf b;
    // Everything from the oldest unacked id onward rides along again, up to NET_REL_PER_PKT of it.
    int idx[NET_REL_PER_PKT]; int nrel = 0;
    for (uint16_t id = p->rel_acked; id != p->rel_next_id && nrel < NET_REL_PER_PKT; id++) {
        int k = id % NET_REL_MAX;
        if (p->rel_out[k].used && p->rel_out[k].id == id) idx[nrel++] = k;
    }
    nb_init_write(&b, pkt, sizeof pkt);
    write_header(&b, ptype, p->local_seq, p->remote_seq, p->ack_bits, p->rel_expect, (uint8_t)nrel);
    for (int i = 0; i < nrel; i++) {
        nb_u16(&b, p->rel_out[idx[i]].id); nb_u16(&b, p->rel_out[idx[i]].len);
        nb_bytes(&b, p->rel_out[idx[i]].body, p->rel_out[idx[i]].len);
    }
    if (payload && payload_len > 0) nb_bytes(&b, payload, (size_t)payload_len);
    if (b.err) return 0;
    p->sent_at[p->local_seq & 255] = now;
    int n = sendto_addr(s, &p->addr, pkt, (int)b.len);
    if (n > 0) { p->local_seq++; p->last_send = now; p->bytes_out += (uint64_t)n; p->packets_out++; }
    return n;
}

int net_send_raw(NetSocket *s, const NetAddr *to, uint8_t ptype, const void *payload, int payload_len) {
    if (!s->open) return 0;
    uint8_t pkt[NET_MAX_PACKET]; NetBuf b;
    nb_init_write(&b, pkt, sizeof pkt);
    write_header(&b, ptype, 0, 0, 0, 0, 0);
    if (payload && payload_len > 0) nb_bytes(&b, payload, (size_t)payload_len);
    if (b.err) return 0;
    return sendto_addr(s, to, pkt, (int)b.len);
}

// ---------------------------------------------------------------- receive

static uint32_t xorshift(uint32_t *st) { uint32_t x = *st ? *st : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *st = x; }

int net_recv(NetSocket *s, NetPacket *out) {
    if (!s->open) return 0;
    struct sockaddr_in from; netsys_socklen fl = (netsys_socklen)sizeof from;
    netsys_ssize n = recvfrom(s->fd, (char *)s->scratch, sizeof s->scratch, 0, (struct sockaddr *)&from, &fl);
    if (n == NETSYS_SOCKET_ERROR) { int e = netsys_errno(); return (netsys_would_block(e) || netsys_is_conn_reset(e)) ? 0 : -1; }
    s->bytes_in += (uint64_t)n; s->packets_in++;
    if (s->loss > 0 && (float)(xorshift(&s->rng) % 10000u) * 0.0001f < s->loss) { s->dropped_in++; return -1; }
    if (n < NET_HEADER) return -1;

    memset(out, 0, sizeof *out);
    out->addr.ip = ntohl(from.sin_addr.s_addr); out->addr.port = ntohs(from.sin_port);
    out->size = (int)n;
    NetBuf b; nb_init_read(&b, s->scratch, (size_t)n);
    if (rb_u16(&b) != NET_MAGIC) return -1;
    if (rb_u8(&b) != NET_PROTO) return -1;
    out->ptype = rb_u8(&b);
    out->seq = rb_u16(&b); out->ack = rb_u16(&b); out->ack_bits = rb_u32(&b);
    out->rel_ack = rb_u16(&b);
    int nrel = rb_u8(&b); (void)rb_u8(&b);
    if (nrel > NET_REL_PER_PKT) return -1;
    for (int i = 0; i < nrel; i++) {
        uint16_t id = rb_u16(&b), len = rb_u16(&b);
        if (b.err || len > NET_REL_BODY || rb_left(&b) < len) return -1;
        out->raw_rel[out->nraw].id = id; out->raw_rel[out->nraw].len = len;
        out->raw_rel[out->nraw].body = s->scratch + b.rd; out->nraw++;
        b.rd += len;
    }
    if (b.err) return -1;
    out->payload = s->scratch + b.rd;
    out->payload_len = (int)rb_left(&b);
    return 1;
}

void net_peer_absorb(NetPeer *p, NetPacket *pk, double now) {
    if (!p->used) return;
    p->last_recv = now;
    p->bytes_in += (uint64_t)pk->size; p->packets_in++;

    // Sequence history: ack_bits[i] means we also saw remote_seq - 1 - i.
    if (!p->got_any) { p->got_any = true; p->remote_seq = pk->seq; p->ack_bits = 0; }
    else if (seq_newer(pk->seq, p->remote_seq)) {
        uint16_t shift = (uint16_t)(pk->seq - p->remote_seq);
        p->ack_bits = shift >= 32 ? 0u : ((p->ack_bits << shift) | (1u << (shift - 1)));
        p->remote_seq = pk->seq;
    } else {
        uint16_t back = (uint16_t)(p->remote_seq - pk->seq);
        if (back >= 1 && back <= 32) p->ack_bits |= 1u << (back - 1);
    }
    // Round trip, from the newest packet of ours they acknowledged.
    double sent = p->sent_at[pk->ack & 255];
    if (sent > 0 && now >= sent && now - sent < 2.0) {
        float sample = (float)(now - sent);
        p->rtt = p->rtt > 0 ? p->rtt * 0.9f + sample * 0.1f : sample;
    }
    // Reliable out: retire everything below their rel_ack.
    if (seq_newer(pk->rel_ack, p->rel_acked)) {
        for (uint16_t id = p->rel_acked; id != pk->rel_ack; id++) {
            int k = id % NET_REL_MAX;
            if (p->rel_out[k].used && p->rel_out[k].id == id) p->rel_out[k].used = false;
        }
        p->rel_acked = pk->rel_ack;
    }
    // Reliable in: park what arrived, then deliver as far as the run is contiguous.
    for (int i = 0; i < pk->nraw; i++) {
        uint16_t id = pk->raw_rel[i].id;
        if ((uint16_t)(id - p->rel_expect) >= NET_REL_MAX) continue;   // already delivered, or too far ahead to hold
        int k = id % NET_REL_MAX;
        if (p->rel_in[k].used && p->rel_in[k].id == id) continue;
        p->rel_in[k].used = true; p->rel_in[k].id = id; p->rel_in[k].len = pk->raw_rel[i].len;
        memcpy(p->rel_in[k].body, pk->raw_rel[i].body, pk->raw_rel[i].len);
    }
    pk->nrel = 0;
    while (pk->nrel < NET_REL_MAX) {
        int k = p->rel_expect % NET_REL_MAX;
        if (!p->rel_in[k].used || p->rel_in[k].id != p->rel_expect) break;
        pk->rel[pk->nrel].len = p->rel_in[k].len;
        memcpy(pk->rel[pk->nrel].body, p->rel_in[k].body, p->rel_in[k].len);
        pk->nrel++;
        p->rel_in[k].used = false;
        p->rel_expect++;
    }
}

float net_peer_loss(const NetPeer *p) {
    if (!p->got_any) return 0.0f;
    int missing = 0;
    for (int i = 0; i < 32; i++) if (!(p->ack_bits & (1u << i))) missing++;
    return (float)missing / 32.0f;
}
