#!/bin/bash
# Loudness-match every take (-14 LUFS integrated, -1 dBTP ceiling) for the edit.
set -e
cd "$(dirname "$0")/../public/takes"
for d in */; do
  n=${d%/}
  [ -f "$n/audio.wav" ] || continue
  I=$(ffmpeg -hide_banner -nostats -i "$n/audio.wav" -af ebur128 -f null - 2>&1 | grep -E "^\s+I:" | tail -1 | awk '{print $2}')
  G=$(python3 -c "print(round(min(12.0, -14.0 - float('$I')), 2))")
  ffmpeg -v error -y -i "$n/audio.wav" -af "volume=${G}dB,alimiter=limit=0.89:attack=2:release=60:level=disabled" -ar 48000 "$n/norm.wav"
  echo "$n: $I LUFS -> gain $G dB"
done
