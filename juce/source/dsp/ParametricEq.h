#pragma once
#include "Fx.h"
#include <algorithm>

namespace fable {
// Four-band WT-1 EQ for BL-1 and each DR-1 pad. Parameter offsets follow
// paramInfo() filtered by fx.eq.*; all state is private to the instrument/pad.
class ParametricEq {
public:
    void prepare(double sr) {
        sr_ = sr;
        wet_.setTime(0.005, sr);
        guard_.prepare(sr);
        for (auto& b : bands_)
            for (auto* r : {&b.freq, &b.gain, &b.q}) r->setSteps(std::max(1, (int)(sr * .015 / 32)));
        primed_ = false;
        reset();
    }
    void reset() {
        for (auto& b : bands_) {
            b.l.reset(); b.r.reset();
            b.freq.snapToTarget(); b.gain.snapToTarget(); b.q.snapToTarget();
            b.dirty = true;
        }
        wet_.snap(wet_.target);
        guard_.reset();
        left_ = 0;
    }
    void setParams(const float* p) {
        static constexpr int keys[4][5] = {{5,1,9,10,11}, {3,2,12,13,14},
                                          {6,8,15,16,17}, {7,4,18,19,20}};
        wet_.target = p[0] > .5f ? 1.f : 0.f;
        if (!primed_) wet_.snap(wet_.target);
        for (int i = 0; i < 4; ++i) {
            auto& b = bands_[i]; const auto& k = keys[i];
            const float f = std::log2(std::clamp(p[k[0]], 20.f, 20000.f));
            const float g = p[k[4]] > .5f ? std::clamp(p[k[1]], -15.f, 15.f) : 0.f;
            const float q = std::clamp(p[k[2]], .2f, 12.f);
            if (!primed_) { b.freq.snap(f); b.gain.snap(g); b.q.snap(q); }
            else { b.freq.setTarget(f); b.gain.setTarget(g); b.q.setTarget(q); }
            const int type = std::clamp((int)p[k[3]], 0, 2);
            b.dirty |= b.type != type;
            b.type = type;
        }
        primed_ = true;
    }
    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) {
            if (left_-- <= 0) {
                left_ = 31;
                for (auto& b : bands_) {
                    bool changed = b.freq.next(); changed |= b.gain.next(); changed |= b.q.next();
                    if (!(changed || b.dirty)) continue;
                    const double f = std::exp2(b.freq.cur), q = b.q.cur, g = b.gain.cur;
                    for (auto* filter : {&b.l, &b.r}) {
                        if (b.type == 0) filter->lowShelf(f, g, sr_, q);
                        else if (b.type == 2) filter->highShelf(f, g, sr_, q);
                        else filter->peaking(f, q, g, sr_);
                    }
                    b.dirty = false;
                }
            }
            const float wet = wet_.next();
            if (wet_.target == 0 && wet < 1.e-6f) {
                if (wet != 0) { wet_.snap(0); for (auto& b : bands_) { b.l.reset(); b.r.reset(); } }
                continue; // exact, zero-latency bypass
            }
            double l = L[i], r = R[i];
            for (auto& b : bands_) { l = b.l.process(l); r = b.r.process(r); }
            L[i] += wet * ((float)l - L[i]); R[i] += wet * ((float)r - R[i]);
            guard_.process(L + i, R + i, 1);
        }
    }
private:
    struct Band { Biquad l, r; ChunkRamp freq, gain, q; int type = 1; bool dirty = true; };
    std::array<Band, 4> bands_;
    Smooth wet_;
    PeakGuard guard_;
    double sr_ = 48000;
    int left_ = 0;
    bool primed_ = false;
};
} // namespace fable
