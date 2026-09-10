// Proximity voice chat. See src/voice.h for the shape of it; this file is the whole subsystem
// except the three pieces it delegates to: voice_dsp.c (the voice changer), voice_jitter.c (one
// jitter buffer + Opus decoder per speaker) and voice_wav.c (the test hooks).
//
// Threads. Everything here runs on the main thread except voice_pull(), which the audio mixer
// calls on the audio thread once per callback block. The two meet at g_v.lock and at the
// per-speaker PCM rings: the main thread decodes 20 ms frames into a ring, the audio thread reads
// samples out of it and applies proximity. Decoding on the main thread (rather than in the
// callback) keeps Opus off the real-time thread; the ring is kept two to three frames deep, so
// this costs at most one tick of latency and cannot underrun at 60 Hz.
//
// Latency budget, capture to ear:
//   20 ms   one capture frame must fill before anything is sent
//   ~8 ms   average wait for the next 60 Hz tick that carries it
//   ~0 ms   loopback / LAN transit, plus one host forward
//   80 ms   jitter buffer target (VOICE_JITTER_MS)
//   ~20 ms  the receiver's PCM ring and the audio device's own buffer
// which is the ~130 ms the four-process test measures.
#include "voice.h"
#include "voice_dsp.h"
#include "voice_jitter.h"
#include "voice_wav.h"
#include "game.h"
#include "audio.h"
#include "debug.h"
#include <SDL3/SDL.h>
#include <opus.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VOICE_JITTER_MS   80        // per-speaker buffer depth; the brief asks for 60-100 ms
#define VOICE_BITRATE     24000     // bits/s, VBR
#define VOICE_RING_FRAMES 8         // 160 ms of decoded PCM per speaker between the two threads
#define VOICE_RING        (VOICE_FRAME * VOICE_RING_FRAMES)
#define VOICE_RING_WANT   3         // frames the main thread keeps queued ahead of the mixer
#define VOICE_OUTBOX      4         // encoded frames waiting for the next packet
#define VOICE_GATE_RMS    0.010f    // open mic: the level that counts as speech
#define VOICE_GATE_HANG   0.35f     // and how long the gate stays open after it drops below
#define VOICE_SPEAK_HOLD  0.30f     // how long the HUD keeps showing someone as talking
#define VOICE_MUFFLE_HZ   700.0f    // one-pole cutoff when a solid block is in the way
#define VOICE_OPEN_HZ     14000.0f  // and when it is not (effectively open)
#define VOICE_MUFFLE_GAIN 0.7f
#define VOICE_PAN_WIDTH   0.85f     // never hard-panned: a voice at your shoulder still has a body

// One remote (or, for the monitor, local) voice.
typedef struct Speaker {
    bool     used;
    VoiceJitter jit;
    // decoded PCM, written by the main thread, drained by the audio thread, both under g_v.lock
    float    pcm[VOICE_RING];
    uint64_t wr, rd;                    // monotonic sample counters; wr - rd = samples queued
    // proximity, recomputed once a tick from the listener's point of view
    float    want_l, want_r, want_cut;  // target left/right gain and muffle cutoff
    float    cur_l, cur_r;              // what the mixer is actually using; ramped across a block
    float    lp;                        // one-pole state for the muffle
    bool     occluded; float dist, gain;   // for the log line (gain before the pan split)
    float    level;                     // smoothed loudness after proximity, for the HUD
    double   last_heard;                // g_v.now when a packet last arrived
    // latency, in milliseconds, capture to the moment the frame entered the playback ring
    float    lat_sum; uint32_t lat_n; float lat_last;
    float    transit_sum; uint32_t transit_n;
    VoiceWavOut dump;                   // HOLLOW_VOICE_DUMP: this speaker alone, post-proximity
} Speaker;

typedef struct VoiceState {
    bool        ok;
    SDL_Mutex  *lock;
    SDL_AudioStream *rec;               // NULL when the source is a WAV file
    OpusEncoder *enc;
    VoiceDsp    dsp;
    VoicePreset preset;

    VoiceMode mode; float volume; bool monitor;
    bool offline;   // no playback device: the mix is rendered on the main thread (see audio_silent)

    // capture
    float    mic[VOICE_FRAME * 4]; int mic_n;   // partial frame waiting to be filled
    float   *wav; int wav_n; double wav_pos;    // HOLLOW_VOICE_WAV source, looped
    bool     from_wav;
    float    mic_level;                          // smoothed, for the HUD dot
    bool     tx; float gate_hang; float last_rms;   // transmitting now / open-mic hangover / last frame's level
    uint16_t seq;

    // outbox: encoded blocks waiting for the next outgoing packet (client) or forward (host)
    struct { int len; uint8_t b[VOICE_MAX_BLOCK]; } out[VOICE_OUTBOX]; int nout;

    Speaker  sp[NET_MAX_PLAYERS];
    int      local;                     // the slot this process drives; sp[local] is the monitor

    // stats, reset every second
    double   now, stat_t;
    uint32_t s_up_bytes, s_down_bytes, s_tx_frames;
    uint32_t s_fwd_bytes;

    VoiceWavOut dump;                   // HOLLOW_VOICE_DUMP: the whole voice bus, stereo
} VoiceState;

static VoiceState g_v;

// ---------------------------------------------------------------- small helpers

static void open_microphone(void);
static float clamp01f(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static uint16_t wall_ms16(void) {
    SDL_Time t = 0; SDL_GetCurrentTime(&t);
    return (uint16_t)((uint64_t)(t / 1000000) & 0xFFFFu);   // ns -> ms, low 16 bits
}
// Milliseconds from a captured stamp to now, over a 65 s wrap.
static float since_ms(uint16_t then) { return (float)(uint16_t)(wall_ms16() - then); }

// ---------------------------------------------------------------- settings

void voice_set_mode(VoiceMode m) { g_v.mode = m; }
void voice_set_mode_name(const char *s) {
    if (!s) return;
    if (!SDL_strcasecmp(s, "ptt")) g_v.mode = VOICE_PTT;
    else if (!SDL_strcasecmp(s, "open")) g_v.mode = VOICE_OPEN;
    else if (!SDL_strcasecmp(s, "off")) g_v.mode = VOICE_OFF;
}
const char *voice_mode_name(void) {
    return g_v.mode == VOICE_OFF ? "off" : (g_v.mode == VOICE_OPEN ? "open" : "ptt");
}
void voice_set_volume(float v) { g_v.volume = v < 0 ? 0 : (v > 2 ? 2 : v); }
float voice_get_volume(void) { return g_v.volume; }
void voice_set_monitor(bool on) { g_v.monitor = on; }
bool voice_ready(void) { return g_v.ok; }

void voice_set_preset(const VoicePreset *p) {
    if (!p) return;
    if (g_v.preset.pitch == p->pitch && g_v.preset.formant == p->formant && g_v.preset.effect == p->effect) return;
    g_v.preset = *p;
    voice_dsp_init(&g_v.dsp, p->pitch, p->formant, p->effect);
    dbg_log("voice: preset pitch %.2f formant %.2f effect %s (%d samples of shifter delay)",
            (double)p->pitch, (double)p->formant, voice_effect_name(p->effect), voice_dsp_latency(&g_v.dsp));
}

// ---------------------------------------------------------------- the mixer's pull
//
// Runs on the audio thread. Each speaker's ring is read at one sample per output frame, muffled
// with a one-pole low-pass whose coefficient is fixed for the block, then panned with left/right
// gains linearly ramped from where the last block left them to this block's target -- which is
// what stops a player running past you from clicking.
static void voice_render(float *out, int frames) {
    static float scratch[4096 * 2];
    SDL_LockMutex(g_v.lock);
    float vol = g_v.volume;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        Speaker *sp = &g_v.sp[s];
        if (!sp->used) continue;
        uint64_t avail = sp->wr - sp->rd;
        if (avail == 0 && sp->cur_l == 0 && sp->cur_r == 0) continue;

        float a = 1.0f - expf(-2.0f * 3.14159265f * sp->want_cut / (float)VOICE_RATE);
        float l0 = sp->cur_l, r0 = sp->cur_r;
        float l1 = sp->want_l * vol, r1 = sp->want_r * vol;
        float dl = (l1 - l0) / (float)frames, dr = (r1 - r0) / (float)frames;
        float peak = 0;
        for (int i = 0; i < frames; i++) {
            float x = 0;
            if (sp->rd < sp->wr) { x = sp->pcm[sp->rd % VOICE_RING]; sp->rd++; }
            sp->lp += a * (x - sp->lp) + 1e-20f;
            float gl = l0 + dl * (float)i, gr = r0 + dr * (float)i;
            float ol = sp->lp * gl, orr = sp->lp * gr;
            scratch[i * 2 + 0] = ol; scratch[i * 2 + 1] = orr;
            out[i * 2 + 0] += ol; out[i * 2 + 1] += orr;
            float m = fabsf(ol) > fabsf(orr) ? fabsf(ol) : fabsf(orr);
            if (m > peak) peak = m;
        }
        sp->cur_l = l1; sp->cur_r = r1;
        sp->level += 0.25f * (clamp01f(peak * 4.0f) - sp->level);
        // HOLLOW_VOICE_DUMP: this one speaker, exactly as the local mixer heard them -- after
        // distance, occlusion and pan, and stereo because the pan is half the point. This is file I/O on the audio thread, which is precisely
        // what a shipping build must not do; it exists only behind the test hook.
        if (sp->dump.open) voice_wav_write(&sp->dump, scratch, frames);
    }
    if (g_v.dump.open) voice_wav_write(&g_v.dump, out, frames);
    SDL_UnlockMutex(g_v.lock);
}

// The audio thread's entry point (see audio.h). Not used on a silent run.
static void voice_pull(float *out, int frames, void *user) {
    (void)user;
    if (!g_v.ok || frames <= 0) return;
    voice_render(out, frames > 4096 ? 4096 : frames);
}

// A silent run (HOLLOW_SILENT, or either voice test hook) opens no playback device, so nothing
// ever calls voice_pull. The whole mix still has to happen: the jitter buffers are clocked by the
// mixer draining their rings, and HOLLOW_VOICE_DUMP is written from inside the render. So on a
// silent run the main thread renders the same blocks at wall-clock speed and throws the samples
// away. Same code, same numbers in the dump, nothing reaches a speaker.
static void voice_render_offline(float dt) {
    static float sink[4096 * 2];
    static double owed = 0;
    owed += (double)dt * VOICE_RATE;
    while (owed >= 1.0) {
        int n = (int)owed; if (n > 4096) n = 4096;
        owed -= n;
        memset(sink, 0, (size_t)n * 2 * sizeof(float));
        voice_render(sink, n);
    }
}

// ---------------------------------------------------------------- capture

// Pull whatever the source has into g_v.mic, then hand out complete 20 ms frames.
static bool next_mic_frame(float *frame, float dt) {
    if (g_v.mic_n >= VOICE_FRAME) {
        memcpy(frame, g_v.mic, sizeof(float) * VOICE_FRAME);
        g_v.mic_n -= VOICE_FRAME;
        memmove(g_v.mic, g_v.mic + VOICE_FRAME, sizeof(float) * (size_t)g_v.mic_n);
        return true;
    }
    int room = (int)(sizeof g_v.mic / sizeof g_v.mic[0]) - g_v.mic_n;
    if (g_v.from_wav) {
        // The file stands in for a microphone, so it is read at wall-clock speed, not as fast as
        // the ticks come: dt seconds of tick is dt * 48000 samples of "microphone".
        if (!g_v.wav || g_v.wav_n <= 0) return false;
        int want = (int)(dt * (float)VOICE_RATE + 0.5f);
        if (want > room) want = room;
        for (int i = 0; i < want; i++) {
            int idx = (int)g_v.wav_pos;
            if (idx >= g_v.wav_n) { g_v.wav_pos = 0; idx = 0; }
            g_v.mic[g_v.mic_n++] = g_v.wav[idx];
            g_v.wav_pos += 1.0;
        }
    } else if (g_v.rec) {
        int have = SDL_GetAudioStreamAvailable(g_v.rec) / (int)sizeof(float);
        if (have > room) have = room;
        if (have > 0) {
            int got = SDL_GetAudioStreamData(g_v.rec, g_v.mic + g_v.mic_n, have * (int)sizeof(float));
            if (got > 0) g_v.mic_n += got / (int)sizeof(float);
        }
    }
    if (g_v.mic_n < VOICE_FRAME) return false;
    memcpy(frame, g_v.mic, sizeof(float) * VOICE_FRAME);
    g_v.mic_n -= VOICE_FRAME;
    memmove(g_v.mic, g_v.mic + VOICE_FRAME, sizeof(float) * (size_t)g_v.mic_n);
    return true;
}

// Should this frame go out? Push-to-talk is the key; open mic is an energy gate with a hangover so
// the ends of words survive; a WAV source has no keyboard, so it talks whenever it is loud enough.
static bool want_transmit(const Input *in, float rms, float dt) {
    if (g_v.mode == VOICE_OFF) return false;
    bool held = (g_v.mode == VOICE_PTT) ? (in && in->voice_ptt) : true;
    if (g_v.from_wav) held = true;
    if (g_v.mode == VOICE_PTT && !g_v.from_wav) return held;
    if (!held) return false;
    if (rms > VOICE_GATE_RMS) { g_v.gate_hang = VOICE_GATE_HANG; return true; }
    g_v.gate_hang -= dt;
    return g_v.gate_hang > 0;
}

// Push a produced frame into the local monitor ring so a solo player hears their own changed voice.
static void monitor_push(const float *frame) {
    Speaker *sp = &g_v.sp[g_v.local];
    SDL_LockMutex(g_v.lock);
    sp->used = true;
    if (sp->wr - sp->rd <= VOICE_RING - VOICE_FRAME) {
        for (int i = 0; i < VOICE_FRAME; i++) sp->pcm[(sp->wr + (uint64_t)i) % VOICE_RING] = frame[i];
        sp->wr += VOICE_FRAME;
    }
    sp->want_l = sp->want_r = g_v.monitor ? 0.7f : 0.0f;
    sp->want_cut = VOICE_OPEN_HZ;
    sp->last_heard = g_v.now;
    SDL_UnlockMutex(g_v.lock);
}

// ---------------------------------------------------------------- wire format
//
//   u8  slot   who is talking (the host stamps it; a client sends its own and the host overwrites)
//   u8  flags  bit 0: the speaker is dead -> heard by everyone in range at half volume
//   u16 seq    per-speaker frame counter, wraps
//   u16 t_ms   wall-clock milliseconds at capture, low 16 bits, for the latency readout
//   u8  len    Opus bytes that follow
static int write_block(uint8_t *out, int cap, int slot, uint8_t flags, uint16_t seq, uint16_t t_ms,
                       const uint8_t *opus, int len) {
    if (cap < VOICE_BLOCK_HDR + len || len <= 0 || len > 255) return 0;
    out[0] = (uint8_t)slot; out[1] = flags;
    out[2] = (uint8_t)(seq & 0xFF); out[3] = (uint8_t)(seq >> 8);
    out[4] = (uint8_t)(t_ms & 0xFF); out[5] = (uint8_t)(t_ms >> 8);
    out[6] = (uint8_t)len;
    memcpy(out + VOICE_BLOCK_HDR, opus, (size_t)len);
    return VOICE_BLOCK_HDR + len;
}
static bool read_block(const uint8_t *b, int len, int *slot, uint8_t *flags, uint16_t *seq,
                       uint16_t *t_ms, const uint8_t **data, int *dlen) {
    if (len < VOICE_BLOCK_HDR) return false;
    int n = b[6];
    if (n <= 0 || VOICE_BLOCK_HDR + n > len) return false;
    *slot = b[0]; *flags = b[1];
    *seq = (uint16_t)(b[2] | (b[3] << 8));
    *t_ms = (uint16_t)(b[4] | (b[5] << 8));
    *data = b + VOICE_BLOCK_HDR; *dlen = n;
    return true;
}

// A received block joins that speaker's jitter buffer. Never called for the local slot.
static void feed_speaker(Game *g, int slot, const uint8_t *body, int len) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS || slot == g_v.local) return;
    int s; uint8_t flags; uint16_t seq, t_ms; const uint8_t *data; int dlen;
    if (!read_block(body, len, &s, &flags, &seq, &t_ms, &data, &dlen)) return;
    Speaker *sp = &g_v.sp[slot];
    SDL_LockMutex(g_v.lock);
    if (!sp->used) {
        if (!voice_jitter_init(&sp->jit, VOICE_JITTER_MS)) { SDL_UnlockMutex(g_v.lock); return; }
        sp->used = true; sp->want_cut = VOICE_OPEN_HZ;
        if (g_v.dump.open) {
            char p[600]; snprintf(p, sizeof p, "%.*s.slot%d.wav",
                (int)(strlen(g_v.dump.path) > 4 ? strlen(g_v.dump.path) - 4 : strlen(g_v.dump.path)), g_v.dump.path, slot);
            voice_wav_open(&sp->dump, p, 2, VOICE_RATE);   // stereo: the pan is part of what the mixer heard
        }
        dbg_log("voice: speaker %d (%s) opened", slot, g->net.slots[slot].name);
    }
    voice_jitter_push(&sp->jit, seq, t_ms, flags, data, dlen);
    sp->transit_sum += since_ms(t_ms); sp->transit_n++;
    sp->last_heard = g_v.now;
    SDL_UnlockMutex(g_v.lock);
    g_v.s_down_bytes += (uint32_t)len;
}

// ---------------------------------------------------------------- net glue

int voice_pack_client(Game *g, uint8_t *out, int cap) {
    (void)g;
    if (!g_v.ok || g_v.nout == 0) return 0;
    int n = 0;
    while (g_v.nout > 0 && n + g_v.out[0].len <= cap) {
        memcpy(out + n, g_v.out[0].b, (size_t)g_v.out[0].len);
        n += g_v.out[0].len;
        g_v.nout--;
        memmove(g_v.out, g_v.out + 1, sizeof g_v.out[0] * (size_t)g_v.nout);
    }
    g_v.s_up_bytes += (uint32_t)n;
    return n;
}

// The host never decodes what it forwards: it rewrites one byte (the slot) and puts the same
// bytes on the wire to everyone else, so forwarding costs nothing but a memcpy.
static void host_forward(Game *g, int from_slot, uint8_t *block, int len) {
    NetGame *n = &g->net;
    block[0] = (uint8_t)from_slot;
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (i == from_slot) continue;
        NetSlot *s = &n->slots[i];
        if (!s->has_peer || !s->active) continue;
        int sent = net_send(&n->sock, &s->peer, NPT_VOICE, block, len, n->now);
        if (sent > 0) g_v.s_fwd_bytes += (uint32_t)sent;
    }
}

void voice_host_block(Game *g, int slot, const uint8_t *body, int len) {
    if (!g_v.ok || len < VOICE_BLOCK_HDR) return;
    // Blocks may be stacked in one payload; walk them.
    int off = 0;
    while (off + VOICE_BLOCK_HDR <= len) {
        int blen = VOICE_BLOCK_HDR + body[off + 6];
        if (blen <= VOICE_BLOCK_HDR || off + blen > len) break;
        uint8_t copy[VOICE_MAX_BLOCK];
        if (blen > (int)sizeof copy) break;
        memcpy(copy, body + off, (size_t)blen);
        host_forward(g, slot, copy, blen);
        feed_speaker(g, slot, copy, blen);
        off += blen;
    }
}

void voice_client_packet(Game *g, const uint8_t *body, int len) {
    if (!g_v.ok || len < VOICE_BLOCK_HDR) return;
    int off = 0;
    while (off + VOICE_BLOCK_HDR <= len) {
        int blen = VOICE_BLOCK_HDR + body[off + 6];
        if (blen <= VOICE_BLOCK_HDR || off + blen > len) break;
        feed_speaker(g, body[off], body + off, blen);
        off += blen;
    }
}

// ---------------------------------------------------------------- proximity
//
// Distance gain is a smoothstep between VOICE_NEAR (full) and VOICE_FAR (silent). A solid block
// between the two heads costs a low-pass at VOICE_MUFFLE_HZ and 30% of the level -- that is the
// whole "he is behind the wall" cue. Panning is the speaker's direction against the camera's
// right vector, equal-power, and never wider than VOICE_PAN_WIDTH so a voice at your shoulder
// still has a body. Dead players are heard by everyone in range at half volume (the hook is here;
// nothing sets hp to zero in the co-op game yet).
static void update_proximity(Game *g) {
    Vec3 ear = g->cam.eye;
    Vec3 fwd = v3_sub(g->cam.target, g->cam.eye); fwd.y = 0;
    float fl = v3_len(fwd);
    fwd = fl > 0.001f ? v3_scale(fwd, 1.0f / fl) : v3(0, 0, 1);
    Vec3 right = v3(-fwd.z, 0, fwd.x);

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Speaker *sp = &g_v.sp[i];
        if (!sp->used || i == g_v.local) continue;
        const Character *c = &g->players[i].c;
        Vec3 mouth = v3_add(c->pos, v3(0, c->height * 0.9f, 0));
        Vec3 d = v3_sub(mouth, ear);
        float dist = v3_len(d);
        float t = clamp01f((VOICE_FAR - dist) / (VOICE_FAR - VOICE_NEAR));
        float gain = t * t * (3.0f - 2.0f * t);
        if (!g->net.slots[i].active) gain = 0;
        if (c->hp <= 0.0f) gain *= VOICE_DEAD_GAIN;
        bool occ = level_ray_solid(&g->level, ear, mouth, 0.0f) < 0.999f;
        if (occ) gain *= VOICE_MUFFLE_GAIN;
        float pan = 0;
        if (dist > 0.05f) pan = clampf(v3_dot(v3_scale(d, 1.0f / dist), right), -1, 1) * VOICE_PAN_WIDTH;
        // equal power: -1 hard left, +1 hard right
        float ang = (pan + 1.0f) * 0.25f * 3.14159265f;
        SDL_LockMutex(g_v.lock);
        sp->want_l = gain * cosf(ang);
        sp->want_r = gain * sinf(ang);
        sp->want_cut = occ ? VOICE_MUFFLE_HZ : VOICE_OPEN_HZ;
        sp->occluded = occ; sp->dist = dist; sp->gain = gain;
        SDL_UnlockMutex(g_v.lock);
    }
}

// Keep every speaker's ring VOICE_RING_WANT frames ahead of the mixer. This is what clocks the
// jitter buffers: the mixer drains at exactly 48 kHz, so this pulls at exactly 50 frames a second.
static void top_up_rings(void) {
    float frame[VOICE_FRAME];
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Speaker *sp = &g_v.sp[i];
        if (!sp->used || i == g_v.local) continue;
        for (int guard = 0; guard < 4; guard++) {
            SDL_LockMutex(g_v.lock);
            uint64_t queued = sp->wr - sp->rd;
            SDL_UnlockMutex(g_v.lock);
            if (queued >= (uint64_t)VOICE_FRAME * VOICE_RING_WANT) break;
            uint16_t t_ms = 0; uint8_t flags = 0;
            VjResult r = voice_jitter_pull(&sp->jit, frame, &t_ms, &flags);
            // Only a really decoded frame carries a real capture timestamp: FEC and PLC frames
            // have one synthesised by the jitter buffer, and a silent one has none at all.
            if (r == VJ_DECODED) {
                float lat = since_ms(t_ms) + (float)queued / (float)(VOICE_RATE / 1000);
                sp->lat_sum += lat; sp->lat_n++; sp->lat_last = lat;
            }
            SDL_LockMutex(g_v.lock);
            for (int k = 0; k < VOICE_FRAME; k++) sp->pcm[(sp->wr + (uint64_t)k) % VOICE_RING] = frame[k];
            sp->wr += VOICE_FRAME;
            SDL_UnlockMutex(g_v.lock);
        }
    }
}

// ---------------------------------------------------------------- stats

// dbg_log caps a line at ~148 characters, so this is one line for the process and one per
// speaker rather than one long one.
static void stats(Game *g, float dt) {
    g_v.stat_t += dt;
    if (g_v.stat_t < 1.0) return;
    dbg_log("voice: %s vol %.2f%s tx %u up %.2f down %.2f fwd %.2f kB/s",
            voice_mode_name(), (double)g_v.volume, g_v.monitor ? " monitor" : "",
            g_v.s_tx_frames, g_v.s_up_bytes / 1024.0, g_v.s_down_bytes / 1024.0,
            g_v.s_fwd_bytes / 1024.0);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Speaker *sp = &g_v.sp[i];
        if (!sp->used || i == g_v.local) continue;
        dbg_log("voice s%d %s: %.1f m gain %.2f%s | in %u rx %u fec %u plc %u mute %u late %u ovf %u sync %u q %d | %.0f/%.0f ms",
                i, g->net.slots[i].name, (double)sp->dist, (double)sp->gain,
                sp->occluded ? " muffled" : "",
                sp->jit.n_pushed, sp->jit.n_decoded, sp->jit.n_fec, sp->jit.n_concealed,
                sp->jit.n_silent, sp->jit.n_late, sp->jit.n_overflow, sp->jit.n_resync,
                voice_jitter_depth(&sp->jit),
                sp->transit_n ? (double)(sp->transit_sum / (float)sp->transit_n) : 0.0,
                sp->lat_n ? (double)(sp->lat_sum / (float)sp->lat_n) : 0.0);
        sp->jit.n_pushed = sp->jit.n_decoded = sp->jit.n_fec = sp->jit.n_concealed = 0;
        sp->jit.n_silent = sp->jit.n_late = sp->jit.n_dup = 0;
        sp->jit.n_overflow = sp->jit.n_resync = 0;
        sp->lat_sum = 0; sp->lat_n = 0; sp->transit_sum = 0; sp->transit_n = 0;
    }
    g_v.stat_t = 0; g_v.s_up_bytes = g_v.s_down_bytes = g_v.s_fwd_bytes = 0; g_v.s_tx_frames = 0;
}

// The local player's character file decides the voice. Checked once a second so a hot-reloaded
// `voice` line takes effect without a restart.
static void follow_character(Game *g) {
    static double next = 0;
    if (g_v.now < next) return;
    next = g_v.now + 1.0;
    const CharSpec *sp = &g->player_models[g->local].spec;
    if (!sp->has_voice) return;
    VoicePreset p = { sp->voice_pitch, sp->voice_formant, sp->voice_effect };
    voice_set_preset(&p);
}

// ---------------------------------------------------------------- per tick

void voice_update(Game *g, const Input *in, float dt) {
    if (!g_v.ok) return;
    g_v.now += dt;
    g_v.local = g->net.local;
    open_microphone();   // no-op unless the mode was turned on after startup
    follow_character(g);

    // 1. capture -> voice changer -> encode -> outbox
    float frame[VOICE_FRAME];
    int made = 0;
    // The gate is decided once a tick (it has a time constant), from the level of the last frame
    // that arrived, so the HUD dot is right even on a tick where no whole frame was ready.
    g_v.tx = want_transmit(in, g_v.last_rms, dt);
    while (made < 4 && next_mic_frame(frame, made == 0 ? dt : 0.0f)) {
        made++;
        g_v.last_rms = voice_dsp_rms(frame, VOICE_FRAME);
        g_v.mic_level += 0.2f * (clamp01f(g_v.last_rms * 12.0f) - g_v.mic_level);
        if (!g_v.tx) continue;
        voice_dsp_process(&g_v.dsp, frame, VOICE_FRAME);
        if (g_v.monitor) monitor_push(frame);
        uint8_t opus[VOICE_MAX_PACKET];
        int n = opus_encode_float(g_v.enc, frame, VOICE_FRAME, opus, (int)sizeof opus);
        if (n <= 1) continue;                     // 1 byte = DTX/"nothing to send"
        uint8_t flags = (g->players[g_v.local].c.hp <= 0.0f) ? 1u : 0u;
        uint8_t block[VOICE_MAX_BLOCK];
        int blen = write_block(block, (int)sizeof block, g_v.local, flags, g_v.seq++, wall_ms16(), opus, n);
        if (blen <= 0) continue;
        g_v.s_tx_frames++;
        if (g->net.mode == NM_HOST) {
            host_forward(g, g_v.local, block, blen);
            g_v.s_up_bytes += (uint32_t)blen;
        } else if (g->net.mode == NM_CLIENT) {
            if (g_v.nout == VOICE_OUTBOX) { memmove(g_v.out, g_v.out + 1, sizeof g_v.out[0] * (VOICE_OUTBOX - 1)); g_v.nout--; }
            g_v.out[g_v.nout].len = blen; memcpy(g_v.out[g_v.nout].b, block, (size_t)blen); g_v.nout++;
        }
    }

    // 2. proximity for everyone we can hear, then keep the mixer fed
    update_proximity(g);
    top_up_rings();
    if (g_v.offline) voice_render_offline(dt);

    // 3. retire speakers who stopped talking, so a slot that leaves does not sit in the mix
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Speaker *sp = &g_v.sp[i];
        if (!sp->used || i == g_v.local) continue;
        if (g_v.now - sp->last_heard > 3.0 && sp->jit.started) voice_jitter_reset(&sp->jit);
    }
    stats(g, dt);
}

// ---------------------------------------------------------------- hud

bool voice_transmitting(void) { return g_v.ok && g_v.tx; }
float voice_mic_level(void) { return g_v.mic_level; }
bool voice_speaking(int slot) {
    if (!g_v.ok || slot < 0 || slot >= NET_MAX_PLAYERS) return false;
    const Speaker *sp = &g_v.sp[slot];
    if (slot == g_v.local) return g_v.tx;
    return sp->used && (g_v.now - sp->last_heard) < VOICE_SPEAK_HOLD && sp->level > 0.01f;
}
float voice_speaker_level(int slot) {
    if (!g_v.ok || slot < 0 || slot >= NET_MAX_PLAYERS) return 0;
    return slot == g_v.local ? g_v.mic_level : g_v.sp[slot].level;
}

// A little speaker cone: a box, a flared mouth, and two arcs whose number grows with the level.
static void speaker_icon(Gfx *x, float px, float py, float lvl, Vec4 col) {
    gfx_ui_rect(x, px, py + 3, 3, 4, col);
    gfx_ui_rect(x, px + 3, py + 1, 2, 8, col);
    gfx_ui_rect(x, px + 5, py, 2, 10, col);
    if (lvl > 0.15f) gfx_ui_rect(x, px + 9, py + 3, 1, 4, col);
    if (lvl > 0.45f) gfx_ui_rect(x, px + 11, py + 1, 1, 8, col);
}

void voice_draw_hud(Game *g) {
    if (!g_v.ok || g_v.mode == VOICE_OFF) return;
    Gfx *x = &g->gfx;
    const float W = (float)INTERNAL_W, H = (float)INTERNAL_H;

    // Transmit dot, bottom left, brightness following the mic level.
    if (g_v.tx) {
        float a = 0.45f + 0.55f * clamp01f(g_v.mic_level);
        gfx_ui_rect(x, 14, H - 40, 8, 8, v4(1.0f, 0.35f, 0.3f, a));
        gfx_ui_text(x, 28, H - 41, 1.0f, v4(1.0f, 0.6f, 0.5f, a), g_v.from_wav ? "wav" : "talking");
    }

    // Name tags over the other goons, with a speaker icon while they are talking. There were no
    // name tags before voice; this is the minimal one, and it is the only thing that tells you
    // whose voice you are hearing.
    Mat4 vp = camera_view_proj(&g->cam, W / H);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == g->local || !g->net.slots[i].active) continue;
        const Character *c = &g->players[i].c;
        Vec3 head = v3_add(c->pos, v3(0, c->height * 1.15f, 0));
        float cx = vp.m[0] * head.x + vp.m[4] * head.y + vp.m[8] * head.z + vp.m[12];
        float cy = vp.m[1] * head.x + vp.m[5] * head.y + vp.m[9] * head.z + vp.m[13];
        float cw = vp.m[3] * head.x + vp.m[7] * head.y + vp.m[11] * head.z + vp.m[15];
        if (cw <= 0.1f) continue;
        float sx = (cx / cw * 0.5f + 0.5f) * W, sy = (0.5f - cy / cw * 0.5f) * H;
        if (sx < -40 || sx > W + 40 || sy < -20 || sy > H + 20) continue;
        bool talking = voice_speaking(i);
        float dist = v3_len(v3_sub(head, g->cam.eye));
        float fade = clamp01f((VOICE_FAR + 8.0f - dist) / 10.0f);
        if (fade <= 0.02f) continue;
        Vec4 tint = g->net.slots[i].tint;
        Vec4 col = talking ? v4(1.0f, 0.95f, 0.75f, fade)
                           : v4(0.55f + tint.x * 0.3f, 0.55f + tint.y * 0.3f, 0.55f + tint.z * 0.3f, fade * 0.65f);
        const char *nm = g->net.slots[i].name[0] ? g->net.slots[i].name : "goon";
        float tw = gfx_ui_text_width(1.1f, nm);
        float ix = talking ? 8.0f : 0.0f;
        gfx_ui_text(x, sx - (tw + ix) * 0.5f + ix, sy, 1.1f, col, nm);
        if (talking) speaker_icon(x, sx - (tw + ix) * 0.5f - 6, sy - 1, g_v.sp[i].level, col);
    }
}

// ---------------------------------------------------------------- life cycle

// The recording device is opened lazily and only when this process might actually talk: `voice off`
// must not light the operating system's microphone indicator, and a HOLLOW_VOICE_WAV run must not
// touch recording hardware at all. Tried at most once, so a machine with no microphone does not
// retry every tick.
static void open_microphone(void) {
    static bool tried = false;
    if (g_v.from_wav || g_v.rec || tried || g_v.mode == VOICE_OFF) return;
    tried = true;
    SDL_AudioSpec spec = { .format = SDL_AUDIO_F32, .channels = 1, .freq = VOICE_RATE };
    g_v.rec = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL);
    if (!g_v.rec) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "voice: no recording device (%s); you can listen but not talk", SDL_GetError());
    else { SDL_ResumeAudioStreamDevice(g_v.rec); dbg_log("voice: microphone open"); }
}

bool voice_init(Game *g) {
    // The settings are applied before init (main.c reads them long before the mixer exists), so
    // they have to survive the wipe.
    VoiceMode mode = g_v.mode; float vol = g_v.volume; bool mon = g_v.monitor;
    memset(&g_v, 0, sizeof g_v);
    g_v.mode = mode; g_v.volume = vol > 0 ? vol : 1.0f; g_v.monitor = mon;
    g_v.local = g->net.local;
    voice_dsp_init(&g_v.dsp, 1, 1, VFX_NONE);
    g_v.preset.pitch = 1; g_v.preset.formant = 1; g_v.preset.effect = VFX_NONE;

    g_v.lock = SDL_CreateMutex();
    if (!g_v.lock) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "voice: no mutex: %s", SDL_GetError()); return false; }

    int err = 0;
    g_v.enc = opus_encoder_create(VOICE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    if (!g_v.enc || err != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "voice: opus encoder failed (%d)", err);
        SDL_DestroyMutex(g_v.lock); g_v.lock = NULL; return false;
    }
    opus_encoder_ctl(g_v.enc, OPUS_SET_BITRATE(VOICE_BITRATE));
    opus_encoder_ctl(g_v.enc, OPUS_SET_VBR(1));
    opus_encoder_ctl(g_v.enc, OPUS_SET_VBR_CONSTRAINT(1));   // keeps the peak frame small enough to piggyback
    opus_encoder_ctl(g_v.enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(g_v.enc, OPUS_SET_PACKET_LOSS_PERC(20));
    opus_encoder_ctl(g_v.enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(g_v.enc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(g_v.enc, OPUS_SET_DTX(0));

    // The microphone, or a WAV standing in for one. With HOLLOW_VOICE_WAV set no recording device
    // is opened at all, so a headless test never trips the OS microphone permission prompt.
    const char *wav = SDL_getenv("HOLLOW_VOICE_WAV");
    if (wav && wav[0]) {
        g_v.wav = voice_wav_load(wav, &g_v.wav_n);
        if (!g_v.wav) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "voice: could not load HOLLOW_VOICE_WAV %s", wav);
        else { g_v.from_wav = true; dbg_log("voice: microphone replaced by %s (%.2f s)", wav, g_v.wav_n / 48000.0); }
    }
    open_microphone();

    const char *dump = SDL_getenv("HOLLOW_VOICE_DUMP");
    if (dump && dump[0]) {
        if (voice_wav_open(&g_v.dump, dump, 2, VOICE_RATE))
            dbg_log("voice: dumping the local voice bus to %s (and one .slotN.wav per speaker)", dump);
    }

    g_v.ok = true;
    g_v.offline = audio_silent();
    if (!g_v.offline) audio_set_voice_source(voice_pull, NULL);
    dbg_log("voice: ready, mode %s, %d kbps VBR, FEC on, %d ms jitter buffer, source %s, output %s",
            voice_mode_name(), VOICE_BITRATE / 1000, VOICE_JITTER_MS,
            g_v.from_wav ? "wav" : (g_v.rec ? "microphone" : "none"),
            g_v.offline ? "silent (no playback device; the dump is rendered on the main thread)" : "mixer");
    return true;
}

void voice_shutdown(void) {
    if (!g_v.ok) return;
    if (!g_v.offline) { audio_set_voice_source(NULL, NULL); SDL_Delay(30); }   // let one more audio block go by first
    g_v.ok = false;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        voice_wav_close(&g_v.sp[i].dump);
        if (g_v.sp[i].used && i != g_v.local) voice_jitter_free(&g_v.sp[i].jit);
    }
    voice_wav_close(&g_v.dump);
    if (g_v.rec) { SDL_DestroyAudioStream(g_v.rec); g_v.rec = NULL; }
    if (g_v.enc) { opus_encoder_destroy(g_v.enc); g_v.enc = NULL; }
    free(g_v.wav); g_v.wav = NULL;
    if (g_v.lock) { SDL_DestroyMutex(g_v.lock); g_v.lock = NULL; }
}
