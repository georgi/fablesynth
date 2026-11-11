// SQ-4 hosted-clip plumbing shared by the three WebAssembly bridges
// (bass_wasm.cpp, drum_wasm.cpp, wt_wasm.cpp; docs/wasm-sq4.md).
// It adds no DSP: the engines' own ClipHost/AutoPlayer do all the work, as in
// SeqProcessor. This header holds only the static buffers the worklet writes
// clip bytes and automation tables into, and the event drain.
#pragma once
#include "../ClipAutomation.h"
#include "../ClipHost.h"
#include "../../seq/dsp/SeqProtocol.h"

#include <algorithm>
#include <cstdint>

namespace fable::wasm {

constexpr int kHostBlock = 128;                    // AudioWorklet render quantum
constexpr int kClipBytes = SQ_MAX_BARS * 512;      // largest clip (WT-1 format)
constexpr int kEventMax = 64;
constexpr int kEventFields = 5;                    // type, frame, step, bar, tag

struct Hosted {
    uint8_t clip[kClipBytes];
    double events[kEventMax * kEventFields];
    AutoBank* bank = nullptr;                      // staging; engines copy it
    void create() { if (!bank) bank = new AutoBank(); }
    // The staged bank with `count` lanes, or nullptr for no automation.
    const AutoBank* automation(int count) {
        if (count < 0) return nullptr;
        bank->count = std::clamp(count, 0, AUTO_MAX_LANES);
        return bank;
    }
    float* lane(int index, int paramId, int numParams, int len, int fit, int rot, int isFx) {
        if (index < 0 || index >= AUTO_MAX_LANES || paramId < 0 || paramId >= numParams
            || len < 1 || len * AUTO_RES > AUTO_TABLE_SIZE) return nullptr;
        auto& l = bank->lanes[(size_t)index];
        l.paramIndex = paramId; l.len = len; l.fit = fit; l.rot = rot; l.isFx = isFx != 0;
        return l.table.data();
    }
    // Copies up to kEventMax pending engine events. Type 0 Start, 1 Stop, 2 Pos.
    template <class E> int take(E& engine) {
        HostEvent buf[kEventMax];
        const int n = engine.takeHostEvents(buf, kEventMax);
        for (int i = 0; i < n; ++i) {
            double* o = events + i * kEventFields;
            o[0] = (double)(int)buf[i].t; o[1] = buf[i].frame;
            o[2] = buf[i].step; o[3] = buf[i].bar; o[4] = buf[i].tag;
        }
        return n;
    }
};

inline SequenceRhythm sequenceRhythm(int enabled, int fit, int steps, int rotation, int cycleBeats) {
    SequenceRhythm r;
    r.enabled = enabled != 0; r.fit = fit != 0;
    r.steps = steps; r.rotation = rotation; r.cycleBeats = cycleBeats;
    return r;
}

inline ScaleMap scaleMap(const int8_t* table, int length) {
    ScaleMap m;
    m.length = std::clamp(length, 0, 64);
    for (int t = 0; t < m.length; ++t)
        for (int k = 0; k < 12; ++k) m.table[(size_t)t][(size_t)k] = table[t * 12 + k];
    return m;
}

} // namespace fable::wasm
