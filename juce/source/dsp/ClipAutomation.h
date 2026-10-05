// Portable clip lanes and fixed-capacity, audio-thread automation playback.
#pragma once
#include "Params.h"
#include "../bass/dsp/BassParams.h"
#include "../drum/dsp/DrumParams.h"
#include "../drum/dsp/DrumRhythm.h"
#include "../seq/dsp/SeqProtocol.h"
#include <algorithm>
#include <cmath>
#include <bitset>
#include <memory>
#include <limits>
#include <vector>

namespace fable {
constexpr int AUTO_MAX_LANES = 8, AUTO_MAX_STEPS = 64, AUTO_MAX_POINTS = 512, AUTO_RES = 16;
constexpr int AUTO_TABLE_SIZE = SQ_MAX_BARS * 16 * AUTO_RES;
struct AutoTime {
    enum Mode { Clip, Grid, Fit, Pad } mode = Clip;
    int steps = 16, cycleBeats = 4;
};
struct AutoPoint { double t = 0, v = 0, c = 0; bool hold = false; };
struct AutoLane { std::string target; bool enabled = true; AutoTime time; std::vector<AutoPoint> points; };
inline const ParamInfo* autoParamDef(Machine machine, const std::string& target) {
    if (target == "seq.bpm" || target == "master.swing") return nullptr;
    int id = machine == Machine::WT1 ? idFromString(target) : machine == Machine::BL1 ? bassIdFromString(target) : drumIdFromString(target);
    if (id < 0) return nullptr;
    const auto& d = machine == Machine::WT1 ? paramInfo()[(size_t)id] : machine == Machine::BL1 ? bassParamInfo()[(size_t)id] : drumParamInfo()[(size_t)id];
    return d.kind == Kind::Float && (d.curve == Curve::Lin || d.curve == Curve::Log) ? &d : nullptr;
}
inline int autoPadOf(const std::string& target) {
    if (target.compare(0, 3, "pad") != 0) return -1;
    size_t i = 3; int pad = 0;
    if (i == target.size() || target[i] < '0' || target[i] > '9') return -1;
    while (i < target.size() && target[i] >= '0' && target[i] <= '9') {
        pad = pad * 10 + target[i++] - '0'; if (pad >= 16) return -1;
    }
    return i < target.size() && target[i] == '.' ? pad : -1;
}
inline bool validateAutomation(const std::vector<AutoLane>& lanes, Machine machine) {
    if (lanes.size() > AUTO_MAX_LANES) return false;
    for (const auto& l : lanes) {
        if (!autoParamDef(machine, l.target) || l.points.size() > AUTO_MAX_POINTS) return false;
        if (l.time.mode == AutoTime::Grid || l.time.mode == AutoTime::Fit) {
            if (l.time.steps < 1 || l.time.steps > AUTO_MAX_STEPS) return false;
            if (l.time.mode == AutoTime::Fit && l.time.cycleBeats != 4 && l.time.cycleBeats != 8) return false;
        } else if (l.time.mode == AutoTime::Pad) {
            if (machine != Machine::DR1 || autoPadOf(l.target) < 0) return false;
        } else if (l.time.mode != AutoTime::Clip) return false;
        double last = -1;
        for (const auto& p : l.points) {
            if (!std::isfinite(p.t) || !std::isfinite(p.v) || !std::isfinite(p.c)
                || p.t < 0 || p.t >= 256 || p.t < last || p.v < 0 || p.v > 1 || p.c < -1 || p.c > 1) return false;
            last = p.t;
        }
    }
    return true;
}
struct AutoCycle { int len = 16, fit = 0, rot = 0; };
inline AutoCycle laneCycle(const AutoLane& l, int bars, const DrumRhythm* rhythm) {
    AutoCycle clip { std::max(1, bars) * 16, 0, 0 };
    if (l.time.mode == AutoTime::Grid) return { l.time.steps, 0, 0 };
    if (l.time.mode == AutoTime::Fit) return { l.time.steps, l.time.cycleBeats, 0 };
    const int pad = autoPadOf(l.target);
    if (l.time.mode == AutoTime::Pad && pad >= 0 && rhythm && rhythm->lanes[(size_t)pad].enabled) {
        const auto& p = rhythm->lanes[(size_t)pad];
        return { p.steps, p.mode == DrumRhythmMode::fit ? p.cycleBeats : 0, p.rotation };
    }
    return clip;
}
inline double autoMod(double x, double n) { return std::fmod(std::fmod(x, n) + n, n); }
inline double bend(double f, double c) {
    if (std::abs(c) < 1e-3) return f;
    return (std::exp(c * 6 * f) - 1) / (std::exp(c * 6) - 1);
}
inline double evalLane(const std::vector<AutoPoint>& points, int len, double x) {
    size_t count = 0; while (count < points.size() && points[count].t < len) ++count;
    if (!count) return 0;
    x = autoMod(x, len);
    size_t i = count - 1; double t0 = points[i].t - len;
    for (size_t j = 0; j < count && points[j].t <= x; ++j) { i = j; t0 = points[j].t; }
    const auto& a = points[i]; const auto& b = points[(i + 1) % count];
    if (a.hold) return a.v;
    const double t1 = b.t + (i == count - 1 ? len : 0);
    if (t1 - t0 <= 1e-9) return b.v;
    return a.v + (b.v - a.v) * bend((x - t0) / (t1 - t0), a.c);
}
struct AutoLaneTable {
    int paramIndex = 0, len = 0, fit = 0, rot = 0;
    bool isFx = false;
    std::array<float, AUTO_TABLE_SIZE> table{};
};
struct AutoBank { int count = 0; std::array<AutoLaneTable, AUTO_MAX_LANES> lanes{}; };
inline void compileAutomation(AutoBank& out, const std::vector<AutoLane>& lanes, int bars, const DrumRhythm* rhythm, Machine machine) {
    out.count = 0;
    if (!validateAutomation(lanes, machine)) return;
    for (const auto& l : lanes) {
        if (!l.enabled) continue;
        const auto* d = autoParamDef(machine, l.target);
        const auto cycle = laneCycle(l, bars, rhythm);
        if (cycle.len < 1 || cycle.len * AUTO_RES > AUTO_TABLE_SIZE || l.points.empty() || l.points.front().t >= cycle.len) continue;
        bool seen = false; for (int i = 0; i < out.count; ++i) seen |= out.lanes[(size_t)i].paramIndex == d->id;
        if (seen) continue;
        auto& table = out.lanes[(size_t)out.count++];
        table.paramIndex = d->id; table.len = cycle.len; table.fit = cycle.fit; table.rot = cycle.rot;
        table.isFx = l.target.find("fx.") != std::string::npos || l.target == "master.volume";
        for (int i = 0; i < cycle.len * AUTO_RES; ++i) {
            double n = evalLane(l.points, cycle.len, (double)i / AUTO_RES);
            // Mirror JS double arithmetic before the Float32 table assignment.
            table.table[(size_t)i] = (float)(d->curve == Curve::Log
                ? d->min * std::pow((double)d->max / d->min, n) : d->min + n * ((double)d->max - d->min));
        }
    }
}
class AutoPlayer {
public:
    void prepare() { if (!playing_) { playing_ = std::make_unique<AutoBank>(); pending_ = std::make_unique<AutoBank>(); } }
    void schedule(const AutoBank* bank) { if (pending_) copy(*pending_, bank); }
    void update(const AutoBank* bank, bool pending) {
        if (!playing_) return;
        copy(pending ? *pending_ : *playing_, bank);
        if (!pending) reconcile_ = true;
    }
    void swap() { playing_.swap(pending_); pending_->count = 0; reconcile_ = true; }
    bool active() const { return playing_ && (playing_->count || pending_->count || held_.any()); }
    bool isHeld(int id) const { return held_[(size_t)id]; }
    template<class Write> void stopPlaying(Write write, bool& dirty) {
        if (playing_) playing_->count = 0;
        reconcile_ = true;
        release(write, dirty);
    }
    bool hold(int id, float value) { if (!held_[(size_t)id]) return false; values_[(size_t)id] = value; return true; }
    template<class Params> Params protect(const Params& incoming, const Params& live) {
        auto out = incoming;
        for (size_t i = 0; i < incoming.size(); ++i) if (hold((int)i, incoming[i])) out[i] = live[i];
        return out;
    }
    template<class Write> void clear(Write write, bool& dirty) {
        if (playing_) playing_->count = pending_->count = 0;
        reconcile_ = true;
        release(write, dirty);
    }
    template<class Params, class Write> void tick(double frame, double anchor, double bpm, double sr, const Params& params, Write write, bool& dirty) {
        release(write, dirty);
        if (!playing_ || !playing_->count) return;
        const double steps = std::max(0.0, frame - anchor) / ((60 / std::clamp(bpm, 60.0, 200.0) / 4) * sr);
        for (int i = 0; i < playing_->count; ++i) {
            const auto& l = playing_->lanes[(size_t)i]; const size_t id = (size_t)l.paramIndex;
            double x = autoMod((l.fit ? std::fmod(steps / 4, l.fit) / l.fit * l.len : steps) - l.rot, l.len) * AUTO_RES;
            int a = (int)x, b = (a + 1) % (l.len * AUTO_RES);
            if (!held_[id]) { values_[id] = params[id]; held_.set(id); fx_.set(id, l.isFx); }
            const float value = (float)(l.table[(size_t)a] + ((double)l.table[(size_t)b] - l.table[(size_t)a]) * (x - a));
            dirty |= l.isFx && params[id] != value;
            write((int)id, value);
            if (trace_) trace_(traceContext_, frame, (int)id, params[id]);
        }
    }
    using Trace = void (*)(void*, double, int, float);
    void setTrace(Trace trace, void* context) { trace_ = trace; traceContext_ = context; }
    size_t capacity() const { return playing_ ? 2 * AUTO_MAX_LANES * AUTO_TABLE_SIZE : 0; }
private:
    static void copy(AutoBank& out, const AutoBank* in) {
        out.count = in ? in->count : 0;
        for (int i = 0; i < out.count; ++i) out.lanes[(size_t)i] = in->lanes[(size_t)i];
    }
    template<class Write> void release(Write write, bool& dirty) {
        if (!reconcile_) return;
        reconcile_ = false;
        if (held_.none()) return;
        for (size_t id = 0; id < held_.size(); ++id) if (held_[id]) {
            bool keep = false;
            if (playing_) for (int i = 0; i < playing_->count; ++i) keep |= playing_->lanes[(size_t)i].paramIndex == (int)id;
            if (!keep) { write((int)id, values_[id]); dirty |= fx_[id]; held_.reset(id); }
        }
    }
    bool reconcile_ = false;
    Trace trace_ = nullptr; void* traceContext_ = nullptr;
    std::unique_ptr<AutoBank> playing_, pending_;
    std::array<float, DR_NUM_PARAMS> values_{};
    std::bitset<DR_NUM_PARAMS> held_, fx_;
};
} // namespace fable
