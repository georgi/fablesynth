// SQ-4 clip-clipboard <-> JSON codec. The grid's copy/cut verbs serialise the
// selected cell rectangle to a tagged JSON document that rides
// juce::SystemClipboard, giving cross-instance paste for free:
//   {"fable":"sq4-clips","v":1,"machines":["DR1",...],
//    "cells":[[{"name":...,"bars":n,"pattern":"<b64>"}|null,...],...]}
// `machines` tags each COLUMN with its source track's machine so paste can
// skip machine-mismatched columns (editing-concept: no partial corruption).
// Same layering as SessionCodec: JUCE owns base64/JSON, the payload carries
// decoded ClipData. Bodies live in ClipClipboardCodec.cpp.
#pragma once

#include <juce_core/juce_core.h>
#include "dsp/SeqModel.h"

namespace fable {

// A rectangle of cells: cells[row][col], hasCell mirroring SceneData's
// hasClip parallel-vector idiom (null slots are legal — empty cells copy as
// null and paste as delete-nothing/skip). Every row spans machines.size()
// columns.
struct ClipClipboardData {
    std::vector<Machine> machines;              // one per column
    std::vector<std::vector<ClipData>> cells;   // rows x cols
    std::vector<std::vector<bool>> hasCell;     // rows x cols
};

// Serialize to the tagged JSON document above.
juce::String clipClipboardToJson(const ClipClipboardData& data);

// Parse a tagged clipboard document. Returns true and fills `out` only when
// the tag matches AND every cell validates (bars in range, byte count ==
// bars * sqBytesPerBar(column machine), row widths consistent); returns
// false (leaving `out` untouched) otherwise, so foreign clipboard text is
// ignored wholesale.
bool clipClipboardFromJson(const juce::String& json, ClipClipboardData& out);

} // namespace fable
