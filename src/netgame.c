// Host-authoritative listen server. The host owns every player and simulates them all at the
// fixed 60 Hz tick; clients send intent and predict only themselves.
//
// Host  tick: drain socket -> (game ticks the host's own player) -> simulate every remote player
//             from its newest intent -> every other tick, send a snapshot to each client.
// Client tick: drain socket -> apply the newest snapshot (interpolate remotes, reconcile self)
//             -> (game ticks the local player, predicted) -> send this tick's intent.
#include "game.h"
#include "netgame.h"
#include "weapons.h"
#include "debug.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "voice.h"   // --- voice ---

// Reliable message types (the first byte of a reliable body).
enum { NRM_JOIN = 1, NRM_ACCEPT = 2, NRM_REJECT = 3, NRM_JOINED = 4, NRM_LEFT = 5, NRM_BYE = 6,
       NRM_ITEM_GRAB = 7, NRM_ITEM_RELEASE = 8,
       // --- weapons ---
       NRM_WEAP_FIRE = 9, NRM_WEAP_RELOAD = 10, NRM_WEAP_SWAP = 11, NRM_WEAP_REVIVE = 12 };
enum { NET_ENT_PLAYER = 1, NET_ENT_ITEM = 2 };

// Item entities: 14 bytes each (type, id, position in centimetres, orientation in four bytes, one
// flag byte). Everything held, changed or actually moving goes out at the full snapshot rate; the
// still majority is swept round robin so every item lands at least twice a second. With 64 items
// that is about 2 kB/s of item traffic on top of ~2.6 kB/s of players: inside the 8 kB/s budget.
#define NET_ITEM_SLEEP_SPAN (NET_SNAP_HZ / 2)   // snapshots one full sweep of the sleepers takes
#define NET_ITEM_BYTES      14

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
// A unit quaternion in four bytes. One degree of error, which nobody can see on a tumbling crate.
static int8_t   q_quat(float v)  { return (int8_t)lrintf(clampf(v, -1, 1) * 127.0f); }
static float    dq_quat(int8_t v){ return (float)v * (1.0f / 127.0f); }

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
    game_give_loadout(g, slot);   // --- loadouts --- the weapon this slot's character file asks for
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
    if (in->jump_held) b |= NB_JUMPHELD;
    if (in->rmouse_held) b |= NB_GUARD;
    if (in->jump) b |= NB_JUMP;
    if (in->crouch) b |= NB_CROUCH;
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
    in->jump = (ni->buttons & NB_JUMP) != 0;
    in->jump_held = (ni->buttons & NB_JUMPHELD) != 0;
    in->crouch = (ni->buttons & NB_CROUCH) != 0;
    in->rmouse_held = (ni->buttons & NB_GUARD) != 0;
    return v3(dq_dir(ni->mx), 0, dq_dir(ni->mz));
}

// ---------------------------------------------------------------- snapshots

// A position in centimetres: 0.01 m over +-327 m, which is a good deal more island than there is.
static int16_t q_pos(float v)  { return (int16_t)lrintf(clampf(v, -327.0f, 327.0f) * 100.0f); }
static float   dq_pos(int16_t v) { return (float)v * 0.01f; }

static void write_item(NetBuf *b, const Item *it) {
    nb_u8(b, NET_ENT_ITEM); nb_u16(b, it->id);
    nb_i16(b, q_pos(it->pos.x)); nb_i16(b, q_pos(it->pos.y)); nb_i16(b, q_pos(it->pos.z));
    Quat q = quat_norm(it->rot);
    nb_i8(b, q_quat(q.x)); nb_i8(b, q_quat(q.y)); nb_i8(b, q_quat(q.z)); nb_i8(b, q_quat(q.w));
    // bit 0 broken, bits 1..3 the slot holding it plus one (0 = nobody), bit 4 in the hold
    uint8_t flags = (uint8_t)((it->broken ? 1u : 0u) | ((unsigned)(it->held_by + 1) & 7u) << 1 | (it->in_hold ? 16u : 0u));
    nb_u8(b, flags);
}

// Worth a slot in this snapshot? Held and changed things always are. "Awake" on its own is not
// enough: a pile of crates settling against each other stays awake for a while without visibly
// moving, and at 30 Hz that pile alone was three times the whole bandwidth budget.
static bool item_live(const Game *g, const Item *it) {
    if (!it->used) return false;
    // --- weapons --- A weapon in a hand is drawn off its owner's hand_r on every client, so its
    // position on the wire says nothing. Only the fact that it changed hands is worth a slot.
    if (it->weapon_hand) return it->dirty;
    if (it->dirty || it->held_by >= 0) return true;
    const PhysBody *pb = phys_body_c(&g->phys, it->body);
    if (!pb || pb->sleeping) return false;
    if (v3_len(v3_sub(it->pos, it->net_pos)) > 0.02f) return true;
    float d = it->rot.x * it->net_rot.x + it->rot.y * it->net_rot.y + it->rot.z * it->net_rot.z + it->rot.w * it->net_rot.w;
    return fabsf(d) < 0.9995f;   // about three and a half degrees
}

static void write_snapshot(Game *g, int for_slot, uint8_t *out, int *out_len, int cap) {
    NetGame *n = &g->net;
    Items *its = &g->items;
    NetBuf b; nb_init_write(&b, out, (size_t)cap);
    nb_u32(&b, g->tick);
    nb_u32(&b, n->slots[for_slot].input_tick);
    int count = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (n->slots[i].active) count++;
    nb_u8(&b, (uint8_t)count);
    // The run's takings ride in the header: a client would otherwise have to know which items are
    // inside a volume it only sees interpolated, and the two answers would disagree at the edges.
    nb_u16(&b, (uint16_t)(its->hold_count < 0 ? 0 : its->hold_count));
    nb_u32(&b, (uint32_t)(its->hold_value < 0 ? 0 : its->hold_value));
    size_t item_count_at = b.len;
    nb_u8(&b, 0);                       // patched once we know how many items fitted
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
        // --- weapons --- three bytes: the weapon hand, what is in it, and how much wind is left
        nb_u8(&b, weapons_pack_flags(g, i));
        nb_u8(&b, (uint8_t)clampf((float)g->weapons.w[i].ammo, 0, 255));
        nb_u8(&b, (uint8_t)clampf(g->weapons.w[i].wind, 0, 255));
    }
    // Pass one: everything that is moving, in someone's hands, or has just changed hands or broken.
    bool sent[ITEMS_MAX]; memset(sent, 0, sizeof sent);
    int nitems = 0;
    for (int i = 0; i < its->n && i < ITEMS_MAX; i++) {
        const Item *it = &its->it[i];
        if (!it->used) continue;
        if (!item_live(g, it)) continue;
        if (b.len + NET_ITEM_BYTES > b.cap) break;
        write_item(&b, it); sent[i] = true; nitems++;
    }
    // Pass two: a slice of the sleepers, so a client that joined late or missed a packet still ends
    // up with the whole island's furniture inside half a second.
    if (its->n > 0) {
        int slice = (its->n + NET_ITEM_SLEEP_SPAN - 1) / NET_ITEM_SLEEP_SPAN;
        for (int k = 0; k < slice; k++) {
            int i = (int)((n->item_cursor + (unsigned)k) % (unsigned)its->n);
            if (i >= ITEMS_MAX || sent[i] || !its->it[i].used) continue;
            if (b.len + NET_ITEM_BYTES > b.cap) break;
            write_item(&b, &its->it[i]); sent[i] = true; nitems++;
        }
    }
    if (!b.err) out[item_count_at] = (uint8_t)(nitems > 255 ? 255 : nitems);
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
    TravMode was = (TravMode)n->hist_trav[k];
    n->s_corrections++; n->t_corrections++;
    n->s_corr_sum += mag; n->t_corr_sum += mag;
    if (mag > n->s_corr_max) n->s_corr_max = mag;
    if (mag > 2.0f) {   // too far gone to smooth: snap
        p->c.pos = v3_add(p->c.pos, err);
        n->pos_error = v3(0, 0, 0);
        n->s_hard_snaps++; n->t_hard_snaps++;
        // --- traversal --- After a teleport the predicted velocity is fiction and a climb is a
        // path to a ledge that is no longer in front of us. The acked tick's velocity is the last
        // one the host agreed with; the climb is simply abandoned.
        p->c.hvel = n->hist_hvel[k];
        if (p->trav != TM_NONE) { p->trav = TM_NONE; p->c.traversing = false; }
        dbg_log("net: hard snap %.2f m at tick %u (was %s)", mag, ack_tick, trav_name(was));
    } else {
        // The error at ack_tick carried forward unchanged through the inputs we have already
        // applied, so correcting the current position by it is right; the view lags behind and
        // decays back, so the player never sees the jump.
        p->c.pos = v3_add(p->c.pos, err);
        n->pos_error = v3_sub(n->pos_error, err);
        // --- traversal --- A mantle is a curve the body is already halfway along: correcting the
        // body alone would be undone by the next tick of that curve, and the eye would ring at the
        // snapshot rate for the length of the climb. Move the curve with it instead.
        player_traverse_shift(p, err);
        if (was != TM_NONE && mag > 0.25f)
            dbg_log("net: %.2f m correction during %s at tick %u", mag, trav_name(was), ack_tick);
    }
    for (int i = 0; i < NET_HIST; i++) n->hist_pos[i] = v3_add(n->hist_pos[i], err);   // do not correct twice
}

static void read_snapshot(Game *g, const uint8_t *data, int len) {
    NetGame *n = &g->net;
    NetBuf b; nb_init_read(&b, data, (size_t)len);
    uint32_t stick = rb_u32(&b), ack = rb_u32(&b);
    int count = rb_u8(&b);
    int hold_count = rb_u16(&b); uint32_t hold_value = rb_u32(&b);
    int nitems = rb_u8(&b);
    if (b.err || count > NET_MAX_PLAYERS) return;
    if (stick < n->server_tick && n->server_tick - stick < 1000) return;   // stale, a newer one already landed
    n->server_tick = stick; n->server_tick_at = n->now;
    if (ack > n->last_ack) n->last_ack = ack;
    items_set_hold_totals(g, hold_count, (int)hold_value);
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
        sp.wflags = rb_u8(&b); sp.wammo = rb_u8(&b); sp.wwind = rb_u8(&b);   // --- weapons ---
        if (b.err) return;
        seen[id] = true;
        if (!n->slots[id].active) {
            if (id == (uint8_t)g->local) { n->slots[id].active = true; n->slots[id].tint = SLOT_TINT[id]; game_ensure_player_model(g, id); }
            else seat(g, id, "player");
            dbg_log("net: slot %d appeared", id);
        }
        weapons_apply_flags(g, id, sp.wflags, sp.wammo, sp.wwind);   // --- weapons ---
        if (id == (uint8_t)g->local) reconcile(g, &sp, ack);
        else push_hist(&n->slots[id], n->now, &sp);
    }
    for (int e = 0; e < nitems; e++) {
        uint8_t type = rb_u8(&b);
        uint16_t id = rb_u16(&b);
        Vec3 pos; pos.x = dq_pos(rb_i16(&b)); pos.y = dq_pos(rb_i16(&b)); pos.z = dq_pos(rb_i16(&b));
        Quat rot; rot.x = dq_quat(rb_i8(&b)); rot.y = dq_quat(rb_i8(&b)); rot.z = dq_quat(rb_i8(&b)); rot.w = dq_quat(rb_i8(&b));
        uint8_t flags = rb_u8(&b);
        if (b.err || type != NET_ENT_ITEM) return;
        int held = (int)((flags >> 1) & 7u) - 1;
        items_net_sample(g, id, pos, quat_norm(rot), held, (flags & 1u) != 0, (flags & 16u) != 0, n->now);
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (i != g->local && n->slots[i].active && !seen[i]) { dbg_log("net: slot %d (%s) left", i, n->slots[i].name); unseat(g, i); }
}

// ---------------------------------------------------------------- host

static int find_slot_by_addr(NetGame *n, const NetAddr *a) {
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (n->slots[i].has_peer && net_addr_eq(&n->slots[i].peer.addr, a)) return i;
    return -1;
}

static void host_send_events(Game *g);          // --- weapons --- defined with the other senders
static void client_read_events(Game *g, const uint8_t *data, int len);

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
    } else if (type == NRM_ITEM_GRAB) {
        uint16_t id = rb_u16(&b);
        if (b.err) return;
        if (!items_net_grab(g, slot, id)) dbg_log("net: slot %d asked for item %u and was refused", slot, id);
    } else if (type == NRM_ITEM_RELEASE) {
        uint16_t id = rb_u16(&b);
        uint8_t thrown = rb_u8(&b);
        Vec3 v; v.x = (float)rb_i16(&b) * 0.01f; v.y = (float)rb_i16(&b) * 0.01f; v.z = (float)rb_i16(&b) * 0.01f;
        if (b.err) return;
        items_net_release(g, slot, id, thrown != 0, v);
    // --- weapons --- What a client says it did with the thing in its other hand. Every one of
    // these is a request; weapons.c decides whether it happened.
    } else if (type == NRM_WEAP_FIRE) {
        Vec3 o, d;
        o.x = rb_f32(&b); o.y = rb_f32(&b); o.z = rb_f32(&b);
        d.x = (float)rb_i16(&b) / 32767.0f; d.y = (float)rb_i16(&b) / 32767.0f; d.z = (float)rb_i16(&b) / 32767.0f;
        if (b.err) return;
        weapons_net_fire(g, slot, o, d);
    } else if (type == NRM_WEAP_RELOAD) {
        weapons_net_reload(g, slot);
    } else if (type == NRM_WEAP_SWAP) {
        weapons_net_swap(g, slot);
    } else if (type == NRM_WEAP_REVIVE) {
        uint8_t target = rb_u8(&b), holding = rb_u8(&b);
        if (b.err) return;
        weapons_net_revive(g, slot, (int)target, holding != 0);
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
            // --- voice --- anything past the 10 input bytes is voice: forwarded to the other
            // clients without being decoded, and played here.
            if (pk.payload_len > 10) voice_host_block(g, slot, pk.payload + 10, pk.payload_len - 10);
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
        } else s->input.buttons &= (uint16_t)(NB_SPRINT | NB_GUARD | NB_CROUCH | NB_JUMPHELD);
        // Crouch and the held jump stay on a repeated intent for the same reason sprint and guard
        // always did: they are states, not edges. Dropping them on a dry tick ended a client's slide
        // half a second early on the host and nowhere else, which the reconciliation then spent the
        // rest of the slide arguing about -- a third of a metre at a time, every snapshot.
        Input in; Vec3 dir = unpack_input(&s->input, &in);
        if (!s->have_input) { memset(&in, 0, sizeof in); dir = v3(0, 0, 0); }
        // Two hands on the painting is two hands off everything else: the host applies the same
        // cap the client predicts, so carrying does not fight the reconciliation.
        if (items_two_handed(g, i)) { dir = v3_scale(dir, ITEM_SLOW_SPEED); in.sprint = false; }
        // --- weapons --- A goon on the floor stays on the floor whatever its client keeps sending.
        if (weapons_frozen(g, i)) { dir = v3(0, 0, 0); in.sprint = false; in.attack = in.parry = in.dodge = in.interact = false; }
        CombatEvents ev = {0};
        World world = { &g->level, &g->terrain };
        player_update(&g->players[i], &in, dir, &world, NULL, dt, &ev);
        Character *c = &g->players[i].c;
        // First person: the client's body faces its view, not its movement, so replay the yaw it
        // sent instead of the one player_update turned toward the move direction. Without this a
        // strafing client looks sideways to everyone else.
        if (g->level.view == VIEW_FIRST && s->have_input) c->yaw = dq_yaw(s->input.yaw);
        game_ground_character(g, c, dt);   // remote players stand on the world exactly as the host's own does
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        for (int j = i + 1; j < NET_MAX_PLAYERS; j++)
            if (n->slots[i].active && n->slots[j].active) character_separate(&g->players[i].c, &g->players[j].c, &g->level);
    // --- weapons --- player_update has just decided idle / walk / run for every remote player from
    // the movement it replayed. A goon lying on the floor, or holding a pistol at the hip, wants
    // something else, and this is the last word before the snapshot goes out.
    weapons_pose(g);
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
    // Every client has now been told, so the change flags can go and the sleeper sweep can move on.
    Items *its = &g->items;
    for (int i = 0; i < its->n; i++) {
        Item *it = &its->it[i];
        // Whatever went out is now what the clients believe, which is what item_live compares to.
        if (item_live(g, it)) { it->net_pos = it->pos; it->net_rot = it->rot; }
        it->dirty = false;
    }
    if (its->n > 0) {
        int slice = (its->n + NET_ITEM_SLEEP_SPAN - 1) / NET_ITEM_SLEEP_SPAN;
        for (int k = 0; k < slice; k++) {
            int i = (int)((n->item_cursor + (unsigned)k) % (unsigned)its->n);
            if (i < ITEMS_MAX) { its->it[i].net_pos = its->it[i].pos; its->it[i].net_rot = its->it[i].rot; }
        }
        n->item_cursor = (n->item_cursor + (unsigned)slice) % (unsigned)its->n;
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
        n->rejected = true;   // --- menu --- the join page turns this into one line on screen
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
        else if (pk.ptype == NPT_EVENT && pk.payload_len > 0) client_read_events(g, pk.payload, pk.payload_len);   // --- weapons ---
        else if (pk.ptype == NPT_VOICE && pk.payload_len > 0) voice_client_packet(g, pk.payload, pk.payload_len);   // --- voice ---
    }
}

static void client_send_input(Game *g) {
    NetGame *n = &g->net;
    uint8_t buf[16 + VOICE_MAX_BLOCK * 2]; NetBuf b; nb_init_write(&b, buf, 16);
    nb_u32(&b, n->cur.tick); nb_i8(&b, n->cur.mx); nb_i8(&b, n->cur.mz); nb_i16(&b, n->cur.yaw); nb_u16(&b, n->cur.buttons);
    int len = (int)b.len;
    // --- voice --- talking rides on the packet the client already sends every tick, so it costs
    // its own bytes and not a second datagram. The host reads the input from the first 10 and
    // hands whatever follows to voice.c.
    len += voice_pack_client(g, buf + len, (int)sizeof buf - len);
    int sent = net_send(&n->sock, &n->server, NPT_INPUT, buf, len, n->now);
    if (sent > 0) { n->s_pkt_out++; n->s_bytes_out += (uint64_t)sent; }
}

// --- weapons --- Tracers, flashes and thuds, host -> every client, unreliable. A lost event is
// simply not seen: it describes one frame and there is no state to fall out of step.
static void host_send_events(Game *g) {
    NetGame *n = &g->net;
    FireEvent ev[WEAP_EVENTS];
    int nev = weapons_events_take(g, ev, WEAP_EVENTS);
    if (nev <= 0) return;
    uint8_t buf[NET_MAX_PACKET - NET_HEADER]; NetBuf b; nb_init_write(&b, buf, sizeof buf);
    nb_u8(&b, (uint8_t)nev);
    for (int i = 0; i < nev; i++) {
        nb_u8(&b, ev[i].slot); nb_u8(&b, ev[i].kind); nb_u8(&b, ev[i].hit); nb_u8(&b, ev[i].pellets);
        nb_i16(&b, q_pos(ev[i].from.x)); nb_i16(&b, q_pos(ev[i].from.y)); nb_i16(&b, q_pos(ev[i].from.z));
        nb_i16(&b, q_pos(ev[i].to.x));   nb_i16(&b, q_pos(ev[i].to.y));   nb_i16(&b, q_pos(ev[i].to.z));
    }
    if (b.err) return;
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        NetSlot *s = &n->slots[i];
        if (!s->has_peer || !s->active) continue;
        int sent = net_send(&n->sock, &s->peer, NPT_EVENT, buf, (int)b.len, n->now);
        if (sent > 0) { n->s_pkt_out++; n->s_bytes_out += (uint64_t)sent; }
    }
}

// Client side of the same: play what the host says happened.
static void client_read_events(Game *g, const uint8_t *data, int len) {
    NetBuf b; nb_init_read(&b, data, (size_t)len);
    int nev = rb_u8(&b);
    for (int i = 0; i < nev; i++) {
        FireEvent e;
        e.slot = rb_u8(&b); e.kind = rb_u8(&b); e.hit = rb_u8(&b); e.pellets = rb_u8(&b);
        e.from.x = dq_pos(rb_i16(&b)); e.from.y = dq_pos(rb_i16(&b)); e.from.z = dq_pos(rb_i16(&b));
        e.to.x   = dq_pos(rb_i16(&b)); e.to.y   = dq_pos(rb_i16(&b)); e.to.z   = dq_pos(rb_i16(&b));
        if (b.err) return;
        // Our own shots were played the moment we fired them; hearing them again would double up.
        if (e.slot == (uint8_t)g->local && (e.kind == FE_SHOT || e.kind == FE_SWING || e.kind == FE_CLICK)) continue;
        weapons_event_apply(g, &e);
    }
}

void netgame_send_weapon_fire(Game *g, Vec3 origin, Vec3 dir) {
    NetGame *n = &g->net;
    if (n->mode != NM_CLIENT || !n->connected) return;
    uint8_t body[24]; NetBuf b; nb_init_write(&b, body, sizeof body);
    nb_u8(&b, NRM_WEAP_FIRE);
    nb_f32(&b, origin.x); nb_f32(&b, origin.y); nb_f32(&b, origin.z);
    nb_i16(&b, (int16_t)lrintf(clampf(dir.x, -1, 1) * 32767.0f));
    nb_i16(&b, (int16_t)lrintf(clampf(dir.y, -1, 1) * 32767.0f));
    nb_i16(&b, (int16_t)lrintf(clampf(dir.z, -1, 1) * 32767.0f));
    if (!b.err) net_reliable_send(&n->server, body, (uint16_t)b.len);
}
void netgame_send_weapon_reload(Game *g) {
    NetGame *n = &g->net;
    if (n->mode != NM_CLIENT || !n->connected) return;
    uint8_t body[1] = { NRM_WEAP_RELOAD };
    net_reliable_send(&n->server, body, 1);
}
void netgame_send_weapon_swap(Game *g) {
    NetGame *n = &g->net;
    if (n->mode != NM_CLIENT || !n->connected) return;
    uint8_t body[1] = { NRM_WEAP_SWAP };
    net_reliable_send(&n->server, body, 1);
}
void netgame_send_weapon_revive(Game *g, int target, bool holding) {
    NetGame *n = &g->net;
    if (n->mode != NM_CLIENT || !n->connected) return;
    // Held, not tapped: this goes out once every few ticks while the button is down, which is
    // cheap enough on a reliable channel and means letting go is noticed within a frame or two.
    static uint32_t last = 0;
    if (holding && g->net.net_tick - last < 6) return;
    last = g->net.net_tick;
    uint8_t body[4]; NetBuf b; nb_init_write(&b, body, sizeof body);
    nb_u8(&b, NRM_WEAP_REVIVE); nb_u8(&b, (uint8_t)target); nb_u8(&b, holding ? 1 : 0);
    if (!b.err) net_reliable_send(&n->server, body, (uint16_t)b.len);
}

void netgame_send_item_grab(Game *g, uint16_t id) {
    NetGame *n = &g->net;
    if (n->mode != NM_CLIENT || !n->connected) return;
    uint8_t body[8]; NetBuf b; nb_init_write(&b, body, sizeof body);
    nb_u8(&b, NRM_ITEM_GRAB); nb_u16(&b, id);
    if (!b.err) net_reliable_send(&n->server, body, (uint16_t)b.len);
}
void netgame_send_item_release(Game *g, uint16_t id, bool thrown, Vec3 vel) {
    NetGame *n = &g->net;
    if (n->mode != NM_CLIENT || !n->connected) return;
    uint8_t body[16]; NetBuf b; nb_init_write(&b, body, sizeof body);
    nb_u8(&b, NRM_ITEM_RELEASE); nb_u16(&b, id); nb_u8(&b, thrown ? 1 : 0);
    nb_i16(&b, (int16_t)lrintf(clampf(vel.x, -320, 320) * 100.0f));
    nb_i16(&b, (int16_t)lrintf(clampf(vel.y, -320, 320) * 100.0f));
    nb_i16(&b, (int16_t)lrintf(clampf(vel.z, -320, 320) * 100.0f));
    if (!b.err) net_reliable_send(&n->server, body, (uint16_t)b.len);
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
        host_send_events(g);   // --- weapons --- every tick, not every snapshot: a tracer is worth a packet
    } else {
        int k = (int)(n->net_tick % NET_HIST);
        const Player *lp = &g->players[g->local];
        n->hist_pos[k] = lp->c.pos; n->hist_tick[k] = n->net_tick;
        n->hist_hvel[k] = lp->c.hvel; n->hist_trav[k] = (uint8_t)lp->trav; n->hist_trav_t[k] = lp->trav_t;
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
