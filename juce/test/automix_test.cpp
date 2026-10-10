// Parity test for the native auto-mix core (source/dsp/automix) against the
// web implementation. The vectors come from
// src/engine/automixVectors.generate.test.ts:
//   AUTOMIX_VECTORS=write npx vitest run src/engine/automixVectors.generate.test.ts
// Tolerances: values computed in double on identical inputs must agree to
// 1e-9 (relative, floor 1e-9 absolute); rounded values (EQ gains/freqs,
// thresholds, releases, fader and pad levels), flags and every string
// (moves, notes, toFixed) must match exactly. Paths through float32 data
// (detector dB traces) allow 1e-6.
#include "../source/dsp/automix/AutoComp.h"
#include "../source/dsp/automix/AutoEq.h"
#include "../source/dsp/automix/KitMix.h"
#include "../source/dsp/automix/MixAssistant.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace fable::automix;

namespace {
int failures = 0, checks = 0;

// ---- minimal JSON reader (fixture only) ----
struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a; std::map<std::string, Json> o;

    const Json& operator[](const std::string& k) const {
        static const Json none;
        const auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
    const Json& operator[](size_t i) const { return a.at(i); }
    size_t size() const { return a.size(); }
    bool isNull() const { return kind == Null; }
    double num() const {
        if (kind == Str) return s == "Infinity" ? INFINITY : s == "-Infinity" ? -INFINITY : NAN;
        return n;
    }
    std::vector<double> nums() const { std::vector<double> v; for (const auto& x : a) v.push_back(x.num()); return v; }
    std::vector<float> floats() const { std::vector<float> v; for (const auto& x : a) v.push_back((float)x.num()); return v; }
};

struct Parser {
    const char* p;
    void ws() { while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') ++p; }
    Json parse() {
        ws();
        Json j;
        if (*p == '{') {
            j.kind = Json::Obj; ++p; ws();
            if (*p == '}') { ++p; return j; }
            for (;;) {
                ws(); const auto key = parse().s; ws(); ++p; // ':'
                j.o[key] = parse(); ws();
                if (*p++ == '}') return j;
            }
        }
        if (*p == '[') {
            j.kind = Json::Arr; ++p; ws();
            if (*p == ']') { ++p; return j; }
            for (;;) { j.a.push_back(parse()); ws(); if (*p++ == ']') return j; }
        }
        if (*p == '"') {
            j.kind = Json::Str; ++p;
            while (*p != '"') {
                if (*p == '\\') {
                    ++p;
                    if (*p == 'u') { // fixture strings are ASCII apart from UTF-8 passthrough
                        const unsigned cp = (unsigned)std::strtoul(std::string(p + 1, 4).c_str(), nullptr, 16);
                        p += 5;
                        if (cp < 0x80) j.s += (char)cp;
                        else if (cp < 0x800) { j.s += (char)(0xc0 | cp >> 6); j.s += (char)(0x80 | (cp & 63)); }
                        else { j.s += (char)(0xe0 | cp >> 12); j.s += (char)(0x80 | (cp >> 6 & 63)); j.s += (char)(0x80 | (cp & 63)); }
                        continue;
                    }
                    j.s += *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
                    ++p;
                } else j.s += *p++;
            }
            ++p;
            return j;
        }
        if (!std::strncmp(p, "null", 4)) { p += 4; return j; }
        if (!std::strncmp(p, "true", 4)) { p += 4; j.kind = Json::Bool; j.b = true; return j; }
        if (!std::strncmp(p, "false", 5)) { p += 5; j.kind = Json::Bool; return j; }
        char* end = nullptr;
        j.kind = Json::Num; j.n = std::strtod(p, &end); p = end;
        return j;
    }
};

// ---- checks ----
void fail(const std::string& what) {
    if (++failures <= 40) std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}
bool close(double a, double e, double tol) {
    if (std::isnan(e)) return std::isnan(a);
    if (std::isinf(e)) return a == e;
    return std::abs(a - e) <= tol * std::max(1.0, std::abs(e));
}
void near(double a, double e, const std::string& what, double tol = 1e-9) {
    ++checks;
    if (!close(a, e, tol)) {
        char buf[128]; std::snprintf(buf, sizeof buf, " got %.17g want %.17g", a, e);
        fail(what + buf);
    }
}
void same(const std::string& a, const std::string& e, const std::string& what) {
    ++checks;
    if (a != e) fail(what + " got '" + a + "' want '" + e + "'");
}
void truth(bool a, bool e, const std::string& what) {
    ++checks;
    if (a != e) fail(what + (a ? " got true" : " got false"));
}
template <class V> void nearAll(const V& a, const Json& e, const std::string& what, double tol = 1e-9) {
    ++checks;
    if (a.size() != e.size()) { fail(what + " size " + std::to_string(a.size()) + " want " + std::to_string(e.size())); return; }
    for (size_t i = 0; i < a.size(); ++i) near((double)a[i], e[i].num(), what + "[" + std::to_string(i) + "]", tol);
}
void boolAll(const std::vector<bool>& a, const Json& e, const std::string& what) {
    ++checks;
    if (a.size() != e.size()) { fail(what + " size"); return; }
    for (size_t i = 0; i < a.size(); ++i) truth(a[i], e[i].b, what + "[" + std::to_string(i) + "]");
}

EqBand bandFrom(const Json& j) {
    return {j["freq"].num(), j["gain"].num(), j["q"].num(), (int)j["type"].num(), j["on"].b};
}

void checkEqFit(const EqFit& fit, const Json& e, const std::string& tag) {
    truth(fit.bands.size() == 4, e["bands"].size() == 4, tag + " bands");
    for (size_t b = 0; b < 4; ++b) {
        const auto want = bandFrom(e["bands"][b]);
        const auto t = tag + " band" + std::to_string(b);
        near(fit.bands[b].freq, want.freq, t + ".freq", 0);
        near(fit.bands[b].gain, want.gain, t + ".gain", 0);
        near(fit.bands[b].q, want.q, t + ".q", 0);
        near(fit.bands[b].type, want.type, t + ".type", 0);
    }
    nearAll(fit.measured, e["measured"], tag + " measured");
    nearAll(fit.deviation, e["deviation"], tag + " deviation");
    nearAll(fit.residual, e["residual"], tag + " residual");
    boolAll(fit.present, e["present"], tag + " present");
    near(fit.change, e["change"].num(), tag + " change", 0);
}

void testFormatting(const Json& v) {
    for (const auto& c : v["toFixed"].a) {
        same(toFixed(c["v"].num(), 0), c["d0"].s, "toFixed0");
        same(toFixed(c["v"].num(), 1), c["d1"].s, "toFixed1");
    }
    for (const auto& c : v["round"].a) near(jsRound(c["v"].num()), c["r"].num(), "jsRound", 0);
}

void testEq(const Json& v) {
    for (const auto& r : v["reference"].a) {
        const auto& ref = referenceById(r["id"].s);
        same(ref.id, r["id"].s, "reference id");
        std::vector<double> got;
        for (double f : BAND_HZ) got.push_back(referenceDb(ref, f));
        got.push_back(referenceDb(ref, 20)); got.push_back(referenceDb(ref, 22050));
        nearAll(got, r["db"], std::string("referenceDb ") + ref.id);
    }
    for (const auto& f : v["fits"].a) {
        const auto levels = v["spectra"][(size_t)f["spectrum"].num()].nums();
        const auto& ref = referenceById(f["ref"].s);
        const auto tag = "fitEq s" + std::to_string((int)f["spectrum"].num()) + " " + ref.id;
        const auto al = alignToReference(levels, ref);
        near(al.offset, f["align"]["offset"].num(), tag + " offset");
        boolAll(al.present, f["align"]["present"], tag + " align.present");
        checkEqFit(fitEq(levels, ref, f["amount"].num(), f["sampleRate"].num()), f["fit"], tag);
    }
    for (const auto& c : v["coeffs"].a) {
        const auto band = bandFrom(c["band"]);
        const double sr = c["sampleRate"].num();
        const auto k = eqCoefficients(band, sr);
        const double got[5] = {k.b0, k.b1, k.b2, k.a1, k.a2};
        const char* names[5] = {"b0", "b1", "b2", "a1", "a2"};
        for (int i = 0; i < 5; ++i) near(got[i], c["c"][names[i]].num(), std::string("eqCoefficients ") + names[i]);
        const double freqs[] = {20, 31.5, 100, 440, 1000, 3000, 9000, 15000, 21000};
        std::vector<double> resp;
        for (double f : freqs) resp.push_back(eqResponseDb(k, f, sr));
        nearAll(resp, c["response"], "eqResponseDb", 1e-8);
    }
    std::vector<EqBand> bands;
    for (const auto& b : v["curve"]["bands"].a) bands.push_back(bandFrom(b));
    std::vector<double> curve;
    for (double f : BAND_HZ) curve.push_back(eqCurveDb(bands, f, 48000));
    nearAll(curve, v["curve"]["db"], "eqCurveDb", 1e-8);
    nearAll(removeEq(v["spectra"][1].nums(), bands, 48000), v["curve"]["removed"], "removeEq", 1e-8);

    const auto& cap = v["capture"];
    SpectrumCapture capture(cap["sampleRate"].num(), (int)cap["fftSize"].num());
    for (const auto& fr : cap["frames"].a) {
        const auto spec = fr["spectrum"].floats(), time = fr["time"].floats();
        truth(capture.add(spec.data(), time.data(), (int)time.size()), fr["audible"].b, "SpectrumCapture.add");
    }
    nearAll(capture.bandPower, cap["bandPower"], "SpectrumCapture.bandPower");
    nearAll(capture.levels(), cap["levels"], "SpectrumCapture.levels");
    near(capture.seconds(), cap["seconds"].num(), "SpectrumCapture.seconds");
}

void testComp(const Json& v) {
    const double sr = v["sampleRate"].num();
    const auto blocks = v["blocks"].nums();
    std::vector<std::vector<float>> data;
    std::vector<Trace> traces, holey;
    for (const auto& t : v["traces"].a) data.push_back(t.isNull() ? std::vector<float>{} : t.floats());
    for (size_t i = 0; i < data.size(); ++i) {
        const Trace t = v["traces"][i].isNull() ? Trace{} : Trace{data[i].data(), (int)data[i].size()};
        traces.push_back(t);
        holey.push_back((int)i == (int)v["holeIndex"].num() ? Trace{} : t);
    }
    for (const auto& c : v["bpm"].a) {
        const auto got = estimateBpm(c["blocks"].nums());
        truth(got.has_value(), !c["bpm"].isNull(), "estimateBpm has value");
        if (got && !c["bpm"].isNull()) near(*got, c["bpm"].num(), "estimateBpm", 0);
    }
    const auto& d = v["detector"];
    std::vector<Trace> detTraces(traces.begin(), traces.begin() + (long)d["count"].num());
    detTraces[(size_t)d["nullIndex"].num()] = {};
    const auto det = detectorDb(detTraces, d["attack"].num(), d["release"].num(), d["sampleRate"].num());
    for (size_t i = 0; i < det.size(); ++i) {
        truth(det[i].present, !d["out"][i].isNull(), "detectorDb present");
        if (det[i].present) nearAll(det[i].db, d["out"][i], "detectorDb", 1e-6);
    }
    for (const auto& f : v["fits"].a) {
        const auto& style = compStyleById(f["style"].s);
        const auto tag = std::string("fitComp ") + style.id + " " + f["traces"].s;
        FitOptions o;
        if (!f["bpm"].isNull()) o.bpm = f["bpm"].num();
        if (f["traces"].s != "none") { o.traces = f["traces"].s == "full" ? traces : holey; o.sampleRate = sr; }
        const auto in = f["blocks"].isNull() ? blocks : f["blocks"].nums();
        const auto fit = fitComp(in, style, f["depth"].num(), o);
        const auto& e = f["fit"];
        truth(fit.has_value(), !e.isNull(), tag + " has value");
        if (!fit || e.isNull()) continue;
        near(fit->threshold, e["threshold"].num(), tag + " threshold", 0);
        near(fit->ratio, e["ratio"].num(), tag + " ratio", 0);
        near(fit->attack, e["attack"].num(), tag + " attack", 0);
        near(fit->release, e["release"].num(), tag + " release", 0);
        const double tol = fit->exact ? 1e-6 : 1e-9;
        near(fit->loudGr, e["loudGr"].num(), tag + " loudGr", tol);
        near(fit->meanGr, e["meanGr"].num(), tag + " meanGr", tol);
        near(fit->rangeDb, e["rangeDb"].num(), tag + " rangeDb");
        truth(fit->bpm.has_value(), !e["bpm"].isNull(), tag + " bpm present");
        if (fit->bpm && !e["bpm"].isNull()) near(*fit->bpm, e["bpm"].num(), tag + " bpm", 0);
        nearAll(fit->levels, e["levels"], tag + " levels", tol);
        truth(fit->exact, e["exact"].b, tag + " exact");
    }
}

Machine machineFrom(const std::string& s) { return s == "DR1" ? Machine::DR1 : s == "BL1" ? Machine::BL1 : Machine::WT1; }
std::string roleKey(MixRole r) { return roleInfo(r).key; }
MixRole roleFrom(const std::string& s) {
    for (const auto& r : MIX_ROLES) if (s == r.key) return r.id;
    return MixRole::Drums;
}
TrackStats statsFrom(const Json& t) {
    return {t["lufs"].num(), t["power"].num(), t["activity"].num(), t["lowShare"].num(), t["rangeDb"].num()};
}
MixAnalysis analysisFrom(const Json& j) {
    MixAnalysis a;
    a.spectrum = j["spectrum"].nums();
    a.lufsIn = j["lufsIn"].num(); a.lufsOut = j["lufsOut"].num(); a.peakDb = j["peakDb"].num();
    a.crestDb = j["crestDb"].num(); a.loudDb = j["loudDb"].num(); a.kOffsetDb = j["kOffsetDb"].num();
    a.seconds = j["seconds"].num();
    for (const auto& t : j["tracks"].a) a.tracks.push_back(t.isNull() ? std::optional<TrackStats>{} : statsFrom(t));
    return a;
}

void checkAnalysis(const MixAnalysis& a, const Json& e) {
    nearAll(a.spectrum, e["spectrum"], "analyse spectrum");
    near(a.lufsIn, e["lufsIn"].num(), "analyse lufsIn");
    near(a.lufsOut, e["lufsOut"].num(), "analyse lufsOut");
    near(a.peakDb, e["peakDb"].num(), "analyse peakDb");
    near(a.crestDb, e["crestDb"].num(), "analyse crestDb");
    near(a.loudDb, e["loudDb"].num(), "analyse loudDb");
    near(a.kOffsetDb, e["kOffsetDb"].num(), "analyse kOffsetDb");
    near(a.seconds, e["seconds"].num(), "analyse seconds");
    truth(a.tracks.size() == e["tracks"].size(), true, "analyse track count");
    for (size_t i = 0; i < a.tracks.size() && i < e["tracks"].size(); ++i) {
        const auto& t = e["tracks"][i];
        truth(a.tracks[i].has_value(), !t.isNull(), "analyse track present");
        if (!a.tracks[i] || t.isNull()) continue;
        const auto w = statsFrom(t);
        const auto tag = "analyse track" + std::to_string(i);
        near(a.tracks[i]->lufs, w.lufs, tag + " lufs");
        near(a.tracks[i]->power, w.power, tag + " power");
        near(a.tracks[i]->activity, w.activity, tag + " activity");
        near(a.tracks[i]->lowShare, w.lowShare, tag + " lowShare");
        near(a.tracks[i]->rangeDb, w.rangeDb, tag + " rangeDb");
    }
}

void testMix(const Json& v) {
    for (const auto& k : v["kweight"].a) near(kWeightPower(k["f"].num()), k["p"].num(), "kWeightPower");
    for (const auto& c : v["loud"].a) near(gatedLoudness(c["blocks"].nums(), c["k"].num()), c["lufs"].num(), "gatedLoudness");
    std::vector<TrackStats> stats;
    for (const auto& s : v["roleStats"].a) stats.push_back(statsFrom(s));
    for (const auto& c : v["roles"].a)
        same(roleKey(detectRole(machineFrom(c["machine"].s), stats[(size_t)c["stats"].num()], c["name"].s)), c["role"].s,
             "detectRole '" + c["name"].s + "' " + c["machine"].s);

    // MixListener + analyse over the stored analyser frames.
    const int tracks = (int)v["tracks"].num();
    MixListener l(v["sampleRate"].num(), (int)v["fftSize"].num(), tracks);
    auto samples = [](const std::vector<float>& x) { return Samples{x.data(), (int)x.size()}; };
    for (const auto& fr : v["frames"].a) {
        const auto spec = fr["spectrum"].floats(), in = fr["input"].floats(), out = fr["output"].floats();
        std::vector<std::vector<float>> keep;
        keep.reserve(16);
        std::vector<TrackFrame> tf;
        for (const auto& t : fr["tracks"].a) {
            TrackFrame f;
            if (!t.isNull()) {
                f.time = samples(keep.emplace_back(t["time"].floats()));
                if (!t["spectrum"].isNull()) f.spectrum = samples(keep.emplace_back(t["spectrum"].floats()));
            }
            tf.push_back(f);
        }
        truth(l.add(samples(spec), samples(in), fr["output"].isNull() ? Samples{} : samples(out), &tf), fr["audible"].b, "MixListener.add");
    }
    const auto& lw = v["listener"];
    nearAll(l.inBlocks, lw["inBlocks"], "listener inBlocks");
    nearAll(l.outBlocks, lw["outBlocks"], "listener outBlocks");
    nearAll(l.trackPower, lw["trackPower"], "listener trackPower");
    near(l.peak, lw["peak"].num(), "listener peak", 0);
    near(l.frames, lw["frames"].num(), "listener frames", 0);
    checkAnalysis(analyse(l), v["analysis"]);

    int n = 0;
    for (const auto& c : v["plans"].a) {
        const auto tag = "plan" + std::to_string(n++);
        const auto& in = c["input"];
        PlanInput p;
        p.analysis = analysisFrom(in["analysis"]);
        p.reference = in["reference"].s;
        p.loudness = (int)in["loudness"].num();
        p.amount = in["amount"].num();
        p.bpm = in["bpm"].num();
        p.sampleRate = in["sampleRate"].num();
        for (const auto& [k, x] : in["masterFx"].o) p.masterFx[k] = (float)x.num();
        p.masterVol = in["masterVol"].num();
        p.trackVol = in["trackVol"].nums();
        for (const auto& t : in["tracks"].a) p.tracks.push_back({machineFrom(t["machine"].s), t["name"].s});
        for (const auto& r : in["roles"].a) p.roles.push_back(r.isNull() ? std::optional<MixRole>{} : roleFrom(r.s));
        const auto plan = buildPlan(p);
        const auto& e = c["plan"];

        truth(plan.masterFx.size() == e["masterFx"].o.size(), true, tag + " masterFx key count");
        for (const auto& [k, x] : e["masterFx"].o) {
            const auto it = plan.masterFx.find(k);
            if (it == plan.masterFx.end()) { fail(tag + " missing " + k); continue; }
            near(it->second, (float)x.num(), tag + " " + k, 0);
        }
        truth(plan.trackVol.size() == e["trackVol"].size(), true, tag + " trackVol size");
        for (size_t i = 0; i < plan.trackVol.size() && i < e["trackVol"].size(); ++i) {
            truth(plan.trackVol[i].has_value(), !e["trackVol"][i].isNull(), tag + " trackVol present");
            if (plan.trackVol[i] && !e["trackVol"][i].isNull()) near(*plan.trackVol[i], (float)e["trackVol"][i].num(), tag + " trackVol", 0);
        }
        truth(plan.masterVol.has_value(), !e["masterVol"].isNull(), tag + " masterVol present");
        if (plan.masterVol && !e["masterVol"].isNull()) near(*plan.masterVol, (float)e["masterVol"].num(), tag + " masterVol", 0);
        checkEqFit(plan.eq, e["eq"], tag + " eq");
        truth(plan.levels.size() == e["levels"].size(), true, tag + " levels size");
        for (size_t i = 0; i < plan.levels.size() && i < e["levels"].size(); ++i) {
            const auto& g = plan.levels[i]; const auto& w = e["levels"][i];
            near(g.index, w["index"].num(), tag + " level index", 0);
            same(g.name, w["name"].s, tag + " level name");
            same(roleKey(g.role), w["role"].s, tag + " level role");
            same(roleKey(g.detected), w["detected"].s, tag + " level detected");
            near(g.current, w["current"].num(), tag + " level current");
            near(g.target, w["target"].num(), tag + " level target");
            near(g.move, w["move"].num(), tag + " level move");
        }
        truth(plan.moves.size() == e["moves"].size(), true, tag + " moves size");
        for (size_t i = 0; i < plan.moves.size() && i < e["moves"].size(); ++i) {
            const auto& g = plan.moves[i]; const auto& w = e["moves"][i];
            same(g.id, w["id"].s, tag + " move id");
            same(g.label, w["label"].s, tag + " move label");
            same(g.from, w["from"].s, tag + " move from");
            same(g.to, w["to"].s, tag + " move to");
        }
        truth(plan.notes.size() == e["notes"].size(), true, tag + " notes size");
        for (size_t i = 0; i < plan.notes.size() && i < e["notes"].size(); ++i) same(plan.notes[i], e["notes"][i].s, tag + " note");
    }
}

std::vector<Bands> bandsFrom(const Json& j) {
    std::vector<Bands> out;
    for (const auto& p : j.a) {
        Bands b{};
        for (size_t i = 0; i < b.size(); ++i) b[i] = p[i].num();
        out.push_back(b);
    }
    return out;
}

void testKit(const Json& v) {
    KitCapture cap(v["sampleRate"].num(), (int)v["pads"].num());
    for (const auto& blk : v["blocks"].a) {
        const auto data = blk["data"].floats();
        cap.add((int)blk["pad"].num(), data.data(), (int)data.size(), (int)blk["frames"].num());
    }
    near(cap.frames, v["frames"].num(), "KitCapture frames", 0);
    const auto bands = bandsFrom(v["bands"]), mean = bandsFrom(v["mean"]), got = cap.meanBands();
    for (size_t p = 0; p < bands.size(); ++p) {
        nearAll(cap.bands[p], v["bands"][p], "KitCapture bands", 1e-9);
        nearAll(got[p], v["mean"][p], "KitCapture mean", 1e-9);
    }
    int n = 0;
    for (const auto& c : v["fits"].a) {
        const auto tag = "fitKitMix" + std::to_string(n++) + " " + c["ref"].s;
        const auto fit = fitKitMix(bandsFrom(c["pads"]), c["lvl"].nums(), referenceById(c["ref"].s), c["amount"].num());
        const auto& e = c["fit"];
        truth(fit.has_value(), !e.isNull(), tag + " has value");
        if (!fit || e.isNull()) continue;
        boolAll(fit->heard, e["heard"], tag + " heard");
        nearAll(fit->moves, e["moves"], tag + " moves", 1e-9);
        for (size_t p = 0; p < fit->levels.size(); ++p) {
            truth(fit->levels[p].has_value(), !e["levels"][p].isNull(), tag + " level present");
            if (fit->levels[p] && !e["levels"][p].isNull()) near(*fit->levels[p], e["levels"][p].num(), tag + " level", 0);
        }
        nearAll(fit->before, e["before"], tag + " before");
        nearAll(fit->after, e["after"], tag + " after");
        nearAll(fit->target, e["target"], tag + " target");
        boolAll(fit->present, e["present"], tag + " present");
        near(fit->errorBefore, e["errorBefore"].num(), tag + " errorBefore");
        near(fit->errorAfter, e["errorAfter"].num(), tag + " errorAfter");
        nearAll(fit->share, e["share"], tag + " share");
    }
}

void testAnalyserSpectrum() {
    // A bin-centred sine of amplitude A reads A * 0.42 / 2 (Blackman coherent gain).
    const int n = 1024;
    std::vector<float> x(n), out(n / 2);
    for (int i = 0; i < n; ++i) x[i] = (float)(0.5 * std::sin(2 * 3.14159265358979323846 * 64 * i / n));
    analyserSpectrumDb(x.data(), n, out.data());
    near(out[64], 20 * std::log10(0.5 * 0.42 / 2), "analyserSpectrumDb peak", 1e-4);
    truth(out[80] < out[64] - 80, true, "analyserSpectrumDb leakage");
}
}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/')) + "/fixtures/automix-vectors.json";
    std::ifstream file(path);
    if (!file) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 2; }
    std::stringstream ss; ss << file.rdbuf();
    const std::string text = ss.str();
    Parser parser{text.c_str()};
    const Json v = parser.parse();

    testFormatting(v);
    testEq(v["eq"]);
    testComp(v["comp"]);
    testMix(v["mix"]);
    testKit(v["kit"]);
    testAnalyserSpectrum();

    if (failures) { std::fprintf(stderr, "automix_test: %d of %d checks failed\n", failures, checks); return 1; }
    std::printf("automix_test: all %d checks passed\n", checks);
    return 0;
}
