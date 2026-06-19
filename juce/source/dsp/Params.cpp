#include "Params.h"
#include <cmath>
#include <algorithm>

namespace fable {

namespace {
using V = std::vector<std::string>;

// Helpers mirroring the param-group factories in params.ts.
void addOsc(std::vector<ParamInfo>& v, const std::string& pre, int base, float defOn, float defTable) {
    v.push_back({base + OSC_ON,     pre + ".on",     "ON",     0, 1, defOn,    Curve::Int, Kind::Bool, nullptr});
    v.push_back({base + OSC_TABLE,  pre + ".table",  "TABLE",  0, (float)tableSlotNames().size() - 1, defTable, Curve::Int, Kind::Enum, &tableSlotNames()});
    v.push_back({base + OSC_POS,    pre + ".pos",    "POS",    0, 1, 0,        Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_OCT,    pre + ".oct",    "OCT",   -3, 3, 0,        Curve::Int, Kind::Float, nullptr});
    v.push_back({base + OSC_SEMI,   pre + ".semi",   "SEMI", -12, 12, 0,       Curve::Int, Kind::Float, nullptr});
    v.push_back({base + OSC_FINE,   pre + ".fine",   "FINE",-100, 100, 0,      Curve::Int, Kind::Float, nullptr});
    v.push_back({base + OSC_UNISON, pre + ".unison", "UNI",    1, 7, 1,        Curve::Int, Kind::Float, nullptr});
    v.push_back({base + OSC_DETUNE, pre + ".detune", "DETUNE", 0, 1, 0.2f,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_SPREAD, pre + ".spread", "SPREAD", 0, 1, 0.6f,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_LEVEL,  pre + ".level",  "LEVEL",  0, 1, 0.75f,    Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_PAN,    pre + ".pan",    "PAN",   -1, 1, 0,        Curve::Lin, Kind::Float, nullptr});
}

void addFilter(std::vector<ParamInfo>& v, const std::string& pre, int base, float defOn, float defType, float defCut) {
    v.push_back({base + FLT_ON,     pre + ".on",     "ON",     0, 1, defOn,    Curve::Int, Kind::Bool, nullptr});
    v.push_back({base + FLT_TYPE,   pre + ".type",   "TYPE",   0, (float)FILTER_TYPES.size() - 1, defType, Curve::Int, Kind::Enum, &FILTER_TYPES});
    v.push_back({base + FLT_CUTOFF, pre + ".cutoff", "CUTOFF", 20, 20000, defCut, Curve::Log, Kind::Float, nullptr});
    v.push_back({base + FLT_RES,    pre + ".res",    "RES",    0, 1, 0.18f,    Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + FLT_DRIVE,  pre + ".drive",  "DRIVE",  0, 1, 0,        Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + FLT_ENV,    pre + ".env",    "ENV",   -1, 1, 0,        Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + FLT_KEY,    pre + ".key",    "KEY",    0, 1, 0,        Curve::Lin, Kind::Float, nullptr});
}

void addMat(std::vector<ParamInfo>& v, int n, int base) {
    v.push_back({base + MAT_SRC, "mat" + std::to_string(n) + ".src", "SRC", 0, (float)MOD_SOURCES.size() - 1, 0, Curve::Int, Kind::Enum, &MOD_SOURCES});
    v.push_back({base + MAT_DST, "mat" + std::to_string(n) + ".dst", "DST", 0, (float)MOD_DESTS.size() - 1, 0, Curve::Int, Kind::Enum, &MOD_DESTS});
    v.push_back({base + MAT_AMT, "mat" + std::to_string(n) + ".amt", "AMT", -1, 1, 0, Curve::Lin, Kind::Float, nullptr});
}

std::array<ParamInfo, NUM_PARAMS> build() {
    std::vector<ParamInfo> v;
    v.reserve(NUM_PARAMS);

    addOsc(v, "oscA", OSCA_BASE, 1, 0);
    addOsc(v, "oscB", OSCB_BASE, 0, 1);

    v.push_back({SUB_ON,    "sub.on",    "ON",    0, 1, 0,    Curve::Int, Kind::Bool, nullptr});
    v.push_back({SUB_SHAPE, "sub.shape", "SHAPE", 0, (float)SUB_SHAPES.size() - 1, 0, Curve::Int, Kind::Enum, &SUB_SHAPES});
    v.push_back({SUB_OCT,   "sub.oct",   "OCT",  -2, -1, -1,  Curve::Int, Kind::Float, nullptr});
    v.push_back({SUB_LEVEL, "sub.level", "LEVEL", 0, 1, 0.5f, Curve::Lin, Kind::Float, nullptr});

    v.push_back({NOISE_ON,    "noise.on",    "ON",    0, 1, 0,     Curve::Int, Kind::Bool, nullptr});
    v.push_back({NOISE_TYPE,  "noise.type",  "TYPE",  0, (float)NOISE_TYPES.size() - 1, 0, Curve::Int, Kind::Enum, &NOISE_TYPES});
    v.push_back({NOISE_LEVEL, "noise.level", "LEVEL", 0, 1, 0.25f, Curve::Lin, Kind::Float, nullptr});

    addFilter(v, "filter", FILTER1_BASE, 1, 1, 9000);
    v.push_back({FILTER_ROUTE, "filter.route", "ROUTE", 0, (float)FILTER_ROUTES.size() - 1, 0, Curve::Int, Kind::Enum, &FILTER_ROUTES});
    addFilter(v, "filter2", FILTER2_BASE, 0, 0, 2000);

    v.push_back({ENV1_BASE + 0, "env1.a", "ATK", 0.001f, 8,  0.004f, Curve::Log, Kind::Float, nullptr});
    v.push_back({ENV1_BASE + 1, "env1.d", "DEC", 0.005f, 10, 0.25f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({ENV1_BASE + 2, "env1.s", "SUS", 0,      1,  0.8f,   Curve::Lin, Kind::Float, nullptr});
    v.push_back({ENV1_BASE + 3, "env1.r", "REL", 0.005f, 12, 0.3f,   Curve::Log, Kind::Float, nullptr});
    v.push_back({ENV2_BASE + 0, "env2.a", "ATK", 0.001f, 8,  0.01f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({ENV2_BASE + 1, "env2.d", "DEC", 0.005f, 10, 0.35f,  Curve::Log, Kind::Float, nullptr});
    v.push_back({ENV2_BASE + 2, "env2.s", "SUS", 0,      1,  0,      Curve::Lin, Kind::Float, nullptr});
    v.push_back({ENV2_BASE + 3, "env2.r", "REL", 0.005f, 12, 0.3f,   Curve::Log, Kind::Float, nullptr});

    auto addLfo = [&](const std::string& pre, int base, float defRate) {
        v.push_back({base + LFO_SHAPE,    pre + ".shape",    "SHAPE", 0, (float)LFO_SHAPES.size() - 1, 0, Curve::Int, Kind::Enum, &LFO_SHAPES});
        v.push_back({base + LFO_RATE,     pre + ".rate",     "RATE",  0.02f, 30, defRate, Curve::Log, Kind::Float, nullptr});
        v.push_back({base + LFO_SYNC,     pre + ".sync",     "SYNC",  0, 1, 0, Curve::Int, Kind::Bool, nullptr});
        v.push_back({base + LFO_SYNCRATE, pre + ".syncrate", "DIV",   0, (float)LFO_DIVS.size() - 1, 2, Curve::Int, Kind::Enum, &LFO_DIVS});
        v.push_back({base + LFO_RISE,     pre + ".rise",     "RISE",  0, 5, 0, Curve::Lin, Kind::Float, nullptr});
        v.push_back({base + LFO_PHASE,    pre + ".phase",    "PHASE", 0, 1, 0, Curve::Lin, Kind::Float, nullptr});
        v.push_back({base + LFO_RETRIG,   pre + ".retrig",   "RETRIG",0, 1, 1, Curve::Int, Kind::Bool, nullptr});
    };
    addLfo("lfo1", LFO1_BASE, 2);
    addLfo("lfo2", LFO2_BASE, 5);

    addMat(v, 1, MAT1_BASE);
    addMat(v, 2, MAT2_BASE);
    addMat(v, 3, MAT3_BASE);
    addMat(v, 4, MAT4_BASE);

    v.push_back({FXDRIVE_ON,     "fx.drive.on",    "ON",     0, 1, 0,     Curve::Int, Kind::Bool, nullptr});
    v.push_back({FXDRIVE_AMT,    "fx.drive.amt",   "AMOUNT", 0, 1, 0.3f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXDRIVE_MIX,    "fx.drive.mix",   "MIX",    0, 1, 1,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXCHORUS_ON,    "fx.chorus.on",   "ON",     0, 1, 0,     Curve::Int, Kind::Bool, nullptr});
    v.push_back({FXCHORUS_RATE,  "fx.chorus.rate", "RATE",   0.05f, 8, 0.6f, Curve::Log, Kind::Float, nullptr});
    v.push_back({FXCHORUS_DEPTH, "fx.chorus.depth","DEPTH",  0, 1, 0.5f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXCHORUS_MIX,   "fx.chorus.mix",  "MIX",    0, 1, 0.5f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXDELAY_ON,     "fx.delay.on",    "ON",     0, 1, 0,     Curve::Int, Kind::Bool, nullptr});
    v.push_back({FXDELAY_TIME,   "fx.delay.time",  "TIME",   0.02f, 1.5f, 0.36f, Curve::Log, Kind::Float, nullptr});
    v.push_back({FXDELAY_FB,     "fx.delay.fb",    "FDBK",   0, 0.92f, 0.35f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXDELAY_MIX,    "fx.delay.mix",   "MIX",    0, 1, 0.3f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXREVERB_ON,    "fx.reverb.on",   "ON",     0, 1, 0,     Curve::Int, Kind::Bool, nullptr});
    v.push_back({FXREVERB_SIZE,  "fx.reverb.size", "SIZE",   0, 1, 0.5f,  Curve::Lin, Kind::Float, nullptr});
    v.push_back({FXREVERB_MIX,   "fx.reverb.mix",  "MIX",    0, 1, 0.3f,  Curve::Lin, Kind::Float, nullptr});

    v.push_back({MASTER_VOLUME, "master.volume", "MASTER", 0, 1,    0.75f, Curve::Lin, Kind::Float, nullptr});
    v.push_back({MASTER_GLIDE,  "master.glide",  "GLIDE",  0, 0.5f, 0,     Curve::Lin, Kind::Float, nullptr});

    std::array<ParamInfo, NUM_PARAMS> out{};
    for (auto& info : v) out[info.id] = info; // place by id so [Pid] indexing is exact
    return out;
}
} // namespace

const std::array<ParamInfo, NUM_PARAMS>& paramInfo() {
    static const std::array<ParamInfo, NUM_PARAMS> info = build();
    return info;
}

int idFromString(const std::string& pid) {
    const auto& info = paramInfo();
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (info[i].pid == pid) return i;
    return -1;
}

ParamArray defaultParams() {
    ParamArray p{};
    const auto& info = paramInfo();
    for (int i = 0; i < NUM_PARAMS; ++i) p[i] = info[i].def;
    return p;
}

float normToValue(const ParamInfo& d, float n) {
    n = std::min(1.0f, std::max(0.0f, n));
    if (d.curve == Curve::Log) return d.min * std::pow(d.max / d.min, n);
    float v = d.min + (d.max - d.min) * n;
    return d.curve == Curve::Int ? std::round(v) : v;
}

float valueToNorm(const ParamInfo& d, float v) {
    if (d.curve == Curve::Log) return std::log(v / d.min) / std::log(d.max / d.min);
    return (v - d.min) / (d.max - d.min);
}

} // namespace fable
