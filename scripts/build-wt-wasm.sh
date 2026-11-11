#!/usr/bin/env bash
# Build the WT-1 C++ engine (juce/source/dsp) to one standalone .wasm
# for src/engine/worklet-wasm.js. See docs/wasm-wt1.md.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=src/engine/wasm/wt1.wasm
mkdir -p "$(dirname "$OUT")"
SRC=(
  juce/source/dsp/wasm/wt_wasm.cpp
  juce/source/dsp/Engine.cpp
  juce/source/dsp/Fx.cpp
  juce/source/dsp/Params.cpp
  juce/source/dsp/Wavetables.cpp
)
# Export exactly the WT_EXPORT C API.
EXPORTS=$(grep -o '^WT_EXPORT [^(]*' juce/source/dsp/wasm/wt_wasm.cpp | awk '{print "_"$NF}' | sed 's/^_\*/_/' | paste -sd, -)
em++ -std=c++20 -O3 -flto -fno-exceptions -fno-rtti -DNDEBUG \
  -I juce/source \
  --no-entry -sSTANDALONE_WASM=1 \
  -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=128MB -sSTACK_SIZE=1MB \
  -sMALLOC=emmalloc -sSUPPORT_LONGJMP=0 \
  -sEXPORTED_FUNCTIONS="$EXPORTS" \
  "${SRC[@]}" -o "$OUT"
ls -l "$OUT" | awk '{print "wt1.wasm", $5, "bytes"}'
gzip -9c "$OUT" | wc -c | awk '{print "gzipped", $1, "bytes"}'
