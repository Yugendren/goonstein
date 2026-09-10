// Host-authoritative listen server: four players on one map.
//
// The host runs the whole simulation at the fixed 60 Hz tick and owns every player. Clients send
// their intent (a quantised world-space move direction, a yaw and a button bitfield) every tick
// and get a snapshot back at 30 Hz. Remote players are interpolated ~100 ms in the past; the local
// player is predicted locally and the host's answer is folded back in as a smoothed correction.
//
// Nothing here knows about sockets (that is net.h) and net.h knows nothing about the game.
#pragma once
#include "net.h"
#include "combat.h"
#include "hmath.h"

struct Game;

#define NET_MAX_PLAYERS   4
#define NET_SNAP_HZ       30
#define NET_TIMEOUT       5.0     // seconds of silence before a peer is dropped
#define NET_INTERP_DELAY  0.10    // how far behind the newest snapshot remote players are drawn
#define NET_HIST          128     // ticks of local prediction history
#define NET_SNAP_BUF      24      // snapshots kept per remote slot
#define NET_INPUT_Q       8       // host-side jitter buffer, one intent per tick
#define NET_SNAP_MAX_AGE  1.0     // a slot with no snapshot this recent stops moving

typedef enum NetMode { NM_OFF, NM_HOST, NM_CLIENT } NetMode;

// Buttons in a NetInput.
enum { NB_ATTACK = 1, NB_PARRY = 2, NB_DODGE = 4, NB_INTERACT = 8, NB_SPRINT = 16, NB_GUARD = 32,
       NB_JUMP = 64, NB_CROUCH = 128 };   // a remote goon has to be able to jump and duck like a local one

// One tick of intent. The move direction is world space and quantised on the client before it is
// used locally, so the host's replay of it is bit-identical to the client's prediction.
typedef struct NetInput {
    uint32_t tick;
    int8_t   mx, mz;      // world XZ direction * 127
    int16_t  yaw;         // radians * (32767 / PI)
    uint16_t buttons;
} NetInput;

// What a snapshot says about one player.
typedef struct NetSnapPlayer {
    Vec3    pos; float yaw;
    uint8_t anim, pstate;
    float   anim_t, hp;
    // --- weapons --- three bytes: what the weapon hand is doing, what is left in it, and how much
    // wind its owner has before they end up on the floor. See weapons_pack_flags.
    uint8_t wflags, wammo, wwind;
} NetSnapPlayer;

typedef struct NetSlot {
    bool     active;
    char     name[24];
    Vec4     tint;
    // host side
    NetPeer  peer; bool has_peer;      // the client that owns this slot (never set for the host's own slot)
    NetInput input; bool have_input;   // the intent applied on the last tick
    uint32_t input_tick;               // its client tick, echoed back for reconciliation
    NetInput q[NET_INPUT_Q]; int qn;   // jitter buffer of intents not applied yet
    uint32_t last_queued; bool have_queued;
    double   last_heard;
    // client side: snapshot history, oldest first, for interpolation
    struct { double t; NetSnapPlayer s; } hist[NET_SNAP_BUF]; int nhist;
    double   last_snap_t;
} NetSlot;

typedef struct NetGame {
    NetMode   mode;
    NetSocket sock;
    char      join_target[128], name[24];
    uint16_t  port;
    int       slots_max;
    int       local;                  // the slot this process drives
    bool      started;
    NetSlot   slots[NET_MAX_PLAYERS];
    // client connection state
    NetPeer   server;
    bool      connected; int join_tries;
    bool      rejected;               // --- menu --- the host answered "full": the menu says so instead of waiting out the timeout
    double    now;                    // seconds since netgame_start
    uint32_t  net_tick;               // ticks since start (what the client stamps its inputs with)
    int       snap_countdown;         // host: ticks until the next snapshot
    uint32_t  server_tick;            // newest server tick seen (client) / g->tick (host)
    double    server_tick_at;         // local time that snapshot arrived
    NetInput  cur;                    // the local player's intent this tick
    char      level_name[32];         // what the host says everyone is playing
    float     bot_heading; uint32_t bot_next_turn;   // --bot on a client: wander state
    unsigned  item_cursor;            // host: where the round-robin over sleeping items has got to
    // client prediction and reconciliation
    Vec3      hist_pos[NET_HIST]; uint32_t hist_tick[NET_HIST];
    Vec3      pos_error;              // visual offset that decays back to zero after a correction
    uint32_t  last_ack;               // newest input tick the host says it has consumed
    // stats, reset once a second for the log line
    uint32_t  s_pkt_in, s_pkt_out; uint64_t s_bytes_in, s_bytes_out;
    uint32_t  s_corrections, s_snaps_applied, s_hard_snaps, s_drops;
    float     s_corr_sum, s_corr_max;
    double    stat_t;
    // running totals, for the closing report
    double    total_time;
    uint32_t  t_corrections, t_hard_snaps; double t_corr_sum;
} NetGame;

// --host PORT [--slots N] | --join HOST:PORT | --name NAME. Called before the game is created.
void netgame_parse_args(NetGame *n, int argc, char **argv);
// Open the socket and seat the local player. Returns false if the socket could not be opened.
bool netgame_start(struct Game *g);
void netgame_shutdown(struct Game *g);
static inline bool netgame_on(const NetGame *n) { return n->mode != NM_OFF; }

// Start of a tick: drain the socket. Host: collect client intent, seat joiners, drop the silent.
// Client: apply the newest snapshot to remote slots and reconcile the local player.
void netgame_pre_tick(struct Game *g, float dt);
// End of a tick. Host: simulate every remote player from its latest intent, then send a snapshot
// at NET_SNAP_HZ. Client: send this tick's intent. Both: per-second stats to the log.
void netgame_post_tick(struct Game *g, float dt);
// Called by the movement code with the camera-relative direction the local player asked for.
// Records the intent for this tick and returns the direction actually used (quantised when
// networked, unchanged when not).
Vec3 netgame_local_input(struct Game *g, Vec3 move_dir, const Input *in);
// Where the local player should be drawn: its simulated position plus the decaying correction.
Vec3 netgame_view_pos(const NetGame *n, int slot, Vec3 sim_pos);
// Reliable item commands, client -> host. The host validates reach and ownership; the client
// predicts the result immediately and lets the next snapshot correct it.
void netgame_send_item_grab(struct Game *g, uint16_t id);
void netgame_send_item_release(struct Game *g, uint16_t id, bool thrown, Vec3 vel);
// --- weapons --- Reliable weapon commands, client -> host. The host re-runs the hitscan from its
// own copy of the world; the client only says where it was looking and when.
void netgame_send_weapon_fire(struct Game *g, Vec3 origin, Vec3 dir);
void netgame_send_weapon_reload(struct Game *g);
void netgame_send_weapon_swap(struct Game *g);
void netgame_send_weapon_revive(struct Game *g, int target, bool holding);
// Bot client: wander so a screenshot shows movement.
void netgame_bot_wander(struct Game *g, Input *in);
