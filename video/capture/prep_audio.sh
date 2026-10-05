#!/bin/bash
# Loudness-match every take (-14 LUFS integrated, -1 dBTP ceiling) for the edit.
set -e
cd "$(dirname "$0")/../public/takes"
for d in */; do
  n=${d%/}
  [ -f "$n/audio.wav" ] || continue
  # NEON CHASE parts keep the SQ-4 master level, so the track's dynamics survive the edit.
  # Their first sample is padded to take time 0, so audio and frames share one clock.
  if [[ $n == neon-* ]]; then
    D=$(python3 -c "import json; print(json.load(open('$n/take.json'))['audio'][0]['offsetMs'])")
    ffmpeg -v error -y -i "$n/audio.wav" -af "adelay=${D}:all=1" -ar 48000 "$n/norm.wav"; echo "$n: raw, +${D} ms"; continue
  fi
  I=$(ffmpeg -hide_banner -nostats -i "$n/audio.wav" -af ebur128 -f null - 2>&1 | grep -E "^\s+I:" | tail -1 | awk '{print $2}')
  G=$(python3 -c "print(round(min(12.0, -14.0 - float('$I')), 2))")
  ffmpeg -v error -y -i "$n/audio.wav" -af "volume=${G}dB,alimiter=limit=0.89:attack=2:release=60:level=disabled" -ar 48000 "$n/norm.wav"
  echo "$n: $I LUFS -> gain $G dB"
done
