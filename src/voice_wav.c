// WAV in and out for the voice test hooks. See voice_wav.h.
#include "voice_wav.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Little-endian helpers so the file is byte-identical no matter the host's endianness, rather
// than relying on fwrite of a struct (which would flip on a big-endian machine).
static void wav_put_u32le(FILE *f, unsigned v) {
    unsigned char b[4] = {
        (unsigned char)(v & 0xffu), (unsigned char)((v >> 8) & 0xffu),
        (unsigned char)((v >> 16) & 0xffu), (unsigned char)((v >> 24) & 0xffu),
    };
    fwrite(b, 1, 4, f);
}

static void wav_put_u16le(FILE *f, unsigned v) {
    unsigned char b[2] = { (unsigned char)(v & 0xffu), (unsigned char)((v >> 8) & 0xffu) };
    fwrite(b, 1, 2, f);
}

// ---------------------------------------------------------------------- load

float *voice_wav_load(const char *path, int *out_frames) {
    if (out_frames) *out_frames = 0;
    if (!path) return NULL;

    SDL_AudioSpec src_spec;
    Uint8 *buf = NULL;
    Uint32 buf_len = 0;
    if (!SDL_LoadWAV(path, &src_spec, &buf, &buf_len)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "voice_wav_load: failed to load '%s': %s", path, SDL_GetError());
        return NULL;
    }

    SDL_AudioSpec dst_spec = { .format = SDL_AUDIO_F32, .channels = 1, .freq = 48000 };
    Uint8 *dst_data;
    int dst_len;
    if (src_spec.format == dst_spec.format && src_spec.channels == dst_spec.channels &&
        src_spec.freq == dst_spec.freq) {
        // Already in the target spec; no conversion needed.
        dst_data = buf;
        dst_len = (int)buf_len;
    } else {
        Uint8 *converted = NULL;
        int converted_len = 0;
        bool ok = SDL_ConvertAudioSamples(&src_spec, buf, (int)buf_len, &dst_spec, &converted, &converted_len);
        SDL_free(buf);
        if (!ok || !converted || converted_len <= 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "voice_wav_load: failed to convert '%s': %s", path, SDL_GetError());
            if (converted) SDL_free(converted);
            return NULL;
        }
        dst_data = converted;
        dst_len = converted_len;
    }

    int frame_count = dst_len / (int)sizeof(float);
    float *frames = (float *)malloc(sizeof(float) * (size_t)frame_count);
    if (!frames) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "voice_wav_load: out of memory loading '%s'", path);
        SDL_free(dst_data);
        return NULL;
    }
    memcpy(frames, dst_data, sizeof(float) * (size_t)frame_count);
    SDL_free(dst_data);

    SDL_Log("voice_wav_load: '%s' src %d Hz / %d ch -> %d frames mono 48kHz (%.2f s)",
            path, src_spec.freq, src_spec.channels, frame_count, (double)frame_count / 48000.0);

    if (out_frames) *out_frames = frame_count;
    return frames;
}

// --------------------------------------------------------------------- write

bool voice_wav_open(VoiceWavOut *w, const char *path, int channels, int rate) {
    if (!w || !path || channels <= 0 || rate <= 0) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    unsigned block_align = (unsigned)channels * 2u; // 16-bit samples
    unsigned byte_rate = (unsigned)rate * block_align;

    fwrite("RIFF", 1, 4, f);
    wav_put_u32le(f, 36); // placeholder RIFF chunk size, patched in voice_wav_close
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    wav_put_u32le(f, 16);          // fmt chunk size
    wav_put_u16le(f, 1);           // PCM
    wav_put_u16le(f, (unsigned)channels);
    wav_put_u32le(f, (unsigned)rate);
    wav_put_u32le(f, byte_rate);
    wav_put_u16le(f, block_align);
    wav_put_u16le(f, 16);          // bits per sample
    fwrite("data", 1, 4, f);
    wav_put_u32le(f, 0);           // placeholder data chunk size, patched in voice_wav_close

    w->f = f;
    w->channels = channels;
    w->rate = rate;
    w->frames = 0;
    w->open = true;
    snprintf(w->path, sizeof(w->path), "%s", path);
    return true;
}

// Single-threaded use only: no locking, so only ever call open/write/close on this writer from
// one thread.
void voice_wav_write(VoiceWavOut *w, const float *interleaved, int frames) {
    if (!w || !w->open || !w->f || !interleaved || frames <= 0) return;

    enum { WAV_CHUNK = 2048 }; // int16 values per stack-buffered write, so a big block never
                                // needs a heap allocation
    unsigned char bytes[WAV_CHUNK * 2];

    int total_samples = frames * w->channels;
    int pos = 0;
    while (pos < total_samples) {
        int n = total_samples - pos;
        if (n > WAV_CHUNK) n = WAV_CHUNK;
        for (int i = 0; i < n; i++) {
            float s = interleaved[pos + i];
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            long v = lrintf(s * 32767.0f);
            if (v > 32767) v = 32767;
            else if (v < -32768) v = -32768;
            unsigned uv = (unsigned)(int)v;
            bytes[i * 2 + 0] = (unsigned char)(uv & 0xffu);
            bytes[i * 2 + 1] = (unsigned char)((uv >> 8) & 0xffu);
        }
        fwrite(bytes, 1, (size_t)n * 2, w->f);
        pos += n;
    }

    w->frames += (unsigned)frames;
}

void voice_wav_close(VoiceWavOut *w) {
    if (!w) return;
    if (!w->open || !w->f) {
        w->open = false;
        w->f = NULL;
        return;
    }

    unsigned data_bytes = w->frames * (unsigned)w->channels * 2u;
    unsigned riff_size = 36 + data_bytes;

    fflush(w->f);
    fseek(w->f, 4, SEEK_SET);
    wav_put_u32le(w->f, riff_size);
    fseek(w->f, 40, SEEK_SET);
    wav_put_u32le(w->f, data_bytes);

    fclose(w->f);
    w->f = NULL;
    w->open = false;
}
