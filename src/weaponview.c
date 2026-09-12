// Weapon drawing: the first-person viewmodel, every goon's held-or-holstered weapon out in the
// world, tracers, muzzle flashes and the small HUD that goes with all of it. weapons.c owns the
// rules (equip, fire, hit, fall over); this file only ever turns state that already exists into
// pixels and sound. See weapons.h for the shape of things and weapons.c for the authority model.
#include "game.h"
#include "audio.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ---------------------------------------------------------------- tuning

#define VM_TRACER_LIFE      0.07f   // a tracer is a hint, not a laser beam
#define VM_FLASH_LIFE       0.06f   // matches the "max is 0.06 s" weapons_lights assumes
#define VM_SWAY_MAX          0.05f  // metres, both axes
#define VM_SWAY_YAW_GAIN      1.4f
#define VM_SWAY_PITCH_GAIN    1.0f
#define VM_SWAY_LAG           0.35f // the old per-call blend, kept only as the value VM_SWAY_RATE below
                                     // is matched against; see the comment in weapons_draw
// weapons_draw used to run once a tick (60 Hz); now it runs once a rendered frame, up to 240 Hz, so a
// per-call blend of VM_SWAY_LAG would smooth four times as hard. VM_SWAY_RATE is the same lag turned
// into a frame-rate-independent exponential rate, chosen so 1 - expf(-VM_SWAY_RATE * dt) equals
// VM_SWAY_LAG at a 60 Hz frame: VM_SWAY_RATE = -60 * logf(1 - VM_SWAY_LAG) = -60 * logf(0.65) = 25.847.
#define VM_SWAY_RATE          25.847f
#define VM_SWAY_ROLL_GAIN    60.0f  // metres of sway -> degrees of roll
#define VM_SWAY_ROLL_MAX       3.0f
#define VM_ARM_THICK          0.075f

// --- traversal --- momentum sway: the gun is a mass on the end of an arm, not welded to the eye,
// so it lags a run, drifts with a strafe, sinks a little at speed, and floats or drops with the
// feet leaving and finding the ground. Driven by Player.c.hvel/vy, never by camera motion (that is
// the mouse sway above -- a different signal, already smoothed by VM_SWAY_RATE).
#define VM_RUN_LAG    0.010f  // metres the gun lags back, per m/s of forward ground speed
#define VM_RUN_SIDE   0.006f  // metres of lateral drift, per m/s of sideways ground speed
#define VM_RUN_DROP   0.004f  // metres the gun sinks, per m/s of total ground speed
#define VM_RUN_ROLL   0.9f    // degrees of roll, per m/s of sideways ground speed
#define VM_RUN_RATE  12.0f    // frame-rate-independent smoothing rate shared by all of the above
#define VM_AIR_LIFT   0.006f  // metres the gun floats (jump) or sinks (fall), per m/s of vy, while airborne

// The grip: how a weapon model sits in a hand, and the same numbers serve the viewmodel and the
// bone attachment. HOLLOW_GRIP="x y z yaw pitch roll scale" overrides it for tuning, and
// HOLLOW_GRIP_<NAME> (NAME = the item's file name, upper-cased: PISTOL, SHOTGUN, BAT, WRENCH...)
// overrides it for one weapon only, so four guns can be tuned without four rebuilds.
static void grip_xform(const ItemDef *d, Vec3 *pos, float *yaw, float *pitch, float *roll, float *scale) {
    // The item file's own `grip` line is the answer; the environment overrides exist so the
    // framing can be found by eye without a rebuild, and then written back into the file.
    *pos = d ? d->grip : v3(0, 0, 0);
    *yaw = d ? d->grip_yaw : 0; *pitch = d ? d->grip_pitch : 0; *roll = d ? d->grip_roll : 0;
    *scale = d ? d->scale : 1.0f;
    const char *env = SDL_getenv("HOLLOW_GRIP");
    if (env) sscanf(env, "%f %f %f %f %f %f %f", &pos->x, &pos->y, &pos->z, yaw, pitch, roll, scale);
    if (d && d->name[0]) {
        char key[48] = "HOLLOW_GRIP_";
        size_t kl = strlen(key);
        for (int i = 0; d->name[i] && kl < sizeof key - 1; i++) key[kl++] = (char)toupper((unsigned char)d->name[i]);
        key[kl] = 0;
        const char *env2 = SDL_getenv(key);
        if (env2) sscanf(env2, "%f %f %f %f %f %f %f", &pos->x, &pos->y, &pos->z, yaw, pitch, roll, scale);
    }
}

// The grip numbers turned into a matrix: scale, then roll, pitch, yaw, then the offset. Local +Z
// is a model's own forward, matching charmodel's yaw-0-faces-+Z convention.
static Mat4 grip_matrix(Vec3 pos, float yaw_deg, float pitch_deg, float roll_deg, float scale) {
    Mat4 m = m4_translate(pos);
    m = m4_mul(m, m4_rotate_y(yaw_deg * DEG2RAD));
    m = m4_mul(m, m4_rotate_x(pitch_deg * DEG2RAD));
    m = m4_mul(m, m4_rotate_z(roll_deg * DEG2RAD));
    m = m4_mul(m, m4_scale(v3(scale, scale, scale)));
    return m;
}

// A world matrix built directly from three axes rather than an angle: used for the camera-facing
// viewmodel frame, the from->to arms and tracers, and the holster's fixed lay-flat placement.
static Mat4 basis_matrix(Vec3 pos, Vec3 x, Vec3 y, Vec3 z, Vec3 scale) {
    Mat4 m = m4_identity();
    m.m[0] = x.x * scale.x; m.m[1] = x.y * scale.x; m.m[2] = x.z * scale.x;
    m.m[4] = y.x * scale.y; m.m[5] = y.y * scale.y; m.m[6] = y.z * scale.y;
    m.m[8] = z.x * scale.z; m.m[9] = z.y * scale.z; m.m[10] = z.z * scale.z;
    m.m[12] = pos.x; m.m[13] = pos.y; m.m[14] = pos.z;
    return m;
}

// A unit cube stretched and turned to run from `from` to `to`, `thick` metres square in cross
// section. Used for the blocky viewmodel arms and for tracers.
static Mat4 limb_matrix(Vec3 from, Vec3 to, float thick) {
    Vec3 d = v3_sub(to, from);
    float len = v3_len(d);
    Vec3 z = len > 1e-5f ? v3_scale(d, 1.0f / len) : v3(0, 0, 1);
    Vec3 upref = fabsf(z.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
    Vec3 x = v3_norm(v3_cross(upref, z));
    Vec3 y = v3_cross(z, x);
    Vec3 mid = v3_scale(v3_add(from, to), 0.5f);
    return basis_matrix(mid, x, y, z, v3(thick, thick, fmaxf(len, 0.001f)));
}

static void char_axes(float yaw, Vec3 *fwd, Vec3 *right) {
    *fwd = v3(sinf(yaw), 0, cosf(yaw));
    *right = v3(fwd->z, 0, -fwd->x);
}

// weapons.c's own item lookup (wdef) is static; this is the same handful of bounds checks, kept
// here rather than exported for one more function.
static const ItemDef *weap_def(const Game *g, int item) {
    const Items *its = &g->items;
    if (item < 0 || item >= its->n || !its->it[item].used) return NULL;
    const ItemDef *d = item_def(its, &its->it[item]);
    return d->ok ? d : NULL;
}

// Shotgun and bat read as two hands on the grip; a pistol and a wrench read as one.
static bool two_handed_look(const ItemDef *d) {
    return d && (!strcmp(d->name, "shotgun") || !strcmp(d->name, "bat"));
}

static float atten(const Game *g, Vec3 at, float gain) {
    float dist = v3_len(v3_sub(at, g->cam.eye));
    return gain * clampf(1.0f - dist / 25.0f, 0.08f, 1.0f);
}

// ---------------------------------------------------------------- tracers and flashes

static void push_tracer(Weapons *ws, Vec3 a, Vec3 b) {
    if (ws->ntracer >= WEAP_TRACERS) return;   // dropped, not queued: no allocations, and the next
                                                // one is a fourteenth of a second away anyway
    int i = ws->ntracer++;
    ws->tracer[i].a = a; ws->tracer[i].b = b;
    ws->tracer[i].life = VM_TRACER_LIFE; ws->tracer[i].max_life = VM_TRACER_LIFE;
}

static void push_flash(Weapons *ws, Vec3 at) {
    if (ws->nflash >= WEAP_FLASHES) return;
    int i = ws->nflash++;
    ws->flash[i].at = at; ws->flash[i].life = VM_FLASH_LIFE;
}

static void draw_tracers(Gfx *x, const Weapons *ws) {
    for (int i = 0; i < ws->ntracer; i++) {
        float k = ws->tracer[i].max_life > 0 ? clampf(ws->tracer[i].life / ws->tracer[i].max_life, 0, 1) : 0;
        if (k <= 0) continue;
        Mat4 m = limb_matrix(ws->tracer[i].a, ws->tracer[i].b, 0.02f);
        Material mat = material_default();
        mat.unlit = 1.0f;
        mat.emissive = v3_scale(v3(1.0f, 0.85f, 0.5f), k * 3.0f);
        gfx_set_material(x, &mat);
        gfx_draw(x, &x->cube, &x->white, m, v4(1, 1, 1, k), v4(1, 1, 0, 0));
        gfx_set_material(x, NULL);
    }
}

static void draw_flashes(Gfx *x, const Weapons *ws) {
    for (int i = 0; i < ws->nflash; i++) {
        float k = clampf(ws->flash[i].life / VM_FLASH_LIFE, 0, 1);
        if (k <= 0) continue;
        gfx_billboard(x, ws->flash[i].at, 0.28f * (0.7f + 0.3f * k), v4(1.0f, 0.85f, 0.55f, k), true);
    }
}

// ---------------------------------------------------------------- the local viewmodel

// --- traversal --- 0 outside a mantle or a vault; otherwise a curve that rises fast, holds near
// its peak, and eases back to 0 with zero slope at both ends (k = 0 entering, k = 1 leaving), so
// the hand-plant below never pops into the move or snaps out of it.
static float mantle_reach(const Player *p) {
    if (p->trav != TM_MANTLE && p->trav != TM_VAULT) return 0.0f;
    float k = clampf(p->trav_t / fmaxf(p->trav_dur, 0.0001f), 0.0f, 1.0f);
    return sinf(clampf(k / 0.45f, 0.0f, 1.0f) * PI * 0.5f) *
           (1.0f - smoothstep(clampf((k - 0.55f) / 0.45f, 0.0f, 1.0f)));
}

static void draw_viewmodel(Game *g) {
    Gfx *x = &g->gfx;
    Weapons *ws = &g->weapons;
    int slot = g->local;
    Weapon *w = &ws->w[slot];
    const ItemDef *d = weap_def(g, w->item);
    if (!d || !d->model[0]) return;

    Vec3 eye = g->cam.eye;
    Vec3 fwd = v3_norm(v3_sub(g->cam.target, eye));
    Vec3 right = v3_norm(v3_cross(fwd, v3(0, 1, 0)));
    if (v3_len(right) < 1e-3f) right = v3(1, 0, 0);
    Vec3 up = v3_cross(right, fwd);

    Vec3 pos = v3_add(eye, v3_add(v3_scale(fwd, 0.45f), v3_add(v3_scale(right, 0.20f), v3_scale(up, -0.18f))));

    // Bob: the camera's own render-rate bob phase and gain, not the character's walk_phase (which
    // only advances at the 60 Hz sim tick and, driven at 4x for the vertical figure-of-eight, was a
    // ~10 Hz vibration at a jog). Riding the same phase as the head means the gun and the eye bob to
    // one beat instead of two, and it is deliberately a little larger than the eye's own bob -- that
    // is what sells motion in first person without shaking the horizon.
    float ph = camera_bob_phase(&g->cam), gain = camera_bob_gain(&g->cam);
    pos = v3_add(pos, v3_scale(right, sinf(ph) * 0.022f * gain));          // one sway a stride
    pos = v3_add(pos, v3_scale(up, sinf(ph * 2.0f) * 0.014f * gain));      // one dip a footfall

    Vec3 extra_pos = v3(0, 0, 0);
    float extra_yaw = 0, extra_pitch = 0, extra_roll = 0;

    // Momentum sway: see the VM_RUN_* comment above. right is already flat (cross(fwd, world-up)
    // has no y component by construction), so only fwd needs its own y zeroed and renormalised.
    const Player *p = &g->players[slot];
    Vec3 fwd_flat = v3_norm(v3(fwd.x, 0, fwd.z));
    float vf = v3_dot(p->c.hvel, fwd_flat);   // + is running forward
    float vl = v3_dot(p->c.hvel, right);      // + is strafing right
    float hspd = hypotf(p->c.hvel.x, p->c.hvel.z);
    float dt = fmaxf(game_frame_dt(g), 0.0f);
    float rk = 1.0f - expf(-VM_RUN_RATE * dt);   // 0 when dt <= 0, so a paused frame leaves state untouched
    static float s_run_lag = 0.0f, s_run_side = 0.0f, s_run_drop = 0.0f, s_run_roll = 0.0f, s_air_lift = 0.0f;
    // -0.075..0.075 rather than a one-sided clamp: backpedalling flips the sign of vf and the lag
    // should flip with it, not pin against a wall the forward case never touches.
    s_run_lag  = lerpf(s_run_lag,  clampf(-VM_RUN_LAG  * vf, -0.075f, 0.075f), rk);
    s_run_side = lerpf(s_run_side, clampf(-VM_RUN_SIDE * vl, -0.05f, 0.05f), rk);
    s_run_drop = lerpf(s_run_drop, clampf(-VM_RUN_DROP * hspd, -0.03f, 0.0f), rk);
    s_run_roll = lerpf(s_run_roll, clampf(VM_RUN_ROLL * vl, -4.0f, 4.0f), rk);
    // Air lift snaps its target to 0 the instant the feet are down, so landing eases the gun back
    // to rest at the same smoothed rate it floated up on the way into the jump.
    float lift_target = p->c.grounded ? 0.0f : clampf(p->c.vy * VM_AIR_LIFT, -0.04f, 0.04f);
    s_air_lift = lerpf(s_air_lift, lift_target, rk);
    extra_pos = v3_add(extra_pos, v3_scale(fwd, s_run_lag));
    extra_pos = v3_add(extra_pos, v3_scale(right, s_run_side));
    extra_pos = v3_add(extra_pos, v3_scale(up, s_run_drop + s_air_lift));
    extra_roll += s_run_roll;

    // Hand-plant: a mantle or vault borrows the gun hand to slap the ledge, so the weapon dips out
    // of frame and rolls with it instead of floating in place while the arms do something else.
    // Unarmed, draw_mantle_hand below covers the same beat instead of this offset.
    float reach = mantle_reach(p);
    if (reach > 0.0f) {
        extra_pos = v3_add(extra_pos, v3_scale(up, -0.30f * reach));
        extra_pos = v3_add(extra_pos, v3_scale(right, 0.12f * reach));
        extra_roll += 35.0f * reach;
    }

    // Recoil: w->kick is already decayed for us. m4_rotate_x's handedness tips +Z toward -Y for a
    // positive angle, so a negative pitch here is what raises the muzzle.
    extra_pos = v3_add(extra_pos, v3_scale(fwd, -w->kick * 0.10f));
    extra_pos = v3_add(extra_pos, v3_scale(up, -w->kick * 0.02f));
    extra_pitch += -w->kick * 9.0f;

    // Melee swing: wind-up, contact, follow-through, driven by code rather than a linear sweep.
    // weapons.c's own contact frame lands at s == 0.45 (WEAP_SWING_TIME * 0.55 left to go), which
    // is why the first leg of the curve runs to s == 0.6 rather than ending exactly on contact --
    // the swing keeps snapping through for a beat after the hit lands.
    if (w->swing > 0) {
        float s = clampf(1.0f - w->swing / WEAP_SWING_TIME, 0, 1);
        float sy, sp;
        if (s < 0.6f) {
            float t = smoothstep(s / 0.6f);
            sy = lerpf(40.0f, -50.0f, t);
            sp = lerpf(-25.0f, 35.0f, t);
        } else {
            float t = smoothstep((s - 0.6f) / 0.4f);
            sy = lerpf(-50.0f, -10.0f, t);
            sp = lerpf(35.0f, 5.0f, t);
        }
        extra_yaw += sy; extra_pitch += sp;
        extra_pos = v3_add(extra_pos, v3_scale(fwd, sinf(s * PI) * 0.18f));
    }

    // Sway: ws->sway_x/sway_y were updated for this frame in weapons_draw, since that is where the
    // camera's yaw/pitch delta gets measured.
    extra_pos = v3_add(extra_pos, v3_scale(right, ws->sway_x));
    extra_pos = v3_add(extra_pos, v3_scale(up, ws->sway_y));
    extra_roll += clampf(ws->sway_x * VM_SWAY_ROLL_GAIN, -VM_SWAY_ROLL_MAX, VM_SWAY_ROLL_MAX);

    // Draw / holster: dips out of frame and rolls back in.
    if (ws->swap_t > 0) {
        float k = ws->swap_t / 0.25f;
        extra_pos = v3_add(extra_pos, v3_scale(up, -k * 0.35f));
        extra_roll += k * 55.0f;
    }

    Vec3 pos_total = v3_add(pos, extra_pos);
    Mat4 frame = basis_matrix(pos_total, right, up, fwd, v3(1, 1, 1));
    frame = m4_mul(frame, m4_mul(m4_rotate_y(extra_yaw * DEG2RAD), m4_mul(m4_rotate_x(extra_pitch * DEG2RAD), m4_rotate_z(extra_roll * DEG2RAD))));

    Vec3 gpos; float gyaw, gpitch, groll, gscale;
    grip_xform(d, &gpos, &gyaw, &gpitch, &groll, &gscale);
    Mat4 world = m4_mul(frame, grip_matrix(gpos, gyaw, gpitch, groll, gscale));
    props_draw_matrix(&g->gfx, &g->props, d->model, world, d->tint, v3(0, 0, 0), NULL, 0, 0);

    // Arms: two blocky, untextured limbs in the goon's slot colour, off the bottom of the frame.
    Vec4 tint = g->net.slots[slot].tint;
    Vec3 rfrom = v3_add(eye, v3_add(v3_scale(fwd, 0.10f), v3_add(v3_scale(right, 0.32f), v3_scale(up, -0.55f))));
    Mat4 rm = limb_matrix(rfrom, pos_total, VM_ARM_THICK);
    gfx_set_material(x, NULL);
    gfx_draw(x, &x->cube, &x->white, rm, tint, v4(1, 1, 0, 0));
    if (two_handed_look(d)) {
        Vec3 lfrom = v3_add(eye, v3_add(v3_scale(fwd, 0.10f), v3_add(v3_scale(right, -0.10f), v3_scale(up, -0.55f))));
        Vec3 fore = v3_add(pos_total, v3_scale(fwd, 0.12f));
        Mat4 lm = limb_matrix(lfrom, fore, VM_ARM_THICK);
        gfx_draw(x, &x->cube, &x->white, lm, tint, v4(1, 1, 0, 0));
    }
}

// --- traversal --- Unarmed, there is no weapon and no arms below to carry the beat of a mantle or
// a vault, so a bare hand stands in: one forearm swinging up and forward to slap the ledge and
// back again. Same limb_matrix + untextured-cube draw the arms above use, so it reads as the same
// body. `reach` is mantle_reach(p) from the caller; the caller has already checked it is > 0.
static void draw_mantle_hand(Game *g, float reach) {
    Gfx *x = &g->gfx;
    int slot = g->local;
    Vec3 eye = g->cam.eye;
    Vec3 fwd = v3_norm(v3_sub(g->cam.target, eye));
    Vec3 right = v3_norm(v3_cross(fwd, v3(0, 1, 0)));
    if (v3_len(right) < 1e-3f) right = v3(1, 0, 0);
    Vec3 up = v3_cross(right, fwd);

    // Elbow: the same point the armed right arm starts from. Hand: swings from resting near the
    // hip up and out to the ledge as reach rises, then eases back as the move finishes.
    Vec3 elbow = v3_add(eye, v3_add(v3_scale(fwd, 0.10f), v3_add(v3_scale(right, 0.34f), v3_scale(up, -0.55f))));
    Vec3 hand = v3_add(eye, v3_add(v3_scale(fwd, 0.30f + 0.35f * reach),
                        v3_add(v3_scale(right, 0.26f - 0.06f * reach), v3_scale(up, -0.42f + 0.62f * reach))));
    Mat4 hm = limb_matrix(elbow, hand, VM_ARM_THICK);
    gfx_set_material(x, NULL);
    gfx_draw(x, &x->cube, &x->white, hm, g->net.slots[slot].tint, v4(1, 1, 0, 0));
}

// ---------------------------------------------------------------- remote goons

static void draw_remote_weapon(Game *g, int slot) {
    const Weapon *w = &g->weapons.w[slot];
    if (w->item < 0) return;
    const ItemDef *d = weap_def(g, w->item);
    if (!d || !d->model[0]) return;
    const CharModel *cm = &g->player_models[slot];
    const Character *c = &g->players[slot].c;

    Vec3 gpos; float gyaw, gpitch, groll, gscale;
    grip_xform(d, &gpos, &gyaw, &gpitch, &groll, &gscale);
    Mat4 grip = grip_matrix(gpos, gyaw, gpitch, groll, gscale);

    Mat4 bone, world;
    if (w->drawn) {
        if (charmodel_bone_world(cm, c, "hand_r", &bone)) {
            world = m4_mul(bone, grip);
        } else {
            // No bone (a sprite, or the model has not loaded yet): the same hand point weapons.c
            // itself falls back to, oriented by the character's own facing.
            Vec3 fwd, right;
            char_axes(c->yaw, &fwd, &right);
            Mat4 frame = basis_matrix(weapons_hand_point(g, slot), right, v3(0, 1, 0), fwd, v3(1, 1, 1));
            world = m4_mul(frame, grip);
        }
        props_draw_matrix(&g->gfx, &g->props, d->model, world, d->tint, v3(0, 0, 0), NULL, 0, 0);
        return;
    }

    // Holstered: on the spine, pushed back and laid flat across it. No bone, no fallback -- a
    // weapon nobody can see the back of is not worth inventing a floating one for.
    if (!charmodel_bone_world(cm, c, "spine_03", &bone)) return;
    Vec3 spine = v3(bone.m[12], bone.m[13], bone.m[14]);
    Vec3 fwd, right;
    char_axes(c->yaw, &fwd, &right);
    Vec3 hp = v3_sub(spine, v3_scale(fwd, 0.14f));
    Mat4 frame = basis_matrix(hp, right, v3(0, 1, 0), v3_scale(fwd, -1), v3(1, 1, 1));
    world = m4_mul(frame, m4_mul(m4_rotate_x(90.0f * DEG2RAD), grip));
    props_draw_matrix(&g->gfx, &g->props, d->model, world, d->tint, v3(0, 0, 0), NULL, 0, 0);
}

// ---------------------------------------------------------------- world pass

void weapons_draw(Game *g) {
    // A viewmodel casting a shadow of a giant floating gun onto the ground is exactly the bug this
    // one early return avoids; same for a tracer or a flash baked into the sun's depth map.
    if (g->gfx.in_shadow) return;

    // Sway is derived here rather than passed in, because weapons_draw sees no Input: measure how
    // far the camera turned since the last time we were called and lag the weapon behind it.
    static float s_prev_cam_yaw = 0.0f, s_prev_cam_pitch = 0.0f;
    float dyaw = angle_wrap(g->cam.yaw - s_prev_cam_yaw);
    float dpitch = g->cam.pitch - s_prev_cam_pitch;
    s_prev_cam_yaw = g->cam.yaw; s_prev_cam_pitch = g->cam.pitch;
    Weapons *ws = &g->weapons;
    // This function is now called once a rendered frame rather than once a tick, so dyaw/dpitch are a
    // per-frame delta, not the per-tick one the sway gains were tuned against: a fast turn spread over
    // four 240 Hz frames would otherwise sway four times less per frame than the same turn at 60 Hz.
    // Divide by the frame's dt and rescale to a nominal 60 Hz tick to keep the sway the same size
    // regardless of frame rate; dt <= 0 (a paused frame, the very first one) leaves the sway where it
    // was rather than dividing by zero.
    float dt = fmaxf(game_frame_dt(g), 0.0f);
    float norm = dt > 0.0f ? (1.0f / 60.0f) / dt : 0.0f;
    dyaw *= norm; dpitch *= norm;
    float k = 1.0f - expf(-VM_SWAY_RATE * dt);   // frame-rate-correct blend; see VM_SWAY_RATE above
    ws->sway_x = lerpf(ws->sway_x, clampf(-dyaw * VM_SWAY_YAW_GAIN, -VM_SWAY_MAX, VM_SWAY_MAX), k);
    ws->sway_y = lerpf(ws->sway_y, clampf(-dpitch * VM_SWAY_PITCH_GAIN, -VM_SWAY_MAX, VM_SWAY_MAX), k);

    bool did_viewmodel = false;
    if (g->cam.mode == CAM_FIRST && weapons_drawn(g, g->local)) { draw_viewmodel(g); did_viewmodel = true; }

    // --- traversal --- Unarmed (or between weapons), draw_viewmodel above never ran, so the hand
    // that would otherwise carry the mantle/vault plant (see draw_viewmodel's own hand-plant block)
    // gets drawn on its own here instead. A weapon with a model always wins this over the bare hand.
    if (!did_viewmodel && g->cam.mode == CAM_FIRST) {
        float mreach = mantle_reach(&g->players[g->local]);
        const ItemDef *hd = weap_def(g, ws->w[g->local].item);
        if (mreach > 0.001f && (!hd || !hd->model[0])) draw_mantle_hand(g, mreach);
    }

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g->net.slots[i].active) continue;
        if (i == g->local && did_viewmodel) continue;   // that one was just drawn as the viewmodel
        draw_remote_weapon(g, i);
    }

    draw_tracers(&g->gfx, ws);
    draw_flashes(&g->gfx, ws);
}

int weapons_lights(const Game *g, PointLight *out, int max) {
    const Weapons *ws = &g->weapons;
    int n = 0;
    for (int i = 0; i < ws->nflash && n < max; i++) {
        float life = ws->flash[i].life;
        if (life <= 0) continue;
        out[n].pos = ws->flash[i].at;
        out[n].radius = 4.0f;
        out[n].color = v3(1.0f, 0.85f, 0.55f);
        out[n].intensity = 3.5f * (life / VM_FLASH_LIFE);
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- effects bookkeeping

void weapons_fx_tick(Game *g, float dt) {
    Weapons *ws = &g->weapons;
    int j = 0;
    for (int i = 0; i < ws->ntracer; i++) {
        ws->tracer[i].life -= dt;
        if (ws->tracer[i].life > 0) { if (j != i) ws->tracer[j] = ws->tracer[i]; j++; }
    }
    ws->ntracer = j;
    j = 0;
    for (int i = 0; i < ws->nflash; i++) {
        ws->flash[i].life -= dt;
        if (ws->flash[i].life > 0) { if (j != i) ws->flash[j] = ws->flash[i]; j++; }
    }
    ws->nflash = j;
    ws->swap_t = fmaxf(0.0f, ws->swap_t - dt);
    // prompt_t just tracks "is ws->prompt non-empty right now": refreshed every tick the string is
    // set, so weapons_draw_hud gets a short fade-out for free once weapons.c clears it.
    ws->prompt_t = ws->prompt[0] ? 0.2f : fmaxf(0.0f, ws->prompt_t - dt);
}

// ---------------------------------------------------------------- a shot becomes sound and light

void weapons_event_apply(Game *g, const FireEvent *e) {
    Weapons *ws = &g->weapons;
    int slot = e->slot < NET_MAX_PLAYERS ? (int)e->slot : -1;
    const ItemDef *d = slot >= 0 ? weap_def(g, ws->w[slot].item) : NULL;

    switch (e->kind) {
    case FE_SHOT: {
        push_tracer(ws, e->from, e->to);
        Vec3 dir = v3_sub(e->to, e->from);
        Vec3 muzzle = v3_len(dir) > 1e-4f ? v3_add(e->from, v3_scale(v3_norm(dir), 0.35f)) : e->from;
        push_flash(ws, muzzle);
        particles_burst(&g->particles, PT_SMOKE, muzzle, v3(0, 1, 0), 3, 0.6f, v3(0.6f, 0.58f, 0.55f), 0.10f, 0.35f);
        SoundId snd = (d && d->fire_sound >= 0) ? (SoundId)d->fire_sound : SND_SHOT;
        audio_play(snd, atten(g, e->from, 0.9f), 1.0f);
        if (slot == g->local) camera_add_shake(&g->cam, 0.15f);
        break;
    }
    case FE_SWING:
        audio_play(SND_WHOOSH, atten(g, e->from, 0.7f), 1.0f);
        break;
    case FE_CLICK:
        audio_play(SND_CLICK, atten(g, e->from, 0.3f), 1.0f);
        break;
    case FE_RELOAD:
        audio_play(SND_RELOAD, atten(g, e->from, 0.6f), 1.0f);
        break;
    case FE_DOWN:
        audio_play(SND_THUD, atten(g, e->from, 0.7f), 1.0f);
        // Comedy dust, not blood: grey-brown, no red anywhere near it.
        particles_burst(&g->particles, PT_SMOKE, e->from, v3(0, 1, 0), 6, 0.8f, v3(0.55f, 0.5f, 0.45f), 0.14f, 0.6f);
        break;
    case FE_UP:
        audio_play(SND_GRAB, atten(g, e->from, 0.35f), 1.05f);   // a single soft cue; nothing dramatic
        break;
    default: break;
    }

    switch (e->hit) {
    case FH_WORLD:
        particles_burst(&g->particles, PT_SMOKE, e->to, v3(0, 0, 0), 4, 0.5f, v3(0.5f, 0.45f, 0.4f), 0.10f, 0.4f);
        break;
    case FH_ITEM:
        particles_burst(&g->particles, PT_SPARK, e->to, v3(0, 0, 0), 5, 2.2f, v3(1.0f, 0.85f, 0.5f), 0.05f, 0.2f);
        break;
    case FH_PLAYER:
        // No gore, no red, nothing that reads as injury: a dull sound and a couple of grey puffs.
        audio_play(SND_HIT, atten(g, e->to, 0.7f), 1.0f);
        particles_burst(&g->particles, PT_SMOKE, e->to, v3(0, 0, 0), 2, 0.4f, v3(0.6f, 0.6f, 0.6f), 0.12f, 0.35f);
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- hud

static void text_center(Gfx *g, float cx, float y, float scale, Vec4 c, const char *s) {
    float w = gfx_ui_text_width(scale, s);
    gfx_ui_text(g, cx - w * 0.5f, y, scale, c, s);
}

static void bar(Gfx *g, float x, float y, float w, float h, float k, Vec4 back, Vec4 front) {
    gfx_ui_rect(g, x - 1, y - 1, w + 2, h + 2, v4(0, 0, 0, 0.7f));
    gfx_ui_rect(g, x, y, w, h, back);
    gfx_ui_rect(g, x, y, w * clampf(k, 0, 1), h, front);
}

void weapons_draw_hud(Game *g) {
    Gfx *x = &g->gfx;
    const float W = INTERNAL_W, H = INTERNAL_H;
    const Weapons *ws = &g->weapons;
    int slot = g->local;
    const Weapon *w = &ws->w[slot];
    const Downed *dn = &ws->dn[slot];
    Vec4 white = v4(0.9f, 0.88f, 0.85f, 1), dim = v4(0.6f, 0.58f, 0.55f, 1);

    // Ammo (or a weapon's name), bottom right.
    if (w->item >= 0) {
        const ItemDef *d = weap_def(g, w->item);
        if (d) {
            if (!w->drawn) {
                char s[64]; snprintf(s, sizeof s, "%s   Q", d->display);
                float tw = gfx_ui_text_width(1.1f, s);
                gfx_ui_text(x, W - tw - 24, H - 40, 1.1f, dim, s);
            } else if (d->weapon == 2) {
                char s[32]; snprintf(s, sizeof s, "%d / %d", w->ammo, d->ammo);
                float frac = d->ammo > 0 ? (float)w->ammo / (float)d->ammo : 0.0f;
                Vec4 c = w->ammo <= 0 ? v4(0.85f, 0.2f, 0.15f, 1) : frac < (1.0f / 3.0f) ? v4(0.9f, 0.6f, 0.2f, 1) : white;
                float tw = gfx_ui_text_width(1.8f, s);
                gfx_ui_text(x, W - tw - 24, H - 46, 1.8f, c, s);
            } else {
                float tw = gfx_ui_text_width(1.4f, d->display);
                gfx_ui_text(x, W - tw - 24, H - 42, 1.4f, white, d->display);
            }
        }
    }

    // Wind, bottom left, above the item HUD's carry line, and only while it isn't full.
    if (w->wind < WEAP_WIND - 0.5f) {
        float k = clampf(w->wind / WEAP_WIND, 0, 1);
        Vec4 front = v4(0.75f + 0.2f * (1 - k), 0.5f + 0.3f * k, 0.25f, 1);
        bar(x, 24, H - 108, 180, 5, k, v4(0.15f, 0.12f, 0.08f, 1), front);
    }

    // Down. Never "dead", "killed", "death" or "kill" -- the seconds left and, when a mate is
    // holding E over them, how far along the pick-up is.
    if (weapons_is_down(g, slot)) {
        text_center(x, W * 0.5f, H * 0.5f - 20, 2.2f, v4(0.85f, 0.2f, 0.15f, 1), "DOWN");
        char s[32]; snprintf(s, sizeof s, "%.0fs", fmaxf(0.0f, WEAP_DOWN_TIME - dn->t));
        text_center(x, W * 0.5f, H * 0.5f + 12, 1.2f, dim, s);
        if (dn->reviver >= 0) {
            char r[64];
            snprintf(r, sizeof r, "%s IS PICKING YOU UP   %d%%", g->net.slots[dn->reviver].name, (int)(clampf(dn->revive / WEAP_REVIVE_HOLD, 0, 1) * 100));
            text_center(x, W * 0.5f, H * 0.5f + 34, 1.0f, v4(0.6f, 0.9f, 0.6f, 1), r);
        }
    }

    // Pick-up / revive prompt, same place and style as game.c's own "E   talk to %s".
    if (ws->prompt[0] && ws->prompt_t > 0) {
        float a = clampf(ws->prompt_t / 0.2f, 0, 1);
        text_center(x, W * 0.5f, H - 70, 1.3f, v4(1, 0.9f, 0.6f, a), ws->prompt);
    }

    // Crosshair: a four-tick reticle that opens up with recoil, only while a weapon is drawn in
    // first person.
    if (g->cam.mode == CAM_FIRST && weapons_drawn(g, slot)) {
        float cx = W * 0.5f, cy = H * 0.5f;
        float gap = 6.0f + w->kick * 10.0f, len = 6.0f, th = 2.0f;
        Vec4 c = v4(0.9f, 0.92f, 0.9f, 0.85f);
        gfx_ui_rect(x, cx - gap - len, cy - th * 0.5f, len, th, c);
        gfx_ui_rect(x, cx + gap, cy - th * 0.5f, len, th, c);
        gfx_ui_rect(x, cx - th * 0.5f, cy - gap - len, th, len, c);
        gfx_ui_rect(x, cx - th * 0.5f, cy + gap, th, len, c);
    }
}
