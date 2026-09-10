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
    }

    j->q[slot].used  = true;
    j->q[slot].seq   = seq;
    j->q[slot].t_ms  = t_ms;
    j->q[slot].flags = flags;
    j->q[slot].len   = len;
    memcpy(j->q[slot].data, data, (size_t)len);
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
        depth = voice_jitter_depth(j);
    }

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

    // Neither the packet nor its FEC carrier is here. Conceal, but never more than a few frames
    // in a row -- past that the guess is worthless and we would rather go quiet than hallucinate.
    if (j->conceal_run >= 3) {
        j->conceal_run = 0;
        j->next_seq++;   // still move the cursor forward, in step with real time
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
