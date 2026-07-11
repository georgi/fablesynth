#pragma once
#include <juce_core/juce_core.h>

// Value formatting — mirrors the formatter functions in src/params.ts
// (fmtHz / fmtSec / fmtPct / fmtPan / fmtSigned / fmtBi), selected per parameter
// id so knob read-outs match the web app exactly.
namespace fui {

inline juce::String fmtHz(float v) {
    return v >= 1000 ? juce::String(v / 1000.0f, 2) + " kHz"
                     : juce::String(v, v < 100 ? 1 : 0) + " Hz";
}
inline juce::String fmtSec(float v) {
    return v < 1 ? juce::String(juce::roundToInt(v * 1000)) + " ms"
                 : juce::String(v, 2) + " s";
}
inline juce::String fmtPct(float v)    { return juce::String(juce::roundToInt(v * 100)) + "%"; }
inline juce::String fmtSigned(float v) { return (v > 0 ? "+" : "") + juce::String(juce::roundToInt(v)); }
inline juce::String fmtBi(float v)     { return (v > 0 ? "+" : "") + juce::String(juce::roundToInt(v * 100)); }
inline juce::String fmtPan(float v) {
    if (std::abs(v) < 0.01f) return "C";
    return v < 0 ? juce::String(juce::roundToInt(-v * 100)) + "L"
                 : juce::String(juce::roundToInt(v * 100)) + "R";
}

inline juce::String formatParam(const juce::String& pid, float v) {
    auto ends = [&](const char* s) { return pid.endsWith(s); };
    // DR-1 per-pad params ("pad<i>.…", src/drum/params.ts fmt column). No WT-1
    // id starts with "pad", so this block never changes WT-1 read-outs.
    if (pid.startsWith("pad")) {
        if (ends(".tune") || ends("penv.amt"))            return fmtSigned(v) + " ST";
        if (ends(".fine"))                                return fmtSigned(v) + " CT";
        if (ends(".att") || ends(".hold") || ends(".dec")) return fmtSec(v);
        if (ends(".cut"))                                 return fmtHz(v);
        if (ends(".unison"))                              return juce::String(juce::roundToInt(v));
        if (ends(".color") || ends(".amt"))               return fmtBi(v);
        if (ends(".pan"))                                 return fmtPan(v);
        return fmtPct(v); // pos, phase, detune, level, res, drive, lvl, v2l, v2m, curve
    }
    // BL-1 flat ids (src/bass/params.ts fmt column). Guarded to exact ids /
    // prefixes WT-1 never uses, so WT-1 read-outs are untouched.
    if (pid == "osc.tune")                           return fmtSigned(v) + " ST";
    if (pid == "osc.fine")                           return fmtSigned(v) + " CT";
    if (pid == "flt.cut")                            return fmtHz(v);
    if (pid == "flt.env")                            return fmtBi(v);
    if (pid == "seq.bpm")                            return juce::String(juce::roundToInt(v));
    if (pid == "slide.time" || pid.startsWith("fenv.")
        || (pid.startsWith("aenv.") && pid != "aenv.sus")) return fmtSec(v);
    if (ends(".cutoff"))                              return fmtHz(v);
    if (ends(".rate"))                               return juce::String(v, 2) + " Hz";
    if (ends(".a") || ends(".d") || ends(".r") || ends(".rise")
        || ends(".time") || pid == "master.glide")   return fmtSec(v);
    if (ends(".pan"))                                return fmtPan(v);
    if (ends(".oct") || ends(".semi") || ends(".fine")) return fmtSigned(v);
    if (ends(".unison"))                             return juce::String(juce::roundToInt(v));
    // .amt is bipolar only on the mod matrix; fx.drive.amt is a plain percent.
    if (ends(".env") || (ends(".amt") && pid.startsWith("mat"))) return fmtBi(v);
    return fmtPct(v); // pos, detune, spread, level, res, drive, key, sus, depth, mix, fb, size, volume
}

} // namespace fui
