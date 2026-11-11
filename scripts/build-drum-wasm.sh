#!/usr/bin/env bash
# Build the DR-1 C++ engine (juce/source/drum/dsp) to one standalone .wasm
# for src/drum/engine/worklet-drum-wasm.js. See docs/wasm-dr1.md.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=src/drum/engine/wasm/dr1.wasm
mkdir -p "$(dirname "$OUT")"
SRC=(
  juce/source/drum/wasm/drum_wasm.cpp
  juce/source/drum/dsp/DrumEngine.cpp
  juce/source/drum/dsp/DrumFx.cpp
  juce/source/drum/dsp/DrumParams.cpp
  juce/source/drum/dsp/DrumRhythm.cpp
  juce/source/drum/dsp/OneShotSamples.gen.cpp
  juce/source/dsp/Params.cpp
  juce/source/dsp/Wavetables.cpp
  juce/source/dsp/Fx.cpp
)
# Export exactly the DR_EXPORT C API.
EXPORTS=$(grep -o '^DR_EXPORT [^(]*' juce/source/drum/wasm/drum_wasm.cpp | awk '{print "_"$NF}' | sed 's/^_\*/_/' | paste -sd, -)
em++ -std=c++20 -O3 -flto -fno-exceptions -fno-rtti -DNDEBUG \
  -I juce/source \
  --no-entry -sSTANDALONE_WASM=1 \
  -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=128MB -sSTACK_SIZE=1MB \
  -sMALLOC=emmalloc -sSUPPORT_LONGJMP=0 \
  -sEXPORTED_FUNCTIONS="$EXPORTS" \
  "${SRC[@]}" -o "$OUT"
ls -l "$OUT" | awk '{print "dr1.wasm", $5, "bytes"}'
gzip -9c "$OUT" | wc -c | awk '{print "gzipped", $1, "bytes"}'
