// Full-chain, in-context track-level measurement for the SQ-4 factory songs.
//
// For every factory session (song) and every track (DR-1 drums, BL-1 bass, two
// WT-1 voices = lead + pad), this renders the track's actual clip through its
// real engine AND its full FX chain — including WT-1's new leveling compressor —
// then measures loudness with the same RMS-dB metric as scripts/measure-*.mjs.
//
// It reports each track's pre-fader loudness, its current fader / resulting
// level, and a recommended fader that lands every track on one common target
// (the mean drum-bus level), so all four tracks sit balanced in every song.
// Aggregated per (role, program) it prints copy-pasteable fader tables for
// SeqFactory.cpp / sessionPresets.ts.
//
// Not a pass/fail test — a calibration tool. Build target: measure_track_levels.
#include "../source/seq/dsp/SeqFactory.h"
#include "../source/seq/dsp/SeqModel.h"
#include "../source/dsp/Engine.h"
#include "../source/dsp/Fx.h"
#include "../source/dsp/Params.h"
#include "../source/dsp/Presets.h"
#include "../source/dsp/Wavetables.h"
#include "../source/bass/dsp/BassEngine.h"
#include "../source/bass/dsp/BassFx.h"
#include "../source/bass/dsp/BassParams.h"
#include "../source/bass/dsp/BassPatches.h"
#include "../source/drum/dsp/DrumEngine.h"
#include "../source/drum/dsp/DrumParams.h"
#include "../source/drum/dsp/DrumKits.h"
#include "../source/drum/dsp/DrumTables.h"
#include "../source/drum/dsp/SampledTables.gen.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace fable;

namespace {

constexpr double kSr = 48000.0;
constexpr int    kBlock = 128;
constexpr double kAnchor = 256.0;
constexpr int    kWarmupBars = 2; // let attack + comp/limiter settle into the loop
constexpr int    kMeasureBars = 4; // one full 4-bar loop

// Per-role loudness offset (dB) added to the common drum-bus target, so the
// rebalance is perceptually weighted rather than a flat RMS match: bass and pads
// sit hotter than the transient-heavy drum bus (which reads louder than its RMS).
// role: 0 drum (reference), 1 bass, 2 lead, 3 pad.
constexpr double kRoleOffsetDb[4] = { 0.0, 4.0, 0.0, 2.0 };

// Fader curve applied by the seq mixer (store.ts gainCurve / SeqProcessor).
inline double gainCurve(double g) { return g * g * 1.4; }
// Invert it: fader that scales pre-fader rms up to the target amplitude.
inline double faderFor(double targetLin, double rmsPre) {
    if (rmsPre <= 1e-12) return 1.0;
    double g = std::sqrt(targetLin / (rmsPre * 1.4));
    return g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
}
inline double lin2db(double lin) { return 20.0 * std::log10(lin <= 1e-12 ? 1e-12 : lin); }
inline double db2lin(double db) { return std::pow(10.0, db / 20.0); }
// Per-role target amplitude = common drum target lifted by the role offset.
inline double roleTarget(double baseLin, int role) { return baseLin * db2lin(kRoleOffsetDb[role]); }

// Combined table sets, built once.
struct Tables {
    std::vector<TablePtr> wt, drum;
    Tables() {
        for (auto& g : generateTables()) wt.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
        for (auto& g : generateDrumTables()) drum.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
        for (auto& g : generateTables()) drum.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
        for (auto& g : generateSampledDrumTables()) drum.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    }
};

// Find a track's clip in a session: first scene whose cell for `t` is filled.
const ClipData* trackClip(const SessionData& s, int t) {
    for (const auto& sc : s.scenes)
        if (t < (int)sc.hasClip.size() && sc.hasClip[(size_t)t] && !sc.clips[(size_t)t].bytes.empty())
            return &sc.clips[(size_t)t];
    return nullptr;
}

struct Render { double rmsPre = 0.0; };

// Accumulate stereo mean-square over the measurement window only.
struct Meter {
    double sumSq = 0.0; long n = 0; double startFrame, endFrame;
    void add(double frame, const float* L, const float* R, int len) {
        for (int i = 0; i < len; ++i) {
            double f = frame + i;
            if (f < startFrame || f >= endFrame) continue;
            sumSq += (double)L[i] * L[i] + (double)R[i] * R[i];
            n += 2;
        }
    }
    double rms() const { return n ? std::sqrt(sumSq / (double)n) : 0.0; }
};

Render renderWt(const ClipData& clip, const ParamArray& params, double bpm, double swing, const Tables& tb) {
    Engine e; e.prepare(kSr); e.setTables(tb.wt);
    e.setParams(params);
    e.setHostClipMode(true, kBlock);
    e.hostTempo(bpm, swing, kAnchor);
    e.hostClip(clip.bytes.data(), (int)clip.bytes.size(), clip.bars, 0.0);
    Fx fx; fx.prepare(kSr); ParamArray p = params; fx.setParams(p);

    double spb = kSr * 60.0 / bpm * 4.0; // samples per bar
    Meter m; m.startFrame = kAnchor + kWarmupBars * spb; m.endFrame = m.startFrame + kMeasureBars * spb;
    double frame = kAnchor;
    float L[kBlock], R[kBlock];
    while (frame < m.endFrame) {
        e.hostSetFrame(frame); e.render(L, R, kBlock); fx.process(L, R, kBlock);
        m.add(frame, L, R, kBlock); frame += kBlock;
    }
    return { m.rms() };
}

Render renderBass(const ClipData& clip, const BassParamArray& params, double bpm, double swing, const Tables& tb) {
    BassEngine e; e.prepare(kSr); e.setTables(tb.wt);
    e.setParams(params);
    e.setHostClipMode(true, kBlock);
    e.hostTempo(bpm, swing, kAnchor);
    e.hostClip(clip.bytes.data(), (int)clip.bytes.size(), clip.bars, 0.0);
    BassFx fx; fx.prepare(kSr); BassParamArray p = params; fx.setParams(p);

    double spb = kSr * 60.0 / bpm * 4.0;
    Meter m; m.startFrame = kAnchor + kWarmupBars * spb; m.endFrame = m.startFrame + kMeasureBars * spb;
    double frame = kAnchor;
    float L[kBlock], R[kBlock];
    while (frame < m.endFrame) {
        e.hostSetFrame(frame); e.render(L, R, kBlock); fx.process(L, R, kBlock);
        m.add(frame, L, R, kBlock); frame += kBlock;
    }
    return { m.rms() };
}

Render renderDrum(const ClipData& clip, const DrumParamArray& params, double bpm, double swing, const Tables& tb) {
    DrumEngine e; e.prepare(kSr); e.enablePadFx(true); e.setTables(tb.drum);
    e.params() = params;
    e.setHostClipMode(true, kBlock);
    e.hostTempo(bpm, swing, kAnchor);
    e.hostClip(clip.bytes.data(), (int)clip.bytes.size(), clip.bars, 0.0);

    double spb = kSr * 60.0 / bpm * 4.0;
    Meter m; m.startFrame = kAnchor + kWarmupBars * spb; m.endFrame = m.startFrame + kMeasureBars * spb;
    double frame = kAnchor;
    // 5 stereo buses; sum them all into the measured mix.
    std::vector<float> aux((size_t)(2 * (DR_NBUSES - 1)) * kBlock, 0.0f);
    float mainL[kBlock], mainR[kBlock];
    float sumL[kBlock], sumR[kBlock];
    while (frame < m.endFrame) {
        e.hostSetFrame(frame);
        float* outs[DR_NBUSES][2];
        outs[0][0] = mainL; outs[0][1] = mainR;
        for (int b = 1; b < DR_NBUSES; ++b) {
            outs[b][0] = &aux[(size_t)(2 * (b - 1)) * kBlock];
            outs[b][1] = &aux[(size_t)(2 * (b - 1) + 1) * kBlock];
        }
        e.render(outs, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            double l = mainL[i], r = mainR[i];
            for (int b = 1; b < DR_NBUSES; ++b) {
                l += outs[b][0][i]; r += outs[b][1][i];
            }
            sumL[i] = (float)l; sumR[i] = (float)r;
        }
        m.add(frame, sumL, sumR, kBlock); frame += kBlock;
    }
    return { m.rms() };
}

const char* roleName(int t) { return t == 0 ? "DRUM" : t == 1 ? "BASS" : t == 2 ? "LEAD" : "PAD"; }

} // namespace

int main() {
    Tables tb;
    const auto& lib = factorySessionLibrary();

    struct Row { std::string song; int track; int program; double rmsPre; double curGain; };
    std::vector<Row> rows;
    // per-role,program accumulation of pre-fader rms (linear)
    std::map<std::pair<int,int>, std::vector<double>> byPatch;
    std::vector<double> drumFinalLin; // current drum-bus level per song = target basis

    for (const auto& preset : lib) {
        const auto& s = preset.session;
        for (int t = 0; t < 4; ++t) {
            const ClipData* clip = trackClip(s, t);
            if (!clip) continue;
            const auto& tr = s.tracks[(size_t)t];
            int program = tr.patch.index;
            Render r;
            if (t == 0) {
                const auto& kits = factoryKits();
                DrumParamArray p = applyKit(kits[(size_t)(program % (int)kits.size())]);
                r = renderDrum(*clip, p, s.bpm, s.swing, tb);
            } else if (t == 1) {
                const auto& bank = bassFactoryPatches();
                BassParamArray p = applyBassPatch(bank[(size_t)(program % (int)bank.size())]);
                r = renderBass(*clip, p, s.bpm, s.swing, tb);
            } else {
                const auto& bank = factoryPresets();
                ParamArray p = applyPreset(bank[(size_t)(program % (int)bank.size())]);
                r = renderWt(*clip, p, s.bpm, s.swing, tb);
            }
            rows.push_back({ preset.name, t, program, r.rmsPre, tr.gain });
            byPatch[{ t, program }].push_back(r.rmsPre);
            if (t == 0) drumFinalLin.push_back(r.rmsPre * gainCurve(tr.gain));
        }
    }

    // Common target = mean current drum-bus level across songs (drums = reference).
    double targetLin = 0.0;
    for (double d : drumFinalLin) targetLin += d;
    targetLin /= (double)std::max<size_t>(1, drumFinalLin.size());

    std::printf("Common target: %.2f dB  (mean drum-bus level, %zu songs)\n\n",
                lin2db(targetLin), drumFinalLin.size());
    std::printf("%-18s %-5s %4s %10s %8s %10s %8s %10s\n",
                "SONG", "ROLE", "PROG", "preRMSdB", "curGain", "curFinal", "recGain", "recFinal");
    for (const auto& r : rows) {
        double curFinal = r.rmsPre * gainCurve(r.curGain);
        double rec = faderFor(roleTarget(targetLin, r.track), r.rmsPre);
        double recFinal = r.rmsPre * gainCurve(rec);
        std::printf("%-18s %-5s %4d %10.2f %8.2f %10.2f %8.2f %10.2f\n",
                    r.song.c_str(), roleName(r.track), r.program,
                    lin2db(r.rmsPre), r.curGain, lin2db(curFinal), rec, lin2db(recFinal));
    }

    std::printf("\n=== Recommended faders per (role, program) — drum target %.2f dB, offsets d/b/l/p = %.0f/%.0f/%.0f/%.0f dB ===\n",
                lin2db(targetLin), kRoleOffsetDb[0], kRoleOffsetDb[1], kRoleOffsetDb[2], kRoleOffsetDb[3]);
    for (int role = 0; role < 4; ++role) {
        std::printf("\n%s (target %.2f dB):\n", roleName(role), lin2db(roleTarget(targetLin, role)));
        for (const auto& [key, vals] : byPatch) {
            if (key.first != role) continue;
            double mean = 0.0; for (double v : vals) mean += v; mean /= (double)vals.size();
            double rec = faderFor(roleTarget(targetLin, role), mean);
            std::printf("  program %2d : %.2f  (n=%zu, preRMS %.2f dB)\n",
                        key.second, rec, vals.size(), lin2db(mean));
        }
    }
    return 0;
}
