// SQ-4 session <-> JSON codec (web SessionDoc v:1 schema, src/seq/protocol.ts
// SessionDoc). Bodies live in SessionCodec.cpp. The JUCE layer owns base64
// (juce::Base64) and JSON (juce::JSON) — the pure model (SeqModel.h) stays
// JUCE-free.
#pragma once

#include <juce_core/juce_core.h>
#include "dsp/SeqModel.h"

namespace fable {

// Serialize a session to the web SessionDoc v:1 JSON string.
// `embedFactoryPatches` is for portable files/state: factory references are
// resolved into inline parameter maps. Internal preset-library comparisons use
// the compact default representation so their factory identity is preserved.
juce::String sessionToJson(const SessionData& session, bool embedFactoryPatches = false);

// Parse a SessionDoc v:1 JSON string. Returns true and fills `out` on success;
// returns false (leaving `out` untouched) on any shape/validation error so the
// caller can keep its current session.
bool sessionFromJson(const juce::String& json, SessionData& out);

} // namespace fable
