// Serum 1 import converter tests on synthetic data. The .fxp is built here
// from the documented layout (SerumPatch.h), so no Serum content is needed.
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../source/serumimport/SerumPatch.h"

using namespace serumimport;

static int g_fail = 0;
static void check(bool ok, const char* name, const juce::String& detail = {}) {
    std::printf("  [%s] %s%s\n", ok ? "PASS" : "FAIL", name, detail.isEmpty() ? "" : (" -> " + detail).toRawUTF8());
    if (!ok) ++g_fail;
}
static bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }
static float param(const ImportedPreset& p, const std::string& id, float fallback = -999) {
    for (const auto& [k, v] : p.params) if (k == id) return v;
    return fallback;
}

static juce::MemoryBlock makeFxp(const juce::MemoryBlock& main, const char* fxId = "XfsX") {
    juce::MemoryOutputStream z;
    { juce::GZIPCompressorOutputStream gz(z, 6); gz.write(main.getData(), main.getSize()); } // zlib format
    const auto chunk = z.getMemoryBlock();
    juce::MemoryOutputStream f;
    auto be = [&](uint32_t v) { f.writeIntBigEndian((int)v); };
    f.write("CcnK", 4); be((uint32_t)(52 + chunk.getSize())); f.write("FPCh", 4); be(1); f.write(fxId, 4); be(1); be(1);
    char name[28] = {}; std::strncpy(name, "TEST PATCH", 27); f.write(name, 28);
    be((uint32_t)chunk.getSize()); f.write(chunk.getData(), chunk.getSize());
    return f.getMemoryBlock();
}

int main() {
    std::printf("== Serum 1 import: fxp parsing ==\n");
    // Main struct: 16 slot markers, one ENV 2 -> Fil Cutoff route, strings, voice mode.
    juce::MemoryBlock main((size_t)MAIN_STRUCT_SIZE, true);
    auto* b = (uint8_t*)main.getData();
    for (int k = 0; k < 16; ++k) { b[32 + 40 * k + 2] = (uint8_t)k; b[32 + 40 * k + 3] = 0xff; }
    const int dump_cutoff = 3; // index of "Fil Cutoff" in the synthetic dump below
    { int32_t src = 3; float amt = 0.5f; uint16_t dest = dump_cutoff;
      std::memcpy(b + 32 - 12, &src, 4); std::memcpy(b + 32 - 28, &amt, 4); std::memcpy(b + 32 - 6, &dest, 2); }
    std::strcpy((char*)b + 15368, "Analog/Test Table.wav");
    { float one = 1, zero = 0, voices = 0.484f;
      std::memcpy(b + 14344, &one, 4); std::memcpy(b + 14348, &zero, 4); std::memcpy(b + 14372, &voices, 4); }

    FxpData fxp; juce::String error;
    check(parseFxp(makeFxp(main), fxp, error), "parses a Serum program chunk", error);
    check(fxp.programName == "TEST PATCH", "reads the program name");
    const auto slots = fxp.slots();
    check(slots.size() == 1 && slots[0].src == 3 && slots[0].dest == dump_cutoff && std::abs(slots[0].amt - 0.5f) < 1e-6f,
          "decodes the active mod slot only");
    FxpData other;
    check(!parseFxp(makeFxp(main, "Abcd"), other, error), "rejects another plugin's fxp", error);

    std::printf("\n== Serum 1 import: parameter mapping ==\n");
    std::vector<SerumParam> dump = {
        {"Osc A On", 1, "on"}, {"A Vol", 0.75f, "75% (-5.0 dB)"}, {"A Octave", 0.4f, "-1 Oct"}, {"Fil Cutoff", 0.5f, "425 Hz"},
        {"A WTPos", 0.5f, "128"}, {"Filter On", 1, "on"}, {"Fil Type", 0, "MG Low 24"}, {"Fil Reso", 0.1f, "10 %"},
        {"OscA>Fil", 1, "on"}, {"Env1 Atk", 0.5f, "1.00 s"}, {"Env1 Sus", 1, "0.0 dB"}, {"Env2 Dec", 0.45f, "590 s"},
        {"Env2 Sus", 0, "0.00 %"}, {"A curve1", 0.5f, "50 %"}, {"D curve2", 0.666f, "67 %"}, {"Mod 1 out", 1, "100"},
        {"MasterVol", 0.7f, "70% (-9.3 dB)"}, {"LFO1 Rate", 0.5f, "1/4"}, {"PortTime", 0.5f, "250 ms"},
        {"Osc S On", 1, "on"}, {"SubOscShape", 0, "Saw"}, {"SubOscOctave", 0.5f, "1 Oct"}, {"Sub Osc Level", 0.5f, "50 %"},
        {"OscS>Fil", 1, "on"}};
    Capture cap;
    cap.oscA.resize(64 * FRAME);
    for (size_t i = 0; i < cap.oscA.size(); ++i) cap.oscA[i] = 0.3f * (float)std::sin(2 * juce::MathConstants<double>::pi * (double)(i % FRAME) / FRAME);
    const auto p = convert(dump, fxp, cap, "TEST PATCH", "Leads");

    check(near(param(p, "oscA.on"), 1) && near(param(p, "oscA.table"), 6), "osc A plays its captured table");
    check(p.tables.size() == 2 && p.tables[0].frames == 1 && std::abs(p.tables[0].wave[FRAME / 4] - 1.0f) < 1e-5f,
          "a static capture collapses to one peak-normalized frame");
    check(p.tables[0].name == "Test Table", "names the table after Serum's table file");
    check(near(param(p, "oscA.oct"), -1) && std::abs(param(p, "oscA.pos") - 0.5f) < 1e-6f, "maps octave and WTPos");
    check(near(param(p, "oscB.on"), 1) && near(param(p, "oscB.table"), 7) && near(param(p, "oscB.oct"), 1) && p.tables[1].name == "SUB SAW",
          "a sub moves into the free oscillator at its own octave and shape");
    check(near(param(p, "filter.on"), 1) && near(param(p, "filter.type"), 1) && near(param(p, "filter.cutoff"), 425), "maps the filter");
    check(near(param(p, "env1.model"), 1) && std::abs(param(p, "env1.a") - 1.0f) < 1e-4f, "envelope times use the knob law 32 s * v^5");
    check(std::abs(param(p, "env2.d") - 32 * std::pow(0.45f, 5)) < 1e-4f, "ignores Serum's ambiguous time text");
    // ENV 2 -> cutoff at Serum depth 0.5 = 5.7 octaves: two WT-1 slots (each <= 1 = 5 octaves).
    const float part = 0.5f * (float)std::log2(22050.0 / 8.0) / 5 / 2;
    check(near(param(p, "mat1.src"), 3) && near(param(p, "mat1.dst"), 3) && near(param(p, "mat2.dst"), 3)
              && std::abs(param(p, "mat1.amt") - part) < 1e-5f && std::abs(param(p, "mat2.amt") - part) < 1e-5f,
          "a deep cutoff route spreads over two slots");
    check(near(param(p, "master.mono"), 1) && near(param(p, "master.legato"), 0), "reads MONO and LEGATO from the chunk");
    check(near(param(p, "lfo1.sync"), 1) && near(param(p, "lfo1.syncrate"), 2), "maps a synced LFO rate");
    check(std::abs(param(p, "master.glide") - 0.25f) < 1e-4f, "glide uses the knob law 8 s * v^5");
    check(std::abs(param(p, "master.volume") - 0.75f) < 1e-5f, "Serum's default master maps to WT-1's default");

    std::printf("\n== Serum 1 import: preset JSON ==\n");
    const auto json = juce::JSON::toString(toJson(p), true);
    ImportedPreset back;
    check(fromJson(juce::JSON::parse(json), back), "parses its own JSON");
    check(back.params.size() == p.params.size() && back.tables.size() == 2 && back.tables[0].wave == p.tables[0].wave,
          "round-trips parameters and table samples");
    Table one; one.name = "ONE"; one.frames = 1; one.wave.assign(FRAME, 0.0f); one.wave[0] = 1.0f;
    ImportedPreset tiny; tiny.name = "T"; tiny.tables.push_back(one);
    const auto wave = toJson(tiny)["tables"][0]["wave"].toString();
    check(wave.startsWith("AACAPw"), "tables use standard base64 of float32 LE (web usertables.ts)", wave.substring(0, 12));

    std::printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
