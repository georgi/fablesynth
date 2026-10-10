#include "SerumPatch.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <regex>
#include <set>

namespace serumimport {
namespace {

constexpr int NUM_FACTORY_TABLES = 6; // WT-1 TABLE_NAMES; user tables follow

float clampf(double x, double lo, double hi) { return (float)std::max(lo, std::min(hi, x)); }
// Parameter values here are exact integers or flags; > 0.5 reads them without float ==.
bool isOn(float x) { return x > 0.5f; }

// First number in a display string ("-12.0 dB" -> -12, "75% (-5.0 dB)" -> 75).
double num(const std::string& text) {
    static const std::regex re(R"(-?\d+(?:\.\d+)?)");
    std::smatch m;
    return std::regex_search(text, m, re) ? std::stod(m.str()) : 0.0;
}
double pct(const std::string& text) { return num(text) / 100.0; }
bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// dB from "75% (-5.0 dB)" or "-3.6 dB"; minus infinity (any encoding) -> silent.
bool dbOf(const std::string& text, double& out) {
    static const std::regex paren(R"(\((-?[\d.]+) dB\))"), plain(R"((-?[\d.]+) dB)");
    std::smatch m;
    if (contains(text, "oo") || (!std::regex_search(text, m, paren) && !std::regex_search(text, m, plain))) return false;
    out = std::stod(m[1].str());
    return true;
}
double ampOfDbText(const std::string& text) {
    double d;
    return dbOf(text, d) ? std::pow(10.0, d / 20.0) : 0.0;
}

// Tempo-synced rate text -> cycles per quarter note, or < 0 when not synced.
double cyclesPerBeat(std::string t) {
    if (t == "fast" || t.empty()) return -1;
    double mul = 1;
    if (t.size() > 2 && t.compare(t.size() - 2, 2, " t") == 0) { mul = 1.5; t.resize(t.size() - 2); }
    if (!t.empty() && t.back() == '.') { mul *= 2.0 / 3.0; t.pop_back(); }
    if (t.size() >= 3 && t.compare(t.size() - 3, 3, "bar") == 0) {
        const double bars = std::regex_search(t, std::regex(R"(\d)")) ? num(t) : 1.0;
        return mul / (4.0 * bars);
    }
    std::smatch m;
    if (std::regex_match(t, m, std::regex(R"(1/(\d+))"))) return mul * std::stoi(m[1].str()) / 4.0;
    return -1;
}

const char* FILTER_TYPES[] = {"LP 12", "LP 24", "BP 12", "HP 12", "NOTCH", "COMB", "VOWEL"};

// Serum filter name -> WT-1 FILTER_TYPES index (-1: not available), exact?
int filterType(const std::string& n, double var, bool& exact) {
    exact = false;
    auto in = [&](std::initializer_list<const char*> l) { for (auto* x : l) if (n == x) return true; return false; };
    if (in({"Low 12", "LP 12", "MG Low 12"})) { exact = true; return 0; }
    if (in({"MG Low 6", "Low 6"})) return 0;
    if (in({"Low 24", "MG Low 24"})) { exact = true; return 1; }
    if (in({"Low 18", "MG Low 18", "German LP", "French LP", "Add Bass"})) return 1;
    if (in({"High 12", "HP 12"})) { exact = true; return 3; }
    if (startsWith(n, "High")) return 3;
    if (in({"Band 12", "BP 12"})) { exact = true; return 2; }
    if (in({"Band 24", "Band EQ 12", "Peak 24", "LH 12", "HB 12", "PP 12"})) return 2;
    if (startsWith(n, "Notch") || in({"NN 12", "BN 12", "HN 12", "LN 12"})) { exact = startsWith(n, "Notch"); return 4; }
    if (startsWith(n, "B/P/N")) return var < 2.0 / 3.0 ? 2 : 4;
    if (startsWith(n, "L/B/H") || startsWith(n, "L/P/H") || startsWith(n, "L/N/H")) {
        const int mid = startsWith(n, "L/N/H") ? 4 : 2;
        return var < 1.0 / 3.0 ? 0 : var < 2.0 / 3.0 ? mid : 3;
    }
    if (startsWith(n, "Cmb") || startsWith(n, "Combs") || startsWith(n, "Flg")) return 5;
    if (startsWith(n, "Formant")) return 6;
    return -1; // phasers, EQ shapes, reverb, ring mod, S&H
}

struct Div { const char* name; double cpb; };
const Div LFO_DIVS[] = {{"1/1", 0.25}, {"1/2", 0.5}, {"1/4", 1}, {"1/4.", 2.0 / 3}, {"1/4T", 1.5}, {"1/8", 2},
                        {"1/8.", 4.0 / 3}, {"1/8T", 3}, {"1/16", 4}, {"1/16T", 6}, {"1/32", 8}};
const Div DELAY_DIVS[] = {{"1/4", 1}, {"1/4.", 2.0 / 3}, {"1/8", 2}, {"1/8.", 4.0 / 3}, {"1/8T", 3}, {"1/16", 4}};

template <size_t N> int nearestDiv(const Div (&divs)[N], double cpb) {
    int best = 0;
    for (int i = 1; i < (int)N; ++i)
        if (std::abs(std::log(divs[i].cpb / cpb)) < std::abs(std::log(divs[best].cpb / cpb))) best = i;
    return best;
}

// Serum mod source id -> WT-1 MOD_SOURCES index (0: none). Measured by
// routing each id to A Vol inside Serum (s1host srcprobe). Env 1 (2) maps to
// MOD ENV 2 when Serum's Env 3 is free (see convert()).
std::map<int, int> sourceMap() {
    return {{1, 13}, {3, 3}, {4, 8}, {5, 1}, {6, 2}, {7, 6}, {8, 7}, {9, 4}, {10, 5}, {11, 14},
            {12, 15}, {13, 15}, {15, 15}, {16, 15}, {17, 9}, {18, 10}, {19, 11}, {20, 12}};
}
std::string sourceName(int s) {
    static const std::map<int, std::string> names = {
        {1, "MOD WHEEL"}, {2, "ENV 1"}, {3, "ENV 2"}, {4, "ENV 3"}, {5, "LFO 1"}, {6, "LFO 2"}, {7, "LFO 3"},
        {8, "LFO 4"}, {9, "VELO"}, {10, "NOTE"}, {11, "AFTERTOUCH"}, {12, "CHAOS 1"}, {13, "CHAOS 2"},
        {15, "NOTEON RAND 1"}, {16, "NOTEON RAND 2"}, {17, "MACRO 1"}, {18, "MACRO 2"}, {19, "MACRO 3"},
        {20, "MACRO 4"}, {21, "PITCH BEND"}};
    auto it = names.find(s);
    return it != names.end() ? it->second : "src" + std::to_string(s);
}
bool isLfoSource(int wt) { return wt == 1 || wt == 2 || wt == 6 || wt == 7; } // Serum LFOs modulate unipolar

const double CUT_OCT = std::log2(22050.0 / 8.0); // Serum cutoff knob span in octaves
struct DestMap { int dst; double scale; };
const std::map<std::string, DestMap>& destMap() {
    static const std::map<std::string, DestMap> m = {
        {"A WTPos", {1, 1}}, {"B WTPos", {2, 1}}, {"Fil Cutoff", {3, CUT_OCT / 5}},
        {"Mast.Tun", {4, 1.0 / 12}}, {"A CoarsePit", {4, 64.0 / 12}}, {"B CoarsePit", {4, 64.0 / 12}},
        {"Amp.", {5, 1}}, {"A Vol", {7, 1}}, {"B Vol", {8, 1}}, {"A UniDet", {11, 1}}, {"A Uni LR", {12, 1}},
        {"A Pan", {13, 1}}, {"B UniDet", {14, 1}}, {"B Uni LR", {15, 1}}, {"B Pan", {16, 1}},
        {"Fil Reso", {17, 0.9}}, {"Fil Driv", {18, 1}}, {"Sub Osc Level", {24, 1}}, {"Noise Level", {25, 1}},
        {"A UniBlend", {26, 1}}, {"B UniBlend", {27, 1}}};
    return m;
}

std::string fmtSigned(double x, int decimals) {
    return (x >= 0 ? "+" : "") + juce::String(x, decimals).toStdString();
}

} // namespace

// ------------------------------------------------------------------ fxp

std::vector<ModSlot> FxpData::slots() const {
    std::vector<ModSlot> out;
    if (main.getSize() < (size_t)MAIN_STRUCT_SIZE) return out;
    const auto* b = (const uint8_t*)main.getData();
    for (int k = 0; k < 16; ++k) {
        const int m = 32 + 40 * k;
        if (b[m + 2] != k || b[m + 3] != 0xff) continue; // first two marker bytes are padding
        int32_t src; float amt; uint16_t dest;
        std::memcpy(&src, b + m - 12, 4); std::memcpy(&amt, b + m - 28, 4); std::memcpy(&dest, b + m - 6, 2);
        if ((src & 0xffff) == 0) continue;
        out.push_back({k + 1, src & 0xffff, (src >> 16) & 0xffff, amt, dest});
    }
    return out;
}

std::string FxpData::cstr(int offset) const {
    if ((size_t)offset + 512 > main.getSize()) return {};
    const auto* p = (const char*)main.getData() + offset;
    return std::string(p, strnlen(p, 512));
}

float FxpData::floatAt(int offset) const {
    float f = 0;
    if ((size_t)offset + 4 <= main.getSize()) std::memcpy(&f, (const char*)main.getData() + offset, 4);
    return f;
}

bool parseFxp(const juce::MemoryBlock& file, FxpData& out, juce::String& error) {
    const auto* d = (const uint8_t*)file.getData();
    if (file.getSize() < 64 || std::memcmp(d, "CcnK", 4) != 0 || std::memcmp(d + 8, "FPCh", 4) != 0) {
        error = "not an fxp program chunk"; return false;
    }
    if (std::memcmp(d + 16, "XfsX", 4) != 0) { error = "not a Serum preset"; return false; }
    out.programName = juce::String::fromUTF8((const char*)d + 28, (int)strnlen((const char*)d + 28, 28));
    const size_t n = std::min<size_t>(juce::ByteOrder::bigEndianInt(d + 56), file.getSize() - 60);
    out.chunk = juce::MemoryBlock(d + 60, n);
    juce::MemoryInputStream in(out.chunk, false);
    juce::GZIPDecompressorInputStream gz(&in, false, juce::GZIPDecompressorInputStream::zlibFormat);
    juce::MemoryOutputStream main;
    main.writeFromInputStream(gz, MAIN_STRUCT_SIZE);
    out.main = main.getMemoryBlock();
    if (out.main.getSize() != (size_t)MAIN_STRUCT_SIZE) { error = "unsupported Serum chunk layout"; return false; }
    return true;
}

// ------------------------------------------------------------------ tables

Table tableFromCapture(const std::vector<float>& x, const std::string& name) {
    Table t;
    t.name = name.substr(0, 24);
    const int frames = (int)(x.size() / FRAME);
    float peak = 0;
    for (float v : x) peak = std::max(peak, std::abs(v));
    if (peak <= 0) peak = 1;
    bool still = true; // static table: capture noise only
    for (int f = 1; f < frames && still; ++f)
        for (int i = 0; i < FRAME; i += 8)
            if (std::abs(x[(size_t)(f * FRAME + i)] - x[(size_t)i]) >= 2e-3f * peak) { still = false; break; }
    t.frames = still ? 1 : frames;
    t.wave.assign(x.begin(), x.begin() + (long)t.frames * FRAME);
    for (auto& v : t.wave) v /= peak;
    return t;
}

Table subTable(const std::string& shape) {
    Table t;
    t.name = "SUB " + juce::String(shape).toUpperCase().toStdString();
    t.frames = 1;
    t.wave.resize(FRAME);
    for (int i = 0; i < FRAME; ++i) {
        const double x = (double)i / FRAME;
        double v;
        if (shape == "Sine") v = std::sin(2 * juce::MathConstants<double>::pi * x);
        else if (shape == "Triangle") v = x < 0.25 ? 4 * x : x < 0.75 ? 2 - 4 * x : 4 * x - 4;
        else if (shape == "Saw") v = 1 - 2 * x;
        else if (shape == "Square") v = x < 0.5 ? 1 : -1;
        else if (shape == "Pulse") v = x < 0.25 ? 1 : -1;
        else v = std::tanh(4 * std::sin(2 * juce::MathConstants<double>::pi * x)) / std::tanh(4.0); // RoundRect
        t.wave[(size_t)i] = (float)v;
    }
    return t;
}

// ------------------------------------------------------------------ convert

ImportedPreset convert(const std::vector<SerumParam>& dump, const FxpData& fxp, const Capture& capture,
                       const std::string& name, const std::string& category) {
    ImportedPreset r;
    r.name = name; r.category = category;
    std::map<std::string, std::string> P;
    std::map<std::string, float> V;
    std::vector<std::string> vstNames;
    for (const auto& p : dump) { P[p.name] = p.text; V[p.name] = p.v; vstNames.push_back(p.name); }
    auto txt = [&](const std::string& k) { auto it = P.find(k); return it != P.end() ? it->second : std::string(); };
    auto val = [&](const std::string& k) { auto it = V.find(k); return it != V.end() ? (double)it->second : 0.0; };
    std::map<std::string, float> out;
    auto has = [&](const std::string& k) { auto it = out.find(k); return it != out.end() && isOn(it->second); };
    auto& losses = r.losses;
    auto& tables = r.tables;

    // Oscillators A/B
    for (const char* X : {"A", "B"}) {
        const std::string x = X, pre = "osc" + x;
        const bool on = txt("Osc " + x + " On") == "on";
        out[pre + ".on"] = on ? 1 : 0;
        if (!on) continue;
        const auto& cap = x == "A" ? capture.oscA : capture.oscB;
        if (!cap.empty()) {
            std::string tname = fxp.cstr(x == "A" ? 15368 : 15880);
            tname = tname.substr(tname.find_last_of('/') == std::string::npos ? 0 : tname.find_last_of('/') + 1);
            tname = tname.substr(0, tname.find_last_of('.'));
            if (tname.empty()) tname = name.substr(0, 16) + " " + x;
            out[pre + ".table"] = (float)(NUM_FACTORY_TABLES + tables.size());
            tables.push_back(tableFromCapture(cap, tname));
        } else {
            losses.push_back("osc " + x + ": no capture");
        }
        out[pre + ".pos"] = (float)val(x + " WTPos");
        out[pre + ".oct"] = clampf(num(txt(x + " Octave")), -3, 3);
        out[pre + ".semi"] = clampf(num(txt(x + " Semi")), -12, 12);
        out[pre + ".fine"] = clampf(num(txt(x + " Fine")), -100, 100);
        out[pre + ".unison"] = clampf((int)num(txt(x + " Unison")), 1, 16);
        out[pre + ".detune"] = clampf(num(txt(x + " UniDet")), 0, 1);
        out[pre + ".blend"] = clampf(pct(txt(x + " UniBlend")), 0, 1);
        out[pre + ".spread"] = clampf(pct(txt(x + " Uni LR")), 0, 1);
        // Serum amplitude = knob^2; keep the default 75% at WT-1's default 0.75.
        out[pre + ".level"] = clampf(0.75 * std::pow(val(x + " Vol"), 2) / (0.75 * 0.75), 0, 1);
        out[pre + ".pan"] = clampf(num(txt(x + " Pan")) / 50, -1, 1);
        const auto warp = txt("WarpOsc" + x);
        if (!warp.empty() && warp != "Off") losses.push_back("osc " + x + " warp " + warp + ": static shape baked, modulation lost");
    }

    // Sub. Serum's sub plays at note + octave (0 = note pitch) with six shapes; a
    // free WT-1 oscillator reproduces it exactly, WT-1's own sub is the fallback.
    std::string subSlot;
    out["sub.on"] = 0;
    if (txt("Osc S On") == "on") {
        const auto shape = txt("SubOscShape");
        const int octv = (int)num(txt("SubOscOctave"));
        const std::string free = !has("oscB.on") ? "B" : !has("oscA.on") ? "A" : "";
        if (!free.empty()) {
            subSlot = free;
            const std::string pre = "osc" + free;
            out[pre + ".on"] = 1;
            out[pre + ".table"] = (float)(NUM_FACTORY_TABLES + tables.size());
            out[pre + ".pos"] = 0; out[pre + ".oct"] = clampf(octv, -3, 3); out[pre + ".semi"] = 0; out[pre + ".fine"] = 0;
            out[pre + ".unison"] = 1;
            out[pre + ".pan"] = clampf(num(txt("Sub Osc Pan")) / 50, -1, 1);
            out[pre + ".level"] = clampf(0.879 * val("Sub Osc Level"), 0, 1); // Serum sub RMS is linear in its knob
            tables.push_back(subTable(shape));
        } else {
            out["sub.on"] = 1;
            out["sub.shape"] = (shape == "Square" || shape == "Pulse" || shape == "Saw") ? 1 : 0;
            out["sub.oct"] = clampf(octv, -2, -1);
            if ((shape != "Sine" && shape != "Square") || (octv != -1 && octv != -2))
                losses.push_back("sub " + shape + " " + fmtSigned(octv, 0) + " oct -> " + (isOn(out["sub.shape"]) ? "SQR " : "SINE ")
                                 + fmtSigned(out["sub.oct"], 0) + " oct (both oscillators in use)");
        }
        // Serum sub RMS is linear in its knob; WT-1's sub gain is level^2 * 0.3.
        out["sub.level"] = clampf(0.968 * std::sqrt(val("Sub Osc Level")), 0, 1);
    }
    out["noise.on"] = txt("Osc N On") == "on" ? 1 : 0;
    if (isOn(out["noise.on"])) {
        const auto nname = fxp.cstr(16392);
        const bool pink = juce::String(nname).containsIgnoreCase("pink");
        out["noise.type"] = pink ? 1 : 0;
        out["noise.level"] = clampf(0.662 * val("Noise Level"), 0, 1); // Serum noise RMS = 0.086 v^2
        losses.push_back("noise sample \"" + nname + "\" -> " + (pink ? "PINK" : "WHITE"));
    }

    // Filter. WT-1 SPLIT sends osc A (+ sub, noise) through filter 1 and osc B
    // through filter 2, so an oscillator that bypasses Serum's filter rides the
    // path whose filter is off.
    std::map<char, bool> on = {{'A', has("oscA.on")}, {'B', has("oscB.on")}, {'N', has("noise.on")}, {'S', has("sub.on")}};
    std::map<char, bool> fil;
    for (char o : std::string("ABNS")) fil[o] = txt(std::string("Osc") + o + ">Fil") == "on";
    if (!subSlot.empty()) fil[subSlot[0]] = fil['S']; // the sub now lives in that oscillator
    std::set<char> routed;
    for (char o : std::string("ABNS")) if (on[o] && fil[o]) routed.insert(o);
    std::string fpre;
    if (txt("Filter On") == "on" && !routed.empty()) {
        const bool aPath = routed.count('A') || routed.count('N') || routed.count('S');
        if (!routed.count('B') && on['B']) { fpre = "filter"; out["filter.route"] = 2; out["filter2.on"] = 0; }
        else if (routed.count('B') && !aPath) { fpre = "filter2"; out["filter.route"] = 2; out["filter.on"] = 0; }
        else { fpre = "filter"; out["filter.route"] = 0; }
        const bool split = out["filter.route"] > 1.5f;
        const std::string path = split ? (fpre == "filter" ? "ANS" : "B") : "ABNS";
        std::string stray;
        for (char o : path) if (on[o] && !routed.count(o)) stray += o;
        if (!stray.empty()) losses.push_back("filter: " + stray + " bypass not reproducible (filtered too)");
        if (fpre == "filter2" && aPath) losses.push_back("filter: A-path oscillators routed but left dry");
    }
    if (!fpre.empty()) {
        out[fpre + ".on"] = 1;
        bool exact;
        int ft = filterType(txt("Fil Type"), pct(txt("Fil Var")), exact);
        if (ft < 0) { losses.push_back("filter " + txt("Fil Type") + " not available: filter off"); out[fpre + ".on"] = 0; ft = 1; }
        else if (!exact) losses.push_back("filter " + txt("Fil Type") + " -> " + FILTER_TYPES[ft]);
        out[fpre + ".type"] = (float)ft;
        out[fpre + ".cutoff"] = clampf(num(txt("Fil Cutoff")), 20, 20000);
        out[fpre + ".res"] = clampf(pct(txt("Fil Reso")) * 0.9, 0, 1);
        out[fpre + ".drive"] = clampf(pct(txt("Fil Driv")), 0, 1);
        out[fpre + ".env"] = 0;
        out[fpre + ".key"] = 0;
    } else {
        out["filter.on"] = 0;
    }
    // Filter routes follow the filter that stands in for Serum's.
    const std::map<int, int> retarget = fpre == "filter2" ? std::map<int, int>{{3, 9}, {17, 10}, {18, 21}} : std::map<int, int>{};

    // Envelopes: Serum's time text is ambiguous ("620 s" means 620 ms); the
    // measured knob law is exact: t = 32 s * v^5. Segments use WT-1's CURVE model.
    for (int n = 1; n <= 3; ++n) {
        const std::string e = "Env" + std::to_string(n), pre = "env" + std::to_string(n);
        auto t = [&](const char* k) { return 32.0 * std::pow(val(e + " " + k), 5); };
        const double a = t("Atk"), h = t(n == 1 ? "Hold" : "Hld"), d = t("Dec"), rl = t("Rel");
        const double sus = n == 1 ? ampOfDbText(txt(e + " Sus")) : pct(txt(e + " Sus"));
        out[pre + ".model"] = 1;
        out[pre + ".h"] = clampf(h, 0, 8);
        out[pre + ".ac"] = (float)val("A curve" + std::to_string(n));
        out[pre + ".dc"] = (float)val("D curve" + std::to_string(n));
        out[pre + ".rc"] = (float)val("R curve" + std::to_string(n));
        out[pre + ".a"] = clampf(a, 0.001, 8);
        out[pre + ".d"] = clampf(d, 0.005, 10);
        out[pre + ".s"] = clampf(sus, 0, 1);
        out[pre + ".r"] = clampf(rl, 0.005, 12);
    }
    const auto slots = fxp.slots();
    auto srcMap = sourceMap();
    bool env1Src = false, env3Used = false;
    for (const auto& m : slots) {
        env1Src = env1Src || m.src == 2 || m.aux == 2;
        env3Used = env3Used || m.src == 4 || m.aux == 4;
    }
    if (env1Src && !env3Used) {
        for (const char* k : {"a", "d", "s", "r", "model", "h", "ac", "dc", "rc"})
            out[std::string("env3.") + k] = out[std::string("env1.") + k];
        srcMap[2] = 8; // Env 1 copied into MOD ENV 2
    }

    // LFOs. Shapes are not decoded yet: Serum's default triangle.
    for (int n = 1; n <= 4; ++n) {
        const std::string pre = "lfo" + std::to_string(n), rate = txt("LFO" + std::to_string(n) + " Rate");
        if (juce::String(rate).endsWith("Hz")) { out[pre + ".sync"] = 0; out[pre + ".rate"] = clampf(num(rate), 0.02, 30); }
        else if (rate == "fast") { out[pre + ".sync"] = 0; out[pre + ".rate"] = 30; }
        else {
            const double cpb = cyclesPerBeat(rate);
            if (cpb < 0) losses.push_back("LFO " + std::to_string(n) + " rate \"" + rate + "\" unparsed");
            else if (cpb < 0.25) {
                out[pre + ".sync"] = 0; out[pre + ".rate"] = clampf(cpb * 2, 0.02, 30); // at 120 BPM
                losses.push_back("LFO " + std::to_string(n) + " " + rate + " -> free " + juce::String(out[pre + ".rate"], 3).toStdString() + " Hz (120 BPM)");
            } else {
                const int best = nearestDiv(LFO_DIVS, cpb);
                out[pre + ".sync"] = 1; out[pre + ".syncrate"] = (float)best;
                if (std::abs(std::log(LFO_DIVS[best].cpb / cpb)) > 0.01)
                    losses.push_back("LFO " + std::to_string(n) + " " + rate + " -> " + LFO_DIVS[best].name);
            }
        }
        out[pre + ".shape"] = 1;
        const auto rise = txt("LFO" + std::to_string(n) + " Rise");
        if (!rise.empty() && rise != "Off" && rise != "0.0 s") {
            const double cpb = cyclesPerBeat(rise);
            const double secs = juce::String(rise).contains("ms") ? num(rise) / 1000 : num(rise);
            out[pre + ".rise"] = clampf(cpb < 0 ? secs : 0.5 / cpb, 0, 5);
        }
    }

    // Mod matrix
    struct Route { int src, dst; double amt; int aux; std::string label; };
    std::vector<Route> routes;
    std::vector<std::pair<int, double>> noteOffsets;
    for (const auto& m : slots) {
        const auto sname = sourceName(m.src);
        const std::string dname = m.dest < (int)vstNames.size() ? vstNames[(size_t)m.dest] : "dest" + std::to_string(m.dest);
        const auto si = srcMap.find(m.src);
        if (si == srcMap.end()) { losses.push_back("route " + sname + " -> " + dname + ": source not available"); continue; }
        const auto di = destMap().find(dname);
        if (di == destMap().end()) { losses.push_back("route " + sname + " -> " + dname + ": destination not available"); continue; }
        if ((dname == "A CoarsePit" && has("oscB.on")) || (dname == "B CoarsePit" && has("oscA.on")))
            losses.push_back("route " + sname + " -> " + dname + ": per-osc pitch applied to both oscillators");
        int dst = di->second.dst;
        if (dname == "Sub Osc Level" && !subSlot.empty()) dst = subSlot == "A" ? 7 : 8;
        if (auto rt = retarget.find(dst); rt != retarget.end()) dst = rt->second;
        const std::string outKey = m.slot >= 10 ? "Mod" + std::to_string(m.slot) + " out" : "Mod " + std::to_string(m.slot) + " out";
        double amt = m.amt * di->second.scale * num(txt(outKey)) / 100.0;
        if (m.src == 12 || m.src == 13) losses.push_back("route " + sname + " -> " + dname + ": " + sname + " -> RANDOM (per note)");
        if (m.src == 10) {
            // Serum NOTE = n/127 (unipolar); WT-1 NOTE = (n - 60)/24. Match the
            // slope, then fold Serum's value at note 60 into the base below.
            noteOffsets.emplace_back(dst, amt * 60.0 / 127.0);
            amt *= 24.0 / 127.0;
        }
        int aux = 0;
        if (m.aux) {
            auto ai = srcMap.find(m.aux);
            if (ai == srcMap.end()) losses.push_back("route " + sname + " -> " + dname + ": aux src" + std::to_string(m.aux) + " not available");
            else aux = ai->second;
        }
        routes.push_back({si->second, dst, amt, aux, sname + " -> " + dname});
    }
    const std::map<int, std::string> baseOf = {{1, "oscA.pos"}, {2, "oscB.pos"}, {3, "filter.cutoff"}, {9, "filter2.cutoff"},
                                               {7, "oscA.level"}, {8, "oscB.level"}, {17, "filter.res"}, {10, "filter2.res"},
                                               {24, "sub.level"}, {25, "noise.level"}};
    for (auto [dst, off] : noteOffsets) {
        auto b = baseOf.find(dst);
        if (b == baseOf.end() || !out.count(b->second)) continue;
        auto& v = out[b->second];
        v = juce::String(b->second).endsWith("cutoff") ? clampf(v * std::pow(2.0, off * 5), 20, 20000) : clampf(v + off, 0, 1);
    }
    // Routes sum, so a depth beyond +-1 spreads over extra slots while free.
    std::vector<int> need;
    for (const auto& rt : routes) need.push_back(std::max(1, (int)std::ceil(std::abs(rt.amt) - 1e-9)));
    auto total = [&] { int s = 0; for (int x : need) s += x; return s; };
    while (total() > 16 && std::any_of(need.begin(), need.end(), [](int x) { return x > 1; }))
        --*std::max_element(need.begin(), need.end());
    int slot = 1;
    for (size_t i = 0; i < routes.size(); ++i) {
        const auto& rt = routes[i];
        const int n = need[i];
        if (std::abs(rt.amt) > n) losses.push_back("route " + rt.label + ": depth " + fmtSigned(rt.amt, 2) + " clipped");
        const float part = clampf(rt.amt / n, -1, 1);
        for (int k = 0; k < n; ++k) {
            if (slot > 16) { losses.push_back("route " + rt.label + ": no free slot"); break; }
            const std::string p = "mat" + std::to_string(slot);
            out[p + ".src"] = (float)rt.src; out[p + ".dst"] = (float)rt.dst; out[p + ".amt"] = part;
            out[p + ".pol"] = isLfoSource(rt.src) ? 1.0f : 0.0f; out[p + ".aux"] = (float)rt.aux;
            ++slot;
        }
    }

    // Macros, performance, voice mode
    for (int n = 1; n <= 4; ++n) out["macro" + std::to_string(n)] = clampf(pct(txt("Macro " + std::to_string(n))), 0, 1);
    out["perf.modwheel"] = clampf(val("Mod Wheel"), 0, 1);
    out["master.glide"] = clampf(8.0 * std::pow(val("PortTime"), 5), 0, 0.5); // knob law t = 8 s * v^5
    // Voice mode lives in the chunk's internal floats (validated by measuring
    // polyphony in Serum): MONO, LEGATO, and voice count (0 = one voice).
    out["master.mono"] = (fxp.floatAt(14344) >= 0.5f || fxp.floatAt(14372) <= 0.0f) ? 1.0f : 0.0f;
    out["master.legato"] = fxp.floatAt(14348) >= 0.5f ? 1.0f : 0.0f;

    // FX
    const std::pair<const char*, const char*> lostFx[] = {{"flanger", "Flg"}, {"phaser", "Phs"}, {"hyper/dimension", "Hyp"}, {"FX filter", "FX Fil"}};
    for (auto [label, key] : lostFx)
        if (txt(std::string(key) + " Enable") == "on") losses.push_back(std::string("FX ") + label + " not available");
    if (txt("Rev Enable") == "on") {
        out["fx.reverb.on"] = 1; out["fx.reverb.mix"] = (float)pct(txt("Verb Wet")); out["fx.reverb.size"] = (float)pct(txt("VerbSize"));
    }
    if (txt("Dly Enable") == "on") {
        out["fx.delay.on"] = 1; out["fx.delay.mix"] = (float)pct(txt("Dly_Wet"));
        out["fx.delay.fb"] = clampf(pct(txt("Dly_Feed")), 0, 0.92);
        out["fx.delay.mode"] = txt("Dly_Mode") == "Ping-Pong" ? 0.0f : 1.0f;
        out["fx.delay.wow"] = 0; out["fx.delay.flutter"] = 0; out["fx.delay.sat"] = 0;
        const double cpb = cyclesPerBeat(txt("Dly_TimL"));
        if (txt("Dly_BPM_Sync") == "on" && cpb > 0) { out["fx.delay.sync"] = 1; out["fx.delay.div"] = (float)nearestDiv(DELAY_DIVS, cpb); }
        else { out["fx.delay.sync"] = 0; out["fx.delay.time"] = clampf(num(txt("Dly_TimL")) / 1000, 0.02, 1.5); }
    }
    if (txt("Cho Enable") == "on") {
        out["fx.chorus.on"] = 1; out["fx.chorus.rate"] = clampf(num(txt("Cho_Rate")), 0.05, 8);
        out["fx.chorus.depth"] = clampf(num(txt("Cho_Dep")) / 13, 0, 1); out["fx.chorus.mix"] = (float)pct(txt("Cho_Wet"));
    }
    if (txt("Dist Enable") == "on") {
        const auto mode = txt("Dist_Mode");
        out["fx.drive.on"] = 1; out["fx.drive.amt"] = (float)pct(txt("Dist_Drv")); out["fx.drive.mix"] = (float)pct(txt("Dist_Wet"));
        out["fx.drive.type"] = (contains(mode, "Hard") || contains(mode, "Diode")) ? 2.0f : 0.0f;
        if (mode != "Tube" && mode != "SoftClip" && mode != "HardClip") losses.push_back("distortion mode " + mode + " approximated");
    }
    // WT-1's leveling compressor defaults on; Serum's compressor is off unless enabled.
    out["fx.comp.on"] = 0;
    if (txt("Comp Enable") == "on" && txt("CmpMBnd") == "MultBand") {
        out["fx.ott.on"] = 1; out["fx.ott.depth"] = clampf(pct(txt("Comp_Wet")) * 0.6, 0, 1);
    } else if (txt("Comp Enable") == "on") {
        out["fx.comp.on"] = 1; out["fx.comp.thr"] = clampf(num(txt("Cmp_Thr")), -40, 0);
        out["fx.comp.ratio"] = txt("Cmp_Rat") == "Limit" ? 20.0f : clampf(num(txt("Cmp_Rat")), 1, 20);
        out["fx.comp.att"] = clampf(num(txt("Cmp_Att")) / 1000, 0.0001, 0.1);
        out["fx.comp.rel"] = clampf(num(txt("Cmp_Rel")) / 1000, 0.01, 2);
        out["fx.comp.gain"] = clampf(num(txt("CmpGain")), 0, 12);
    }
    if (txt("EQ Enable") == "on") {
        out["fx.eq.on"] = 1;
        out["fx.eq.low"] = clampf(num(txt("EQ VolL")), -15, 15); out["fx.eq.lfreq"] = clampf(num(txt("EQ FrqL")), 20, 20000);
        out["fx.eq.high"] = clampf(num(txt("EQ VolH")), -15, 15); out["fx.eq.hfreq"] = clampf(num(txt("EQ FrqH")), 20, 20000);
        out["fx.eq.ltype"] = txt("EQ TypL") == "Shelf" ? 0.0f : 1.0f;
        out["fx.eq.htype"] = txt("EQ TypH") == "Shelf" ? 2.0f : 1.0f;
    }
    // Both masters are knob-squared gains: map the knob, default 70% -> 0.75.
    out["master.volume"] = clampf(val("MasterVol") * 0.75 / 0.70, 0, 1);

    r.params.assign(out.begin(), out.end());
    return r;
}

// ------------------------------------------------------------------ JSON

juce::var toJson(const ImportedPreset& p) {
    auto* o = new juce::DynamicObject();
    o->setProperty("name", juce::String(p.name));
    o->setProperty("category", juce::String(p.category));
    o->setProperty("source", "serum1");
    auto* params = new juce::DynamicObject();
    for (const auto& [k, v] : p.params) params->setProperty(juce::Identifier(k), v);
    o->setProperty("params", juce::var(params));
    juce::Array<juce::var> tables;
    for (const auto& t : p.tables) {
        auto* tj = new juce::DynamicObject();
        tj->setProperty("v", 1);
        tj->setProperty("name", juce::String(t.name));
        tj->setProperty("frames", t.frames);
        // Standard base64 of little-endian float32 (web usertables.ts encodeWave).
        tj->setProperty("wave", juce::Base64::toBase64(t.wave.data(), t.wave.size() * sizeof(float)));
        tables.add(juce::var(tj));
    }
    o->setProperty("tables", tables);
    juce::Array<juce::var> losses;
    for (const auto& l : p.losses) losses.add(juce::String(l));
    o->setProperty("losses", losses);
    return juce::var(o);
}

bool fromJson(const juce::var& v, ImportedPreset& p) {
    if (!v.isObject()) return false;
    p.name = v["name"].toString().toStdString();
    p.category = v["category"].toString().toStdString();
    p.params.clear(); p.tables.clear(); p.losses.clear();
    if (auto* params = v["params"].getDynamicObject())
        for (const auto& kv : params->getProperties()) p.params.emplace_back(kv.name.toString().toStdString(), (float)(double)kv.value);
    if (auto* tables = v["tables"].getArray())
        for (const auto& tj : *tables) {
            Table t;
            t.name = tj["name"].toString().toStdString();
            t.frames = (int)tj["frames"];
            juce::MemoryOutputStream mo;
            if (!juce::Base64::convertFromBase64(mo, tj["wave"].toString())) return false;
            const auto mb = mo.getMemoryBlock();
            t.wave.resize(mb.getSize() / sizeof(float));
            std::memcpy(t.wave.data(), mb.getData(), t.wave.size() * sizeof(float));
            if (t.frames <= 0 || (int)t.wave.size() != t.frames * FRAME) return false;
            p.tables.push_back(std::move(t));
        }
    if (auto* losses = v["losses"].getArray())
        for (const auto& l : *losses) p.losses.push_back(l.toString().toStdString());
    return !p.name.empty();
}

} // namespace serumimport
