#!/usr/bin/env bash
# Build the BL-1 C++ engine (juce/source/bass/dsp) to one standalone .wasm
# for src/bass/engine/worklet-bass-wasm.js. See docs/wasm-bl1.md.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=src/bass/engine/wasm/bl1.wasm
mkdir -p "$(dirname "$OUT")"
SRC=(
  juce/source/bass/wasm/bass_wasm.cpp
  juce/source/bass/dsp/BassEngine.cpp
  juce/source/bass/dsp/BassFx.cpp
  juce/source/bass/dsp/BassParams.cpp
  juce/source/dsp/Params.cpp
  juce/source/dsp/Wavetables.cpp
  juce/source/dsp/Fx.cpp
)
# Export exactly the BL_EXPORT C API.
EXPORTS=$(grep -o '^BL_EXPORT [^(]*' juce/source/bass/wasm/bass_wasm.cpp | awk '{print "_"$NF}' | sed 's/^_\*/_/' | paste -sd, -)
em++ -std=c++20 -O3 -flto -fno-exceptions -fno-rtti -DNDEBUG \
  -I juce/source \
  --no-entry -sSTANDALONE_WASM=1 \
  -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB \
  -sMALLOC=emmalloc -sSUPPORT_LONGJMP=0 \
  -sEXPORTED_FUNCTIONS="$EXPORTS" \
  "${SRC[@]}" -o "$OUT"
ls -l "$OUT" | awk '{print "bl1.wasm", $5, "bytes"}'
gzip -9c "$OUT" | wc -c | awk '{print "gzipped", $1, "bytes"}'
