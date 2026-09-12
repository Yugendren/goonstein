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
#include <stdlib.h>
#include "debug.h"

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

static void push_flash(Weapons *ws, Vec3 at, bool world_vis) {
    if (ws->nflash >= WEAP_FLASHES) return;
    int i = ws->nflash++;
    ws->flash[i].at = at; ws->flash[i].life = VM_FLASH_LIFE; ws->flash[i].world_vis = world_vis;
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
        if (k <= 0 || !ws->flash[i].world_vis) continue;
        gfx_billboard(x, ws->flash[i].at, 0.28f * (0.7f + 0.3f * k), v4(1.0f, 0.85f, 0.55f, k), true);
    }
}

// ---------------------------------------------------------------- the viewmodel's own lens

// The world's first-person lens is 70 degrees and opens to 78 at a sprint. A gun held at arm's
// length through that lens is a plank: the barrel stretches away toward the edge of frame, the
// grip swells, and the whole thing swims every time the player starts running. Every shooter since
// Quake has drawn the viewmodel through a narrower lens of its own -- Half-Life 2 and Titanfall
// expose it as a setting, Doom 2016 bakes it in -- and somewhere around 55 to 60 degrees is where
// a gun stops looking like a caricature of one. The world keeps its own field of view, and the
// sprint still opens it, which is the entire point: speed is sold by the world, not by the gun.
#define VM_FOV_DEFAULT 58.0f
#define VM_NEAR         0.02f   // the grip sits 25 cm from the eye; 10 cm of near plane would clip it
#define VM_FAR         12.0f
// The slice of the depth buffer the viewmodel is squeezed into. Nothing in the world can be in
// front of it, so a barrel pressed against a wall stays a barrel instead of vanishing into the
// plaster, and -- unlike clearing the depth buffer -- the world's own depth survives for the sky,
// the particles and everything else drawn after this. See gfx_depth_range.
#define VM_DEPTH        0.12f

// Where the gun sits when nothing is happening to it: down and to the right, with the muzzle
// angled back toward the middle of the screen. Doom and Quake both put it there, and for the same
// reason: the lower right corner is the one part of the frame a player never needs to read.
#define VM_REST_RIGHT   0.145f
#define VM_REST_DOWN    0.175f
#define VM_REST_FWD     0.500f

// Recoil is a spring, not a curve. A shot hands the gun a velocity and the spring brings it home;
// a second shot fired before the first has settled therefore stacks on top of it instead of
// restarting an animation halfway through, which is what makes a fast gun feel fast. omega picks
// the settling time: 34 rad/s is critically damped back to nothing in about 120 ms.
#define VM_KICK_OMEGA  34.0f
#define VM_KICK_BACK    0.055f  // metres straight back, per unit of recoil
#define VM_KICK_UP      0.013f  // and a little up, so the gun rises out of the rest pose
#define VM_KICK_PITCH  10.0f    // degrees of muzzle rise
#define VM_KICK_YAW     2.2f    // degrees sideways, alternating shot to shot so it never drifts
#define VM_KICK_ROLL    3.5f
#define VM_FLASH_VM     0.045f  // seconds the first-person flash is on screen: two frames at 60 Hz

// A sprint takes the gun out of the aim: down, turned across the body, muzzle up. It is the
// clearest "you cannot shoot right now" the game has, and it costs nothing to read.
#define VM_SPRINT_RATE  9.0f
#define VM_SPRINT_DROP  0.085f
#define VM_SPRINT_ROLL 26.0f
#define VM_SPRINT_YAW  17.0f
#define VM_SPRINT_PITCH 13.0f

#define VM_SHELL_LIFE   0.85f   // seconds a spent case is on screen before it is gone
#define VM_SHELL_G      7.5f    // metres per second squared; a touch light, so it arcs readably

// Framing and grip, tunable without a rebuild while the numbers are being found by eye -- the same
// bargain HOLLOW_GRIP strikes for a weapon's own placement.
//   HOLLOW_VM_REST="right down fwd"    where the grip sits, metres from the eye
//   HOLLOW_VM_HAND="yaw pitch roll"    the rotation between the hand bone and the gun's own frame
//   HOLLOW_VM_ARMS="yaw pitch roll"    the arms turned about the grip, to keep the idle hand out
//                                      of the middle of the picture without moving the gun
static void vm_rest(float *r, float *d, float *f) {
    *r = VM_REST_RIGHT; *d = VM_REST_DOWN; *f = VM_REST_FWD;
    const char *e = SDL_getenv("HOLLOW_VM_REST");
    if (e) sscanf(e, "%f %f %f", r, d, f);
}
// A hand bone's axes are whatever the rigger chose; a weapon model's are "+Z is the muzzle". The
// clip already points the pistol somewhere sensible, so this is a correction and not a full
// reorientation -- but it has to exist, because "somewhere sensible for a body standing in a
// field" and "down the middle of a first-person screen" are not the same direction.
// The rotation that takes a hand bone's own axes back to the model's, measured once from the first
// pose the arms ever strike. A rigger's hand bone points wherever the rig wanted it to; a weapon
// model's +Z is its muzzle, by the convention in ASSETS.md. Cancelling the bone's REST rotation
// makes a gun leave the hand pointing the way the body faces -- which, for arms hung off the
// camera, is down the middle of the screen -- while every later frame of the clip still turns it,
// so the flick of a shot and the roll of a reload survive intact. Measuring it beats a table of
// per-rig magic numbers: a new character with a differently-built hand needs nothing added here.
// The arms, turned about the grip point. The gun does not move: only the body hanging off it does.
// Zero by default -- the arms are the trigger arm alone (tools/blender/make_arms.py --side right),
// which is where Doom and Quake left theirs, and it needs no correction. The knob stays because the
// day this cast gets a two-handed rifle clip, the second hand will want nudging out of the middle
// of the picture and nobody should have to rebuild to find the angle.
static void vm_arms_turn(float *yaw, float *pitch, float *roll) {
    *yaw = 0.0f; *pitch = 0.0f; *roll = 0.0f;
    const char *e = SDL_getenv("HOLLOW_VM_ARMS");
    if (e) sscanf(e, "%f %f %f", yaw, pitch, roll);
}

static Mat4 vm_rest_fix(Mat4 hb) {
    Vec3 ax = v3_norm(v3(hb.m[0], hb.m[1], hb.m[2]));
    Vec3 ay = v3(hb.m[4], hb.m[5], hb.m[6]);
    ay = v3_norm(v3_sub(ay, v3_scale(ax, v3_dot(ax, ay))));   // Gram-Schmidt, in case the bone is scaled
    Vec3 az = v3_cross(ax, ay);
    Mat4 inv = m4_identity();   // the transpose of an orthonormal basis is its inverse
    inv.m[0] = ax.x; inv.m[4] = ax.y; inv.m[8]  = ax.z;
    inv.m[1] = ay.x; inv.m[5] = ay.y; inv.m[9]  = ay.z;
    inv.m[2] = az.x; inv.m[6] = az.y; inv.m[10] = az.z;
    return inv;
}

static void vm_hand_fix(float *yaw, float *pitch, float *roll) {
    *yaw = 0; *pitch = 0; *roll = 0;
    const char *e = SDL_getenv("HOLLOW_VM_HAND");
    if (e) sscanf(e, "%f %f %f", yaw, pitch, roll);
}

static float vm_fov(void) {
    const char *e = SDL_getenv("HOLLOW_VM_FOV");
    float f = e ? (float)atof(e) : VM_FOV_DEFAULT;
    return clampf(f, 20.0f, 110.0f);
}

// ---------------------------------------------------------------- hands

// The arms are not two boxes any more. They are the local goon's OWN arms: the same MakeHuman body
// everybody else can see, trimmed to the two limbs by tools/blender/make_arms.py and posed by the
// same Quaternius pistol clips the third-person body uses. Fidelity is not the main prize -- the
// prize is that the gun now rides `hand_r` on this model exactly as it rides `hand_r` on a remote
// goon, so there is one grip to tune instead of two that quietly disagree with each other.
//
// It lives in a file static rather than in Weapons because it is a mesh and a texture belonging to
// whoever is sitting at this keyboard: never replicated, never saved, never reset with the level.
static struct {
    CharModel cm;
    char      from[256];        // the character file it was built from; "" = nothing tried yet
    Vec3      anchor;           // hand_r in the model's own space, taken on the first posed frame
    Mat4      rest_fix;         // and the rotation that cancels that bone's rest orientation
    bool      anchored;
    Character body;             // a goon who does not exist, so the clip player has state to read
} s_arms;

// "models/characters/goon_a.glb" -> "goon_a" -> assets/characters/goon_a_fp.txt. Deriving the
// arms from the model the player is already wearing means a new goon needs no table in here; a
// goon with no _fp file simply gets the old blocky arms and a line in the log.
static CharModel *vm_arms(Game *g, int slot) {
    const CharModel *worn = &g->player_models[slot];
    if (!worn->loaded || worn->is_sprite || !worn->spec.model[0]) return NULL;
    const char *slash = strrchr(worn->spec.model, '/');
    char stem[96]; SDL_strlcpy(stem, slash ? slash + 1 : worn->spec.model, sizeof stem);
    char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';
    char path[256];
    snprintf(path, sizeof path, "%s/characters/%s_fp.txt", HOLLOW_ASSET_DIR, stem);
    if (strcmp(path, s_arms.from) != 0) {
        if (s_arms.cm.loaded) charmodel_destroy(&g->gfx, &s_arms.cm);
        memset(&s_arms, 0, sizeof s_arms);
        SDL_strlcpy(s_arms.from, path, sizeof s_arms.from);
        s_arms.body.height = 1.8f; s_arms.body.anim = ANIM_IDLE;
        if (!charmodel_load(&g->gfx, &s_arms.cm, path))
            dbg_log("viewmodel: no first-person arms at %s, using the blocky ones", path);
    }
    return s_arms.cm.loaded ? &s_arms.cm : NULL;
}

// ---------------------------------------------------------------- recoil, sprint, spent cases

// One axis of a critically damped spring, advanced by dt. Critically damped rather than merely
// damped because a gun that overshoots its rest position wobbles, and a wobbling gun reads as
// rubber. x and v are the caller's; nothing here allocates or remembers.
static void vm_spring(float *x, float *v, float omega, float dt) {
    if (dt <= 0.0f) return;
    float a = -omega * omega * (*x) - 2.0f * omega * (*v);
    *v += a * dt;
    *x += (*v) * dt;
    if (fabsf(*x) < 1e-5f && fabsf(*v) < 1e-4f) { *x = 0; *v = 0; }
}

// A shot happened in front of this player's own eye. Everything that makes it feel like one --
// the gun going back and up, the muzzle rising, the camera flinching, the lens punching open, the
// reticle blooming, the flash, the case -- starts here, in one place, so a weapon that fires
// twice as fast automatically kicks twice as often rather than needing its own code.
static void vm_recoil(Game *g, const ItemDef *d) {
    Weapons *ws = &g->weapons;
    // Weight the kick by what the gun does rather than by which gun it is: a heavy, slow round
    // shoves harder than a light, fast one, and a text file is where that lives.
    float heft = clampf(d ? (d->damage / 60.0f) * (1.4f - clampf(d->rate / 8.0f, 0, 0.9f)) : 1.0f, 0.35f, 2.2f);
    float side = (ws->vm.shots++ & 1u) ? 1.0f : -1.0f;
    ws->vm.back_v  -= VM_KICK_BACK  * heft * VM_KICK_OMEGA;
    ws->vm.up_v    += VM_KICK_UP    * heft * VM_KICK_OMEGA;
    ws->vm.pitch_v -= VM_KICK_PITCH * heft * VM_KICK_OMEGA;
    ws->vm.yaw_v   += VM_KICK_YAW   * heft * VM_KICK_OMEGA * side;
    ws->vm.roll_v  += VM_KICK_ROLL  * heft * VM_KICK_OMEGA * side;
    ws->vm.flash = VM_FLASH_VM;
    ws->vm.flash_seed = (float)(ws->vm.shots * 37u % 360u);
    ws->vm.bloom = fminf(1.0f, ws->vm.bloom + 0.55f * heft);
    camera_add_shake(&g->cam, clampf(0.10f * heft, 0.05f, 0.30f));
    camera_add_fov_punch(&g->cam, clampf(1.6f * heft, 0.6f, 3.5f));
    // A spent case, thrown out of the port and a little forward, tumbling. Camera-local metres.
    if (d && d->weapon == 2 && ws->vm.nshell < (int)(sizeof ws->vm.shell / sizeof ws->vm.shell[0])) {
        int i = ws->vm.nshell++;
        float ex = d->eject.x != 0.0f || d->eject.y != 0.0f || d->eject.z != 0.0f ? 1.0f : 0.0f;
        ws->vm.shell[i].pos = v3(VM_REST_RIGHT + (ex ? d->eject.x : 0.02f),
                                 -VM_REST_DOWN + (ex ? d->eject.y : 0.03f),
                                 VM_REST_FWD + (ex ? d->eject.z : 0.05f));
        ws->vm.shell[i].vel = v3(1.5f + 0.4f * side, 1.1f, -0.25f);
        ws->vm.shell[i].spin = 16.0f + 4.0f * side;
        ws->vm.shell[i].roll = 0;
        ws->vm.shell[i].life = VM_SHELL_LIFE;
    }
}

// Everything the viewmodel owns that moves on its own, advanced once a rendered frame. Kept apart
// from the draw so a frame that draws nothing (third person, holstered, down) still lets the
// springs settle and the last case finish falling.
static void vm_advance(Game *g, float dt) {
    Weapons *ws = &g->weapons;
    if (dt <= 0.0f) return;
    vm_spring(&ws->vm.back,  &ws->vm.back_v,  VM_KICK_OMEGA, dt);
    vm_spring(&ws->vm.up,    &ws->vm.up_v,    VM_KICK_OMEGA, dt);
    vm_spring(&ws->vm.side,  &ws->vm.side_v,  VM_KICK_OMEGA, dt);
    vm_spring(&ws->vm.pitch, &ws->vm.pitch_v, VM_KICK_OMEGA, dt);
    vm_spring(&ws->vm.yaw,   &ws->vm.yaw_v,   VM_KICK_OMEGA, dt);
    vm_spring(&ws->vm.roll,  &ws->vm.roll_v,  VM_KICK_OMEGA, dt);
    ws->vm.flash = fmaxf(0.0f, ws->vm.flash - dt);
    ws->vm.hitmark = fmaxf(0.0f, ws->vm.hitmark - dt);
    ws->vm.bloom = fmaxf(0.0f, ws->vm.bloom - dt * 2.6f);

    // Sprinting: measured off the body's own speed rather than off the sprint key, so a goon
    // shoved down a hill puts the gun away too.
    const Player *p = &g->players[g->local];
    float hspd = hypotf(p->c.hvel.x, p->c.hvel.z);
    float want = clampf((hspd - 4.9f) / 1.8f, 0.0f, 1.0f);
    ws->vm.sprint = lerpf(ws->vm.sprint, want, 1.0f - expf(-VM_SPRINT_RATE * dt));

    int j = 0;
    for (int i = 0; i < ws->vm.nshell; i++) {
        ws->vm.shell[i].life -= dt;
        if (ws->vm.shell[i].life <= 0) continue;
        ws->vm.shell[i].vel.y -= VM_SHELL_G * dt;
        ws->vm.shell[i].pos = v3_add(ws->vm.shell[i].pos, v3_scale(ws->vm.shell[i].vel, dt));
        ws->vm.shell[i].roll += ws->vm.shell[i].spin * dt;
        if (j != i) ws->vm.shell[j] = ws->vm.shell[i];
        j++;
    }
    ws->vm.nshell = j;
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

// A reload, animated by code rather than by a clip, because the clip belongs to a body we are not
// drawing. Three beats: the gun drops out of frame and rolls over as the hand comes off the grip,
// it sits there while the magazine is dealt with, and it snaps back level at the end. Returns the
// 0..1 depth of the dip and writes the roll and pitch that go with it.
static float vm_reload_curve(float reload_left, float *roll, float *pitch) {
    float r = clampf(1.0f - reload_left / WEAP_RELOAD_TIME, 0.0f, 1.0f);
    // The drop takes 0.22 of the reload and the snap back takes 0.16 of it, and that asymmetry is
    // the whole trick: a reload that ends slowly feels broken, because the gun is back in the fight
    // the instant the magazine seats and the picture has to agree with that.
    float down = smoothstep(clampf(r / 0.22f, 0, 1)) * (1.0f - smoothstep(clampf((r - 0.80f) / 0.16f, 0, 1)));
    *roll = 34.0f * down;
    *pitch = 26.0f * down;
    return down;
}

// --- traversal --- Unarmed, there is no weapon and no arms below to carry the beat of a mantle or
// a vault, so a bare hand stands in: one forearm swinging up and forward to slap the ledge and
// back again. It takes the picture's axes from the caller because it is drawn inside the
// viewmodel's own lens, where a hand-rolled cross product of the camera would not match.
static void draw_mantle_hand(Game *g, float reach, Vec3 eye, Vec3 right, Vec3 up, Vec3 fwd) {
    Gfx *x = &g->gfx;
    // Elbow: where an armed right arm would start from. Hand: swings from resting near the hip up
    // and out to the ledge as reach rises, then eases back as the move finishes.
    Vec3 elbow = v3_add(eye, v3_add(v3_scale(fwd, 0.10f), v3_add(v3_scale(right, 0.34f), v3_scale(up, -0.55f))));
    Vec3 hand = v3_add(eye, v3_add(v3_scale(fwd, 0.30f + 0.35f * reach),
                        v3_add(v3_scale(right, 0.26f - 0.06f * reach), v3_scale(up, -0.42f + 0.62f * reach))));
    Mat4 hm = limb_matrix(elbow, hand, VM_ARM_THICK);
    gfx_set_material(x, NULL);
    gfx_draw(x, &x->cube, &x->white, hm, g->net.slots[g->local].tint, v4(1, 1, 0, 0));
}


// Which clip the hands should be playing. The arms are a character like any other, so this is the
// same question weapons.c asks about the third-person body -- asked again here because the hands
// answer to the gun's own clock (a flash, a reload) rather than to the locomotion.
static Anim vm_hand_anim(const Game *g, const Weapon *w, const ItemDef *d) {
    if (w->reload > 0) return ANIM_GUN_RELOAD;
    if (w->swing > 0) return ANIM_MELEE_SWING;
    if (d && d->weapon == 1) return ANIM_MELEE_IDLE;
    if (g->weapons.vm.flash > 0) return ANIM_GUN_FIRE;
    return ANIM_GUN_IDLE;
}

// The muzzle in the gun model's own frame. An item file that has measured it says so; one that has
// not gets the front of its own bounding box, which is right for a pistol and roughly right for
// anything else. Guessing is better than putting the flash in the shooter's palm.
static Vec3 vm_muzzle_local(const ItemDef *d) {
    if (d->muzzle.x != 0.0f || d->muzzle.y != 0.0f || d->muzzle.z != 0.0f) return d->muzzle;
    return v3(0.0f, d->half.y * 0.55f, d->half.z * 1.55f);
}

static Vec3 mat_point(Mat4 m, Vec3 p) {
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

void weapons_draw_viewmodel(Game *g) {
    Gfx *x = &g->gfx;
    Weapons *ws = &g->weapons;
    int slot = g->local;
    float dt = fmaxf(game_frame_dt(g), 0.0f);
    vm_advance(g, dt);                      // springs settle even on a frame that draws nothing
    if (x->in_shadow || g->cam.mode != CAM_FIRST) return;

    Weapon *w = &ws->w[slot];
    const ItemDef *d = weapons_drawn(g, slot) ? weap_def(g, w->item) : NULL;
    float reach = mantle_reach(&g->players[slot]);
    if ((!d || !d->model[0]) && reach <= 0.001f && ws->vm.nshell == 0) return;

    // The picture's own eye and axes -- shake included, roll included. Placing the gun against
    // these rather than against cam.eye is what keeps it still on screen while the world rattles.
    Vec3 eye, right, up, fwd;
    camera_view_basis(&g->cam, &eye, &right, &up, &fwd);

    float rest_r, rest_d, rest_f;
    vm_rest(&rest_r, &rest_d, &rest_f);
    Vec3 pos = v3_add(eye, v3_add(v3_scale(fwd, rest_f),
                                  v3_add(v3_scale(right, rest_r), v3_scale(up, -rest_d))));

    // Bob: the camera's own render-rate bob phase and gain, not the character's walk_phase (which
    // only advances at the 60 Hz sim tick). Riding the same phase as the head means the gun and the
    // eye bob to one beat instead of two, and it is deliberately a little larger than the eye's own
    // bob -- that is what sells motion in first person without shaking the horizon.
    float ph = camera_bob_phase(&g->cam), gain = camera_bob_gain(&g->cam);
    pos = v3_add(pos, v3_scale(right, sinf(ph) * 0.022f * gain));          // one sway a stride
    pos = v3_add(pos, v3_scale(up, sinf(ph * 2.0f) * 0.014f * gain));      // one dip a footfall

    Vec3 extra_pos = v3(0, 0, 0);
    float extra_yaw = 0, extra_pitch = 0, extra_roll = 0;

    // --- traversal --- Momentum sway: the gun is a mass on the end of an arm, not welded to the
    // eye, so it lags a run, drifts with a strafe, sinks a little at speed, and floats or drops
    // with the feet leaving and finding the ground. Driven by Player.c.hvel/vy, never by camera
    // motion -- that is the mouse sway below, a different signal smoothed at a different rate.
    const Player *p = &g->players[slot];
    float vf = v3_dot(p->c.hvel, v3_norm(v3(fwd.x, 0, fwd.z)));   // + is running forward
    float vl = v3_dot(p->c.hvel, right);                          // + is strafing right
    float hspd = hypotf(p->c.hvel.x, p->c.hvel.z);
    float rk = 1.0f - expf(-VM_RUN_RATE * dt);   // 0 when dt <= 0, so a paused frame changes nothing
    static float s_run_lag = 0.0f, s_run_side = 0.0f, s_run_drop = 0.0f, s_run_roll = 0.0f, s_air_lift = 0.0f;
    // -0.075..0.075 rather than a one-sided clamp: backpedalling flips the sign of vf and the lag
    // should flip with it, not pin against a wall the forward case never touches.
    s_run_lag  = lerpf(s_run_lag,  clampf(-VM_RUN_LAG  * vf, -0.075f, 0.075f), rk);
    s_run_side = lerpf(s_run_side, clampf(-VM_RUN_SIDE * vl, -0.05f, 0.05f), rk);
    s_run_drop = lerpf(s_run_drop, clampf(-VM_RUN_DROP * hspd, -0.03f, 0.0f), rk);
    s_run_roll = lerpf(s_run_roll, clampf(VM_RUN_ROLL * vl, -4.0f, 4.0f), rk);
    float lift_target = p->c.grounded ? 0.0f : clampf(p->c.vy * VM_AIR_LIFT, -0.04f, 0.04f);
    s_air_lift = lerpf(s_air_lift, lift_target, rk);
    extra_pos = v3_add(extra_pos, v3_scale(fwd, s_run_lag));
    extra_pos = v3_add(extra_pos, v3_scale(right, s_run_side));
    extra_pos = v3_add(extra_pos, v3_scale(up, s_run_drop + s_air_lift));
    extra_roll += s_run_roll;

    // Sprint: the gun comes down and across the body, muzzle up. It is the clearest "you cannot
    // shoot right now" the game has, and it is free to read at a glance.
    float sp = ws->vm.sprint;
    extra_pos = v3_add(extra_pos, v3_scale(up, -VM_SPRINT_DROP * sp));
    extra_pos = v3_add(extra_pos, v3_scale(right, -0.03f * sp));
    extra_roll += VM_SPRINT_ROLL * sp;
    extra_yaw -= VM_SPRINT_YAW * sp;
    extra_pitch -= VM_SPRINT_PITCH * sp;

    // Hand-plant: a mantle or vault borrows the gun hand to slap the ledge, so the weapon dips out
    // of frame and rolls with it instead of floating in place while the arms do something else.
    if (reach > 0.0f) {
        extra_pos = v3_add(extra_pos, v3_scale(up, -0.30f * reach));
        extra_pos = v3_add(extra_pos, v3_scale(right, 0.12f * reach));
        extra_roll += 35.0f * reach;
    }

    // Recoil, out of the spring: back along the barrel, up out of the rest pose, muzzle rising.
    // m4_rotate_x tips +Z toward -Y for a positive angle, so a negative pitch is what raises it.
    extra_pos = v3_add(extra_pos, v3_scale(fwd, ws->vm.back));
    extra_pos = v3_add(extra_pos, v3_scale(up, ws->vm.up));
    extra_pos = v3_add(extra_pos, v3_scale(right, ws->vm.side));
    extra_pitch += ws->vm.pitch; extra_yaw += ws->vm.yaw; extra_roll += ws->vm.roll;

    // Reload, animated by code: the clip belongs to a body we are not drawing.
    if (w->reload > 0) {
        float rr, rp;
        float down = vm_reload_curve(w->reload, &rr, &rp);
        extra_pos = v3_add(extra_pos, v3_scale(up, -0.20f * down));
        extra_pos = v3_add(extra_pos, v3_scale(right, 0.04f * down));
        extra_roll += rr; extra_pitch += rp;
    }

    // Melee swing: wind-up, contact, follow-through, driven by code rather than a linear sweep.
    // weapons.c's own contact frame lands at s == 0.45, which is why the first leg runs to s == 0.6
    // -- the swing keeps snapping through for a beat after the hit lands.
    if (w->swing > 0) {
        float s = clampf(1.0f - w->swing / WEAP_SWING_TIME, 0, 1);
        float sy, spi;
        if (s < 0.6f) { float t = smoothstep(s / 0.6f); sy = lerpf(40.0f, -50.0f, t); spi = lerpf(-25.0f, 35.0f, t); }
        else { float t = smoothstep((s - 0.6f) / 0.4f); sy = lerpf(-50.0f, -10.0f, t); spi = lerpf(35.0f, 5.0f, t); }
        extra_yaw += sy; extra_pitch += spi;
        extra_pos = v3_add(extra_pos, v3_scale(fwd, sinf(s * PI) * 0.18f));
    }

    // Mouse sway: ws->sway_x/sway_y were updated for this frame in weapons_draw, which is where the
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

    Mat4 frame = basis_matrix(v3_add(pos, extra_pos), right, up, fwd, v3(1, 1, 1));
    frame = m4_mul(frame, m4_mul(m4_rotate_y(extra_yaw * DEG2RAD),
                    m4_mul(m4_rotate_x(extra_pitch * DEG2RAD), m4_rotate_z(extra_roll * DEG2RAD))));

    // ---- everything below is drawn through the viewmodel's own lens and its own depth slice ----
    Mat4 vp = camera_view_proj_lens(&g->cam, (float)INTERNAL_W / (float)INTERNAL_H, vm_fov(), VM_NEAR, VM_FAR);
    gfx_set_view_proj(x, vp);
    gfx_depth_range(x, 0.0f, VM_DEPTH);

    // Hands. It is the ARMS that are hung off the gun, not the other way round: the weapon sits in
    // `frame`, where the framing, the recoil springs, the sprint and the reload curve have already
    // put it, and the arms model is placed so that its hand_r bone lands exactly on frame's origin.
    // Doing it in that order is what keeps the barrel pointing down the middle of the screen -- a
    // gun hung off a bone points wherever the animator aimed a body standing in a field, which is
    // never where a first-person crosshair is -- and it means the `grip` numbers in the item files
    // still mean what they always meant. The anchor is measured once, from the settled aim pose, so
    // the clip's own motion still shows in the hands.
    Mat4 hand = frame;
    CharModel *arms = d && d->model[0] ? vm_arms(g, slot) : NULL;
    if (arms) {
        static unsigned s_last_shots = 0;
        Anim want = vm_hand_anim(g, w, d);
        if (s_arms.body.anim != want) { s_arms.body.anim = want; s_arms.body.anim_t = 0; }
        else if (want == ANIM_GUN_FIRE && ws->vm.shots != s_last_shots) s_arms.body.anim_t = 0;
        else s_arms.body.anim_t += dt;
        s_last_shots = ws->vm.shots;
        charmodel_drive_simple(arms, &s_arms.body, dt);
        model_pose(&arms->model, &arms->player, &arms->pose);
        Mat4 hb;
        if (charmodel_bone_posed(arms, "hand_r", &hb)) {
            // The reference pose is the settled gun-idle one, not whatever the model happened to be
            // holding on its first frame -- which is the bind pose, arms out sideways, and taking
            // the reference from that would leave the gun rotated by however far the aim clip moves
            // the hand away from a T-pose. Waiting for the cross-fade to finish costs a quarter of a
            // second on the very first draw and nothing ever again.
            if (!s_arms.anchored && want == ANIM_GUN_IDLE && s_arms.body.anim_t > 0.25f) {
                s_arms.anchor = v3(hb.m[12], hb.m[13], hb.m[14]);
                s_arms.rest_fix = vm_rest_fix(hb);
                s_arms.anchored = true;
            }
            if (!s_arms.anchored) { s_arms.anchor = v3(hb.m[12], hb.m[13], hb.m[14]); s_arms.rest_fix = vm_rest_fix(hb); }
            float sc = arms->scale > 0.01f ? arms->scale : 1.0f;
            float ay, ap, ar;
            vm_arms_turn(&ay, &ap, &ar);
            Mat4 turn = m4_mul(m4_rotate_y(ay * DEG2RAD), m4_mul(m4_rotate_x(ap * DEG2RAD), m4_rotate_z(ar * DEG2RAD)));
            Mat4 world = m4_mul(m4_mul(frame, turn), m4_mul(m4_translate(v3_scale(s_arms.anchor, -sc)), m4_scale(v3(sc, sc, sc))));
            charmodel_draw_posed(x, arms, &arms->pose, world, g->net.slots[slot].tint);
        }
    }

    // The weapon itself, with a hint of rim light so its silhouette survives against a dark wall
    // without lighting it differently from the world it is standing in.
    if (d && d->model[0]) {
        Vec3 gpos; float gyaw, gpitch, groll, gscale;
        grip_xform(d, &gpos, &gyaw, &gpitch, &groll, &gscale);
        float hy, hp, hr;
        vm_hand_fix(&hy, &hp, &hr);
        Mat4 fix = m4_mul(m4_rotate_y(hy * DEG2RAD), m4_mul(m4_rotate_x(hp * DEG2RAD), m4_rotate_z(hr * DEG2RAD)));
        Mat4 world = m4_mul(m4_mul(hand, fix), grip_matrix(gpos, gyaw, gpitch, groll, gscale));
        Material mat = material_default();
        mat.rim = 0.35f; mat.rim_color = v3(0.85f, 0.90f, 1.0f);
        gfx_set_material(x, &mat);
        props_draw_matrix(&g->gfx, &g->props, d->model, world, d->tint, v3(0, 0, 0), NULL, 0, 0);
        gfx_set_material(x, NULL);

        // Muzzle flash, at the model's real muzzle rather than a fixed distance down the barrel:
        // a stubby star and a soft glow, gone in two frames. The point light that goes with it is
        // in weapons_lights, in world space, where the rest of the level's lighting lives.
        if (ws->vm.flash > 0) {
            float k = clampf(ws->vm.flash / VM_FLASH_VM, 0, 1);
            Vec3 at = mat_point(world, v3_scale(vm_muzzle_local(d), 1.0f / fmaxf(gscale, 0.0001f)));
            gfx_billboard(x, at, 0.13f + 0.05f * k, v4(1.0f, 0.92f, 0.70f, k), true);
            gfx_billboard(x, at, 0.30f * (1.3f - k), v4(1.0f, 0.72f, 0.35f, k * 0.5f), true);
        }
    }

    // Unarmed, the bare hand carries the mantle plant on its own.
    if ((!d || !d->model[0]) && reach > 0.001f) draw_mantle_hand(g, reach, eye, right, up, fwd);

    // Spent cases: camera-local, so they stay in the frame the gun is drawn in rather than being
    // left behind in a world the viewmodel lens is not looking at.
    for (int i = 0; i < ws->vm.nshell; i++) {
        float k = clampf(ws->vm.shell[i].life / VM_SHELL_LIFE, 0, 1);
        Vec3 at = v3_add(eye, v3_add(v3_scale(right, ws->vm.shell[i].pos.x),
                          v3_add(v3_scale(up, ws->vm.shell[i].pos.y), v3_scale(fwd, ws->vm.shell[i].pos.z))));
        Mat4 m = basis_matrix(at, right, up, fwd, v3(0.009f, 0.009f, 0.024f));
        m = m4_mul(m, m4_rotate_x(ws->vm.shell[i].roll));
        Material mat = material_default();
        mat.emissive = v3(0.10f, 0.07f, 0.02f);
        gfx_set_material(x, &mat);
        gfx_draw(x, &x->cube, &x->white, m, v4(0.72f, 0.56f, 0.24f, k), v4(1, 1, 0, 0));
        gfx_set_material(x, NULL);
    }

    gfx_depth_range(x, 0.0f, 1.0f);
    gfx_reset_view_proj(x);
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

    // The local viewmodel is NOT drawn here. It has its own lens and its own slice of the depth
    // buffer, and both of those only work if it goes in after everything else the world draws --
    // so game.c calls weapons_draw_viewmodel at the end of the pass instead. See weapons.h.
    bool did_viewmodel = g->cam.mode == CAM_FIRST && weapons_drawn(g, g->local);

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g->net.slots[i].active) continue;
        if (i == g->local && did_viewmodel) continue;   // that one was just drawn as the viewmodel
        draw_remote_weapon(g, i);
    }

    draw_tracers(&g->gfx, ws);
    draw_flashes(&g->gfx, ws);
    projectiles_draw(g);   // --- projectiles --- in the world, through the world's own lens
}

int weapons_lights(const Game *g, PointLight *out, int max) {
    const Weapons *ws = &g->weapons;
    int n = projectiles_lights(g, out, max);   // --- projectiles --- blasts first: they are bigger
                                               // and rarer than a muzzle flash, and the light
                                               // budget is sixteen for the whole frame
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

    // --- projectiles --- An impact is feedback and nothing else: the machine that owned the
    // projectile already drew the dust and played the thud when it died, and doubling those is
    // worse than missing them. All that is left is the one thing only the host knew.
    if (e->kind == FE_IMPACT) {
        if (slot == g->local && e->hit != FH_NONE) { ws->vm.hitmark = 0.30f; ws->vm.hitmark_solid = e->hit == FH_PLAYER; }
        return;
    }
    // --- projectiles --- A detonation: everyone draws the same bang in the same place.
    if (e->kind == FE_BOOM) { projectiles_boom_fx(g, e->from, (float)e->pellets * 0.1f); return; }

    switch (e->kind) {
    case FE_SHOT: {
        bool mine = slot == g->local;
        bool first_person = mine && g->cam.mode == CAM_FIRST;
        // A gun that throws a projectile draws its own streak as it travels; a second, instant one
        // from the muzzle to wherever the shot was aimed would arrive before the bullet did.
        if (!d || !d->proj) push_tracer(ws, e->from, e->to);
        Vec3 dir = v3_sub(e->to, e->from);
        Vec3 muzzle = v3_len(dir) > 1e-4f ? v3_add(e->from, v3_scale(v3_norm(dir), 0.35f)) : e->from;
        // The light always: a shot in a dark room lights the room whoever fired it. The billboard
        // only when somebody else fired it, or when we are watching ourselves in third person --
        // our own first-person flash belongs on the model's muzzle, in the viewmodel pass.
        push_flash(ws, muzzle, !first_person);
        // The smoke is in world space and would sit 35 cm from the eye, in the middle of the
        // picture, drawn through the wrong lens -- a white blob over the crosshair on every shot.
        // In first person the viewmodel's own flash at the model's real muzzle covers this beat.
        if (!first_person)
            particles_burst(&g->particles, PT_SMOKE, muzzle, v3(0, 1, 0), 3, 0.6f, v3(0.6f, 0.58f, 0.55f), 0.10f, 0.35f);
        SoundId snd = (d && d->fire_sound >= 0) ? (SoundId)d->fire_sound : SND_SHOT;
        audio_play(snd, atten(g, e->from, 0.9f), 1.0f);
        // Recoil: the kick, the shake, the lens punch, the reticle bloom and the spent case, all
        // from one call, so a gun that fires twice as fast kicks twice as often for free.
        if (mine) vm_recoil(g, d);
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

    // A hit marker is the one piece of feedback a shooter cannot get from the world: at thirty
    // metres a goon taking a round looks exactly like a goon not taking one. Ours only.
    if (slot == g->local && (e->kind == FE_SHOT || e->kind == FE_SWING) && e->hit != FH_NONE) {
        ws->vm.hitmark = 0.30f;
        ws->vm.hitmark_solid = e->hit == FH_PLAYER;
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

// A shot we fired ourselves, coming back from the host with the one thing we could not know: what
// it hit. Replaying the whole event would double the bang and the kick, so this plays the marker
// and the impact effect and nothing else.
void weapons_event_own_echo(Game *g, const FireEvent *e) {
    Weapons *ws = &g->weapons;
    if (e->hit == FH_NONE) return;
    ws->vm.hitmark = 0.30f;
    ws->vm.hitmark_solid = e->hit == FH_PLAYER;
    switch (e->hit) {
    case FH_WORLD:  particles_burst(&g->particles, PT_SMOKE, e->to, v3(0, 0, 0), 4, 0.5f, v3(0.5f, 0.45f, 0.4f), 0.10f, 0.4f); break;
    case FH_ITEM:   particles_burst(&g->particles, PT_SPARK, e->to, v3(0, 0, 0), 5, 2.2f, v3(1.0f, 0.85f, 0.5f), 0.05f, 0.2f); break;
    case FH_PLAYER: audio_play(SND_HIT, atten(g, e->to, 0.7f), 1.0f);
                    particles_burst(&g->particles, PT_SMOKE, e->to, v3(0, 0, 0), 2, 0.4f, v3(0.6f, 0.6f, 0.6f), 0.12f, 0.35f); break;
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
                // --- ammo --- What is in the gun, big, and what is left in the pockets, small
                // beside it. Two numbers with different weights, because at a glance the one that
                // decides whether you can keep shooting right now is the first one.
                char s[32]; snprintf(s, sizeof s, "%d", w->ammo);
                float frac = d->ammo > 0 ? (float)w->ammo / (float)d->ammo : 0.0f;
                Vec4 c = w->ammo <= 0 ? v4(0.85f, 0.2f, 0.15f, 1) : frac < (1.0f / 3.0f) ? v4(0.9f, 0.6f, 0.2f, 1) : white;
                int reserve = weapons_reserve_held(g, slot);
                char r[32]; snprintf(r, sizeof r, reserve >= 0 ? " / %d" : " / %d", reserve >= 0 ? reserve : d->ammo);
                float tw = gfx_ui_text_width(1.8f, s), rw = gfx_ui_text_width(1.1f, r);
                gfx_ui_text(x, W - tw - rw - 24, H - 46, 1.8f, c, s);
                gfx_ui_text(x, W - rw - 24, H - 40, 1.1f, reserve == 0 ? v4(0.85f, 0.2f, 0.15f, 1) : dim, r);
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
        // The reticle opens with the recoil and with the run, and closes again as both settle. It
        // is the only honest thing a crosshair can say about a gun whose next shot will not go
        // where this one did: a fixed cross is a promise the weapon cannot keep.
        float spread = ws->vm.bloom + ws->vm.sprint * 1.2f;
        float gap = 6.0f + spread * 12.0f, len = 6.0f, th = 2.0f;
        Vec4 c = v4(0.9f, 0.92f, 0.9f, 0.85f);
        gfx_ui_rect(x, cx - gap - len, cy - th * 0.5f, len, th, c);
        gfx_ui_rect(x, cx + gap, cy - th * 0.5f, len, th, c);
        gfx_ui_rect(x, cx - th * 0.5f, cy - gap - len, th, len, c);
        gfx_ui_rect(x, cx - th * 0.5f, cy + gap, th, len, c);
        // Hit marker: four diagonal ticks, brighter for a goon than for a crate. At thirty metres
        // a goon taking a round looks exactly like a goon not taking one, and this is the only
        // place the game can say otherwise.
        if (ws->vm.hitmark > 0) {
            float k = clampf(ws->vm.hitmark / 0.30f, 0, 1);
            Vec4 hc = ws->vm.hitmark_solid ? v4(1.0f, 0.95f, 0.85f, k) : v4(0.75f, 0.78f, 0.75f, k * 0.8f);
            float in0 = 7.0f, in1 = 14.0f, t = 2.0f;
            for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2)
                for (float u = in0; u < in1; u += 1.0f)
                    gfx_ui_rect(x, cx + (float)sx * u - t * 0.5f, cy + (float)sy * u - t * 0.5f, t, t, hc);
        }
    }
}
