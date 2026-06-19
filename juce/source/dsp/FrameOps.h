// Pure frame-list operations for the wavetable editor (headless, JUCE-free, so
// they unit-test in engine_test). A frame is a SIZE-sample single cycle; a table
// is an ordered list of frames the engine morphs through via the POS param.
#pragma once
#include "UserTables.h" // SIZE (via Wavetables.h) + MAX_FRAMES
#include <vector>
#include <algorithm>

namespace fable {

using Frame = std::vector<float>;

// Insert a copy of frame `i` right after it (cap at MAX_FRAMES). Returns a copy.
inline std::vector<Frame> duplicateFrame(const std::vector<Frame>& frames, int i) {
    if (i < 0 || i >= (int)frames.size() || (int)frames.size() >= MAX_FRAMES) return frames;
    auto out = frames;
    out.insert(out.begin() + i + 1, frames[(size_t)i]);
    return out;
}

// Remove frame `i`; refuses to drop the last remaining frame. Returns a copy.
inline std::vector<Frame> deleteFrame(const std::vector<Frame>& frames, int i) {
    if ((int)frames.size() <= 1 || i < 0 || i >= (int)frames.size()) return frames;
    auto out = frames;
    out.erase(out.begin() + i);
    return out;
}

// Move frame from `from` to index `to` (clamped). Returns a copy.
inline std::vector<Frame> moveFrame(const std::vector<Frame>& frames, int from, int to) {
    if (from < 0 || from >= (int)frames.size()) return frames;
    int dest = std::max(0, std::min((int)frames.size() - 1, to));
    if (dest == from) return frames;
    auto out = frames;
    Frame f = out[(size_t)from];
    out.erase(out.begin() + from);
    out.insert(out.begin() + dest, f);
    return out;
}

// Sample `n` points out of a frame for the pad / a thumbnail.
inline std::vector<float> framePoints(const Frame& frame, int n) {
    std::vector<float> out((size_t)n, 0.0f);
    if (frame.empty()) return out;
    const float step = (float)frame.size() / n;
    for (int i = 0; i < n; ++i) out[(size_t)i] = frame[(size_t)std::min((int)frame.size() - 1, (int)(i * step))];
    return out;
}

// Split a packed user-table wave (frameCount*SIZE) into independent SIZE frames.
// Clamps frameCount to what `wave` can actually hold, so a malformed pair can't
// read past the buffer.
inline std::vector<Frame> framesFromWave(const std::vector<float>& wave, int frameCount) {
    const int fit = (int)(wave.size() / (size_t)SIZE);
    frameCount = std::max(0, std::min(frameCount, fit));
    std::vector<Frame> out;
    for (int f = 0; f < frameCount; ++f)
        out.emplace_back(wave.begin() + (size_t)f * SIZE, wave.begin() + (size_t)(f + 1) * SIZE);
    return out;
}

} // namespace fable
