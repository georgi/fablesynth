// SQ-4 session <-> JSON codec (web SessionDoc v:1 schema, src/seq/protocol.ts
// SessionDoc). Declared here so Task 14 can move/extend the tests; the bodies
// live in SeqProcessor.cpp for now. The JUCE layer owns base64 (juce::Base64)
// and JSON (juce::JSON) — the pure model (SeqModel.h) stays JUCE-free.
#pragma once

#include <juce_core/juce_core.h>
#include "dsp/SeqModel.h"

namespace fable {

// Serialize a session to the web SessionDoc v:1 JSON string.
juce::String sessionToJson(const SessionData& session);

// Parse a SessionDoc v:1 JSON string. Returns true and fills `out` on success;
// returns false (leaving `out` untouched) on any shape/validation error so the
// caller can keep its current session.
bool sessionFromJson(const juce::String& json, SessionData& out);

} // namespace fable
