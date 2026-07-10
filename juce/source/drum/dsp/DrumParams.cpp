// Transcribes src/drum/params.ts row for row: PAD_DEFS (params.ts:29-71)
// repeated for 16 pads, then GLOBAL_DEFS (params.ts:75-96).
#include "DrumParams.h"

#include <unordered_map>

namespace fable {

const std::vector<std::string> DRUM_TABLE_NAMES  = {"THUD", "CRACK", "TINE", "GRIT", "PRIME", "BLOOM", "PULSE", "VOX", "CHIME", "GLITCH", "808SD", "808CP", "808CH", "808OH", "808CY"};
const std::vector<std::string> DRUM_FILTER_TYPES = {"LP 12", "LP 24", "BP 12", "HP 12", "NOTCH"};
const std::vector<std::string> DMOD_SOURCES      = {"—", "MOD ENV", "VELO", "RAND"};
const std::vector<std::string> DMOD_DESTS        = {"—", "A POS", "B POS", "LEVEL", "CUTOFF", "PITCH", "A FINE", "B FINE", "NOISE LVL", "RES"};
const std::vector<std::string> CHOKE_NAMES       = {"—", "CHK 1", "CHK 2", "CHK 3", "CHK 4"};
const std::vector<std::string> OUT_NAMES         = {"MAIN", "AUX 1", "AUX 2", "AUX 3", "AUX 4"};

const std::vector<std::string>& drumTableSlotNames() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out = DRUM_TABLE_NAMES;
        for (int i = 0; i < MAX_USER_TABLES; ++i) out.push_back("USER " + std::to_string(i + 1));
        return out;
    }();
    return v;
}

namespace {

// oscFields() in params.ts:29-40 — A/B differ only in table + level defaults.
void addOsc(std::vector<ParamInfo>& v, int base, const std::string& pre, bool isA) {
    const auto& slots = drumTableSlotNames();
    v.push_back({base + 0, pre + ".table",  "TABLE",    0, (float)slots.size() - 1, isA ? 0.0f : 1.0f, Curve::Int, Kind::Enum, &slots});
    v.push_back({base + 1, pre + ".pos",    "POS",      0, 1,   0,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + 2, pre + ".tune",   "TUNE",   -48, 48,  0,     Curve::Int, Kind::Float, nullptr});
    v.push_back({base + 3, pre + ".fine",   "FINE",  -100, 100, 0,     Curve::Int, Kind::Float, nullptr});
    v.push_back({base + 4, pre + ".phase",  "PHASE",    0, 1,   0,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + 5, pre + ".unison", "UNI",      1, 7,   1,     Curve::Int, Kind::Float, nullptr});
    v.push_back({base + 6, pre + ".detune", "DET",      0, 1,   0.2f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + 7, pre + ".level",  "LVL",      0, 1,   isA ? 0.75f : 0.0f, Curve::Lin, Kind::Float, nullptr});
}

// PAD_DEFS (params.ts:43-71), pids prefixed "pad<i>.".
void addPad(std::vector<ParamInfo>& v, int i) {
    const int b = i * DPAD_NFIELDS;
    const std::string p = "pad" + std::to_string(i) + ".";

    addOsc(v, b + DP_OSCA_TABLE, p + "oscA", true);
    addOsc(v, b + DP_OSCB_TABLE, p + "oscB", false);

    v.push_back({b + DP_NOISE_COLOR, p + "noise.color", "COLOR",  -1, 1,      0,      Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_NOISE_LEVEL, p + "noise.level", "LVL",     0, 1,      0,      Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_PENV_AMT,    p + "penv.amt",    "AMT",   -48, 48,     0,      Curve::Int, Kind::Float, nullptr});
    v.push_back({b + DP_PENV_DEC,    p + "penv.dec",    "DEC", 0.005f, 2,     0.06f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({b + DP_AENV_ATT,    p + "aenv.att",    "ATT",0.0005f, 0.5f,  0.001f, Curve::Log, Kind::Float, nullptr});
    v.push_back({b + DP_AENV_HOLD,   p + "aenv.hold",   "HOLD",    0, 0.25f,  0.01f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_AENV_DEC,    p + "aenv.dec",    "DEC", 0.005f, 4,     0.24f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({b + DP_AENV_CURVE,  p + "aenv.curve",  "CURVE",   0, 1,      0.35f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_FLT_ON,      p + "flt.on",      "ON",      0, 1,      0,      Curve::Int, Kind::Bool,  nullptr});
    v.push_back({b + DP_FLT_TYPE,    p + "flt.type",    "TYPE",    0, (float)DRUM_FILTER_TYPES.size() - 1, 0, Curve::Int, Kind::Enum, &DRUM_FILTER_TYPES});
    v.push_back({b + DP_FLT_CUT,     p + "flt.cut",     "CUT",    20, 20000,  1800,   Curve::Log, Kind::Float, nullptr});
    v.push_back({b + DP_FLT_RES,     p + "flt.res",     "RES",     0, 1,      0.18f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_FLT_DRIVE,   p + "flt.drive",   "DRIVE",   0, 1,      0,      Curve::Lin, Kind::Float, nullptr});

    for (int n = 1; n <= 4; ++n) {
        const int mb = b + DP_MOD1_SRC + (n - 1) * 3;
        const std::string mp = p + "mod" + std::to_string(n) + ".";
        v.push_back({mb + 0, mp + "src", "SRC",  0, (float)DMOD_SOURCES.size() - 1, 0, Curve::Int, Kind::Enum, &DMOD_SOURCES});
        v.push_back({mb + 1, mp + "dst", "DST",  0, (float)DMOD_DESTS.size() - 1,   0, Curve::Int, Kind::Enum, &DMOD_DESTS});
        v.push_back({mb + 2, mp + "amt", "AMT", -1, 1,                              0, Curve::Lin, Kind::Float, nullptr});
    }

    v.push_back({b + DP_MODENV_DEC, p + "modenv.dec", "DEC", 0.005f, 2, 0.084f, Curve::Log, Kind::Float, nullptr});
    v.push_back({b + DP_LVL,        p + "lvl",        "LVL",      0, 1, 0.8f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_PAN,        p + "pan",        "PAN",     -1, 1, 0,      Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_V2L,        p + "v2l",        "V→LVL",    0, 1, 0.6f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_V2M,        p + "v2m",        "V→MOD",    0, 1, 0.4f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({b + DP_CHOKE,      p + "choke",      "CHOKE",    0, 4, 0,      Curve::Int, Kind::Enum, &CHOKE_NAMES});
    v.push_back({b + DP_OUT,        p + "out",        "OUT",      0, 4, 0,      Curve::Int, Kind::Enum, &OUT_NAMES});
}

// GLOBAL_DEFS (params.ts:75-96).
void addGlobals(std::vector<ParamInfo>& v) {
    v.push_back({DG_SEQ_BPM,        "seq.bpm",         "BPM",      60, 200,   126,    Curve::Int, Kind::Float, nullptr});
    v.push_back({DG_MASTER_SWING,   "master.swing",    "SWING",     0, 1,     0.22f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_MASTER_VOLUME,  "master.volume",   "VOL",       0, 1,     0.78f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXDRIVE_ON,     "fx.drive.on",     "ON",        0, 1,     0,      Curve::Int, Kind::Bool,  nullptr});
    v.push_back({DG_FXDRIVE_AMT,    "fx.drive.amt",    "AMT",       0, 1,     0.3f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXDRIVE_MIX,    "fx.drive.mix",    "MIX",       0, 1,     1,      Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXCOMP_ON,      "fx.comp.on",      "ON",        0, 1,     1,      Curve::Int, Kind::Bool,  nullptr});
    v.push_back({DG_FXCOMP_THR,     "fx.comp.thr",     "THRESH",  -40, 0,    -16,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXCOMP_GAIN,    "fx.comp.gain",    "MAKEUP",    0, 12,    4,      Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXCHORUS_ON,    "fx.chorus.on",    "ON",        0, 1,     0,      Curve::Int, Kind::Bool,  nullptr});
    v.push_back({DG_FXCHORUS_RATE,  "fx.chorus.rate",  "RATE",  0.05f, 8,     0.6f,   Curve::Log, Kind::Float, nullptr});
    v.push_back({DG_FXCHORUS_DEPTH, "fx.chorus.depth", "DEPTH",     0, 1,     0.4f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXCHORUS_MIX,   "fx.chorus.mix",   "MIX",       0, 1,     0.2f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXDELAY_ON,     "fx.delay.on",     "ON",        0, 1,     0,      Curve::Int, Kind::Bool,  nullptr});
    v.push_back({DG_FXDELAY_TIME,   "fx.delay.time",   "TIME",  0.02f, 1.5f,  0.36f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({DG_FXDELAY_FB,     "fx.delay.fb",     "FDBK",      0, 0.92f, 0.35f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXDELAY_MIX,    "fx.delay.mix",    "MIX",       0, 1,     0.15f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXREVERB_ON,    "fx.reverb.on",    "ON",        0, 1,     1,      Curve::Int, Kind::Bool,  nullptr});
    v.push_back({DG_FXREVERB_SIZE,  "fx.reverb.size",  "SIZE",      0, 1,     0.4f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({DG_FXREVERB_MIX,   "fx.reverb.mix",   "MIX",       0, 1,     0.16f,  Curve::Lin, Kind::Float, nullptr});
}

std::vector<ParamInfo> build() {
    std::vector<ParamInfo> v;
    v.reserve(DR_NUM_PARAMS);
    for (int i = 0; i < DR_NPADS; ++i) addPad(v, i);
    addGlobals(v);
    return v;
}

} // namespace

const std::vector<ParamInfo>& drumParamInfo() {
    static const std::vector<ParamInfo> info = build();
    return info;
}

DrumParamArray defaultDrumParams() {
    DrumParamArray p{};
    const auto& info = drumParamInfo();
    for (int i = 0; i < DR_NUM_PARAMS; ++i) p[i] = info[i].def;
    return p;
}

int drumIdFromString(const std::string& pid) {
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        for (const auto& d : drumParamInfo()) m.emplace(d.pid, d.id);
        return m;
    }();
    auto it = map.find(pid);
    return it == map.end() ? -1 : it->second;
}

} // namespace fable
