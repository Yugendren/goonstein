// --- boss --- The cave boss. See boss.h for what this is and why it is not the Warden.
#include "game.h"
#include "boss.h"
#include "weapons.h"
#include "projectile.h"
#include "audio.h"
#include "debug.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CB(g)  (&(g)->cave)
#define DEF(g) (&(g)->cave.b.def)

// Gravity is game.c's, and a shockwave that clears the ground by half a metre has to be measured
// against the same jump the player file describes. Kept here rather than shared because nothing
// else in this file cares how fast a goon falls.
#define BOSS_TELL_LIGHT     2.6f    // metres of point-light radius per unit of telegraph fill
#define BOSS_FLASH_DECAY   12.0f    // the white hit flash fades in about 80 ms
#define BOSS_SOLO_RESPAWN   3.0f    // seconds a lone goon lies there before waking up at the door
#define BOSS_THREAT_DECAY   0.12f   // per second, as a fraction: recent damage is what it reacts to
#define BOSS_HURT_ANIM_MIN 25.0f    // damage below this does not interrupt what it is doing

static float randf01(void) { return (float)rand() / (float)RAND_MAX; }
static Vec3  fwd(float yaw) { return v3(sinf(yaw), 0, cosf(yaw)); }
static float yaw_to_point(Vec3 from, Vec3 to) { return atan2f(to.x - from.x, to.z - from.z); }

// ---------------------------------------------------------------- lifetime

bool boss_present(const Game *g) { return g->cave.active && g->cave.b.def.nmoves > 0; }
bool boss_alive(const Game *g)   { return boss_present(g) && g->cave.b.state != BS_DEAD && g->cave.b.c.hp > 0; }

void boss_shutdown(Game *g) {
    CaveBoss *cb = CB(g);
    if (cb->model_ok) charmodel_destroy(&g->gfx, &cb->model);
    memset(cb, 0, sizeof *cb);
    cb->target = -1; cb->tell_move = -1; cb->fireball_def = -1;
}

// Every machine runs this when a level finishes loading. A level with no `boss_def` line simply
// has no boss, which is every level except the cave.
void boss_level_changed(Game *g) {
    boss_shutdown(g);
    CaveBoss *cb = CB(g);
    if (!g->level.boss_def[0]) return;

    char path[640]; snprintf(path, sizeof path, "%s/enemies/%s.txt", HOLLOW_ASSET_DIR, g->level.boss_def);
    // Loaded into a local and then handed to boss_init, NOT straight into cb->b.def: boss_init
    // zeroes the whole Boss before copying the def in, so a def that lives inside that Boss is
    // wiped by the call that is meant to install it.
    BossDef def;
    if (!boss_def_load(&def, path)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "boss: %s failed to load; this level has no boss", path);
        return;
    }
    boss_init(&cb->b, &def, g->level.boss_spawn, g->level.boss_yaw);
    const BossDef *d = &cb->b.def;
    cb->b.state = BS_IDLE; cb->b.think = 1.2f;
    cb->b.c.radius = fmaxf(d->size.x, d->size.z) * 0.5f;
    cb->b.c.height = d->size.y;
    cb->b.c.ground_block = -1;
    game_ground_character(g, &cb->b.c, 0);
    cb->active = true;
    cb->target = -1; cb->tell_move = -1;
    cb->log.started = g->time;

    const char *model = d->model[0] ? d->model : "warden";
    char mp[640]; snprintf(mp, sizeof mp, "%s/characters/%s.txt", HOLLOW_ASSET_DIR, model);
    cb->model_ok = charmodel_load(&g->gfx, &cb->model, mp);
    if (!cb->model_ok) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "boss: character %s failed to load; the box figure stands in", mp);

    // The fireball is an ordinary item file with a `projectile` line and nothing else: it is never
    // placed, never carried and never picked up. Loading it HERE, on every machine rather than
    // only on the host, is what lets a client resolve a fireball out of a snapshot -- the wire
    // carries the one-byte kind hash and nothing more, and itemdef_by_kind can only find a def
    // this process has actually read.
    cb->fireball_def = itemdef_get(&g->items, "fireball");
    if (cb->fireball_def < 0) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "boss: assets/items/fireball.txt missing; volleys will throw nothing");

    dbg_log("boss: %s loaded from %s, %.0f hp, %d moves, model %s", d->name, path, (double)d->hp, d->nmoves, model);
    SDL_Log("boss: %s (%.0f hp, %d moves) waiting at %.1f %.1f", d->name, (double)d->hp, d->nmoves,
            (double)g->level.boss_spawn.x, (double)g->level.boss_spawn.z);
}

// ---------------------------------------------------------------- who it hates

// Alive, seated, and not already flat on their back. A boss that keeps hammering a downed goon is
// not menacing, it is a bully, and it also means nobody ever gets revived.
static bool targetable(const Game *g, int slot) {
    return g->net.slots[slot].active && !weapons_is_down(g, slot);
}

static void pick_target(Game *g) {
    CaveBoss *cb = CB(g);
    const Character *c = &cb->b.c;
    int best = -1; float best_score = -1e9f;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!targetable(g, i)) continue;
        float dist = v3_len(v3_sub(g->players[i].c.pos, c->pos));
        if (DEF(g)->aggro_range > 0 && dist > DEF(g)->aggro_range) continue;
        // Near counts, and hurting it counts more. The threat table decays, so the goon who has
        // been unloading a rifle into its back for the last five seconds is the one it turns on --
        // which is the whole reason four players is a different fight from one.
        float score = cb->threat[i] * 1.0f + 400.0f / (8.0f + dist);
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best < 0) {   // everyone is down: stand over the nearest one anyway
        float nd = 1e9f;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (!g->net.slots[i].active) continue;
            float dist = v3_len(v3_sub(g->players[i].c.pos, c->pos));
            if (dist < nd) { nd = dist; best = i; }
        }
    }
    if (best != cb->target && best >= 0)
        dbg_log("boss: turning on slot %d (threat %.0f)", best, (double)cb->threat[best]);
    cb->target = best;
    cb->retarget_t = DEF(g)->retarget > 0 ? DEF(g)->retarget : 2.5f;
}

// ---------------------------------------------------------------- events

static void queue(Game *g, uint8_t kind, uint8_t slot, uint8_t hit, uint8_t amount, Vec3 from, Vec3 to) {
    FireEvent e = { .slot = slot, .kind = kind, .hit = hit, .pellets = amount, .from = from, .to = to };
    weapons_event(g, &e);   // queues for the clients AND plays it here, which is what we want
}

// ---------------------------------------------------------------- damage out

// One place decides what the boss hitting a goon means, so a charge, a shockwave and a sweep all
// hurt the same way and all produce the same red flash on the right screen.
static void land_on_player(Game *g, int slot, float damage, Vec3 from, float knock, bool floor_them) {
    if (!targetable(g, slot)) return;
    Vec3 dir = v3_sub(g->players[slot].c.pos, from); dir.y = 0;
    if (v3_len(dir) < 1e-3f) dir = fwd(CB(g)->b.c.yaw);
    weapons_hurt_player(g, slot, damage, v3_norm(dir), knock);
    if (floor_them) weapons_force_down(g, slot, "knocked over by the boss");
    // log.taken is NOT added here: weapons_hurt_player queues an FE_HURT for every point of damage
    // from any source, fireballs included, and the fight log counts those instead. Counting here as
    // well would double every sweep and miss every volley.
    if (weapons_is_down(g, slot)) CB(g)->log.downs++;
    dbg_log("boss: hit slot %d for %.0f (%s)", slot, (double)damage, floor_them ? "knockdown" : "wind");
}

// ---------------------------------------------------------------- the moves

static bool move_allowed(const Game *g, int i, float dist) {
    const BossMove *m = &DEF(g)->moves[i];
    if (CB(g)->cd[i] > 0) return false;
    if (m->max_range > 0 && (dist < m->min_range || dist > m->max_range)) return false;
    if (m->kind == BMK_VOLLEY && CB(g)->fireball_def < 0) return false;
    return true;
}

static int pick_move(Game *g, float dist) {
    const BossDef *d = DEF(g);
    float total = 0; float w[16];
    for (int i = 0; i < d->nmoves; i++) {
        w[i] = move_allowed(g, i, dist) ? fmaxf(d->moves[i].weight, 0.01f) : 0.0f;
        if (i == CB(g)->b.last_move) w[i] *= 0.35f;   // not the same thing twice in a row, usually
        total += w[i];
    }
    if (total <= 0) {   // everything on cooldown or out of band: take whatever is off cooldown
        for (int i = 0; i < d->nmoves; i++) if (CB(g)->cd[i] <= 0) return i;
        return 0;
    }
    float r = randf01() * total;
    for (int i = 0; i < d->nmoves; i++) { r -= w[i]; if (r <= 0) return i; }
    return d->nmoves - 1;
}

static void enter(Game *g, BState s, Anim a) {
    CaveBoss *cb = CB(g);
    cb->b.state = s; cb->b.t = 0; cb->b.hit_applied = false;
    character_set_anim(&cb->b.c, a);
}

static void begin_windup(Game *g, float dist) {
    CaveBoss *cb = CB(g);
    cb->b.move = pick_move(g, dist);
    const BossMove *m = &DEF(g)->moves[cb->b.move];
    enter(g, BS_WINDUP, ANIM_WINDUP);
    cb->b.c.move_id = cb->b.move;
    cb->b.c.tell_color = m->tell;
    cb->tell_move = cb->b.move;
    cb->log.moves[m->kind < BMK_KIND_COUNT ? m->kind : 0]++;
    // The roar IS the telegraph, as much as the colour is: it is the half of it that reaches a
    // player who is looking the other way, which in a cavern with pillars is most of them.
    queue(g, FE_BOSS_ROAR, (uint8_t)cb->b.move, (uint8_t)m->kind, 0, cb->b.c.pos, cb->b.c.pos);
    dbg_log("boss: winds up %s (%s) at %.1f m", m->name, boss_move_kind_name(m->kind), (double)dist);
}

// A charge: commit to a line and run down it. Anything inside the corridor goes over.
static void go_stagger(Game *g);

static void charge_tick(Game *g, const BossMove *m, float dt) {
    CaveBoss *cb = CB(g);
    Character *c = &cb->b.c;
    float step = fminf(cb->charge_speed * dt, cb->charge_left);
    Vec3 before = c->pos;
    c->pos = level_move(&g->level, c->pos, c->radius, c->height, v3_scale(cb->charge_dir, step));
    float moved = v3_len(v3_sub(c->pos, before));
    cb->charge_left -= step;
    c->speed = cb->charge_speed;
    c->walk_phase += moved * 2.0f;

    float reach = c->radius + m->p3;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!targetable(g, i)) continue;
        Vec3 d = v3_sub(g->players[i].c.pos, c->pos); d.y = 0;
        if (v3_len(d) > reach + g->players[i].c.radius) continue;
        if (cb->shock.caught[i]) continue;           // the charge reuses the ring's "already got you" flags
        cb->shock.caught[i] = true;
        land_on_player(g, i, m->damage, before, 7.0f, true);
    }
    // It hit a wall, or it has run its length. Either ends the charge; hitting a wall ends it hard,
    // which is the window the fight is built around.
    if (moved < step * 0.4f) {
        // Into stone at twelve metres a second. This is the other way to stagger it, and the one a
        // goon on their own can actually reach: 120 damage in two seconds is three players' worth
        // of rifle fire, but baiting a charge into a pillar is a thing one person can do on purpose.
        cb->charge_left = 0;
        queue(g, FE_BOSS_ROAR, (uint8_t)cb->b.move, 200, 0, c->pos, c->pos);   // hit 200 = "into a wall"
        dbg_log("boss: charge hit a wall at %.1f %.1f", (double)c->pos.x, (double)c->pos.z);
        go_stagger(g);
        return;
    } else if (cb->charge_left <= 0.001f) {
        enter(g, BS_RECOVER, ANIM_STRIKE);
    }
}

static void shock_tick(Game *g, float dt, bool host) {
    CaveBoss *cb = CB(g);
    BossShock *s = &cb->shock;
    if (!s->live) return;
    float prev = s->r;
    s->r += s->speed * dt;
    if (host) {
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (!targetable(g, i) || s->caught[i]) continue;
            const Character *pc = &g->players[i].c;
            Vec3 d = v3_sub(pc->pos, s->at); d.y = 0;
            float dist = v3_len(d);
            // Inside the band the ring swept this tick, and with the feet low enough to be caught.
            // A goon in the air above the ring's height is over it, which is the whole move.
            if (dist < prev - pc->radius || dist > s->r + pc->radius) continue;
            if (pc->pos.y - s->at.y > s->height) continue;
            s->caught[i] = true;
            land_on_player(g, i, s->damage, s->at, s->knock, true);
        }
    }
    if (s->r >= s->r_max) s->live = false;
}

static void volley_tick(Game *g, const BossMove *m, float dt) {
    CaveBoss *cb = CB(g);
    cb->volley_t -= dt;
    if (cb->volley_left <= 0 || cb->volley_t > 0) return;
    cb->volley_t = fmaxf(m->p2, 0.05f);
    cb->volley_left--;
    int t = cb->target;
    if (t < 0 || !g->net.slots[t].active || cb->fireball_def < 0) return;
    Character *c = &cb->b.c;
    Vec3 from = v3(c->pos.x, c->pos.y + c->height * 0.62f, c->pos.z);
    from = v3_add(from, v3_scale(fwd(c->yaw), c->radius + 0.4f));
    const Character *pc = &g->players[t].c;
    Vec3 to = v3(pc->pos.x, pc->pos.y + pc->height * 0.55f, pc->pos.z);
    // Aimed where they are, not where they will be. A fireball you cannot walk out of is not a
    // dodgeable projectile, it is a delayed hitscan, and the whole point of the move is the walk.
    Vec3 dir = v3_norm(v3_sub(to, from));
    projectile_spawn(g, WEAP_BOSS_SLOT, cb->fireball_def, from, dir, false, 0);
}

// The moment a windup becomes a move.
static void strike(Game *g) {
    CaveBoss *cb = CB(g);
    const BossMove *m = &DEF(g)->moves[cb->b.move];
    Character *c = &cb->b.c;
    memset(cb->shock.caught, 0, sizeof cb->shock.caught);
    switch (m->kind) {
    case BMK_CHARGE:
        cb->charge_dir = fwd(c->yaw);
        cb->charge_left = m->p2 > 0 ? m->p2 : 14.0f;
        cb->charge_speed = m->p1 > 0 ? m->p1 : 12.0f;
        break;
    case BMK_SLAM: {
        BossShock *s = &cb->shock;
        s->live = true; s->at = c->pos; s->r = 0.5f;
        s->r_max = m->p1 > 0 ? m->p1 : 6.0f;
        s->speed = m->p2 > 0 ? m->p2 : 11.0f;
        s->height = m->p3 > 0 ? m->p3 : 0.5f;
        s->damage = m->damage; s->knock = 6.0f;
        memset(s->caught, 0, sizeof s->caught);
        audio_play(SND_BOOM, 0.8f, 0.55f);
    } break;
    case BMK_VOLLEY: {
        int n = (int)(m->p1 > 0 ? m->p1 : 5);
        if (cb->b.phase2) n *= 2;   // phase two: the volley comes twice
        cb->volley_left = n; cb->volley_t = 0;
    } break;
    default: break;   // BMK_SWEEP lands its hit in the active window below
    }
    enter(g, BS_ACTIVE, ANIM_STRIKE);
    c->tell = 1;
}

// ---------------------------------------------------------------- stagger and death

static void go_stagger(Game *g) {
    CaveBoss *cb = CB(g);
    cb->stag_t = DEF(g)->stagger_time > 0 ? DEF(g)->stagger_time : 1.5f;
    cb->stag_acc = 0; cb->stag_win = 0;
    cb->shock.live = false; cb->volley_left = 0; cb->charge_left = 0;
    cb->b.c.tell = 0; cb->tell_move = -1;
    enter(g, BS_STAGGER, ANIM_STAGGER);
    cb->log.staggers++;
    queue(g, FE_BOSS_STAGGER, 0, 0, 0, cb->b.c.pos, cb->b.c.pos);
    dbg_log("boss: staggered (%u so far), %.1f s open at %.0fx damage",
            cb->log.staggers, (double)cb->stag_t, (double)DEF(g)->stagger_damage_mult);
}

static void go_dead(Game *g) {
    CaveBoss *cb = CB(g);
    cb->b.c.hp = 0;
    cb->shock.live = false; cb->volley_left = 0; cb->charge_left = 0;
    cb->b.c.tell = 0; cb->tell_move = -1; cb->stag_t = 0;
    enter(g, BS_DEAD, ANIM_DEAD);
    cb->death_t = 0;
    cb->log.ended = g->time;
    queue(g, FE_BOSS_DIE, 0, 0, 0, cb->b.c.pos, cb->b.c.pos);
    boss_report(g);
}

void boss_hurt(Game *g, int by_slot, float damage, Vec3 dir, Vec3 at) {
    if (!boss_alive(g) || g->net.mode == NM_CLIENT) return;
    CaveBoss *cb = CB(g);
    const BossDef *d = DEF(g);
    float mult = cb->stag_t > 0 ? (d->stagger_damage_mult > 0 ? d->stagger_damage_mult : 2.0f) : 1.0f;
    // While staggered every move is cancelled, so the multiplier is the reward for the window
    // rather than a number attached to a state nobody can reach. damage_scale is what turns a
    // number tuned against a goon's wind pool into a number against six hundred points of boss.
    float dealt = damage * mult * (d->damage_scale > 0 ? d->damage_scale : 1.0f);
    cb->b.c.hp = fmaxf(0, cb->b.c.hp - dealt);
    // log.dealt and log.hits are counted in boss_event_apply, not here: the event is played on this
    // machine as well as sent, so counting in both places would double the host's numbers -- and
    // counting only here would leave every client's closing report reading "dealt 0", which is the
    // one number a client can actually see happening.
    if (by_slot >= 0 && by_slot < NET_MAX_PLAYERS) cb->threat[by_slot] += dealt;

    // Damage taken WHILE staggered does not count toward the next stagger. Without that the window
    // pays for itself: double damage for a second and a half is more than the 120 the window costs,
    // so one goon could chain staggers forever off the first one. The window is a reward for a
    // burst, not a state you can live in.
    if (cb->stag_t <= 0) {
        // A FIXED two-second window that opens on the first hit and closes on its own, not a
        // rolling "two seconds since the last hit". The rolling version never closes while anybody
        // is still shooting, which quietly turns "120 damage inside two seconds" into "120 damage,
        // eventually" and lets one goon stagger it on their own.
        if (cb->stag_win <= 0) { cb->stag_acc = 0; cb->stag_win = d->stagger_window > 0 ? d->stagger_window : 2.0f; }
        cb->stag_acc += dealt;
    }

    bool staggering = d->stagger_damage > 0 && cb->stag_acc >= d->stagger_damage && cb->stag_t <= 0 && cb->b.state != BS_DEAD;
    queue(g, FE_BOSS_HIT, (uint8_t)(by_slot < 0 ? 255 : by_slot), staggering ? 1 : 0,
          (uint8_t)clampf(dealt, 1, 255), at, at);

    if (cb->b.c.hp <= 0) { go_dead(g); return; }
    if (staggering) { go_stagger(g); return; }
    // A big hit rocks it out of a windup; small-arms fire does not, or a boss with four rifles on
    // it would never get a move off.
    if (dealt >= BOSS_HURT_ANIM_MIN && cb->b.state == BS_IDLE) character_set_anim(&cb->b.c, ANIM_HURT);
    (void)dir;
}

// ---------------------------------------------------------------- the tick

// Solo: nobody is coming to pick you up. Rather than lie there for six seconds and then get up at
// the boss's feet, a lone goon wakes at the level's spawn after three, and the boss keeps every
// point of damage it has taken. Losing is losing time, not losing progress.
static void solo_respawn(Game *g, float dt) {
    CaveBoss *cb = CB(g);
    int seated = 0, up = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (g->net.slots[i].active) { seated++; if (!weapons_is_down(g, i)) up++; }
    if (seated > 1 || up > 0) { cb->solo_down_t = 0; return; }
    cb->solo_down_t += dt;
    if (cb->solo_down_t < BOSS_SOLO_RESPAWN) return;
    cb->solo_down_t = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g->net.slots[i].active || !weapons_is_down(g, i)) continue;
        weapons_revive_now(g, i, "alone in the cave, so the cave puts you back at the door");
        Vec3 at = v3_add(g->level.spawn, v3(((float)i - 1.5f) * 1.5f, 0, 0));
        g->players[i].c.pos = at; g->players[i].c.ground_block = -1;
        game_ground_character(g, &g->players[i].c, 0);
        g->players[i].c.hvel = v3(0, 0, 0); g->players[i].c.vy = 0;
        SDL_Log("boss: back at the cave door with %.0f of the boss's health left", (double)CB(g)->b.c.hp);
    }
    // Give it a moment to come at you rather than meeting you at the door.
    cb->b.think = fmaxf(cb->b.think, 1.5f);
    cb->threat[0] = cb->threat[1] = cb->threat[2] = cb->threat[3] = 0;
}

void boss_tick(Game *g, float dt) {
    CaveBoss *cb = CB(g);
    if (!cb->active) return;
    Character *c = &cb->b.c;
    bool host = g->net.mode != NM_CLIENT;

    // Look, on every machine: the flash decays, the ring keeps growing between snapshots.
    if (c->flash > 0) c->flash = fmaxf(0, c->flash - dt * BOSS_FLASH_DECAY);
    cb->tell_k = c->tell;
    shock_tick(g, dt, host && cb->b.state != BS_DEAD);

    if (!host) { c->anim_t += dt; return; }   // a client decides nothing else about this boss

    c->anim_t += dt; cb->b.t += dt;
    for (int i = 0; i < DEF(g)->nmoves && i < 16; i++) if (cb->cd[i] > 0) cb->cd[i] -= dt;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) cb->threat[i] *= expf(-BOSS_THREAT_DECAY * dt);
    if (cb->stag_win > 0) { cb->stag_win -= dt; if (cb->stag_win <= 0) cb->stag_acc = 0; }

    if (cb->b.state == BS_DEAD) {
        cb->death_t += dt;
        c->tell = 0;
        // The loot is NOT dropped here. It is dropped in boss_event_apply's FE_BOSS_DIE, which runs
        // on this machine too (weapons_event plays what it queues) and on every client, at the same
        // fixed id -- and it has to be, because an item that appears mid-level only exists on a
        // client if that client made it itself: the snapshot's item entry carries an id and a
        // position and no definition at all.
        game_ground_character(g, c, dt);
        return;
    }

    solo_respawn(g, dt);

    // Who, and is that still a person.
    cb->retarget_t -= dt;
    if (cb->target < 0 || !targetable(g, cb->target) || cb->retarget_t <= 0) pick_target(g);
    if (cb->target < 0) { character_set_anim(c, ANIM_IDLE); game_ground_character(g, c, dt); return; }

    const Character *tc = &g->players[cb->target].c;
    Vec3 to_t = v3_sub(tc->pos, c->pos); to_t.y = 0;
    float dist = v3_len(to_t);
    const BossDef *d = DEF(g);
    const BossMove *m = &d->moves[cb->b.move];
    float wm = cb->b.phase2 ? (d->phase2_windup_mult > 0 ? d->phase2_windup_mult : 0.7f) : 1.0f;

    if (!cb->b.phase2 && c->hp <= c->hp_max * (d->phase2_hp > 0 ? d->phase2_hp : 0.5f)) {
        cb->b.phase2 = true;
        queue(g, FE_BOSS_ROAR, (uint8_t)cb->b.move, 201, 0, c->pos, c->pos);   // hit 201 = "phase two"
        dbg_log("boss: phase two at %.0f hp", (double)c->hp);
        SDL_Log("boss: phase two");
    }

    float want_speed = 0;
    switch (cb->b.state) {
    case BS_STAGGER:
        c->tell = 0;
        cb->stag_t -= dt;
        if (cb->stag_t <= 0) { cb->stag_t = 0; enter(g, BS_IDLE, ANIM_ROAR); cb->b.think = 0.5f; }
        break;

    case BS_IDLE: {
        c->yaw = angle_damp(c->yaw, yaw_to_point(c->pos, tc->pos), 5, dt);
        cb->b.think -= dt;
        // It does not stand still. Between moves it walks a slow arc around whoever it is angry
        // at, which is what makes a pause read as a wind-up to something rather than as a pause.
        bool loco = c->anim == ANIM_IDLE || c->anim == ANIM_WALK || c->anim == ANIM_HURT;
        if (loco && dist < d->attack_range + 6.0f && dist > 2.0f && cb->b.think > 0.2f) {
            Vec3 right = v3(cosf(c->yaw), 0, -sinf(c->yaw));
            Vec3 want = v3_add(v3_scale(right, cb->b.strafe_dir),
                               v3_scale(fwd(c->yaw), clampf(dist - d->attack_range, -1, 1) * 0.9f));
            Vec3 before = c->pos;
            c->pos = level_move(&g->level, c->pos, c->radius, c->height, v3_scale(want, d->speed * 0.55f * dt));
            want_speed = v3_len(v3_sub(c->pos, before)) / fmaxf(dt, 1e-5f);
            c->walk_phase += want_speed * dt * 3.0f;
            character_set_anim(c, ANIM_WALK);
        } else if (loco) character_set_anim(c, ANIM_IDLE);
        if (cb->b.think <= 0) {
            cb->b.strafe_dir = cb->b.strafe_dir >= 0 ? -1.0f : 1.0f;
            // A charge is the move that closes distance, so being far away is not a reason to walk
            // -- it is a reason to pick something that reaches. Walk only when nothing does.
            bool any_reaches = false;
            for (int i = 0; i < d->nmoves; i++) if (move_allowed(g, i, dist)) any_reaches = true;
            if (!any_reaches) enter(g, BS_APPROACH, ANIM_WALK);
            else begin_windup(g, dist);
        }
    } break;

    case BS_APPROACH: {
        c->yaw = angle_damp(c->yaw, yaw_to_point(c->pos, tc->pos), 7, dt);
        Vec3 before = c->pos;
        c->pos = level_move(&g->level, c->pos, c->radius, c->height,
                            v3_scale(fwd(c->yaw), d->speed * (cb->b.phase2 ? 1.2f : 1.0f) * dt));
        float moved = v3_len(v3_sub(c->pos, before));
        want_speed = moved / fmaxf(dt, 1e-5f);
        c->walk_phase += moved * 2.0f;
        bool any_reaches = false;
        for (int i = 0; i < d->nmoves; i++) if (move_allowed(g, i, dist)) any_reaches = true;
        if (any_reaches || cb->b.t > 5.0f) { if (any_reaches) begin_windup(g, dist); else { enter(g, BS_IDLE, ANIM_IDLE); cb->b.think = 0.3f; } }
    } break;

    case BS_WINDUP: {
        float w = fmaxf(m->windup * wm, 0.08f);
        // It tracks you for the first two thirds and then it is committed. That commitment is the
        // whole reason a telegraph is worth reading: after it, moving is an answer.
        if (cb->b.t < w * 0.66f) c->yaw = angle_damp(c->yaw, yaw_to_point(c->pos, tc->pos), 9, dt);
        c->tell = clampf(cb->b.t / w, 0, 1);
        if (cb->b.t >= w) strike(g);
    } break;

    case BS_ACTIVE: {
        switch (m->kind) {
        case BMK_CHARGE: charge_tick(g, m, dt); break;
        case BMK_VOLLEY:
            c->yaw = angle_damp(c->yaw, yaw_to_point(c->pos, tc->pos), 4, dt);
            volley_tick(g, m, dt);
            if (cb->volley_left <= 0 && cb->volley_t <= 0) enter(g, BS_RECOVER, ANIM_STRIKE);
            break;
        case BMK_SLAM:
            if (cb->b.t >= m->active) enter(g, BS_RECOVER, ANIM_STRIKE);
            break;
        default:
            if (m->step > 0 && dist > c->radius + tc->radius + 0.3f) {
                float sp = m->step / fmaxf(m->active, 0.01f);
                c->pos = level_move(&g->level, c->pos, c->radius, c->height, v3_scale(fwd(c->yaw), sp * dt));
                want_speed = sp * 0.4f;
            }
            if (!cb->b.hit_applied && cb->b.t >= m->active * 0.5f) {
                cb->b.hit_applied = true;
                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    if (!targetable(g, i)) continue;
                    if (!character_in_arc(c, g->players[i].c.pos, m->range + g->players[i].c.radius)) continue;
                    land_on_player(g, i, m->damage, c->pos, 5.0f, false);
                }
            }
            if (cb->b.t >= m->active) enter(g, BS_RECOVER, ANIM_STRIKE);
            break;
        }
    } break;

    case BS_RECOVER:
        c->tell = fmaxf(0, c->tell - dt * 4);
        cb->tell_move = -1;
        if (cb->b.t >= m->recovery) {
            cb->b.last_move = cb->b.move;
            cb->cd[cb->b.move] = m->cooldown;
            float combo = d->combo_chance * (cb->b.phase2 ? 1.5f : 1.0f);
            if (randf01() < combo) begin_windup(g, dist);
            else {
                enter(g, BS_IDLE, ANIM_IDLE);
                cb->b.think = lerpf(d->think_min, d->think_max, randf01()) * (cb->b.phase2 ? 0.65f : 1.0f);
            }
        }
        break;

    default: break;
    }

    c->speed = damp(c->speed, want_speed, 10, dt);
    game_ground_character(g, c, dt);

    // Nobody stands inside it. character_separate moves the lighter body more, and the boss is
    // four times a goon's radius, so in practice this shoves the goon out and leaves the boss where
    // it was -- which is what a thing this size walking into you should feel like. Without it the
    // camera ends up inside its chest and the fight is played from within a texture.
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g->net.slots[i].active || weapons_is_down(g, i)) continue;
        character_separate(&g->players[i].c, c, &g->level);
    }
}

// ---------------------------------------------------------------- being shot at

bool boss_ray(const Game *g, Vec3 from, Vec3 dir, float max_dist, float *out_dist, Vec3 *out_point) {
    if (!boss_alive(g)) return false;
    const Character *c = &g->cave.b.c;
    // The same fat-cylinder approximation weapons_trace uses on a goon, sampled up the body. A
    // boss this size does not want a tight hit box: hitting it has to feel certain.
    float best = -1; Vec3 bp = from;
    for (int s = 0; s <= 10; s++) {
        float t = ((float)s / 10.0f) * c->height;
        Vec3 centre = v3(c->pos.x, c->pos.y + t, c->pos.z);
        Vec3 rel = v3_sub(centre, from);
        float along = v3_dot(rel, dir);
        if (along < 0.05f || along > max_dist) continue;
        Vec3 near_p = v3_add(from, v3_scale(dir, along));
        if (v3_len(v3_sub(near_p, centre)) > c->radius * 1.1f) continue;
        if (best < 0 || along < best) { best = along; bp = near_p; }
    }
    if (best < 0) return false;
    if (out_dist) *out_dist = best;
    if (out_point) *out_point = bp;
    return true;
}

// ---------------------------------------------------------------- what a telegraph looks like

// Every telegraph is the same two ideas: a shape on the floor where the hit will be, and a colour
// that fills as the windup runs out. The shape is what tells you which way to move; the fill is
// what tells you when. Both are drawn from state the snapshot already carries, so a client draws
// them from the boss's move id and windup fraction and never needs an event to arrive on time.
static void draw_tell(Game *g, Gfx *x) {
    const CaveBoss *cb = CB(g);
    if (cb->b.state != BS_WINDUP || cb->tell_k <= 0.01f) return;
    const BossDef *d = &cb->b.def;
    if (cb->b.move < 0 || cb->b.move >= d->nmoves) return;
    const BossMove *m = &d->moves[cb->b.move];
    const Character *c = &cb->b.c;
    float k = cb->tell_k;
    // Full brightness at the end of the windup, and a pulse on the way there so it reads as a
    // countdown rather than a fade.
    float pulse = 0.55f + 0.45f * sinf(k * 22.0f);
    Vec4 col = v4(m->tell.x, m->tell.y, m->tell.z, (0.18f + 0.55f * k * k) * pulse);
    Vec3 base = v3(c->pos.x, c->pos.y + 0.05f, c->pos.z);

    switch (m->kind) {
    case BMK_CHARGE: {
        // The lane. Twenty quads down the line it has committed to, narrowing nothing: if you are
        // standing on one of them when the windup ends, you are going over.
        float len = m->p2 > 0 ? m->p2 : 14.0f, half = m->p3 > 0 ? m->p3 : 1.6f;
        Vec3 dir = fwd(c->yaw);
        int n = (int)(len / (half * 0.9f)) + 1; if (n > 40) n = 40;
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)(n - 1 > 0 ? n - 1 : 1);
            Vec3 p = v3_add(base, v3_scale(dir, c->radius + t * len));
            Vec4 cc = col; cc.w *= 1.0f - 0.35f * t;
            gfx_ground_quad(x, p, half, cc, true);
        }
    } break;
    case BMK_SLAM: {
        // The ring's final radius, drawn as a ring rather than a disc: what matters is where the
        // edge will be when it gets to you, and a filled circle says "stand outside" when the real
        // answer is "be in the air".
        float r = m->p1 > 0 ? m->p1 : 6.0f;
        int n = 28;
        for (int i = 0; i < n; i++) {
            float a = (float)i / (float)n * 2.0f * PI;
            gfx_ground_quad(x, v3(base.x + cosf(a) * r, base.y, base.z + sinf(a) * r), 0.85f, col, true);
        }
        Vec4 inner = col; inner.w *= 0.30f;
        gfx_ground_quad(x, base, r * 0.55f, inner, true);
    } break;
    case BMK_VOLLEY: {
        // A mark under whoever it has picked, so the other three know it is not them.
        int t = cb->target;
        // A ring of small patches rather than one big disc. A disc drawn additively is a glow and
        // reads as lighting; a ring of separate marks reads as something drawn on the floor, which
        // is the difference between "it is bright here" and "this is where it lands".
        Vec4 hot = col; hot.w = fminf(col.w * 2.4f, 1.0f);
        if (t >= 0 && t < NET_MAX_PLAYERS && g->net.slots[t].active) {
            Vec3 p = g->players[t].c.pos; p.y += 0.05f;
            float rr = 1.5f + 0.5f * k;
            for (int i = 0; i < 12; i++) {
                float a = (float)i / 12.0f * 2.0f * PI + k * 1.5f;
                gfx_ground_quad(x, v3(p.x + cosf(a) * rr, p.y, p.z + sinf(a) * rr), 0.34f, hot, true);
            }
            gfx_ground_quad(x, p, 0.42f, hot, true);
        }
        for (int i = 0; i < 10; i++) {
            float a = (float)i / 10.0f * 2.0f * PI - k * 1.5f;
            float rr = c->radius * 1.8f;
            gfx_ground_quad(x, v3(base.x + cosf(a) * rr, base.y, base.z + sinf(a) * rr), 0.40f, col, true);
        }
    } break;
    default: {
        // A sweep: the ground in front of it, in two arcs. Two rather than one because from five
        // metres away in first person the far arc is the only part of it above the bottom of the
        // frame, and from two metres away the near arc is the only part still in front of you.
        float r = m->range;
        for (int ring = 0; ring < 2; ring++) {
            float rr = r * (ring ? 0.92f : 0.52f);
            // Small and dim. These are drawn additively and a sweep happens close enough to the
            // camera that a big soft quad is a floodlight rather than a mark on the floor.
            Vec4 cc = col; cc.w *= ring ? 0.55f : 0.40f;
            int n = ring ? 15 : 11;
            for (int i = 0; i < n; i++) {
                float a = c->yaw + (-0.95f + 1.9f * (float)i / (float)(n - 1));
                gfx_ground_quad(x, v3(base.x + sinf(a) * rr, base.y, base.z + cosf(a) * rr), r * 0.11f, cc, true);
            }
        }
    } break;
    }
}

static void draw_shock(Game *g, Gfx *x) {
    const BossShock *s = &CB(g)->shock;
    if (!s->live) return;
    float k = 1.0f - clampf(s->r / fmaxf(s->r_max, 0.01f), 0, 1);
    Vec4 col = v4(2.4f, 0.85f, 0.25f, 0.35f + 0.55f * k);
    int n = (int)clampf(s->r * 6.0f, 12, 64);
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * 2.0f * PI;
        Vec3 p = v3(s->at.x + cosf(a) * s->r, s->at.y + s->height * 0.5f, s->at.z + sinf(a) * s->r);
        gfx_billboard(x, p, s->height * 1.6f, col, true);
        gfx_ground_quad(x, v3(p.x, s->at.y + 0.05f, p.z), 0.8f, col, true);
    }
}

void boss_draw_shadow(Game *g) {
    CaveBoss *cb = CB(g);
    if (!boss_present(g) || !cb->model_ok || cb->model.is_sprite) return;
    charmodel_draw(&g->gfx, &cb->model, &cb->b.c, v4(1, 1, 1, 1));
}

void boss_draw(Game *g) {
    CaveBoss *cb = CB(g);
    if (!cb->active) return;
    Gfx *x = &g->gfx;
    const Character *c = &cb->b.c;

    // White for 80 ms on a hit, and the telegraph's own colour bled into the rim while it winds up.
    // The flash is the single most important piece of feedback in the fight: it is the only thing
    // that says "that shot counted" for a target too big to visibly flinch.
    Vec4 tint = v4(1, 1, 1, 1);
    if (c->flash > 0) { float f = clampf(c->flash, 0, 1); tint = v4(lerpf(1, 2.6f, f), lerpf(1, 2.6f, f), lerpf(1, 2.6f, f), 1); }
    Material mat = material_default();
    mat.rim = 0.45f; mat.rim_color = v3(0.9f, 0.35f, 0.25f);
    if (cb->b.state == BS_WINDUP && cb->tell_k > 0) {
        const BossMove *m = &cb->b.def.moves[cb->b.move];
        mat.rim = 0.45f + 1.1f * cb->tell_k;
        mat.rim_color = m->tell;
        mat.emissive = v3_scale(m->tell, 0.35f * cb->tell_k * cb->tell_k);
    }
    if (cb->stag_t > 0) { mat.rim = 1.2f; mat.rim_color = v3(1.0f, 0.95f, 0.5f); }
    gfx_set_material(x, &mat);
    if (cb->model_ok) charmodel_draw(x, &cb->model, c, tint);
    else gfx_draw_box(x, &g->wt.tex[TEX_METAL], v3(c->pos.x, c->pos.y + c->height * 0.5f, c->pos.z),
                      cb->b.def.size, c->yaw, v4(cb->b.def.color.x * tint.x, cb->b.def.color.y * tint.y, cb->b.def.color.z * tint.z, 1), 1.0f);
    gfx_set_material(x, NULL);

    draw_tell(g, x);
    draw_shock(g, x);
}

int boss_lights(const Game *g, PointLight *out, int max) {
    const CaveBoss *cb = &g->cave;
    int n = 0;
    if (!cb->active || max <= 0) return 0;
    if (cb->b.state == BS_WINDUP && cb->tell_k > 0.05f && cb->b.move < cb->b.def.nmoves) {
        const BossMove *m = &cb->b.def.moves[cb->b.move];
        out[n].pos = v3(cb->b.c.pos.x, cb->b.c.pos.y + cb->b.c.height * 0.6f, cb->b.c.pos.z);
        out[n].color = m->tell;
        out[n].radius = BOSS_TELL_LIGHT + 5.0f * cb->tell_k;
        out[n].intensity = 1.6f * cb->tell_k * cb->tell_k;
        n++;
    }
    if (n < max && cb->shock.live) {
        out[n].pos = v3(cb->shock.at.x, cb->shock.at.y + 0.6f, cb->shock.at.z);
        out[n].color = v3(2.2f, 0.8f, 0.25f);
        out[n].radius = cb->shock.r + 3.0f;
        out[n].intensity = 1.4f * (1.0f - clampf(cb->shock.r / fmaxf(cb->shock.r_max, 0.01f), 0, 1));
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- the bar at the top

void boss_draw_hud(Game *g) {
    const CaveBoss *cb = CB(g);
    if (!cb->active) return;
    Gfx *x = &g->gfx;
    const float W = (float)INTERNAL_W;
    float k = cb->b.c.hp / fmaxf(cb->b.c.hp_max, 1.0f);
    // The bar fades out a couple of seconds after it dies rather than vanishing on the frame it
    // does: the last hit is worth looking at.
    float alpha = cb->b.state == BS_DEAD ? clampf(2.5f - cb->death_t, 0, 1) : 1.0f;
    if (alpha <= 0.01f) return;

    // Below the hint lines game.c writes across the top, not through them.
    // The empty part of the bar has to be visible in a cave lit by three lava pools, so it is a
    // light outline over a dark fill rather than a dark fill on its own -- at 10% health the fill
    // is a sliver and the outline is the only thing telling you the bar is there at all.
    float bw = 560, bx = (W - bw) * 0.5f, by = 96;
    gfx_ui_rect(x, bx - 3, by - 3, bw + 6, 18, v4(0, 0, 0, 0.55f * alpha));
    gfx_ui_rect(x, bx - 1, by - 1, bw + 2, 14, v4(0.62f, 0.56f, 0.52f, 0.85f * alpha));
    gfx_ui_rect(x, bx, by, bw, 12, v4(0.14f, 0.06f, 0.07f, 0.95f * alpha));
    Vec4 front = cb->stag_t > 0 ? v4(1.00f, 0.92f, 0.45f, alpha)
               : cb->b.phase2  ? v4(0.95f, 0.30f, 0.20f, alpha)
                               : v4(0.78f, 0.18f, 0.16f, alpha);
    gfx_ui_rect(x, bx, by, bw * clampf(k, 0, 1), 12, front);
    // The stagger meter rides under the health bar: it only appears while damage is actually
    // accumulating, so it reads as something you are doing rather than as another stat.
    if (cb->stag_win > 0 && cb->b.def.stagger_damage > 0 && cb->stag_t <= 0) {
        float sk = clampf(cb->stag_acc / cb->b.def.stagger_damage, 0, 1);
        gfx_ui_rect(x, bx, by + 13, bw * sk, 3, v4(1.0f, 0.85f, 0.35f, 0.8f * alpha));
    }
    const char *name = cb->b.def.name;
    float tw = gfx_ui_text_width(1.0f, name);
    gfx_ui_text(x, (W - tw) * 0.5f, by - 26, 1.0f, v4(0.92f, 0.88f, 0.85f, alpha), name);
    if (cb->stag_t > 0) {
        const char *s = "STAGGER";
        float sw = gfx_ui_text_width(1.6f, s);
        gfx_ui_text(x, (W - sw) * 0.5f, by + 20, 1.6f, v4(1.0f, 0.92f, 0.45f, alpha), s);
    } else if (cb->b.phase2 && cb->b.state != BS_DEAD) {
        const char *s = "PHASE II";
        float sw = gfx_ui_text_width(1.0f, s);
        gfx_ui_text(x, (W - sw) * 0.5f, by + 20, 1.0f, v4(0.95f, 0.45f, 0.30f, 0.85f * alpha), s);
    }
}

// ---------------------------------------------------------------- the wire

// Sixteen bytes, thirty times a second: half a kilobyte a second for the whole boss. Everything a
// client needs to draw it AND to draw its telegraphs, which is the point -- a telegraph derived
// from replicated state cannot be missed by a dropped packet the way an event can.
void boss_net_write(const Game *g, void *netbuf) {
    NetBuf *b = (NetBuf *)netbuf;
    const CaveBoss *cb = &g->cave;
    if (!cb->active) { nb_u8(b, 0); return; }
    const Character *c = &cb->b.c;
    uint8_t flags = 1u
                  | (cb->b.phase2 ? 2u : 0u)
                  | (cb->stag_t > 0 ? 4u : 0u)
                  | (cb->b.state == BS_DEAD ? 8u : 0u);
    nb_u8(b, flags);
    nb_i16(b, (int16_t)lrintf(clampf(c->pos.x, -327, 327) * 100.0f));
    nb_i16(b, (int16_t)lrintf(clampf(c->pos.y, -327, 327) * 100.0f));
    nb_i16(b, (int16_t)lrintf(clampf(c->pos.z, -327, 327) * 100.0f));
    nb_i16(b, (int16_t)lrintf(clampf(angle_wrap(c->yaw), -PI, PI) * (32767.0f / PI)));
    nb_u16(b, (uint16_t)clampf(c->hp, 0, 65535.0f));
    nb_u8(b, (uint8_t)c->anim);
    nb_u8(b, (uint8_t)cb->b.state);
    nb_u8(b, (uint8_t)cb->b.move);
    nb_u8(b, (uint8_t)clampf(c->tell * 255.0f, 0, 255));
    nb_u8(b, (uint8_t)clampf(cb->shock.live ? cb->shock.r * 10.0f : 0.0f, 0, 255));
    nb_u8(b, (uint8_t)(cb->target < 0 ? 255 : cb->target));
    nb_u8(b, (uint8_t)clampf(cb->stag_acc, 0, 255));
}

void boss_net_read(Game *g, void *netbuf) {
    NetBuf *b = (NetBuf *)netbuf;
    uint8_t flags = rb_u8(b);
    if (b->err) return;
    if (!(flags & 1u)) return;                 // the host says this level has no boss
    CaveBoss *cb = CB(g);
    Vec3 pos;
    pos.x = (float)rb_i16(b) * 0.01f; pos.y = (float)rb_i16(b) * 0.01f; pos.z = (float)rb_i16(b) * 0.01f;
    float yaw = (float)rb_i16(b) * (PI / 32767.0f);
    float hp = (float)rb_u16(b);
    uint8_t anim = rb_u8(b), state = rb_u8(b), move = rb_u8(b), tell = rb_u8(b), shock = rb_u8(b);
    uint8_t target = rb_u8(b), stag = rb_u8(b);
    if (b->err) return;
    if (!cb->active) return;                   // a client on a level whose boss file failed to load
    Character *c = &cb->b.c;
    // The boss is snapped rather than interpolated. It is heavy and slow and sits in the middle of
    // the screen for the whole fight, so a hundred-millisecond interpolation delay would put its
    // telegraph a hundred milliseconds behind the attack, which is the one error the whole design
    // cannot absorb. A snap at 30 Hz on something this size reads as fine; being late does not.
    c->pos = pos; c->yaw = yaw; c->hp = hp;
    if ((Anim)anim != c->anim) { c->anim = (Anim)anim; c->anim_t = 0; }
    cb->b.state = (BState)state;
    cb->b.move = move < cb->b.def.nmoves ? move : 0;
    cb->b.c.move_id = cb->b.move;
    c->tell = (float)tell / 255.0f;
    cb->tell_k = c->tell;
    cb->b.phase2 = (flags & 2u) != 0;
    cb->stag_t = (flags & 4u) ? fmaxf(cb->stag_t, 0.1f) : 0.0f;
    cb->stag_acc = (float)stag;
    cb->stag_win = stag > 0 ? 1.0f : 0.0f;
    cb->target = target < NET_MAX_PLAYERS ? target : -1;
    if (shock > 0) {
        if (!cb->shock.live) {   // the ring is new: adopt the host's numbers and grow it locally
            cb->shock.live = true; cb->shock.at = pos;
            cb->shock.r_max = 40.0f; cb->shock.height = 0.5f; cb->shock.speed = 11.0f;
        }
        cb->shock.r = (float)shock * 0.1f;
    } else cb->shock.live = false;
    if (cb->b.def.moves[cb->b.move].kind == BMK_CHARGE) { /* nothing extra: pos and yaw say it all */ }
}

// ---------------------------------------------------------------- one frame of feedback

void boss_hurt_fx(Game *g, int slot, float damage, Vec3 from) {
    // Everybody hears it; only the goon it happened to gets the screen.
    audio_play(SND_HURT, clampf(0.35f + damage / 120.0f, 0.35f, 1.0f), 0.9f + 0.2f * randf01());
    if (slot != g->local) return;
    float k = clampf(damage / 60.0f, 0.15f, 1.0f);
    // The direction the vignette leans is worked out from where it came from, in the eye's own
    // frame, so a hit from behind darkens the bottom of the screen and a hit from the left darkens
    // the left. game.c owns the drawing; this owns the numbers.
    Vec3 rel = v3_sub(from, g->cam.eye);
    Vec3 fw = v3_norm(v3_sub(g->cam.target, g->cam.eye));
    Vec3 rt = v3_norm(v3_cross(fw, v3(0, 1, 0)));
    float len = v3_len(rel);
    if (len > 0.01f) {
        rel = v3_scale(rel, 1.0f / len);
        g->hurt_dir = v3(v3_dot(rel, rt), 0, v3_dot(rel, fw));
    } else g->hurt_dir = v3(0, 0, 1);
    g->hurt_flash = fmaxf(g->hurt_flash, 0.35f + 0.65f * k);
    camera_add_shake(&g->cam, 0.35f + 1.1f * k);
    camera_add_fov_punch(&g->cam, -3.5f * k);     // a pull IN, not a push out: the world closes on you
    g->hits_taken++;
}

bool boss_event_apply(Game *g, const FireEvent *e) {
    CaveBoss *cb = CB(g);
    switch (e->kind) {
    case FE_HURT:
        boss_hurt_fx(g, (int)e->slot, (float)e->pellets, e->from);
        if (cb->active) cb->log.taken += (float)e->pellets;
        return true;

    case FE_BOSS_ROAR: {
        if (e->hit == 201) {   // phase two
            audio_play(SND_ROAR, 1.0f, 0.72f);
            audio_play(SND_STING, 0.7f, 0.8f);
            camera_add_shake(&g->cam, 0.8f);
            uifx_spawn(&g->fx, UIFX_BANNER, 0, 220, "IT GETS UP", v4(1.0f, 0.4f, 0.25f, 1), 1.5f, 2.0f);
            particles_burst(&g->particles, PT_EMBER, e->from, v3(0, 1, 0), 40, 6.0f, v3(2.4f, 0.7f, 0.2f), 0.10f, 1.2f);
            return true;
        }
        if (e->hit == 200) {   // ran into a wall
            audio_play(SND_THUD, 1.0f, 0.55f);
            camera_add_shake(&g->cam, 0.7f);
            particles_burst(&g->particles, PT_SMOKE, e->from, v3(0, 0.6f, 0), 26, 4.0f, v3(0.55f, 0.5f, 0.46f), 0.35f, 1.4f);
            return true;
        }
        // An ordinary windup. The roar is pitched by the kind of move so the four are told apart
        // by ear as well as by colour, which is what a player looking the other way has.
        static const float PITCH[BMK_KIND_COUNT] = { 1.05f, 0.78f, 0.62f, 1.25f };
        int kind = e->hit < BMK_KIND_COUNT ? e->hit : 0;
        audio_play(SND_ROAR, 0.85f, PITCH[kind]);
        if (kind == BMK_CHARGE || kind == BMK_SLAM) camera_add_shake(&g->cam, 0.22f);
        return true;
    }

    case FE_BOSS_HIT: {
        if (!cb->active) return true;
        float dmg = (float)e->pellets;
        cb->log.dealt += dmg; cb->log.hits++;
        cb->b.c.flash = 1.0f;
        audio_play(SND_HIT, clampf(0.45f + dmg / 90.0f, 0.45f, 1.0f), 1.25f - clampf(dmg / 160.0f, 0, 0.45f));
        // Sparks and dust off armour. There is no blood in this game and there is not going to be.
        particles_burst(&g->particles, PT_SPARK, e->from, v3(0, 0.4f, 0), 9 + (int)clampf(dmg / 6.0f, 0, 14), 5.5f,
                        v3(2.2f, 1.5f, 0.7f), 0.045f, 0.28f);
        particles_burst(&g->particles, PT_SMOKE, e->from, v3(0, 0.5f, 0), 3, 1.6f, v3(0.6f, 0.55f, 0.5f), 0.13f, 0.5f);
        // The number floats from the point of impact, in the world, so four players' numbers do
        // not stack up in one corner of one screen.
        float sx, sy;
        Mat4 vp = camera_view_proj(&g->cam, (float)INTERNAL_W / (float)INTERNAL_H);
        if (uifx_project(vp, e->from, &sx, &sy)) {
            char txt[16]; snprintf(txt, sizeof txt, "%d", (int)(dmg + 0.5f));
            bool big = e->hit == 1 || dmg >= 40.0f;
            uifx_spawn(&g->fx, UIFX_DAMAGE, sx, sy, txt,
                       big ? v4(1.6f, 1.35f, 0.45f, 1) : v4(1.5f, 1.25f, 0.85f, 1),
                       big ? 2.2f : 1.5f, big ? 1.1f : 0.8f);
        }
        return true;
    }

    case FE_BOSS_STAGGER:
        if (cb->active) cb->stag_t = fmaxf(cb->stag_t, cb->b.def.stagger_time);
        audio_play(SND_STAGGER, 1.0f, 0.85f);
        camera_add_shake(&g->cam, 0.75f);
        g->hitstop = fmaxf(g->hitstop, 0.09f);
        particles_burst(&g->particles, PT_SPARK, e->from, v3(0, 1, 0), 40, 7.0f, v3(2.6f, 2.2f, 0.9f), 0.06f, 0.6f);
        uifx_spawn(&g->fx, UIFX_PARRY, (float)INTERNAL_W * 0.5f, 250, "STAGGER", v4(1.0f, 0.92f, 0.45f, 1), 2.0f, 1.1f);
        return true;

    case FE_BOSS_DIE: {
        audio_play(SND_DEATH, 1.0f, 0.8f);
        audio_play(SND_BOOM, 1.0f, 0.5f);
        camera_add_shake(&g->cam, 1.4f);
        g->hitstop = fmaxf(g->hitstop, 0.16f);
        // A column of dust, then a low ring of it rolling outward: it is a big thing falling over,
        // and nothing about it is red.
        particles_burst(&g->particles, PT_SMOKE, e->from, v3(0, 1, 0), 90, 7.5f, v3(0.62f, 0.58f, 0.52f), 0.55f, 2.6f);
        particles_burst(&g->particles, PT_SMOKE, e->from, v3(0, 0.1f, 0), 70, 11.0f, v3(0.55f, 0.51f, 0.46f), 0.42f, 2.2f);
        particles_burst(&g->particles, PT_EMBER, e->from, v3(0, 1, 0), 60, 9.0f, v3(2.2f, 1.2f, 0.4f), 0.09f, 1.8f);
        if (cb->active) {
            cb->b.state = BS_DEAD; cb->b.c.hp = 0; cb->death_t = 0;
            cb->log.ended = g->time;   // on a client too, so its closing report times the fight
            character_set_anim(&cb->b.c, ANIM_DEAD);
        }
        // Every machine creates the loot at the same id off the same event; see items_spawn_named.
        if (!cb->loot_dropped) {
            cb->loot_dropped = true;
            Vec3 at = v3_add(e->from, v3(0, 1.2f, 0));
            if (items_spawn_named(g, "relic", at, ITEM_BOSS_LOOT_ID) < 0)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "boss: nothing to drop -- assets/items/relic.txt did not load");
            else dbg_log("boss: dropped the relic at %.1f %.1f %.1f", (double)at.x, (double)at.y, (double)at.z);
        }
        uifx_spawn(&g->fx, UIFX_BANNER, 0, 200, "IT STOPS MOVING", v4(0.95f, 0.9f, 0.8f, 1), 1.6f, 3.0f);
        audio_music_stop(2.0f);
        return true;
    }

    default: return false;   // not ours; weapons_event_apply carries on with its own kinds
    }
}

// ---------------------------------------------------------------- the harness

// A capture that did not happen has to say so. main.c writes the --screenshot file at the end of
// the run whether or not --shot-when ever matched, so a run that never saw its moment still leaves
// a PNG behind -- of the last frame, which for this fight is a dead boss and a faded HUD. One line
// in the log is the difference between verifying something and looking at the wrong picture.
static bool moment_hit(const char *when) {
    SDL_Log("shot: the moment '%s' is on screen", when);
    return true;
}

bool boss_shot_moment(const Game *g, const char *when) {
    const CaveBoss *cb = &g->cave;
    if (!cb->active) return false;
    // Far enough away that the frame contains the thing being captured. A telegraph is drawn on
    // the floor around a body four metres tall; from two metres away the body is the whole screen
    // and the decal is behind the camera. This is a capture rule, not a gameplay one.
    float dist = v3_len(v3_sub(cb->b.c.pos, g->players[g->local].c.pos));
    // Lying on the floor with the horizon rolled seventy degrees is a fine thing to photograph and
    // a useless way to photograph anything else, and a telegraph behind the camera is not a
    // telegraph. Both apply to every moment except `hurt`, which is about the screen and not the
    // world, and `death`, which is worth having even from the floor.
    Vec3 to_boss = v3_sub(cb->b.c.pos, g->cam.eye);
    Vec3 fwd_cam = v3_norm(v3_sub(g->cam.target, g->cam.eye));
    Vec3 chest = v3(cb->b.c.pos.x, cb->b.c.pos.y + cb->b.c.height * 0.5f, cb->b.c.pos.z);
    bool in_view = v3_len(to_boss) > 0.01f && v3_dot(v3_norm(to_boss), fwd_cam) > 0.45f
                // and not behind a pillar. The arena is full of cover, which is the point of it,
                // and a telegraph photographed through six metres of rock is not a telegraph.
                && level_ray_solid(&g->level, g->cam.eye, chest, 0.0f) >= 0.999f;
    bool upright = !weapons_is_down(g, g->local);
    if (strcmp(when, "hurt") != 0 && (!in_view || !upright)) return false;
    if (!strncmp(when, "tell:", 5)) {
        if (cb->b.state != BS_WINDUP || cb->b.move >= cb->b.def.nmoves) return false;
        // How far away the frame has to be taken from, per move, so that the thing being
        // photographed is in it. A sweep only happens inside six and a half metres and a slam's
        // ring is six metres across -- standing inside that ring is the correct picture of it --
        // while a charge lane and a volley only make sense from down the room.
        int kind = cb->b.def.moves[cb->b.move].kind;
        float floor_m = kind == BMK_SWEEP ? 4.0f : kind == BMK_SLAM ? 4.0f : 7.0f;
        if (dist < floor_m) return false;
        // Two thirds through: the decal is bright, the colour has filled, and the move has not
        // started yet -- which is the frame that has to be legible.
        if (cb->tell_k < 0.55f || cb->tell_k > 0.96f) return false;
        if (strcmp(when + 5, boss_move_kind_name(cb->b.def.moves[cb->b.move].kind)) != 0) return false;
        return moment_hit(when);
    }
    bool ok = false;
    if (!strcmp(when, "shock"))        ok = cb->shock.live && cb->shock.r > 1.5f && cb->shock.r < cb->shock.r_max * 0.8f && dist > 4.0f;
    else if (!strcmp(when, "hurt"))    ok = g->hurt_flash > 0.45f;
    else if (!strcmp(when, "stagger")) ok = cb->stag_t > 0.25f && cb->stag_t < 1.35f && dist > 4.0f;
    else if (!strcmp(when, "death"))   ok = cb->b.state == BS_DEAD && cb->death_t > 0.75f && cb->death_t < 1.7f && dist > 2.5f;
    else if (!strcmp(when, "arena"))   ok = cb->b.state != BS_DEAD && g->tick > 240 && dist > 10.0f;
    return ok ? moment_hit(when) : false;
}

// ---------------------------------------------------------------- the summary

void boss_report(const Game *g) {
    const CaveBoss *cb = &g->cave;
    if (!cb->active) return;
    const BossLog *l = &cb->log;
    double secs = (l->ended > l->started ? l->ended : g->time) - l->started;
    SDL_Log("boss: %s -- %.0f hp left of %.0f after %.0f s", cb->b.def.name, (double)cb->b.c.hp, (double)cb->b.c.hp_max, secs);
    SDL_Log("boss: dealt %.0f, taken %.0f, %u hits, %u stagger(s), %u knockdown(s)",
            (double)l->dealt, (double)l->taken, l->hits, l->staggers, l->downs);
    SDL_Log("boss: moves -- sweep %u  charge %u  slam %u  volley %u",
            l->moves[BMK_SWEEP], l->moves[BMK_CHARGE], l->moves[BMK_SLAM], l->moves[BMK_VOLLEY]);
    dbg_log("boss: %s %.0f/%.0f hp, %.0f dealt, %.0f taken, %u staggers, %u downs, %.1f s",
            cb->b.def.name, (double)cb->b.c.hp, (double)cb->b.c.hp_max, (double)l->dealt, (double)l->taken,
            l->staggers, l->downs, secs);
}
