#!/usr/bin/env bash
# Four processes on loopback, one of them talking, and a WAV of what the host heard.
#
# Nobody needs a microphone: every process gets HOLLOW_VOICE_WAV, which makes voice.c read a file
# instead of opening a recording device (so macOS never shows the permission prompt either). Only
# c1 has `--voice open`, so exactly one client is talking and the host's per-speaker dump is that
# client alone, after distance, occlusion and pan.
#
# The host's distance from the talker is the experiment. `--spawn X Z` moves the host, and the host
# is authoritative over its own position, so it stays where it is put while the bot clients wander
# around the level spawn.
#
#   tools/voice_test.sh near        host at the spawn, ~2 m from the talker
#   tools/voice_test.sh far         host 20 m away, out at the edge of the curve
#   tools/voice_test.sh loss        near, with HOLLOW_NET_LOSS=0.2 on all four processes
#
# Writes to /tmp/voice/<case>/: host.wav (the whole local voice bus, stereo), host.wav.slotN.wav
# (one per speaker, mono, post-proximity) and the four hollow_*.log files.
set -u
cd "$(dirname "$0")/.."
CASE="${1:-near}"
SECS="${2:-25}"
FRAMES=$((SECS * 60))
OUT="/tmp/voice/$CASE"
BIN=./build/bin/goonstein
WAV="$PWD/assets/audio/voice_test.wav"
LEVEL="${HOLLOW_TEST_LEVEL:-lantern}"

case "$CASE" in
  near) SPAWN=""; LOSS="" ;;
  far)  SPAWN="--spawn 20 20"; LOSS="" ;;
  loss) SPAWN=""; LOSS="0.2" ;;
  *) echo "usage: $0 near|far|loss [seconds]"; exit 2 ;;
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
HOLLOW_VOICE_WAV="$WAV" \
  "$BIN" --volume 0 --join 127.0.0.1:7777 --bot --name c1 --start "level:$LEVEL" --first \
         --voice open --frames $((FRAMES - 400)) --log "$OUT/hollow_c1.log" &
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
