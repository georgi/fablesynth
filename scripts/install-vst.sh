#!/usr/bin/env bash
# Build all JUCE plugin targets and install VST3/AU bundles into the system plugin folders.
set -euo pipefail

JUCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/juce"
BUILD_DIR="$JUCE_DIR/build"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
COMPONENT_DEST="$HOME/Library/Audio/Plug-Ins/Components"

if [ ! -d "$BUILD_DIR" ]; then
  cmake -S "$JUCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

cmake --build "$BUILD_DIR" --parallel

mkdir -p "$VST3_DEST" "$COMPONENT_DEST"

for vst3 in "$BUILD_DIR"/*_artefacts/Release/VST3/*.vst3; do
  [ -e "$vst3" ] || continue
  name="$(basename "$vst3")"
  echo "Installing $name -> $VST3_DEST"
  rm -rf "${VST3_DEST:?}/$name"
  cp -R "$vst3" "$VST3_DEST/"
done

for component in "$BUILD_DIR"/*_artefacts/Release/AU/*.component; do
  [ -e "$component" ] || continue
  name="$(basename "$component")"
  echo "Installing $name -> $COMPONENT_DEST"
  rm -rf "${COMPONENT_DEST:?}/$name"
  cp -R "$component" "$COMPONENT_DEST/"
done

echo "Done."
