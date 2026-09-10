#include "voice_jitter.h"
#include <opus.h>
#include <SDL3/SDL.h>
#include <string.h>

// Never compare u16 sequence numbers with < or > directly -- wrap safely through int16_t.
static int seq_diff(uint16_t a, uint16_t b) { return (int16_t)(a - b); }

static int find_slot(const VoiceJitter *j, uint16_t seq) {
    for (int i = 0; i < VJ_SLOTS; i++) {
        if (j->q[i].used && j->q[i].seq == seq) return i;
    }
    return -1;
}

// Logs an opus_decode_float failure, at most once a second so a broken stream cannot flood the
// log.
static void warn_decode_error(VoiceJitter *j, int err) {
    uint64_t now = SDL_GetTicks();
    if (now - j->last_warn_ms >= 1000) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "voice_jitter: opus_decode_float failed: %s",
                    opus_strerror(err));
        j->last_warn_ms = now;
    }
}

bool voice_jitter_init(VoiceJitter *j, int target_ms) {
    memset(j, 0, sizeof(*j));
    if (target_ms < 40)  target_ms = 40;
    if (target_ms > 200) target_ms = 200;

    int err = 0;
    OpusDecoder *dec = opus_decoder_create(VOICE_RATE, 1, &err);
    if (!dec || err != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "voice_jitter: opus_decoder_create failed: %s",
                    opus_strerror(err));
        return false;
    }
    j->dec = dec;
    j->target_frames = target_ms / VOICE_FRAME_MS;
    if (j->target_frames < 1) j->target_frames = 1;
    return true;
}

void voice_jitter_free(VoiceJitter *j) {
    if (j->dec) opus_decoder_destroy((OpusDecoder *)j->dec);
    memset(j, 0, sizeof(*j));
}

void voice_jitter_reset(VoiceJitter *j) {
    for (int i = 0; i < VJ_SLOTS; i++) j->q[i].used = false;
    j->started     = false;
    j->next_seq    = 0;
    j->conceal_run = 0;
    // dec, target_frames and the cumulative counters survive a reset.
}

void voice_jitter_push(VoiceJitter *j, uint16_t seq, uint16_t t_ms, uint8_t flags,
                        const uint8_t *data, int len) {
    if (len <= 0 || len > VOICE_MAX_PACKET) return;
    j->n_pushed++;

    bool any_queued = false;
    for (int i = 0; i < VJ_SLOTS; i++) {
        if (j->q[i].used) { any_queued = true; break; }
    }
    // The first packet after a reset (or after the buffer ran completely dry) sets the cursor.
    if (!j->started && !any_queued) j->next_seq = seq;

    if (find_slot(j, seq) >= 0) { j->n_dup++; return; }
    if (j->started && seq_diff(seq, j->next_seq) < 0) { j->n_late++; return; }

    int slot = -1;
    for (int i = 0; i < VJ_SLOTS; i++) {
        if (!j->q[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        // Evict the oldest queued packet: the lowest seq relative to next_seq.
        slot = 0;
        int oldest_diff = seq_diff(j->q[0].seq, j->next_seq);
        for (int i = 1; i < VJ_SLOTS; i++) {
            int d = seq_diff(j->q[i].seq, j->next_seq);
            if (d < oldest_diff) { oldest_diff = d; slot = i; }
        }
        j->n_overflow++;
        // The cursor cannot wait for a packet that has just been thrown away, and the evicted
        // one is by construction the packet closest to the cursor. Step past it, or a burst of
        // arrivals (a client catching up after a stall) evicts the cursor's packet every time and
        // the buffer conceals for ever while staying full. This is the one failure mode a
        // fixed-size reordering buffer has, and it is why the four-process test measures
        // continuity and not just packet counts.
        if (j->started && seq_diff(j->q[slot].seq, j->next_seq) >= 0) {
            j->next_seq = (uint16_t)(j->q[slot].seq + 1);
            j->conceal_run = 0;
            j->n_resync++;
        }
    }

    j->q[slot].used  = true;
    j->q[slot].seq   = seq;
    j->q[slot].t_ms  = t_ms;
    j->q[slot].flags = flags;
    j->q[slot].len   = len;
    memcpy(j->q[slot].data, data, (size_t)len);
}

// The sequence number of the oldest packet still queued (undefined when nothing is queued).
static uint16_t oldest_queued(const VoiceJitter *j) {
    uint16_t best = j->next_seq; bool any = false;
    for (int i = 0; i < VJ_SLOTS; i++) {
        if (!j->q[i].used) continue;
        if (!any || seq_diff(j->q[i].seq, best) < 0) { best = j->q[i].seq; any = true; }
    }
    return best;
}
static uint16_t newest_queued(const VoiceJitter *j) {
    uint16_t best = j->next_seq; bool any = false;
    for (int i = 0; i < VJ_SLOTS; i++) {
        if (!j->q[i].used) continue;
        if (!any || seq_diff(j->q[i].seq, best) > 0) { best = j->q[i].seq; any = true; }
    }
    return best;
}

// Two ways the play cursor ends up somewhere useless, and one answer to both.
//
//   behind the queue  the stream resumed after a stall with a much higher sequence number, so the
//                     cursor conceals for ever while the queue overflows past it
//   too far behind    the sender ran ahead of us (a burst after a hiccup), so the buffer sits full
//                     and every word arrives late
//
// Either way, jump forward: to the oldest packet we still hold in the first case, and to the depth
// the buffer was asked for in the second. That is what bounds the delay at ~2x the target instead
// of letting it grow to the size of the queue.
static void resync_cursor(VoiceJitter *j) {
    if (!j->started || voice_jitter_depth(j) == 0) return;
    uint16_t oldest = oldest_queued(j), newest = newest_queued(j);
    // The cursor is outside the queue in one direction or the other: put it on the oldest packet
    // we still hold, then fall through to the depth rule, which may skip it forward again. Without
    // that second step a re-sync after a long stall replays a queue full of stale audio and the
    // speaker arrives a second and a half late.
    if (seq_diff(oldest, j->next_seq) > 0 || seq_diff(newest, j->next_seq) < 0) {
        j->next_seq = oldest; j->conceal_run = 0; j->n_resync++;
        newest = newest_queued(j);
    }
    if (seq_diff(newest, j->next_seq) + 1 > j->target_frames + 2) {
        j->next_seq = (uint16_t)(newest - (uint16_t)(j->target_frames - 1));
        j->conceal_run = 0; j->n_resync++;
    }
}

VjResult voice_jitter_pull(VoiceJitter *j, float *out, uint16_t *out_t_ms, uint8_t *out_flags) {
    int depth = voice_jitter_depth(j);

    if (!j->started) {
        if (depth < j->target_frames) {
            memset(out, 0, VOICE_FRAME * sizeof(float));
            j->n_silent++;
            return VJ_SILENT;
        }
        j->started = true;
        j->conceal_run = 0;
        j->next_seq = oldest_queued(j);   // begin at the oldest audio we actually hold
        depth = voice_jitter_depth(j);
    }
    resync_cursor(j);

    if (depth == 0) {
        // The speaker stopped talking, or the stream broke: go back to buffering from scratch.
        j->started     = false;
        j->conceal_run = 0;
        memset(out, 0, VOICE_FRAME * sizeof(float));
        j->n_silent++;
        return VJ_SILENT;
    }

    OpusDecoder *dec = (OpusDecoder *)j->dec;

    int slot = find_slot(j, j->next_seq);
    if (slot >= 0) {
        int n = opus_decode_float(dec, j->q[slot].data, j->q[slot].len, out, VOICE_FRAME, 0);
        if (out_t_ms)  *out_t_ms  = j->q[slot].t_ms;
        if (out_flags) *out_flags = j->q[slot].flags;
        j->last_t_ms   = j->q[slot].t_ms;
        j->last_flags  = j->q[slot].flags;
        j->q[slot].used = false;
        j->next_seq++;
        j->conceal_run = 0;
        if (n < 0) { warn_decode_error(j, n); memset(out, 0, VOICE_FRAME * sizeof(float)); }
        j->n_decoded++;
        return VJ_DECODED;
    }

    // The packet for next_seq is missing; the one right after it may carry FEC data for it.
    int fec_slot = find_slot(j, (uint16_t)(j->next_seq + 1));
    if (fec_slot >= 0) {
        int n = opus_decode_float(dec, j->q[fec_slot].data, j->q[fec_slot].len, out, VOICE_FRAME, 1);
        // Best-effort: the lost packet's own timestamp/flags are gone, so approximate from the
        // packet that carried its FEC data (one frame ahead of it).
        uint16_t t_ms  = (uint16_t)(j->q[fec_slot].t_ms - VOICE_FRAME_MS);
        uint8_t  flags = j->q[fec_slot].flags;
        if (out_t_ms)  *out_t_ms  = t_ms;
        if (out_flags) *out_flags = flags;
        j->last_t_ms  = t_ms;
        j->last_flags = flags;
        j->next_seq++;
        j->conceal_run = 0;
        if (n < 0) { warn_decode_error(j, n); memset(out, 0, VOICE_FRAME * sizeof(float)); }
        j->n_fec++;
        return VJ_FEC;
    }

    // Neither the packet nor its FEC carrier is here. Conceal, but never more than three frames in
    // a row: past 60 ms the guess is worthless, and more importantly this is no longer a hole in
    // the stream, it is the end of one. Go quiet and *hold the cursor where it is* -- advancing it
    // through silence is how a buffer walks off the end of a sender it can then never catch again,
    // because every packet that finally arrives looks "late" and gets thrown away. Holding means
    // the next pull re-buffers from scratch and snaps the cursor to the oldest packet we hold.
    if (j->conceal_run >= 3) {
        j->started = false;
        j->conceal_run = 0;
        memset(out, 0, VOICE_FRAME * sizeof(float));
        j->n_silent++;
        return VJ_SILENT;
    }

    int n = opus_decode_float(dec, NULL, 0, out, VOICE_FRAME, 0);
    uint16_t t_ms  = (uint16_t)(j->last_t_ms + VOICE_FRAME_MS);
    if (out_t_ms)  *out_t_ms  = t_ms;
    if (out_flags) *out_flags = j->last_flags;
    j->last_t_ms = t_ms;
    j->next_seq++;
    j->conceal_run++;
    if (n < 0) { warn_decode_error(j, n); memset(out, 0, VOICE_FRAME * sizeof(float)); }
    j->n_concealed++;
    return VJ_CONCEALED;
}

int voice_jitter_depth(const VoiceJitter *j) {
    int n = 0;
    for (int i = 0; i < VJ_SLOTS; i++) if (j->q[i].used) n++;
    return n;
}
