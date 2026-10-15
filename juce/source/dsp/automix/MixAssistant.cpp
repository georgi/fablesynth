#include "MixAssistant.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace fable::automix {

namespace {
constexpr double INF = std::numeric_limits<double>::infinity();
const auto db = powerDb;

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t h = v.size() >> 1;
    return v.size() % 2 ? v[h] : (v[h - 1] + v[h]) / 2;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return -INF;
    std::sort(v.begin(), v.end());
    const double n = (double)v.size() - 1;
    return v[(size_t)std::clamp(jsRound(n * p), 0.0, n)];
}

double kOffset(const Bands& bands) {
    double total = 0, weighted = 0;
    for (double b : bands) total += b;
    for (size_t i = 0; i < NUM_BANDS; ++i) weighted += bands[i] * kWeightPower(BAND_HZ[i]);
    return total > 0 ? db(weighted / total) : 0;
}

// Template-literal formatting of an integer-valued number (-0 prints as 0).
std::string fmtInt(double v) { return std::to_string((long long)v); }
std::string fmtGain(double v) { return (v > 0 ? "+" : "") + toFixed(v, 1) + " dB"; }
std::string fmtHz(double v) {
    return v >= 1000 ? toFixed(v / 1000, v >= 10000 ? 0 : 1) + " kHz" : fmtInt(jsRound(v)) + " Hz";
}
double volDb(double from, double to) { return 40 * std::log10(std::max(to, 1e-6) / std::max(from, 1e-6)); }

float param(const std::map<std::string, float>& m, const char* id) {
    const auto it = m.find(id);
    return it == m.end() ? 0.f : it->second;
}

// Generic track names state the role outright; authored names fall through.
// Each list matches a whole word, like the web's \b(...)\b expressions.
struct NameRole { std::vector<const char*> words; MixRole role; };
const std::vector<NameRole> NAME_ROLES = {
    {{"KICK", "DRUM", "DRUMS", "BEAT", "KIT"}, MixRole::Drums},
    {{"HAT", "HATS", "PERC", "SHAKER", "RIDE", "CLAP", "TOPS"}, MixRole::Perc},
    {{"BASS", "SUB"}, MixRole::Bass},
    {{"LEAD", "MELODY", "HOOK", "ARP", "STAB", "STABS"}, MixRole::Lead},
    {{"PAD", "PADS", "CHORD", "CHORDS", "STRINGS", "KEYS", "DRONE"}, MixRole::Pad},
};

std::vector<std::string> upperWords(const std::string& name) {
    std::vector<std::string> words;
    std::string cur;
    for (char ch : name) {
        const auto c = (unsigned char)ch;
        if (std::isalnum(c) || c == '_') cur += (char)std::toupper(c);
        else if (!cur.empty()) { words.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}
}

double kWeightPower(double f) {
    const double hp = std::pow(f, 4) / (std::pow(f, 4) + std::pow(38.0, 4));
    const double shelf = 1 + (1.585 - 1) * (f * f / (f * f + 1500 * 1500));
    return hp * shelf * shelf;
}

MixListener::MixListener(double sr, int size, int tracks)
    : sampleRate(sr), fftSize(size), trackBlocks((size_t)tracks),
      trackBandPower((size_t)tracks, Bands{}), trackPower((size_t)tracks, 0.0) {}

bool MixListener::add(Samples spectrum, Samples input, Samples output, const std::vector<TrackFrame>* tracks) {
    const double inDb = rmsDb(input.data, input.size);
    if (inDb < SILENCE_DB) return false;
    ++frames;
    inBlocks.push_back(inDb);
    for (int i = 0; i < input.size; ++i) peak = std::max(peak, (double)std::abs(input.data[i]));
    if (output.data) {
        const double o = rmsDb(output.data, output.size);
        if (o >= SILENCE_DB) outBlocks.push_back(o);
    }
    if (tracks) {
        for (size_t i = 0; i < tracks->size() && i < trackBlocks.size(); ++i) {
            const auto& t = (*tracks)[i];
            if (!t.time.data) continue;
            const double v = rmsDb(t.time.data, t.time.size);
            trackPower[i] += std::pow(10.0, v / 10);
            if (v < SILENCE_DB) continue;
            trackBlocks[i].push_back(v);
            if (t.spectrum.data)
                addBandPower(trackBandPower[i], t.spectrum.data, t.spectrum.size, sampleRate, t.spectrum.size * 2);
        }
    }
    addBandPower(bandPower, spectrum.data, spectrum.size, sampleRate, fftSize);
    return true;
}

double gatedLoudness(const std::vector<double>& blocks, double kOffsetDb) {
    auto lufs = [&](const std::vector<double>& v) {
        double s = 0;
        for (double d : v) s += std::pow(10.0, d / 10);
        return -0.691 + db(s / (double)v.size()) + kOffsetDb + 3.01;
    };
    std::vector<double> abs, gated;
    for (double d : blocks) if (d + kOffsetDb - 0.691 + 3.01 > -70) abs.push_back(d);
    if (abs.empty()) return -INF;
    const double rel = lufs(abs) - 10;
    for (double d : abs) if (-0.691 + d + kOffsetDb + 3.01 > rel) gated.push_back(d);
    return gated.empty() ? lufs(abs) : lufs(gated);
}

MixAnalysis analyse(const MixListener& l) {
    double total = 0;
    for (double p : l.bandPower) total += p;
    if (!truthy(total)) total = 1e-20;
    MixAnalysis a;
    a.kOffsetDb = kOffset(l.bandPower);
    for (double p : l.bandPower) a.spectrum.push_back(db(p / total));
    const double blockSeconds = l.fftSize / l.sampleRate;
    a.lufsIn = gatedLoudness(l.inBlocks, a.kOffsetDb);
    a.peakDb = 20 * std::log10(std::max(l.peak, 1e-10));
    double inSum = 0;
    for (double d : l.inBlocks) inSum += std::pow(10.0, d / 10);
    const double meanIn = db(inSum / std::max<double>(1, (double)l.inBlocks.size()));
    a.lufsOut = gatedLoudness(l.outBlocks, a.kOffsetDb);
    a.crestDb = a.peakDb - meanIn;
    a.loudDb = percentile(l.inBlocks, 0.9);
    // Each track is weighted by its own spectrum; without one it shares the
    // mix estimate. Tracks heard in under a quarter of the window are left out.
    for (size_t i = 0; i < l.trackBlocks.size(); ++i) {
        const auto& b = l.trackBlocks[i];
        if (!l.frames || (double)b.size() < l.frames / 4.0) { a.tracks.emplace_back(); continue; }
        const auto& bands = l.trackBandPower[i];
        double bandTotal = 0, low = 0;
        for (size_t j = 0; j < NUM_BANDS; ++j) { bandTotal += bands[j]; low += BAND_HZ[j] < 160 ? bands[j] : 0; }
        const double k = bandTotal > 0 ? kOffset(bands) : a.kOffsetDb;
        TrackStats t;
        t.lufs = gatedLoudness(b, k);
        t.power = l.trackPower[i] / l.frames * std::pow(10.0, k / 10);
        t.activity = (double)b.size() / l.frames;
        t.lowShare = bandTotal > 0 ? low / bandTotal : 0;
        t.rangeDb = percentile(b, 0.9) - percentile(b, 0.1);
        a.tracks.push_back(t);
    }
    a.seconds = l.frames * blockSeconds;
    return a;
}

// Kick and bass carry the groove; the lead sits clear above the pads.
const std::array<MixRoleInfo, 5> MIX_ROLES = {{
    {MixRole::Drums, "drums", "DRUMS", 0},
    {MixRole::Bass, "bass", "BASS", -2},
    {MixRole::Lead, "lead", "LEAD", -6},
    {MixRole::Perc, "perc", "PERC", -6},
    {MixRole::Pad, "pad", "PAD", -7},
}};

const MixRoleInfo& roleInfo(MixRole r) {
    for (const auto& x : MIX_ROLES) if (x.id == r) return x;
    return MIX_ROLES[0];
}

MixRole detectRole(Machine machine, const TrackStats& t, const std::string& name) {
    const auto words = upperWords(name);
    for (const auto& nr : NAME_ROLES)
        for (const auto& w : words)
            if (std::any_of(nr.words.begin(), nr.words.end(), [&](const char* x) { return w == x; })) return nr.role;
    if (machine == Machine::DR1) return t.lowShare < 0.15 ? MixRole::Perc : MixRole::Drums;
    if (t.lowShare > (machine == Machine::BL1 ? 0.3 : 0.45)) return MixRole::Bass;
    return t.activity > 0.75 && t.rangeDb < 8 ? MixRole::Pad : MixRole::Lead;
}

AssistPlan buildPlan(const PlanInput& input) {
    const auto& a = input.analysis;
    const double amount = input.amount, sampleRate = input.sampleRate;
    const auto& ref = referenceById(input.reference);
    const auto& prev = input.masterFx;
    AssistPlan plan;
    auto& fx = plan.masterFx;
    auto& moves = plan.moves;
    auto& notes = plan.notes;

    plan.eq = fitEq(a.spectrum, ref, amount, sampleRate);
    const auto& eq = plan.eq;
    fx["master.fx.eq.on"] = truthy(eq.change) ? 1.f : 0.f;
    static const char* const keys[4][6] = {
        {"low", "lfreq", "lq", "ltype", "lon", "LOW SHELF"},
        {"mid", "mfreq", "mq", "mtype", "mon", "BELL"},
        {"mid2", "m2freq", "m2q", "m2type", "m2on", "BELL"},
        {"high", "hfreq", "hq", "htype", "hon", "HIGH SHELF"},
    };
    const std::string pre = "master.fx.eq.";
    for (int b = 0; b < 4; ++b) {
        const auto& band = eq.bands[(size_t)b];
        fx[pre + keys[b][0]] = (float)band.gain;
        fx[pre + keys[b][1]] = (float)band.freq;
        fx[pre + keys[b][2]] = (float)band.q;
        fx[pre + keys[b][3]] = (float)band.type;
        fx[pre + keys[b][4]] = 1.f;
    }
    for (int b = 0; b < 4; ++b) {
        const auto& band = eq.bands[(size_t)b];
        if (!truthy(band.gain)) continue;
        moves.push_back({std::string("eq.") + keys[b][0], std::string("EQ ") + keys[b][5] + " " + fmtHz(band.freq),
                         fmtGain(param(prev, (pre + keys[b][0]).c_str())), fmtGain(band.gain)});
    }
    if (!truthy(eq.change)) notes.push_back(std::string("Tonal balance is already inside the ") + ref.label + " reference.");
    std::vector<double> eqDb;
    for (double f : BAND_HZ) eqDb.push_back(truthy(eq.change) ? eqCurveDb(eq.bands, f, sampleRate) : 0);

    // Faders: every heard track has a role target. The common offset is the
    // median error, so the fewest faders move; if a boost runs out of fader
    // travel, all moves shift down together and the master fader makes it up.
    struct Roled { size_t i; TrackStats t; const TrackInfo* info; MixRole detected, role; };
    std::vector<Roled> roled;
    size_t heard = 0;
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& t = a.tracks[i];
        if (!t || !std::isfinite(t->lufs) || i >= input.tracks.size()) continue;
        ++heard;
        const auto detected = detectRole(input.tracks[i].machine, *t, input.tracks[i].name);
        const auto role = i < input.roles.size() && input.roles[i] ? *input.roles[i] : detected;
        roled.push_back({i, *t, &input.tracks[i], detected, role});
    }
    if (heard <= 1) roled.clear();
    const size_t n = roled.size();
    auto vol = [&](size_t i) { return i < input.trackVol.size() ? input.trackVol[i] : NAN; };
    auto count = [&](MixRole r) { return (double)std::count_if(roled.begin(), roled.end(), [&](const Roled& x) { return x.role == r; }); };
    // Tracks that share a role share its space: two of them sit 1.5 dB lower each.
    std::vector<double> target, err, up, want, trackDb;
    for (const auto& x : roled) {
        target.push_back(roleInfo(x.role).lu - 5 * std::log10(count(x.role)));
        err.push_back(target.back() - x.t.lufs);
        up.push_back(volDb(vol(x.i), 1));
    }
    const double m = n ? median(err) : 0;
    double shift = 0;
    for (size_t k = 0; k < n; ++k) {
        want.push_back(std::clamp((err[k] - m) * amount, -6.0, 6.0));
        shift = std::max(shift, want[k] - up[k]);
    }
    if (shift > 0.5) notes.push_back("A track fader is at the top, so the other faders drop " + toFixed(shift, 1) + " dB instead.");
    for (double d : want) { d -= shift; trackDb.push_back(std::abs(d) < 1 && shift <= 0.5 ? 0 : d); }
    // The tracks add as uncorrelated power, so fader moves scale the pre-FX sum.
    auto sumShift = [&] {
        double before = 0, after = 0;
        for (size_t k = 0; k < n; ++k) {
            before += roled[k].t.power;
            after += roled[k].t.power * std::pow(10.0, trackDb[k] / 10);
        }
        return before > 0 ? db(after / before) : 0;
    };

    // Loudness: predict the EQ and fader effect from the measurement. The
    // track faders move together first and the master fader takes the rest.
    double masterDb = 0, masterTo = 0;
    if (std::isfinite(a.lufsOut)) {
        double num = 0, den = 0;
        for (size_t i = 0; i < NUM_BANDS; ++i) {
            const double p = std::pow(10.0, a.spectrum[(size_t)i] / 10) * kWeightPower(BAND_HZ[i]);
            num += p * std::pow(10.0, eqDb[(size_t)i] / 10);
            den += p;
        }
        const double eqShift = db(num / den);
        const double delta = std::clamp(input.loudness - (a.lufsOut + eqShift + sumShift()), -6.0, 6.0);
        if (std::abs(delta) >= 0.5) {
            double room = INF;
            for (size_t k = 0; k < n; ++k) room = std::min(room, up[k] - trackDb[k]);
            const double lift = n ? std::min(delta, std::max(0.0, room)) : 0;
            if (std::abs(lift) >= 0.5) for (auto& d : trackDb) d += lift;
            const double rest = delta - (std::abs(lift) >= 0.5 ? lift : 0);
            if (std::abs(rest) >= 0.5) {
                const double to = std::clamp(input.masterVol * std::pow(10.0, rest / 40), 0.0, 1.0);
                if (std::abs(volDb(input.masterVol, to)) >= 0.5) {
                    plan.masterVol = (float)to;
                    masterTo = to;
                    masterDb = volDb(input.masterVol, to);
                }
                if (n) notes.push_back("The track faders are at the top, so the master fader takes the rest.");
                if (rest - masterDb > 0.5) notes.push_back("The master and track faders are at the top. Raise the comp makeup to get louder.");
            }
        }
    }

    // Dynamics follow the crest factor; the comp threshold follows the fader
    // moves and its release follows the tempo.
    const double crest = a.crestDb;
    const double ottDepth = jsRound((crest > 18 ? 0.32 : crest > 13 ? 0.22 : 0.12) * (0.5 + amount / 2) * 100) / 100;
    const double ratio = crest > 18 ? 3 : crest > 13 ? 2.5 : 2;
    const double thr = jsRound(std::clamp(a.loudDb + sumShift() + 2, -36.0, -6.0));
    const double rel = jsRound(std::clamp(30 / input.bpm, 0.08, 0.4) * 1000) / 1000;
    fx["master.fx.ott.on"] = 1.f; fx["master.fx.ott.depth"] = (float)ottDepth;
    fx["master.fx.comp.on"] = 1.f; fx["master.fx.comp.thr"] = (float)thr; fx["master.fx.comp.att"] = 0.01f;
    fx["master.fx.comp.rel"] = (float)rel; fx["master.fx.comp.ratio"] = (float)ratio;
    fx["master.fx.limiter.on"] = 1.f; fx["master.fx.limiter.ceiling"] = -1.f;
    moves.push_back({"ott.depth", "OTT DEPTH", fmtInt(jsRound(param(prev, "master.fx.ott.depth") * 100.0)) + "%",
                     fmtInt(jsRound(ottDepth * 100)) + "%"});
    moves.push_back({"comp", "GLUE COMP",
                     toFixed(param(prev, "master.fx.comp.ratio"), 1) + ":1 @ " + fmtInt(jsRound(param(prev, "master.fx.comp.thr"))) + " dB",
                     toFixed(ratio, 1) + ":1 @ " + fmtInt(thr) + " dB"});
    moves.push_back({"comp.time", "COMP ATTACK / RELEASE",
                     fmtInt(jsRound(param(prev, "master.fx.comp.att") * 1000.0)) + " / " + fmtInt(jsRound(param(prev, "master.fx.comp.rel") * 1000.0)) + " ms",
                     "10 / " + fmtInt(jsRound(rel * 1000)) + " ms"});
    if (truthy(param(prev, "master.fx.limiter.ceiling") + 1.0) || param(prev, "master.fx.limiter.on") < .5)
        moves.push_back({"limiter", "LIMITER CEILING", fmtGain(param(prev, "master.fx.limiter.ceiling")), "-1.0 dB"});

    plan.trackVol.assign(input.trackVol.size(), std::nullopt);
    bool anyMove = false;
    for (size_t k = 0; k < n; ++k) {
        const auto& x = roled[k];
        const double from = vol(x.i);
        const double to = std::clamp(from * std::pow(10.0, trackDb[k] / 40), 0.0, 1.0);
        const double move = truthy(trackDb[k]) ? volDb(from, to) : 0;
        if (truthy(move)) {
            anyMove = true;
            plan.trackVol[x.i] = (float)to;
            std::string role = roleInfo(x.role).label;
            moves.push_back({"track." + std::to_string(x.i), x.info->name + " \xc2\xb7 " + role,
                             fmtInt(jsRound(from * 100)), fmtInt(jsRound(to * 100)) + " (" + fmtGain(move) + ")"});
        }
        plan.levels.push_back({(int)x.i, x.info->name, x.role, x.detected, x.t.lufs + m, target[k], move});
    }
    if (n && !anyMove) notes.push_back("The track levels already match their roles.");
    if (heard < input.tracks.size()) notes.push_back("Silent or muted tracks were left out of the balance.");
    if (plan.masterVol)
        moves.push_back({"master", "MASTER \xe2\x86\x92 " + std::to_string(input.loudness) + " LUFS",
                         fmtInt(jsRound(input.masterVol * 100)),
                         fmtInt(jsRound(masterTo * 100)) + " (" + fmtGain(masterDb) + ")"});
    return plan;
}

}
