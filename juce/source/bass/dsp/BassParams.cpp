// Transcribes src/bass/params.ts BASS_PARAM_DEFS row for row.
#include "BassParams.h"

#include <unordered_map>

namespace fable {

const std::vector<std::string> BASS_FILTER_TYPES = {"LP 12", "LP 24", "BP 12", "HP 12", "NOTCH"};

namespace {

std::vector<ParamInfo> build() {
    std::vector<ParamInfo> v;
    v.reserve(BL_NUM_PARAMS);
    // ---- oscillator ----
    v.push_back({BL_OSC_TABLE,  "osc.table",  "TABLE",    0, (float)TABLE_NAMES.size() - 1, 0, Curve::Int, Kind::Enum, &TABLE_NAMES});
    v.push_back({BL_OSC_POS,    "osc.pos",    "POS",      0, 1,      0.3f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_OSC_TUNE,   "osc.tune",   "TUNE",   -24, 24,    -12,    Curve::Int, Kind::Float, nullptr});
    v.push_back({BL_OSC_FINE,   "osc.fine",   "FINE",  -100, 100,    0,     Curve::Int, Kind::Float, nullptr});
    v.push_back({BL_OSC_UNISON, "osc.unison", "UNI",      1, 7,      1,     Curve::Int, Kind::Float, nullptr});
    v.push_back({BL_OSC_DETUNE, "osc.detune", "DET",      0, 1,      0.2f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_OSC_SPREAD, "osc.spread", "SPRD",     0, 1,      0,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_OSC_LEVEL,  "osc.level",  "LVL",      0, 1,      0.8f,  Curve::Lin, Kind::Float, nullptr});
    // ---- sub oscillator ----
    v.push_back({BL_SUB_SHAPE,  "sub.shape",  "SHAPE",    0, (float)SUB_SHAPES.size() - 1, 0, Curve::Int, Kind::Enum, &SUB_SHAPES});
    v.push_back({BL_SUB_OCT,    "sub.oct",    "OCT",     -2, -1,    -1,     Curve::Int, Kind::Float, nullptr});
    v.push_back({BL_SUB_LEVEL,  "sub.level",  "LVL",      0, 1,      0.55f, Curve::Lin, Kind::Float, nullptr});
    // ---- filter ----
    v.push_back({BL_FLT_TYPE,   "flt.type",   "TYPE",     0, (float)BASS_FILTER_TYPES.size() - 1, 1, Curve::Int, Kind::Enum, &BASS_FILTER_TYPES});
    v.push_back({BL_FLT_CUT,    "flt.cut",    "CUT",     20, 20000,  340,   Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_FLT_RES,    "flt.res",    "RES",      0, 1,      0.62f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FLT_DRIVE,  "flt.drive",  "DRIVE",    0, 1,      0.45f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FLT_ENV,    "flt.env",    "ENV",     -1, 1,      0.7f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FLT_TRACK,  "flt.track",  "TRACK",    0, 1,      0.3f,  Curve::Lin, Kind::Float, nullptr});
    // ---- envelopes: filter AD, amp ADSR ----
    v.push_back({BL_FENV_ATT,   "fenv.att",   "F-ATT", 0.0005f, 0.5f, 0.001f, Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_FENV_DEC,   "fenv.dec",   "F-DEC", 0.005f,  4,    0.18f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_AENV_ATT,   "aenv.att",   "ATT",   0.0005f, 0.5f, 0.001f, Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_AENV_DEC,   "aenv.dec",   "DEC",   0.005f,  4,    0.3f,   Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_AENV_SUS,   "aenv.sus",   "SUS",      0, 1,      0.5f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_AENV_REL,   "aenv.rel",   "REL",   0.005f,  2,    0.08f,  Curve::Log, Kind::Float, nullptr});
    // ---- accent + slide (one knob each: accent = level + env + decay) ----
    v.push_back({BL_ACC_AMT,    "acc.amt",    "ACC AMT",  0, 1,      0.7f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_SLIDE_TIME, "slide.time", "SLD TIME", 0.01f, 0.5f, 0.06f, Curve::Log, Kind::Float, nullptr});
    // ---- LFO -> cutoff, bar-locked ----
    v.push_back({BL_LFO_RATE,   "lfo.rate",   "RATE",     0, (float)LFO_DIVS.size() - 1,   6, Curve::Int, Kind::Enum, &LFO_DIVS});
    v.push_back({BL_LFO_SHAPE,  "lfo.shape",  "SHAPE",    0, (float)LFO_SHAPES.size() - 1, 0, Curve::Int, Kind::Enum, &LFO_SHAPES});
    v.push_back({BL_LFO_DEPTH,  "lfo.depth",  "DEPTH",    0, 1,      0.15f, Curve::Lin, Kind::Float, nullptr});
    // ---- FX (post-accent drive · no compressor, accents live) ----
    v.push_back({BL_FXDRIVE_ON,    "fx.drive.on",    "ON",     0, 1,      1,     Curve::Int, Kind::Bool,  nullptr});
    v.push_back({BL_FXDRIVE_AMT,   "fx.drive.amt",   "AMT",    0, 1,      0.35f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXDRIVE_MIX,   "fx.drive.mix",   "MIX",    0, 1,      1,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXCHORUS_ON,   "fx.chorus.on",   "ON",     0, 1,      0,     Curve::Int, Kind::Bool,  nullptr});
    v.push_back({BL_FXCHORUS_RATE, "fx.chorus.rate", "RATE", 0.05f, 8,    0.6f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_FXCHORUS_DEPTH,"fx.chorus.depth","DEPTH",  0, 1,      0.3f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXCHORUS_MIX,  "fx.chorus.mix",  "MIX",    0, 1,      0.12f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXDELAY_ON,    "fx.delay.on",    "ON",     0, 1,      0,     Curve::Int, Kind::Bool,  nullptr});
    v.push_back({BL_FXDELAY_TIME,  "fx.delay.time",  "TIME", 0.02f, 1.5f, 0.375f, Curve::Log, Kind::Float, nullptr});
    v.push_back({BL_FXDELAY_FB,    "fx.delay.fb",    "FDBK",   0, 0.92f,  0.42f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXDELAY_MIX,   "fx.delay.mix",   "MIX",    0, 1,      0.18f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXREVERB_ON,   "fx.reverb.on",   "ON",     0, 1,      1,     Curve::Int, Kind::Bool,  nullptr});
    v.push_back({BL_FXREVERB_SIZE, "fx.reverb.size", "SIZE",   0, 1,      0.3f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_FXREVERB_MIX,  "fx.reverb.mix",  "MIX",    0, 1,      0.1f,  Curve::Lin, Kind::Float, nullptr});
    // ---- transport + master ----
    v.push_back({BL_SEQ_BPM,       "seq.bpm",        "BPM",   60, 200,    138,   Curve::Int, Kind::Float, nullptr});
    v.push_back({BL_MASTER_SWING,  "master.swing",   "SWING",  0, 1,      0.3f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({BL_MASTER_VOLUME, "master.volume",  "VOL",    0, 1,      0.78f, Curve::Lin, Kind::Float, nullptr});
    return v;
}

} // namespace

const std::vector<ParamInfo>& bassParamInfo() {
    static const std::vector<ParamInfo> info = build();
    return info;
}

BassParamArray defaultBassParams() {
    BassParamArray p{};
    const auto& info = bassParamInfo();
    for (int i = 0; i < BL_NUM_PARAMS; ++i) p[(size_t)i] = info[(size_t)i].def;
    return p;
}

int bassIdFromString(const std::string& pid) {
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        for (const auto& d : bassParamInfo()) m.emplace(d.pid, d.id);
        return m;
    }();
    auto it = map.find(pid);
    return it == map.end() ? -1 : it->second;
}

} // namespace fable
