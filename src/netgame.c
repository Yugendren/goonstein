// Host-authoritative listen server. The host owns every player and simulates them all at the
// fixed 60 Hz tick; clients send intent and predict only themselves.
//
// Host  tick: drain socket -> (game ticks the host's own player) -> simulate every remote player
//             from its newest intent -> every other tick, send a snapshot to each client.
// Client tick: drain socket -> apply the newest snapshot (interpolate remotes, reconcile self)
//             -> (game ticks the local player, predicted) -> send this tick's intent.
#include "game.h"
#include "netgame.h"
#include "debug.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Reliable message types (the first byte of a reliable body).
enum { NRM_JOIN = 1, NRM_ACCEPT = 2, NRM_REJECT = 3, NRM_JOINED = 4, NRM_LEFT = 5, NRM_BYE = 6 };
enum { NET_ENT_PLAYER = 1 };

static const Vec4 SLOT_TINT[NET_MAX_PLAYERS] = {
    { 1.00f, 1.00f, 1.00f, 1 },   // host: as authored
    { 1.00f, 0.55f, 0.45f, 1 },   // red
    { 0.50f, 0.75f, 1.00f, 1 },   // blue
    { 0.95f, 0.90f, 0.45f, 1 },   // yellow
};

static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static float yaw_lerp(float a, float b, float t) { return a + wrap_pi(b - a) * t; }
static int16_t  q_yaw(float y)   { return (int16_t)lrintf(clampf(wrap_pi(y), -PI, PI) * (32767.0f / PI)); }
static float    dq_yaw(int16_t y){ return (float)y * (PI / 32767.0f); }
static int8_t   q_dir(float v)   { return (int8_t)lrintf(clampf(v, -1, 1) * 127.0f); }
static float    dq_dir(int8_t v) { return (float)v * (1.0f / 127.0f); }

// ---------------------------------------------------------------- setup

void netgame_parse_args(NetGame *n, int argc, char **argv) {
    memset(n, 0, sizeof *n);
    n->mode = NM_OFF; n->slots_max = NET_MAX_PLAYERS; n->local = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) { n->mode = NM_HOST; n->port = (uint16_t)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) { n->mode = NM_CLIENT; snprintf(n->join_target, sizeof n->join_target, "%s", argv[++i]); }
        else if (!strcmp(argv[i], "--slots") && i + 1 < argc) { n->slots_max = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) { snprintf(n->name, sizeof n->name, "%s", argv[++i]); }
    }
    if (n->slots_max < 1) n->slots_max = 1;
    if (n->slots_max > NET_MAX_PLAYERS) n->slots_max = NET_MAX_PLAYERS;
    // Single player leaves the name empty so the log file stays plain hollow.log.
    if (!n->name[0] && n->mode != NM_OFF) snprintf(n->name, sizeof n->name, "%s", n->mode == NM_HOST ? "host" : "player");
}

static void seat(Game *g, int slot, const char *name) {
    NetSlot *s = &g->net.slots[slot];
    s->active = true; s->tint = SLOT_TINT[slot]; s->nhist = 0; s->last_snap_t = g->net.now;
    snprintf(s->name, sizeof s->name, "%s", name && name[0] ? name : "player");
    game_ensure_player_model(g, slot);
    game_spawn_player(g, slot);
}

static void unseat(Game *g, int slot) {
    memset(&g->net.slots[slot], 0, sizeof g->net.slots[slot]);
    memset(&g->players[slot], 0, sizeof g->players[slot]);
}

bool netgame_start(Game *g) {
    NetGame *n = &g->net;
    if (n->mode == NM_OFF) { n->slots[0].active = true; n->slots[0].tint = SLOT_TINT[0]; g->local = n->local = 0; return true; }
    if (!net_open(&n->sock, n->mode == NM_HOST ? n->port : 0)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: could not open UDP socket%s", n->mode == NM_HOST ? " (port in use?)" : "");
        return false;
    }
    n->started = true; n->now = 0; n->net_tick = 0; n->snap_countdown = 0;
    const char *lp = strrchr(g->level.path, '/');
    snprintf(n->level_name, sizeof n->level_name, "%s", lp ? lp + 1 : g->level.path);
    if (n->mode == NM_HOST) {
        g->local = 0; n->local = 0;
        seat(g, 0, n->name);
        dbg_log("net: hosting on UDP %u, %d slots, level %s", n->port, n->slots_max, n->level_name);
        SDL_Log("net: hosting on UDP %u (%d slots)", n->port, n->slots_max);
    } else {
        NetAddr a;
        if (!net_resolve(n->join_target, &a)) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: cannot resolve %s", n->join_target); return false; }
        net_peer_init(&n->server, &a);
        uint8_t body[NET_REL_BODY]; NetBuf b; nb_init_write(&b, body, sizeof body);
        nb_u8(&b, NRM_JOIN); nb_u8(&b, NET_PROTO); nb_bytes(&b, n->name, sizeof n->name);
        net_reliable_send(&n->server, body, (uint16_t)b.len);
        g->local = 0; n->local = 0;
        n->slots[0].active = true; n->slots[0].tint = SLOT_TINT[0];
        dbg_log("net: joining %s as %s", n->join_target, n->name);
        SDL_Log("net: joining %s as %s", n->join_target, n->name);
    }
    return true;
}

void netgame_shutdown(Game *g) {
    NetGame *n = &g->net;
    if (!n->started) return;
    uint8_t body[2] = { NRM_BYE, 0 };
    if (n->mode == NM_CLIENT) { net_reliable_send(&n->server, body, 1); net_send(&n->sock, &n->server, NPT_NONE, NULL, 0, n->now); }
    else for (int i = 1; i < NET_MAX_PLAYERS; i++) if (n->slots[i].has_peer) { net_reliable_send(&n->slots[i].peer, body, 1); net_send(&n->sock, &n->slots[i].peer, NPT_NONE, NULL, 0, n->now); }
    double secs = n->total_time > 0.01 ? n->total_time : 0.01;
    dbg_log("net: %s over %.1f s | up %.0f B/s down %.0f B/s | corrections %u avg %.3f m hard snaps %u | dropped-in %u",
            n->mode == NM_HOST ? "host" : "client", secs,
            (double)n->sock.bytes_out / secs, (double)n->sock.bytes_in / secs,
            n->t_corrections, n->t_corrections ? n->t_corr_sum / n->t_corrections : 0.0, n->t_hard_snaps, n->sock.dropped_in);
    net_close(&n->sock);
    n->started = false;
}

// ---------------------------------------------------------------- input plumbing

Vec3 netgame_local_input(Game *g, Vec3 dir, const Input *in) {
    NetGame *n = &g->net;
    if (n->mode == NM_OFF) return dir;
    float l = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (l > 1.0f) { dir.x /= l; dir.z /= l; }
    n->cur.mx = q_dir(dir.x); n->cur.mz = q_dir(dir.z);
    n->cur.yaw = q_yaw(g->cam.yaw);
    uint16_t b = 0;
    if (in->attack) b |= NB_ATTACK;
    if (in->parry) b |= NB_PARRY;
    if (in->dodge) b |= NB_DODGE;
    if (in->interact) b |= NB_INTERACT;
    if (in->sprint) b |= NB_SPRINT;
    if (in->rmouse_held) b |= NB_GUARD;
    n->cur.buttons = b;
    return v3(dq_dir(n->cur.mx), 0, dq_dir(n->cur.mz));   // the host replays exactly this
}

// Turn a received intent back into the Input and direction the movement code wants.
static Vec3 unpack_input(const NetInput *ni, Input *in) {
    memset(in, 0, sizeof *in);
    in->attack = (ni->buttons & NB_ATTACK) != 0;
    in->parry = (ni->buttons & NB_PARRY) != 0;
    in->dodge = (ni->buttons & NB_DODGE) != 0;
    in->interact = (ni->buttons & NB_INTERACT) != 0;
    in->sprint = (ni->buttons & NB_SPRINT) != 0;
    in->rmouse_held = (ni->buttons & NB_GUARD) != 0;
    return v3(dq_dir(ni->mx), 0, dq_dir(ni->mz));
}

// ---------------------------------------------------------------- snapshots

static void write_snapshot(Game *g, int for_slot, uint8_t *out, int *out_len, int cap) {
    NetGame *n = &g->net;
    NetBuf b; nb_init_write(&b, out, (size_t)cap);
    nb_u32(&b, g->tick);
    nb_u32(&b, n->slots[for_slot].input_tick);
    int count = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (n->slots[i].active) count++;
    nb_u8(&b, (uint8_t)count);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!n->slots[i].active) continue;
        const Character *c = &g->players[i].c;
        nb_u8(&b, NET_ENT_PLAYER); nb_u8(&b, (uint8_t)i);
        nb_f32(&b, c->pos.x); nb_f32(&b, c->pos.y); nb_f32(&b, c->pos.z);
        nb_i16(&b, q_yaw(c->yaw));
        nb_u8(&b, (uint8_t)c->anim);
        nb_u16(&b, (uint16_t)(clampf(c->anim_t, 0, 60.0f) * 1000.0f));
        nb_u16(&b, (uint16_t)clampf(c->hp, 0, 65535.0f));
        nb_u8(&b, (uint8_t)g->players[i].state);
    }
    *out_len = b.err ? 0 : (int)b.len;
}

// Push a sample into a slot's interpolation history (client side).
static void push_hist(NetSlot *s, double t, const NetSnapPlayer *p) {
    if (s->nhist > 0 && t <= s->hist[s->nhist - 1].t) return;   // out of order or duplicate
    if (s->nhist == NET_SNAP_BUF) { memmove(s->hist, s->hist + 1, sizeof s->hist[0] * (NET_SNAP_BUF - 1)); s->nhist--; }
    s->hist[s->nhist].t = t; s->hist[s->nhist].s = *p; s->nhist++;
    s->last_snap_t = t;
}

// Remote players are drawn NET_INTERP_DELAY behind the newest snapshot, between the two samples
// that bracket that time.
static void interpolate_remotes(Game *g) {
    NetGame *n = &g->net;
    double rt = n->now - NET_INTERP_DELAY;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        NetSlot *s = &n->slots[i];
        if (i == g->local || !s->active || s->nhist == 0) continue;
        Character *c = &g->players[i].c;
        NetSnapPlayer out;
        if (rt <= s->hist[0].t) out = s->hist[0].s;
        else if (rt >= s->hist[s->nhist - 1].t) out = s->hist[s->nhist - 1].s;
        else {
            int k = s->nhist - 1;
            while (k > 0 && s->hist[k - 1].t > rt) k--;
            const NetSnapPlayer *a = &s->hist[k - 1].s, *bb = &s->hist[k].s;
            double span = s->hist[k].t - s->hist[k - 1].t;
            float u = span > 1e-6 ? (float)((rt - s->hist[k - 1].t) / span) : 1.0f;
            out = *bb;
            out.pos = v3_lerp(a->pos, bb->pos, u);
            out.yaw = yaw_lerp(a->yaw, bb->yaw, u);
            out.anim = u < 0.5f ? a->anim : bb->anim;
            out.anim_t = u < 0.5f ? a->anim_t : bb->anim_t;
        }
        Vec3 prev = c->pos;
        c->pos = out.pos; c->yaw = out.yaw; c->hp = out.hp;
        c->anim = (Anim)(out.anim < ANIM_COUNT ? out.anim : ANIM_IDLE);
        c->anim_t = out.anim_t;
        c->speed = v3_len(v3_sub(c->pos, prev)) * 60.0f;
        g->players[i].state = (PState)out.pstate;
    }
}

// The host's answer for the tick we predicted: fold the difference in and let the view catch up.
static void reconcile(Game *g, const NetSnapPlayer *auth, uint32_t ack_tick) {
    NetGame *n = &g->net;
    Player *p = &g->players[g->local];
    n->s_snaps_applied++;
    p->c.hp = auth->hp;
    int k = (int)(ack_tick % NET_HIST);
    if (ack_tick == 0 || n->hist_tick[k] != ack_tick) {   // no prediction on file for that tick
        if (v3_len(v3_sub(auth->pos, p->c.pos)) > 2.0f) { p->c.pos = auth->pos; n->pos_error = v3(0, 0, 0); n->s_hard_snaps++; n->t_hard_snaps++; }
        return;
    }
    Vec3 err = v3_sub(auth->pos, n->hist_pos[k]);
    float mag = v3_len(err);
    if (mag < 0.0015f) return;
    n->s_corrections++; n->t_corrections++;
    n->s_corr_sum += mag; n->t_corr_sum += mag;
    if (mag > n->s_corr_max) n->s_corr_max = mag;
    if (mag > 2.0f) {   // too far gone to smooth: snap
        p->c.pos = v3_add(p->c.pos, err);
        n->pos_error = v3(0, 0, 0);
        n->s_hard_snaps++; n->t_hard_snaps++;
        dbg_log("net: hard snap %.2f m at tick %u", mag, ack_tick);
    } else {
        // The error at ack_tick carried forward unchanged through the inputs we have already
        // applied, so correcting the current position by it is right; the view lags behind and
        // decays back, so the player never sees the jump.
        p->c.pos = v3_add(p->c.pos, err);
        n->pos_error = v3_sub(n->pos_error, err);
    }
    for (int i = 0; i < NET_HIST; i++) n->hist_pos[i] = v3_add(n->hist_pos[i], err);   // do not correct twice
}

static void read_snapshot(Game *g, const uint8_t *data, int len) {
    NetGame *n = &g->net;
    NetBuf b; nb_init_read(&b, data, (size_t)len);
    uint32_t stick = rb_u32(&b), ack = rb_u32(&b);
    int count = rb_u8(&b);
    if (b.err || count > NET_MAX_PLAYERS) return;
    if (stick < n->server_tick && n->server_tick - stick < 1000) return;   // stale, a newer one already landed
    n->server_tick = stick; n->server_tick_at = n->now;
    if (ack > n->last_ack) n->last_ack = ack;
    bool seen[NET_MAX_PLAYERS] = { false, false, false, false };
    for (int e = 0; e < count; e++) {
        uint8_t type = rb_u8(&b), id = rb_u8(&b);
        if (b.err) return;
        if (type != NET_ENT_PLAYER || id >= NET_MAX_PLAYERS) return;
        NetSnapPlayer sp;
        sp.pos.x = rb_f32(&b); sp.pos.y = rb_f32(&b); sp.pos.z = rb_f32(&b);
        sp.yaw = dq_yaw(rb_i16(&b));
        sp.anim = rb_u8(&b);
        sp.anim_t = (float)rb_u16(&b) * 0.001f;
        sp.hp = (float)rb_u16(&b);
        sp.pstate = rb_u8(&b);
        if (b.err) return;
        seen[id] = true;
        if (!n->slots[id].active) {
            if (id == (uint8_t)g->local) { n->slots[id].active = true; n->slots[id].tint = SLOT_TINT[id]; game_ensure_player_model(g, id); }
            else seat(g, id, "player");
            dbg_log("net: slot %d appeared", id);
        }
        if (id == (uint8_t)g->local) reconcile(g, &sp, ack);
        else push_hist(&n->slots[id], n->now, &sp);
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (i != g->local && n->slots[i].active && !seen[i]) { dbg_log("net: slot %d (%s) left", i, n->slots[i].name); unseat(g, i); }
}

// ---------------------------------------------------------------- host

static int find_slot_by_addr(NetGame *n, const NetAddr *a) {
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (n->slots[i].has_peer && net_addr_eq(&n->slots[i].peer.addr, a)) return i;
    return -1;
}

static void host_broadcast(NetGame *n, int except, const void *body, uint16_t len) {
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (i != except && n->slots[i].has_peer) net_reliable_send(&n->slots[i].peer, body, len);
}

static void host_reliable(Game *g, int slot, const uint8_t *body, int len) {
    NetGame *n = &g->net;
    if (len < 1) return;
    NetBuf b; nb_init_read(&b, body, (size_t)len);
    uint8_t type = rb_u8(&b);
    if (type == NRM_JOIN) {
        uint8_t proto = rb_u8(&b);
        char name[24]; rb_bytes(&b, name, sizeof name); name[sizeof name - 1] = 0;
        if (b.err || proto != NET_PROTO) { dbg_log("net: rejecting slot %d (protocol %u)", slot, proto); return; }
        if (n->slots[slot].active) return;   // duplicate join, the accept is already in flight
        seat(g, slot, name);
        uint8_t ab[NET_REL_BODY]; NetBuf o; nb_init_write(&o, ab, sizeof ab);
        nb_u8(&o, NRM_ACCEPT); nb_u8(&o, (uint8_t)slot); nb_u8(&o, (uint8_t)n->slots_max);
        nb_bytes(&o, n->level_name, sizeof n->level_name);
        net_reliable_send(&n->slots[slot].peer, ab, (uint16_t)o.len);
        uint8_t jb[NET_REL_BODY]; NetBuf j; nb_init_write(&j, jb, sizeof jb);
        nb_u8(&j, NRM_JOINED); nb_u8(&j, (uint8_t)slot); nb_bytes(&j, n->slots[slot].name, 24);
        host_broadcast(n, slot, jb, (uint16_t)j.len);
        char as[32]; net_addr_str(&n->slots[slot].peer.addr, as, sizeof as);
        dbg_log("net: %s joined as slot %d from %s", n->slots[slot].name, slot, as);
        SDL_Log("net: %s joined as slot %d", n->slots[slot].name, slot);
    } else if (type == NRM_BYE) {
        dbg_log("net: %s (slot %d) said goodbye", n->slots[slot].name, slot);
        uint8_t lb[2] = { NRM_LEFT, (uint8_t)slot };
        unseat(g, slot);
        host_broadcast(n, slot, lb, 2);
    }
}

static void host_receive(Game *g) {
    NetGame *n = &g->net;
    NetPacket pk;
    int r;
    while ((r = net_recv(&n->sock, &pk)) != 0) {
        if (r < 0) continue;
        int slot = find_slot_by_addr(n, &pk.addr);
        if (slot < 0) {
            for (int i = 1; i < n->slots_max && slot < 0; i++) if (!n->slots[i].has_peer) slot = i;
            if (slot < 0) { uint8_t rb_[2] = { NRM_REJECT, 1 }; net_send_raw(&n->sock, &pk.addr, NPT_NONE, rb_, 2); continue; }
            net_peer_init(&n->slots[slot].peer, &pk.addr);
            n->slots[slot].has_peer = true; n->slots[slot].last_heard = n->now;
        }
        NetSlot *s = &n->slots[slot];
        net_peer_absorb(&s->peer, &pk, n->now);
        s->last_heard = n->now;
        n->s_pkt_in++; n->s_bytes_in += (uint64_t)pk.size;
        for (int i = 0; i < pk.nrel; i++) host_reliable(g, slot, pk.rel[i].body, pk.rel[i].len);
        if (pk.ptype == NPT_INPUT && pk.payload_len >= 10 && s->active) {
            NetBuf b; nb_init_read(&b, pk.payload, (size_t)pk.payload_len);
            NetInput ni;
            ni.tick = rb_u32(&b); ni.mx = rb_i8(&b); ni.mz = rb_i8(&b); ni.yaw = rb_i16(&b); ni.buttons = rb_u16(&b);
            if (!b.err && (!s->have_queued || ni.tick > s->last_queued)) {
                if (s->qn == NET_INPUT_Q) { memmove(s->q, s->q + 1, sizeof s->q[0] * (NET_INPUT_Q - 1)); s->qn--; }
                s->q[s->qn++] = ni; s->last_queued = ni.tick; s->have_queued = true;
            }
        }
    }
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        NetSlot *s = &n->slots[i];
        if (!s->has_peer || n->now - s->last_heard < NET_TIMEOUT) continue;
        dbg_log("net: slot %d (%s) timed out after %.1f s", i, s->name, NET_TIMEOUT);
        SDL_Log("net: slot %d (%s) timed out", i, s->name);
        uint8_t lb[2] = { NRM_LEFT, (uint8_t)i };
        unseat(g, i);
        host_broadcast(n, i, lb, 2);
    }
}

// Every remote player is simulated here, by the host, from the newest intent it sent.
static void host_simulate(Game *g, float dt) {
    NetGame *n = &g->net;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        NetSlot *s = &n->slots[i];
        if (i == g->local || !s->active) continue;
        // One intent per tick out of the jitter buffer. Falling behind, drop the oldest and catch
        // up; running dry, repeat the last one with its edges already spent.
        if (s->qn > 0) {
            if (s->qn > 3) { int drop = s->qn - 3; memmove(s->q, s->q + drop, sizeof s->q[0] * (size_t)(s->qn - drop)); s->qn -= drop; }
            s->input = s->q[0]; s->have_input = true; s->input_tick = s->input.tick;
            memmove(s->q, s->q + 1, sizeof s->q[0] * (size_t)(s->qn - 1)); s->qn--;
        } else s->input.buttons &= (uint16_t)(NB_SPRINT | NB_GUARD);
        Input in; Vec3 dir = unpack_input(&s->input, &in);
        if (!s->have_input) { memset(&in, 0, sizeof in); dir = v3(0, 0, 0); }
        CombatEvents ev = {0};
        player_update(&g->players[i], &in, dir, &g->level, NULL, dt, &ev);
        Character *c = &g->players[i].c;
        // First person: the client's body faces its view, not its movement, so replay the yaw it
        // sent instead of the one player_update turned toward the move direction. Without this a
        // strafing client looks sideways to everyone else.
        if (g->level.view == VIEW_FIRST && s->have_input) c->yaw = dq_yaw(s->input.yaw);
        if (g->terrain.present && terrain_inside(&g->terrain, c->pos.x, c->pos.z)) c->pos.y = terrain_height(&g->terrain, c->pos.x, c->pos.z);
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        for (int j = i + 1; j < NET_MAX_PLAYERS; j++)
            if (n->slots[i].active && n->slots[j].active) character_separate(&g->players[i].c, &g->players[j].c, &g->level);
}

static void host_send_snapshots(Game *g) {
    NetGame *n = &g->net;
    uint8_t buf[NET_MAX_PACKET - NET_HEADER]; int len = 0;
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        NetSlot *s = &n->slots[i];
        if (!s->has_peer || !s->active) continue;
        write_snapshot(g, i, buf, &len, (int)sizeof buf);
        if (len <= 0) continue;
        int sent = net_send(&n->sock, &s->peer, NPT_SNAPSHOT, buf, len, n->now);
        if (sent > 0) { n->s_pkt_out++; n->s_bytes_out += (uint64_t)sent; }
    }
}

// ---------------------------------------------------------------- client

static void client_reliable(Game *g, const uint8_t *body, int len) {
    NetGame *n = &g->net;
    if (len < 1) return;
    NetBuf b; nb_init_read(&b, body, (size_t)len);
    uint8_t type = rb_u8(&b);
    if (type == NRM_ACCEPT) {
        uint8_t slot = rb_u8(&b), smax = rb_u8(&b);
        char level[32]; rb_bytes(&b, level, sizeof level); level[sizeof level - 1] = 0;
        if (b.err || slot >= NET_MAX_PLAYERS) return;
        if (n->connected) return;
        n->connected = true; n->join_tries = 1;   // connected once: never warn about the join again
        n->slots_max = smax ? smax : NET_MAX_PLAYERS;
        // Slot 0 is the host's. Drop the model this process loaded for it before it knew its own
        // slot, so it comes back as slot 0's goon instead of this client's --hero.
        if (slot != 0) { n->slots[0].active = false; memset(&g->players[0], 0, sizeof g->players[0]); charmodel_destroy(&g->gfx, &g->player_models[0]); }
        g->local = slot; n->local = slot;
        seat(g, slot, n->name);
        game_snap_camera(g);
        dbg_log("net: accepted as slot %d of %d, level %s", slot, n->slots_max, level);
        SDL_Log("net: accepted as slot %d (level %s)", slot, level);
        if (n->level_name[0] && strcmp(level, n->level_name) != 0)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: host plays %s, this client loaded %s", level, n->level_name);
    } else if (type == NRM_JOINED) {
        uint8_t slot = rb_u8(&b); char name[24]; rb_bytes(&b, name, sizeof name); name[sizeof name - 1] = 0;
        if (!b.err && slot < NET_MAX_PLAYERS) dbg_log("net: %s joined as slot %d", name, slot);
    } else if (type == NRM_LEFT) {
        uint8_t slot = rb_u8(&b);
        if (!b.err && slot < NET_MAX_PLAYERS && slot != (unsigned)g->local) { dbg_log("net: slot %d left", slot); unseat(g, slot); }
    } else if (type == NRM_REJECT) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: the host is full");
        dbg_log("net: rejected, the host is full");
    } else if (type == NRM_BYE) {
        dbg_log("net: the host shut down");
        n->connected = false;
    }
}

static void client_receive(Game *g) {
    NetGame *n = &g->net;
    NetPacket pk; int r;
    while ((r = net_recv(&n->sock, &pk)) != 0) {
        if (r < 0) continue;
        if (!net_addr_eq(&pk.addr, &n->server.addr)) continue;
        net_peer_absorb(&n->server, &pk, n->now);
        n->s_pkt_in++; n->s_bytes_in += (uint64_t)pk.size;
        for (int i = 0; i < pk.nrel; i++) client_reliable(g, pk.rel[i].body, pk.rel[i].len);
        if (pk.ptype == NPT_SNAPSHOT && pk.payload_len > 0) read_snapshot(g, pk.payload, pk.payload_len);
    }
}

static void client_send_input(Game *g) {
    NetGame *n = &g->net;
    uint8_t buf[16]; NetBuf b; nb_init_write(&b, buf, sizeof buf);
    nb_u32(&b, n->cur.tick); nb_i8(&b, n->cur.mx); nb_i8(&b, n->cur.mz); nb_i16(&b, n->cur.yaw); nb_u16(&b, n->cur.buttons);
    int sent = net_send(&n->sock, &n->server, NPT_INPUT, buf, (int)b.len, n->now);
    if (sent > 0) { n->s_pkt_out++; n->s_bytes_out += (uint64_t)sent; }
}

// ---------------------------------------------------------------- per-tick entry points

// Where this process thinks everybody is, stamped with the wall clock so the four logs of a
// headless test can be lined up against each other. Once a second, or 5 Hz with HOLLOW_NET_TRACE.
static void log_positions(Game *g) {
    const NetGame *n = &g->net;
    char pos[192]; size_t k = 0; pos[0] = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (n->slots[i].active)
        k += (size_t)snprintf(pos + k, sizeof pos - k, " p%d %.2f %.2f", i, (double)g->players[i].c.pos.x, (double)g->players[i].c.pos.z);
    SDL_Time wall = 0; SDL_GetCurrentTime(&wall);
    dbg_log("net pos wall=%.3f%s", (double)wall / 1e9, pos);
}

static void stats_line(Game *g, float dt) {
    NetGame *n = &g->net;
    n->stat_t += dt;
    n->total_time += dt;
    if (n->stat_t < 1.0) return;
    float rtt = 0; int peers = 0;
    double age = 0;
    if (n->mode == NM_CLIENT) { rtt = n->server.rtt; peers = n->connected ? 1 : 0; age = n->now - n->server_tick_at; }
    else for (int i = 1; i < NET_MAX_PLAYERS; i++) if (n->slots[i].has_peer) { rtt += n->slots[i].peer.rtt; peers++; }
    if (n->mode == NM_HOST && peers > 0) rtt /= (float)peers;
    dbg_log("net %s slot %d peers %d | in %u pkt %llu B | out %u pkt %llu B | rtt %.1f ms | snap age %.0f ms | corr %u avg %.3f max %.3f m snaps %u | dropped %u",
            n->mode == NM_HOST ? "host" : "client", g->local, peers,
            n->s_pkt_in, (unsigned long long)n->s_bytes_in, n->s_pkt_out, (unsigned long long)n->s_bytes_out,
            rtt * 1000.0f, n->mode == NM_CLIENT ? age * 1000.0 : 0.0,
            n->s_corrections, n->s_corrections ? n->s_corr_sum / (float)n->s_corrections : 0.0f, n->s_corr_max,
            n->s_hard_snaps, n->sock.dropped_in - n->s_drops);
    log_positions(g);
    n->s_drops = n->sock.dropped_in;
    n->stat_t = 0; n->s_pkt_in = n->s_pkt_out = 0; n->s_bytes_in = n->s_bytes_out = 0;
    n->s_corrections = n->s_hard_snaps = n->s_snaps_applied = 0; n->s_corr_sum = n->s_corr_max = 0;
}

void netgame_pre_tick(Game *g, float dt) {
    NetGame *n = &g->net;
    if (n->mode == NM_OFF) return;
    n->now += dt;
    n->cur.tick = n->net_tick;
    n->cur.mx = n->cur.mz = 0; n->cur.buttons = 0;   // a tick that never reaches the movement code sends nothing
    if (n->mode == NM_HOST) { host_receive(g); n->server_tick = g->tick; }
    else { client_receive(g); interpolate_remotes(g); }
}

void netgame_post_tick(Game *g, float dt) {
    NetGame *n = &g->net;
    if (n->mode == NM_OFF) return;
    if (n->mode == NM_HOST) {
        host_simulate(g, dt);
        if (--n->snap_countdown <= 0) { n->snap_countdown = 60 / NET_SNAP_HZ; host_send_snapshots(g); }
    } else {
        int k = (int)(n->net_tick % NET_HIST);
        n->hist_pos[k] = g->players[g->local].c.pos; n->hist_tick[k] = n->net_tick;
        client_send_input(g);
        n->pos_error = v3_scale(n->pos_error, expf(-14.0f * dt));
        if (v3_len(n->pos_error) < 0.002f) n->pos_error = v3(0, 0, 0);
        if (!n->connected && n->now > 10.0 && n->join_tries == 0) { n->join_tries = 1; SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: no answer from %s after 10 s", n->join_target); }
    }
    n->net_tick++;
    if (SDL_getenv("HOLLOW_NET_TRACE") && n->net_tick % 12 == 0) log_positions(g);   // 5 Hz track for the headless test
    stats_line(g, dt);
}

Vec3 netgame_view_pos(const NetGame *n, int slot, Vec3 sim_pos) {
    if (n->mode != NM_CLIENT || slot != n->local) return sim_pos;
    return v3_add(sim_pos, n->pos_error);
}

// ---------------------------------------------------------------- bot client

// Walks a slow arc, jumps to a new heading now and then, and turns back inside about eight metres
// of the spawn, so all four players stay in one screenshot.
void netgame_bot_wander(Game *g, Input *in) {
    NetGame *n = &g->net;
    in->move_x = in->move_y = 0;
    in->attack = in->parry = in->dodge = in->interact = in->skip = false;
    in->sprint = false; in->rmouse_held = false;
    if (g->tick >= n->bot_next_turn) {
        uint32_t h = g->tick * 2654435761u + (uint32_t)(n->local + 1) * 40503u + n->port;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        n->bot_heading = (float)(h % 3600u) * 0.1f * DEG2RAD;
        n->bot_next_turn = g->tick + 90 + (h >> 8) % 120;
    }
    n->bot_heading += 0.9f * DEG2RAD;                       // a lazy circle between the jumps
    Vec3 home = v3_sub(g->level.spawn, g->players[g->local].c.pos); home.y = 0;
    float away = v3_len(home);
    Vec3 want = v3(sinf(n->bot_heading), 0, cosf(n->bot_heading));
    if (away > 8.0f) want = v3_norm(v3_lerp(want, v3_scale(home, 1.0f / away), clampf((away - 8.0f) / 4.0f, 0, 1)));
    if (g->cam.mode == CAM_FIRST) {
        // First person: turn the head toward the heading and walk straight ahead, so a screenshot
        // trail shows the world swinging past instead of a fixed view strafing sideways.
        float d = wrap_pi(atan2f(want.x, want.z) - g->cam.yaw);
        in->look_x = -clampf(d, -0.05f, 0.05f) / camera_mouse_sens();   // the camera subtracts look_x * sens
        in->look_y = 0;                                                 // a stray mouse must not tilt a headless run
        in->move_x = 0; in->move_y = -1;
        return;
    }
    // the movement code is camera relative, so express the world heading in the camera's basis
    Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x);
    in->move_x = v3_dot(want, r); in->move_y = -v3_dot(want, f);
}
