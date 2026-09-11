// --bench, made concrete. Four fixed camera paths through the island, run headless: each is 60
// warm-up frames plus 600 measured ones, driven by a synthetic Input the way a bot drives one,
// timed on the real wall clock (see prof.h -- PROF_FRAME is wall time regardless of fixed_step) and
// dumped as bench.json for tools/bench_check.py.
//
// Warm-up: prof.c drops the first prof_set_warmup() frames of whatever is in its history
// ring from every median/p90 it reports (see prof.c's usable_range). So a path does NOT run an
// explicit warm-up loop of its own and then call prof_reset() a second time -- that would be two
// warm-ups for one path. Instead prof_reset() is called exactly once, at the instant a path's
// teleport/seat/pin finishes, and the path then simply runs BENCH_PATH_FRAMES frames straight
// through; prof's own warm-up eats the first 60 of those, leaving 600 in the median. This is
// documented in bench.h too, since main.c's loop is the thing that has to run that many frames.
//
// BENCH_PATH_FRAMES is 60 + 600 + 2, not 60 + 600: prof_frame_begin cannot time the very first
// frame after a reset (there is no previous frame_start_tick left to close -- see prof.c), and
// bench_record reads prof_frames() at the end of the path's last frame, before the NEXT
// prof_frame_begin (which only happens once the next path's setup has already reset everything)
// gets a chance to close it. Both frames are real losses, not off-by-one bugs in the arithmetic
// below, and without the +2 a path lands 598 in its median instead of the 600 promised.
//
// Every path stands up the ordinary game -- the same tick, the same physics, the same renderer --
// and only the joystick is scripted (an Input written here) or the view is pinned (camera_set_scene,
// the same call a cutscene makes). Nothing here changes how anything is drawn.
#include "game.h"
#include "prof.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// 240 rather than prof's default 60, which is half a second of drawing before the clock starts.
// Runs of this benchmark vary, and when they do, every phase that touches the driver moves
// together by the same factor -- shadow, world, post and the swapchain wait all 1.8x -- while
// `cull`, pure CPU arithmetic over exactly the same props, does not move at all. Whatever that is,
// it is not this program's workload: a cold or throttled GPU, or another process on the machine.
// The longer warm-up costs a third of a second a path and removes one of those explanations.
// It does NOT remove the other: a benchmark on a busy machine is a benchmark of a busy machine,
// which is why bench_check.py takes the best of several runs.
#define BENCH_WARMUP_FRAMES  240
#define BENCH_MEASURE_FRAMES 600
#define BENCH_LOST_FRAMES    2     // see the top comment: the reset's own first frame, plus the last
                                   // frame's close, which the next path's reset pre-empts
#define BENCH_PATH_FRAMES    (BENCH_WARMUP_FRAMES + BENCH_MEASURE_FRAMES + BENCH_LOST_FRAMES)   // 662
#define BENCH_SHOT_FRAME     300   // well past warm-up, well before the path ends

typedef enum BenchPath { BP_PIER = 0, BP_COURTYARD, BP_SUMMIT, BP_SHOOTOUT, BP_COUNT } BenchPath;
static const char *PATH_NAME[BP_COUNT] = { "pier", "courtyard", "summit", "shootout" };

// Phases worth a line in bench.json, in the order the schema lists them. "frame" (wall time) and
// the counters are handled separately; the ones left out are the ones nobody asks a bench number
// for (the breakdown inside the tick, which is 0.02 ms whole, and cap_sleep, which is zero here
// because a bench run is uncapped).
typedef struct BenchPhaseKey { const char *key; ProfPhase phase; } BenchPhaseKey;
static const BenchPhaseKey PHASE_KEYS[] = {
    { "tick", PROF_TICK }, { "render", PROF_RENDER }, { "cull", PROF_CULL }, { "shadow", PROF_SHADOW },
    { "world", PROF_WORLD }, { "water", PROF_WATER }, { "particles", PROF_PARTICLES },
    { "post", PROF_POST }, { "ui", PROF_UI }, { "present_wait", PROF_PRESENT_WAIT },
    { "submit", PROF_SUBMIT }, { "input", PROF_INPUT },
};
#define BENCH_NPHASES ((int)(sizeof(PHASE_KEYS) / sizeof(PHASE_KEYS[0])))

typedef struct BenchStat {
    char     name[16];
    int      frames;
    float    frame_ms_median, frame_ms_p90;
    float    phase_ms[BENCH_NPHASES];
    unsigned draws, instances, tris, batches, props_drawn, props_culled;
    float    input_latency_ms;
} BenchStat;

// Everything below is process-global, one bench run per process -- the same shape prof.c itself
// uses, and for the same reason: there is exactly one of these ever alive, so a struct threaded
// through every call would buy nothing but noise at the call sites.
static const char *s_json_path;
static const char *s_shot_dir;      // NULL: --bench-shots was not given
static int  s_path = 0;             // BenchPath in progress, or BP_COUNT once every path is done
static int  s_frame = 0;            // 0..BENCH_PATH_FRAMES-1: the frame about to be produced
static bool s_need_setup = true;    // the current path's teleport/seat/pin has not run yet
static BenchStat s_stats[BP_COUNT];
static int  s_nstats = 0;
static float s_pier_look_x;         // computed once at pier's setup from the live mouse sens

// Shootout's three circling goons: fixed centre and per-slot phase, so their path is a pure
// function of the tick count and needs no state that could drift between two runs.
static Vec3  s_shoot_center;
static float s_shoot_phase0[NET_MAX_PLAYERS];

#define COURT_X 	-16.0f
#define COURT_Z 	-70.0f
#define COURT_RADIUS	6.0f     // metres, horizontal, eye to player
#define COURT_HEIGHT	3.0f     // metres above the player's feet
#define COURT_CHEST	1.4f     // metres above the feet: where the look-at point sits
#define COURT_SWEEP_DEG	90.0f
#define COURT_FOV	55.0f

#define SUMMIT_X	0.0f
#define SUMMIT_Z	126.0f
#define SUMMIT_RADIUS	45.0f
#define SUMMIT_HEIGHT	25.0f
#define SUMMIT_LOOK_Y	1.5f
#define SUMMIT_SWEEP_DEG 60.0f
#define SUMMIT_FOV	60.0f

// Same spot the courtyard path stands in -- open ground clear of the villa's colonnade and its two
// wings (see island.txt's VILLA AMBERGRIS block: the main building is centred a couple of metres
// north of this point, the wings another 8 m past that), proven clear by the courtyard screenshot.
#define SHOOT_X		COURT_X
#define SHOOT_Z		COURT_Z
#define SHOOT_RADIUS	3.0f      // metres the three circling goons walk from the centre
#define SHOOT_OMEGA	0.6f      // rad/s they circle at
// ~14 m (a wider watch of the group) swings the eye past the courtyard's open pad and into the
// hillside just beyond it, which rises well above the courtyard's own floor height -- terrain the
// eye cannot tell from open air until it is already inside it. COURT_RADIUS/COURT_HEIGHT are the
// numbers the courtyard path already proved clear at this same centre, so shootout reuses them
// rather than a distance that looks fine on paper and puts the lens in the dirt.
#define SHOOT_CAM_RADIUS 6.0f
#define SHOOT_CAM_HEIGHT 3.0f
#define SHOOT_CAM_SWEEP_DEG 60.0f
#define SHOOT_CAM_LOOK_Y 1.2f
#define SHOOT_FIRE_PERIOD 10     // ticks between shots, rotating through slots 1..3
#define SHOOT_FOV	55.0f

// East along the stone mole, away from the dock house at the pier's west end (island.txt's PELICAN
// PIER block) and away from the dune that rises just south of the quay -- the level's own
// spawn_yaw walks a fresh player off the pier and onto the village path, which is the right thing
// for a played game and the wrong thing for a path that wants to stay on the pier for five seconds.
#define PIER_YAW_DEG	90.0f

// ---------------------------------------------------------------- small helpers

// The wire format's quantisation (see netgame.c's q_dir/q_yaw, which are file-static there): a
// world-space direction and a yaw, each packed the same way a real client would pack them, so
// host_simulate replays slots 1..3 exactly as it would a real client's intent.
static int8_t  bench_q_dir(float v) { return (int8_t)lrintf(clampf(v, -1.0f, 1.0f) * 127.0f); }
static int16_t bench_q_yaw(float y) {
    while (y > PI) y -= 2 * PI;
    while (y < -PI) y += 2 * PI;
    return (int16_t)lrintf(y * (32767.0f / PI));
}

// Puts the local player at (x, terrain height + y_extra, z), facing whatever it was already facing,
// with its velocity zeroed and its feet snapped to the ground (dt <= 0: see game_ground_character).
static void bench_teleport_local(Game *g, float x, float z, float y_extra) {
    game_spawn_player(g, g->local);   // clears state (anim, velocity, hp) the way a real spawn does
    Player *p = &PLAYER(g);
    float gy = terrain_height(&g->terrain, x, z);
    p->c.pos = v3(x, gy + y_extra, z);
    p->c.vy = 0; p->c.grounded = false; p->c.ground_block = -1;
    game_ground_character(g, &p->c, 0);
}

// A pinned camera pin: identical to a cutscene's camera_set_scene, plus (on the first frame of the
// path only) clearing prev_valid so the render does not lerp in from wherever the camera was left
// by the path before this one -- see this file's top comment and camera.h's camera_set_scene.
static void bench_pin(Game *g, Vec3 eye, Vec3 target, float fov, bool first_frame) {
    camera_set_scene(&g->cam, eye, target, fov, true);
    if (first_frame) { g->prev_eye = eye; g->prev_target = target; g->prev_valid = false; }
}

// ---------------------------------------------------------------- pier: first person, walking

static void setup_pier(Game *g) {
    g->bot = false;   // this path's script owns the input; HOLLOW_BOT=shoot must not touch it
    // A couple of metres east of the level spawn: centred on the quay's width and clear of the
    // dock house collider at the pier's west end (see PIER_YAW_DEG's comment above).
    bench_teleport_local(g, g->level.spawn.x + 2.0f, g->level.spawn.z, 0.05f);
    PLAYER(g).c.yaw = PIER_YAW_DEG * DEG2RAD;
    g->cam.yaw = PIER_YAW_DEG * DEG2RAD; g->cam.pitch = 0;
    game_snap_camera(g);   // seats the real first-person camera on the player (island is `view first`)
    // Enough mouse_dx per frame to pan 45 degrees over the whole path, whatever mouse_sens is set
    // to -- see camera.c's camera_look: yaw -= mouse_dx * camera_mouse_sens().
    float sens = camera_mouse_sens();
    s_pier_look_x = sens > 1e-6f ? ((45.0f * DEG2RAD) / sens) / (float)BENCH_PATH_FRAMES : 0.0f;
}

static void drive_pier(Game *g, int frame, Input *in) {
    (void)g; (void)frame;
    in->move_y = 1.0f;      // forward, camera-relative -- exactly what a held W does
    in->look_x = s_pier_look_x;
}

// ---------------------------------------------------------------- courtyard: third person, static

static void setup_courtyard(Game *g) {
    g->bot = false;
    bench_teleport_local(g, COURT_X, COURT_Z, 1.0f);
    g->cam.yaw = 0; g->cam.pitch = 0;   // a steady body facing for the third-person shot
}

static void render_courtyard(Game *g, int frame, bool first_frame) {
    Vec3 feet = PLAYER(g).c.pos;
    float t = (float)frame / (float)(BENCH_PATH_FRAMES - 1);
    float az = t * COURT_SWEEP_DEG * DEG2RAD;
    Vec3 eye = v3_add(feet, v3(sinf(az) * COURT_RADIUS, COURT_HEIGHT, cosf(az) * COURT_RADIUS));
    Vec3 target = v3_add(feet, v3(0, COURT_CHEST, 0));
    bench_pin(g, eye, target, COURT_FOV, first_frame);
}

// ---------------------------------------------------------------- summit: third person, overview

static void setup_summit(Game *g) {
    g->bot = false;
    bench_teleport_local(g, SUMMIT_X, SUMMIT_Z, 1.0f);
    g->cam.yaw = 0; g->cam.pitch = 0;
}

static void render_summit(Game *g, int frame, bool first_frame) {
    Vec3 feet = PLAYER(g).c.pos;
    float t = (float)frame / (float)(BENCH_PATH_FRAMES - 1);
    float az = t * SUMMIT_SWEEP_DEG * DEG2RAD;
    Vec3 eye = v3_add(feet, v3(sinf(az) * SUMMIT_RADIUS, SUMMIT_HEIGHT, cosf(az) * SUMMIT_RADIUS));
    Vec3 target = v3_add(feet, v3(0, SUMMIT_LOOK_Y, 0));
    bench_pin(g, eye, target, SUMMIT_FOV, first_frame);
}

// ---------------------------------------------------------------- shootout: four goons fighting

// Seats slots 1..3 the way netgame.c seats a real joiner (see game.h's game_spawn_player /
// game_give_loadout doc comments), without ever opening a socket -- g->net.mode = NM_HOST with no
// net_open is safe because net_recv/net_send both no-op on a socket that was never opened.
// SLOT_TINT below mirrors netgame.c's own (file-static there, so not reachable from here) so a
// bench shootout tints its goons exactly like a real lobby would.
static void setup_shootout(Game *g) {
    static const Vec4 SLOT_TINT[NET_MAX_PLAYERS] = {
        { 1.00f, 1.00f, 1.00f, 1 }, { 1.00f, 0.55f, 0.45f, 1 },
        { 0.50f, 0.75f, 1.00f, 1 }, { 0.95f, 0.90f, 0.45f, 1 },
    };
    g->net.mode = NM_HOST;
    // The courtyard is a raised platform, not a dip sculpted into the heightmap -- terrain_height()
    // alone reports the bare ground underneath it, metres below the real floor. bench_teleport_local
    // gets the right height because game_ground_character (dt <= 0) resolves a teleport against
    // whatever is really under the feet, blocks and prop decks included, not just the terrain; so
    // the local player is seated FIRST and its resolved y is what the centre and the other three
    // goons build from, instead of ever trusting terrain_height() for this spot.
    bench_teleport_local(g, SHOOT_X, SHOOT_Z - SHOOT_RADIUS, 0.05f);
    s_shoot_center = v3(SHOOT_X, PLAYER(g).c.pos.y, SHOOT_Z);
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        NetSlot *s = &g->net.slots[i];
        s->active = true; s->tint = SLOT_TINT[i]; s->qn = 0; s->nhist = 0;
        snprintf(s->name, sizeof s->name, "bench%d", i);
        game_ensure_player_model(g, i);
        game_spawn_player(g, i);
        game_give_loadout(g, i);
        s_shoot_phase0[i] = (float)(i - 1) * (2.0f * PI / 3.0f);
        float az = s_shoot_phase0[i];
        g->players[i].c.pos = v3(s_shoot_center.x + cosf(az) * SHOOT_RADIUS, s_shoot_center.y + 1.0f,
                                  s_shoot_center.z + sinf(az) * SHOOT_RADIUS);
        g->players[i].c.vy = 0; g->players[i].c.grounded = false; g->players[i].c.ground_block = -1;
        game_ground_character(g, &g->players[i].c, 0);
    }
    g->bot = true;   // HOLLOW_BOT=shoot (set by main.c) now drives it: find a target, aim, fire
}

// Every tick: walk slots 1..3 toward the next point on their circle (a NetInput in q[0], consumed
// by host_simulate before the next call), face each one at "the next goon" (1->2, 2->3, 3->local),
// and every SHOOT_FIRE_PERIOD ticks let one of them pull the trigger at that same goon.
static void drive_shootout(Game *g, int frame) {
    float t = (float)frame / 60.0f;   // seconds, at the fixed 60 Hz tick bench mode runs at
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        float az = s_shoot_phase0[i] + SHOOT_OMEGA * t;
        Vec3 want = v3_add(s_shoot_center, v3(cosf(az) * SHOOT_RADIUS, 0, sinf(az) * SHOOT_RADIUS));
        Vec3 cur = g->players[i].c.pos;
        Vec3 diff = v3_sub(want, cur); diff.y = 0;
        float dl = v3_len(diff);
        Vec3 dir = dl > 0.05f ? v3_scale(diff, 1.0f / dl) : v3(0, 0, 0);

        int target = (i < 3) ? (i + 1) : 0;   // 1->2, 2->3, 3->local
        Vec3 to_target = v3_sub(g->players[target].c.pos, cur); to_target.y = 0;
        float yaw = atan2f(to_target.x, to_target.z);   // Character.yaw: 0 faces +Z, + turns toward +X
        g->players[i].c.yaw = yaw;   // correct now, for weapons_net_fire below; host_simulate will
                                      // set the same value again from the NetInput this tick

        NetSlot *s = &g->net.slots[i];
        NetInput ni; memset(&ni, 0, sizeof ni);
        ni.tick = g->tick; ni.mx = bench_q_dir(dir.x); ni.mz = bench_q_dir(dir.z); ni.yaw = bench_q_yaw(yaw);
        s->q[0] = ni; s->qn = 1;
    }
    int fire_slot = 1 + (frame / SHOOT_FIRE_PERIOD) % 3;
    if (frame % SHOOT_FIRE_PERIOD == 0) {
        int target = (fire_slot < 3) ? (fire_slot + 1) : 0;
        Weapon *w = &g->weapons.w[fire_slot];
        if (w->item >= 0 && w->ammo <= 0 && w->reload <= 0) weapons_net_reload(g, fire_slot);
        Vec3 origin = weapons_eye(g, fire_slot);
        Vec3 at = g->players[target].c.pos; at.y += g->players[target].c.height * 0.55f;
        Vec3 dir = v3_sub(at, origin);
        if (v3_len(dir) > 1e-3f) weapons_net_fire(g, fire_slot, origin, v3_norm(dir));
    }
}

static void render_shootout(Game *g, int frame, bool first_frame) {
    (void)g;
    float t = (float)frame / (float)(BENCH_PATH_FRAMES - 1);
    float az = t * SHOOT_CAM_SWEEP_DEG * DEG2RAD;
    Vec3 eye = v3_add(s_shoot_center, v3(sinf(az) * SHOOT_CAM_RADIUS, SHOOT_CAM_HEIGHT, cosf(az) * SHOOT_CAM_RADIUS));
    Vec3 target = v3_add(s_shoot_center, v3(0, SHOOT_CAM_LOOK_Y, 0));
    bench_pin(g, eye, target, SHOOT_FOV, first_frame);
}

// ---------------------------------------------------------------- dispatch

static void bench_setup_path(Game *g, BenchPath p) {
    switch (p) {
        case BP_PIER:      setup_pier(g); break;
        case BP_COURTYARD: setup_courtyard(g); break;
        case BP_SUMMIT:    setup_summit(g); break;
        case BP_SHOOTOUT:  setup_shootout(g); break;
        default: break;
    }
}

static void bench_record(Game *g, Platform *pf, BenchPath p) {
    (void)g; (void)pf;
    BenchStat *st = &s_stats[p];
    memset(st, 0, sizeof *st);
    snprintf(st->name, sizeof st->name, "%s", PATH_NAME[p]);
    st->frames = prof_frames();
    st->frame_ms_median = prof_median(PROF_FRAME);
    st->frame_ms_p90 = prof_p90(PROF_FRAME);
    for (int i = 0; i < BENCH_NPHASES; i++) st->phase_ms[i] = prof_median(PHASE_KEYS[i].phase);
    st->draws = (unsigned)lrintf(prof_counter_median(PROF_C_DRAWS));
    st->instances = (unsigned)lrintf(prof_counter_median(PROF_C_INSTANCES));
    st->tris = (unsigned)lrintf(prof_counter_median(PROF_C_TRIS));
    st->batches = (unsigned)lrintf(prof_counter_median(PROF_C_BATCHES));
    st->props_drawn = (unsigned)lrintf(prof_counter_median(PROF_C_PROPS_DRAWN));
    st->props_culled = (unsigned)lrintf(prof_counter_median(PROF_C_PROPS_CULLED));
    st->input_latency_ms = prof_input_latency_ms();
    if (s_nstats <= (int)p) s_nstats = (int)p + 1;
}

bool bench_requested(int argc, char **argv, const char **out_json, const char **out_shot_dir) {
    static const char *json = "bench.json";
    static const char *shot_dir = NULL;
    bool found = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench")) {
            found = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') json = argv[++i];
        } else if (!strcmp(argv[i], "--bench-shots") && i + 1 < argc) {
            shot_dir = argv[++i];
        }
    }
    if (out_json) *out_json = json;
    if (out_shot_dir) *out_shot_dir = shot_dir;
    return found;
}

void bench_init(Game *g, Platform *pf, const char *json_path, const char *shot_dir) {
    (void)g; (void)pf;
    prof_set_warmup(BENCH_WARMUP_FRAMES);   // see BENCH_WARMUP_FRAMES: a cold GPU is a different machine
    s_json_path = json_path; s_shot_dir = shot_dir;
    s_path = 0; s_frame = 0; s_need_setup = true; s_nstats = 0;
    memset(s_stats, 0, sizeof s_stats);
}

bool bench_pre_tick(Game *g, Platform *pf, Input *in) {
    (void)pf;
    if (s_path >= BP_COUNT) return false;
    BenchPath p = (BenchPath)s_path;
    if (s_need_setup) {
        SDL_Log("bench: path %s starting (%d frames: %d warm-up + %d measured)",
                PATH_NAME[p], BENCH_PATH_FRAMES, BENCH_WARMUP_FRAMES, BENCH_MEASURE_FRAMES);
        bench_setup_path(g, p);
        prof_reset();   // this path's own warm-up starts now -- see the file's top comment
        s_need_setup = false;
    }
    memset(in, 0, sizeof *in);   // a clean slate every frame: nothing carries over between paths
    switch (p) {
        case BP_PIER:      drive_pier(g, s_frame, in); break;
        case BP_SHOOTOUT:  drive_shootout(g, s_frame); break;   // writes net input, not `in`
        case BP_COURTYARD: case BP_SUMMIT: default: break;      // idle: the camera does the work
    }
    return true;
}

void bench_pre_render(Game *g, Platform *pf, char *shot_path_out, size_t shot_path_cap) {
    if (shot_path_out && shot_path_cap) shot_path_out[0] = 0;
    if (s_path >= BP_COUNT) return;
    BenchPath p = (BenchPath)s_path;
    bool first_frame = (s_frame == 0);
    switch (p) {
        case BP_PIER: break;   // unpinned: the real first-person camera runs
        case BP_COURTYARD: render_courtyard(g, s_frame, first_frame); break;
        case BP_SUMMIT:    render_summit(g, s_frame, first_frame); break;
        case BP_SHOOTOUT:  render_shootout(g, s_frame, first_frame); break;
        default: break;
    }
    if (shot_path_out && shot_path_cap && s_shot_dir && s_frame == BENCH_SHOT_FRAME)
        snprintf(shot_path_out, shot_path_cap, "%s/%s.png", s_shot_dir, PATH_NAME[p]);
    s_frame++;
    if (s_frame >= BENCH_PATH_FRAMES) {
        bench_record(g, pf, p);
        prof_dump(PATH_NAME[p]);
        s_path++; s_frame = 0; s_need_setup = true;
    }
}

void bench_finish(Game *g, Platform *pf) {
    FILE *f = fopen(s_json_path, "w");
    if (!f) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "bench: could not write %s", s_json_path); return; }
    char machine_tag[160];
    snprintf(machine_tag, sizeof machine_tag, "%s/%s/%d",
             SDL_GetGPUDeviceDriver(pf->gpu), SDL_GetPlatform(), SDL_GetNumLogicalCPUCores());
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"machine_tag\": \"%s\",\n", machine_tag);
    fprintf(f, "  \"gpu_driver\": \"%s\",\n", SDL_GetGPUDeviceDriver(pf->gpu));
    fprintf(f, "  \"present_mode\": \"%s\",\n", platform_present_mode_name(pf));
    fprintf(f, "  \"internal_w\": %d, \"internal_h\": %d,\n", INTERNAL_W, INTERNAL_H);
    fprintf(f, "  \"paths\": [\n");
    for (int i = 0; i < s_nstats; i++) {
        const BenchStat *st = &s_stats[i];
        fprintf(f, "    { \"name\": \"%s\", \"frames\": %d,\n", st->name, st->frames);
        fprintf(f, "      \"frame_ms_median\": %.3f, \"frame_ms_p90\": %.3f,\n", (double)st->frame_ms_median, (double)st->frame_ms_p90);
        fprintf(f, "      \"phases\": {");
        for (int k = 0; k < BENCH_NPHASES; k++)
            fprintf(f, " \"%s\": %.3f%s", PHASE_KEYS[k].key, (double)st->phase_ms[k], k + 1 < BENCH_NPHASES ? "," : "");
        fprintf(f, " },\n");
        fprintf(f, "      \"draws\": %u, \"instances\": %u, \"tris\": %u, \"batches\": %u,\n", st->draws, st->instances, st->tris, st->batches);
        fprintf(f, "      \"props_drawn\": %u, \"props_culled\": %u,\n", st->props_drawn, st->props_culled);
        fprintf(f, "      \"input_latency_ms\": %.3f }%s\n", (double)st->input_latency_ms, i + 1 < s_nstats ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    SDL_Log("bench: wrote %s (%s, %s, %d paths)", s_json_path, machine_tag, platform_present_mode_name(pf), s_nstats);
    (void)g;
}
