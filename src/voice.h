// Proximity voice chat.
//
// One 48 kHz mono microphone stream, cut into 20 ms frames. Every frame goes through this
// process's own character voice (pitch, formant, effect -- see the `voice` line in
// assets/characters/*.txt) *before* it is encoded, so the host and all three other players hear
// exactly the same voice and the shifter runs once per speaker instead of once per listener.
//
// The wire: client -> host -> everyone else, unreliable, never decoded by the host.
//   * client to host: the voice block is appended to the input packet the client already sends
//     every tick, so voice costs only its own bytes and no extra datagram.
//   * host to client: an NPT_VOICE packet, forwarded the moment it arrives, with the sender's
//     slot stamped in by the host.
//
// Playback: one jitter buffer and one Opus decoder per speaker, then proximity (distance gain,
// stereo pan around the camera, a one-pole muffle when a solid block is in the way) applied in
// the audio callback. The voice bus is added after the game's master volume, so `--volume 0`
// still talks.
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct Game;
struct Input;

#define VOICE_RATE        48000
#define VOICE_FRAME       960     // samples in 20 ms at 48 kHz
#define VOICE_FRAME_MS    20
#define VOICE_MAX_PACKET  400     // an Opus frame at 24 kbps is ~60 B; this is headroom
#define VOICE_BLOCK_HDR   7       // slot, flags, seq, t_ms, len
#define VOICE_MAX_BLOCK   (VOICE_BLOCK_HDR + VOICE_MAX_PACKET)

// Proximity curve: full volume within VOICE_NEAR, silent past VOICE_FAR.
#define VOICE_NEAR        4.0f
#define VOICE_FAR         25.0f
#define VOICE_DEAD_GAIN   0.5f    // a dead player is still heard, at half volume

typedef enum VoiceMode { VOICE_OFF = 0, VOICE_PTT = 1, VOICE_OPEN = 2 } VoiceMode;

// ---------------------------------------------------------------- settings
void  voice_set_mode(VoiceMode m);
void  voice_set_mode_name(const char *s);   // "ptt" | "open" | "off"; unknown names are ignored
const char *voice_mode_name(void);
void  voice_set_volume(float v);            // 0..2, independent of the game master volume
float voice_get_volume(void);
void  voice_set_monitor(bool on);           // hear your own changed voice locally

// ---------------------------------------------------------------- life cycle
// Opens the recording device (or the HOLLOW_VOICE_WAV file instead), the encoder, the decoders and
// the dump file, and hangs the voice bus off the audio mixer. Returns false only if voice is
// unusable; the game runs on regardless.
bool voice_init(struct Game *g);
void voice_shutdown(void);
bool voice_ready(void);

// Once per tick, before game_tick. Reads the microphone, runs the voice changer, encodes, queues
// the frame for sending, refreshes every speaker's proximity parameters and tops up the playback
// rings from the jitter buffers.
void voice_update(struct Game *g, const struct Input *in, float dt);

// ---------------------------------------------------------------- net glue
// All three are called from netgame.c inside `// --- voice ---` blocks.

// Client: append this tick's voice block (if any) to the input payload. Returns bytes written.
int  voice_pack_client(struct Game *g, uint8_t *out, int cap);
// Host: a voice block that rode in on slot `slot`'s input packet. Stamps the slot, forwards it to
// every other client untouched, and plays it locally. Never decodes on the forwarding path.
void voice_host_block(struct Game *g, int slot, const uint8_t *body, int len);
// Client: the payload of an NPT_VOICE packet from the host.
void voice_client_packet(struct Game *g, const uint8_t *body, int len);

// ---------------------------------------------------------------- hud
bool  voice_transmitting(void);      // the local microphone is live this instant
float voice_mic_level(void);         // 0..1 smoothed local input level (the HUD dot's brightness)
bool  voice_speaking(int slot);      // this slot has been heard in the last ~300 ms
float voice_speaker_level(int slot); // 0..1 smoothed loudness of that speaker after proximity

// ---------------------------------------------------------------- character preset
// Parsed from a character file's `voice PITCH FORMANT EFFECT` line and handed to the sender's DSP.
typedef struct VoicePreset { float pitch, formant; int effect; } VoicePreset;
// Re-read the local player's character file and rebuild the sender's voice changer. Called at
// startup and whenever the hero hot-reloads.
void voice_set_preset(const VoicePreset *p);

// ---------------------------------------------------------------- hud drawing
// The transmit dot and the name tags with a speaker icon over whoever is talking. Called from
// game.c's draw_hud inside a `// --- voice ---` block; draws nothing when voice is off.
void voice_draw_hud(struct Game *g);
