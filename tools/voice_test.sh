#!/usr/bin/env bash
# Four processes on loopback, one of them talking, and a WAV of what the host heard.
#
# Nobody needs a microphone: every process gets HOLLOW_VOICE_WAV, which makes voice.c read a file
# instead of opening a recording device (so macOS never shows the permission prompt either). Only
# c1 has `--voice open`, so exactly one client is talking and the host's per-speaker dump is that
# client alone, after distance, occlusion and pan.
#
# The host's distance from the talker is the experiment. `--spawn X Z` moves the host, and the host
# is authoritative over its own position, so it stays exactly where it is put. c1, the one talking,
# is deliberately NOT a bot: it stands at the spawn, so the distance is fixed for the whole run and
# three runs can be compared directly. c2 and c3 do wander, so the host is always mixing one voice
# among four moving bodies.
#
#   tools/voice_test.sh near        host ~1.5 m from the talker: the top of the curve
#   tools/voice_test.sh mid         host ~12 m away
#   tools/voice_test.sh far         host ~20 m away: nearly the 25 m cutoff
#   tools/voice_test.sh wall        corridor level, with solid geometry on the line of sight
#   tools/voice_test.sh loss        near, with HOLLOW_NET_LOSS=0.2 on all four processes
#   tools/voice_test.sh all         near, mid and far in turn, then the attenuation table
#
# TALKER=goon_c picks the voice c1 talks in (default goon_a).
#
# Nothing here is ever audible: HOLLOW_SILENT=1 (and either voice test hook on its own) stops the
# game opening a playback device at all, and voice.c renders its bus on the main thread instead,
# so the dump WAVs are identical to what you would have heard.
#
# Writes to /tmp/voice/<case>/: host.wav (the whole local voice bus, stereo), host.wav.slotN.wav
# (one per speaker, mono, post-proximity) and the four hollow_*.log files.
set -u
cd "$(dirname "$0")/.."
CASE="${1:-near}"
SECS="${2:-25}"
export HOLLOW_SILENT=1        # hard guard: no process in this test opens a playback device
export HOLLOW_FPS=60          # one frame per tick, so --frames N is N/60 seconds of wall clock
FRAMES=$((SECS * 60))
OUT="/tmp/voice/$CASE"
BIN="${BIN:-./build/bin/goonstein}"
WAV="$PWD/assets/audio/voice_test.wav"
LEVEL="${HOLLOW_TEST_LEVEL:-lantern}"

if [ "$CASE" = all ]; then
  for c in near mid far; do "$0" "$c" "$SECS" || exit 1; done
  echo
  echo "--- proximity: the same sentence at three distances ---"
  python3 tools/voice_check.py distance /tmp/voice/near /tmp/voice/mid /tmp/voice/far
  exit 0
fi

case "$CASE" in
  near) SPAWN="--spawn 0 -3";  LOSS="" ;;
  mid)  SPAWN="--spawn 12 -3"; LOSS="" ;;
  far)  SPAWN="--spawn 20 -3"; LOSS="" ;;
  # Occlusion needs actual `block` geometry, and the lantern level is all props: corridor is built
  # out of them. Note what is doing the listening -- the camera eye, not the body, which in third
  # person sits several metres back and above. That is deliberate (you hear from where you are
  # looking) but it means the two spawns below were arrived at by measurement, not by reading the
  # level file: at 0 8 the eye's line to the talker crosses the corridor's structure and the log
  # says `muffled`. Compare it with a `near` run at the same TALKER for the unmuffled version --
  # inside a tunnel with a ceiling there is no reliably clear line to compare against.
  wall)  SPAWN="--spawn 0 8";   LOSS=""; LEVEL=corridor ;;
  loss) SPAWN="--spawn 0 -3";  LOSS="0.2" ;;
  *) echo "usage: $0 near|mid|far|wall|loss|all [seconds]"; exit 2 ;;
esac

rm -rf "$OUT"; mkdir -p "$OUT"
[ -x "$BIN" ] || { echo "build first: cmake --build build -j8"; exit 1; }
[ -f "$WAV" ] || { echo "missing $WAV"; exit 1; }
[ -n "$LOSS" ] && export HOLLOW_NET_LOSS="$LOSS"

# The host listens and dumps. --voice off means it never transmits; receiving is always on.
HOLLOW_VOICE_WAV="$WAV" HOLLOW_VOICE_DUMP="$OUT/host.wav" \
  "$BIN" --volume 0 --host 7777 --start "level:$LEVEL" --third --voice off \
         --frames "$FRAMES" --log "$OUT/hollow_host.log" $SPAWN &
HOST=$!
sleep 6

# c1 talks. c2 and c3 are just bodies moving around, so the host is mixing one voice among four.
# c1 talks and stands still (no --bot), so the distance to the host is fixed for the whole run.
# --hero pins its character too: without it the talker's voice preset follows whichever slot the
# host happened to hand out, and two runs are then not comparable (goon_b's radio preset is several
# dB hotter than goon_d's ring).
HOLLOW_VOICE_WAV="$WAV" \
  "$BIN" --volume 0 --join 127.0.0.1:7777 --name c1 --start "level:$LEVEL" --first \
         --hero "${TALKER:-goon_a}" --voice open --frames $((FRAMES - 400)) \
         --log "$OUT/hollow_c1.log" &
for i in 2 3; do
  HOLLOW_VOICE_WAV="$WAV" \
    "$BIN" --volume 0 --join 127.0.0.1:7777 --bot --name "c$i" --start "level:$LEVEL" --first \
           --voice off --frames $((FRAMES - 400)) --log "$OUT/hollow_c$i.log" &
done
wait

echo "--- voice log lines (host) ---"
grep -h "voice:" "$OUT/hollow_host.log" | tail -12
echo "--- voice log lines (c1, the talker) ---"
grep -h "voice:" "$OUT/hollow_c1.log" | tail -4
echo "--- dumps ---"
ls -la "$OUT"/*.wav 2>/dev/null || echo "no dumps written"
for f in "$OUT"/host.wav.slot*.wav; do
  [ -f "$f" ] || continue
  python3 tools/voice_check.py gaps "$f"
  python3 tools/voice_check.py pitch "$f"
done
