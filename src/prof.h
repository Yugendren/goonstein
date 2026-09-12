// Where the frame went.
//
// A frame that costs 9.8 ms is not a fact you can act on; "shadow 4.3, world 3.1, post 1.1" is.
// This is a per-phase CPU stopwatch with a short history, shown on the F1 overlay as a bar list
// and dumped as a `prof:` block when the process exits.
//
// What it does NOT measure: GPU time. SDL3's GPU API has no timestamp queries (there is no
// SDL_CreateGPUQuery of any kind in 3.4), so nothing here can tell you how long the driver spent
// rasterising. What PROF_SUBMIT and PROF_PRESENT_WAIT give you instead is the CPU cost of
// recording and submitting, and the time the CPU spent blocked waiting for a swapchain image --
// which is where GPU-bound work shows up, as a growing wait. Read a big PRESENT_WAIT as "the GPU
// is behind", not as an idle CPU.
//
// Phases accumulate: calling prof_begin/prof_end on the same phase twice in a frame adds both
// intervals. Nesting one phase inside another is fine and expected (PROF_TICK_PHYS inside
// PROF_TICK); the overlay indents the inner ones and does not subtract them, so an outer phase's
// number is its whole cost.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef enum ProfPhase {
    PROF_FRAME = 0,     // everything, wall clock between one frame's start and the next's
    PROF_INPUT,         // platform_poll + view look
    PROF_TICK,          // the whole fixed-step loop, however many ticks it ran
    PROF_TICK_PHYS,     //   character/prop physics
    PROF_TICK_ITEMS,    //   carried and loose items
    PROF_TICK_NET,      //   netgame pre/post tick: packet build, parse, snapshot
    PROF_TICK_GAME,     //   the rest of game_tick (state machines, combat, bots)
    PROF_TICK_RELOAD,   //   the once-a-second asset polls (hot reload). Must be ~0 in a played game.
    PROF_RENDER,        // game_render, all of it
    PROF_CULL,          //   frustum and size culling, instance list building
    PROF_SHADOW,        //   the sun depth pass
    PROF_WORLD,         //   the HDR world pass (level, terrain, props, characters)
    PROF_WATER,         //   the sea surface
    PROF_PARTICLES,     //   particle and decal quads
    PROF_POST,          //   bright/blur/post/blit
    PROF_UI,            //   HUD, menu, overlay, tool window
    PROF_PRESENT_WAIT,  // blocked in SDL_WaitAndAcquireGPUSwapchainTexture
    PROF_SUBMIT,        // SDL_SubmitGPUCommandBuffer
    PROF_CAP_SLEEP,     // the frame cap's own sleep; subtract it before judging anything
    PROF_COUNT
} ProfPhase;

typedef enum ProfCounter {
    PROF_C_DRAWS = 0,      // gfx draw calls, both passes
    PROF_C_INSTANCES,      // instances drawn (1 per non-instanced draw)
    PROF_C_TRIS,           // triangles submitted
    PROF_C_PROPS_DRAWN,
    PROF_C_PROPS_CULLED,
    PROF_C_BATCHES,        // instanced batches issued
    PROF_C_COUNT
} ProfCounter;

// Phase and counter names, for the overlay, the dump and bench.json keys. Lower case, no spaces.
const char *prof_phase_name(ProfPhase p);
const char *prof_counter_name(ProfCounter c);

// Start of a frame: closes the previous frame's PROF_FRAME, pushes every phase's total into the
// history ring and zeroes the accumulators. Call it once, first thing in the frame loop.
void prof_frame_begin(void);

void prof_begin(ProfPhase p);
void prof_end(ProfPhase p);

void prof_count(ProfCounter c, unsigned add);
unsigned prof_counter_value(ProfCounter c);       // this frame

float prof_ms(ProfPhase p);                       // the frame just finished
void  prof_copy_frame(float out[PROF_COUNT]);     // every phase of the frame just finished, in one go
float prof_median(ProfPhase p);                   // over the history ring, warm-up frames dropped
float prof_p90(ProfPhase p);
float prof_counter_median(ProfCounter c);
int   prof_frames(void);                          // frames in the history ring, warm-up dropped

// How many frames after a reset are dropped before the median starts counting. 60 by default,
// which is right for a played session; the benchmark asks for more, so that a GPU coming up from
// idle has finished doing so before anything is timed. See BENCH_WARMUP_FRAMES in bench.c.
void prof_set_warmup(int frames);

// Throw away everything recorded so far. The benchmark calls this when a camera path starts so the
// loading frames of the path before it do not land in its median.
void prof_reset(void);

// Input-to-photon, roughly: prof_mark_input() stamps the moment the input poll returned and
// prof_mark_present() the moment the frame was handed to the driver. The difference is how old
// the newest input is by the time the GPU is told to draw it -- it excludes the display's own
// pipeline, so it is a floor, not the true photon latency.
void  prof_mark_input(void);
void  prof_mark_present(void);
float prof_input_latency_ms(void);                // median over the history

// A compact bar list for the F1 overlay: "shadow  4.31 ms  ########--". Returns the line count.
int prof_overlay_lines(char lines[][96], int max);

// The `prof:` block: one line per phase with median and p90, plus the counters. `tag` names the
// run (the level, or a bench path). Written with SDL_Log, so it lands in hollow.log.
void prof_dump(const char *tag);
