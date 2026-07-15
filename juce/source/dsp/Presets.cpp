#include "Presets.h"

namespace fable {

using P = std::pair<std::string, float>;

const std::vector<Preset>& factoryPresets() {
    static const std::vector<Preset> presets = {
        {"INIT", {}},

        {"VELVET PAD", {
            {"oscA.table", 1}, {"oscA.pos", 0.34f}, {"oscA.unison", 5}, {"oscA.detune", 0.28f}, {"oscA.spread", 0.85f}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 3}, {"oscB.pos", 0.22f}, {"oscB.level", 0.42f}, {"oscB.unison", 3}, {"oscB.detune", 0.2f}, {"oscB.fine", 6},
            {"filter.type", 1}, {"filter.cutoff", 1400}, {"filter.res", 0.12f}, {"filter.env", 0.35f}, {"filter.key", 0.3f},
            {"env1.a", 0.9f}, {"env1.d", 1.2f}, {"env1.s", 0.82f}, {"env1.r", 1.8f},
            {"env2.a", 1.6f}, {"env2.d", 2.5f}, {"env2.s", 0.55f}, {"env2.r", 2.2f},
            {"lfo1.rate", 0.13f},
            {"mat1.src", 1}, {"mat1.dst", 1}, {"mat1.amt", 0.3f},
            {"mat2.src", 2}, {"mat2.dst", 6}, {"mat2.amt", 0.25f}, {"lfo2.rate", 0.21f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.55f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.72f}, {"fx.reverb.mix", 0.42f},
        }},

        {"ACID LINE", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.level", 0.85f},
            {"sub.on", 1}, {"sub.level", 0.4f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 240}, {"filter.res", 0.78f}, {"filter.env", 0.85f}, {"filter.drive", 0.35f},
            {"env1.a", 0.002f}, {"env1.d", 0.3f}, {"env1.s", 0.45f}, {"env1.r", 0.08f},
            {"env2.a", 0.001f}, {"env2.d", 0.19f}, {"env2.s", 0}, {"env2.r", 0.12f},
            {"master.glide", 0.06f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.45f}, {"fx.drive.mix", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.18f}, {"fx.delay.fb", 0.3f}, {"fx.delay.mix", 0.22f},
        }},

        {"CRYSTAL PLUCK", {
            {"oscA.table", 4}, {"oscA.pos", 0.72f}, {"oscA.level", 0.8f}, {"oscA.unison", 2}, {"oscA.detune", 0.1f}, {"oscA.spread", 0.5f},
            {"filter.type", 0}, {"filter.cutoff", 3200}, {"filter.res", 0.2f}, {"filter.env", 0.6f}, {"filter.key", 0.6f},
            {"env1.a", 0.001f}, {"env1.d", 0.5f}, {"env1.s", 0}, {"env1.r", 0.6f},
            {"env2.a", 0.001f}, {"env2.d", 0.32f}, {"env2.s", 0}, {"env2.r", 0.3f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.35f},
            {"mat2.src", 3}, {"mat2.dst", 1}, {"mat2.amt", -0.4f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.42f}, {"fx.delay.fb", 0.42f}, {"fx.delay.mix", 0.3f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.55f}, {"fx.reverb.mix", 0.35f},
        }},

        {"HYPER SAW", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 7}, {"oscA.detune", 0.42f}, {"oscA.spread", 1}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.oct", 1}, {"oscB.unison", 5}, {"oscB.detune", 0.35f}, {"oscB.spread", 0.9f}, {"oscB.level", 0.45f},
            {"sub.on", 1}, {"sub.level", 0.45f},
            {"filter.type", 0}, {"filter.cutoff", 12000}, {"filter.res", 0.05f},
            {"env1.a", 0.01f}, {"env1.d", 0.4f}, {"env1.s", 0.9f}, {"env1.r", 0.5f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.4f}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.4f}, {"fx.reverb.mix", 0.22f},
        }},

        {"VOWEL TALK", {
            {"oscA.table", 3}, {"oscA.pos", 0.1f}, {"oscA.level", 0.85f}, {"oscA.unison", 3}, {"oscA.detune", 0.12f}, {"oscA.spread", 0.6f},
            {"sub.on", 1}, {"sub.level", 0.5f}, {"sub.oct", -1},
            {"filter.type", 0}, {"filter.cutoff", 4500}, {"filter.res", 0.25f},
            {"env1.a", 0.01f}, {"env1.d", 0.3f}, {"env1.s", 0.85f}, {"env1.r", 0.25f},
            {"lfo1.shape", 1}, {"lfo1.rate", 0.6f},
            {"mat1.src", 1}, {"mat1.dst", 1}, {"mat1.amt", 0.55f},
            {"mat2.src", 5}, {"mat2.dst", 1}, {"mat2.amt", 0.3f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.22f}, {"fx.drive.mix", 0.2f},
        }},

        {"CATHEDRAL BELL", {
            {"oscA.table", 4}, {"oscA.pos", 1}, {"oscA.level", 0.75f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.5f}, {"oscB.oct", 1}, {"oscB.fine", 9}, {"oscB.level", 0.3f},
            {"filter.type", 0}, {"filter.cutoff", 9000}, {"filter.res", 0.05f}, {"filter.key", 0.5f},
            {"env1.a", 0.001f}, {"env1.d", 2.8f}, {"env1.s", 0.12f}, {"env1.r", 3.5f},
            {"env2.a", 0.001f}, {"env2.d", 1.8f}, {"env2.s", 0}, {"env2.r", 1.5f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", -0.5f},
            {"lfo2.rate", 4.6f}, {"mat2.src", 2}, {"mat2.dst", 4}, {"mat2.amt", 0.015f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.9f}, {"fx.reverb.mix", 0.5f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.6f}, {"fx.delay.fb", 0.35f}, {"fx.delay.mix", 0.18f},
        }},

        {"NEURO WOBBLE", {
            {"oscA.table", 5}, {"oscA.pos", 0.3f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 1}, {"oscB.oct", -1}, {"oscB.level", 0.55f},
            {"sub.on", 1}, {"sub.level", 0.55f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 700}, {"filter.res", 0.45f}, {"filter.drive", 0.4f},
            {"env1.a", 0.003f}, {"env1.d", 0.3f}, {"env1.s", 0.9f}, {"env1.r", 0.15f},
            {"lfo1.shape", 0}, {"lfo1.rate", 2.2f},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.55f},
            {"mat2.src", 1}, {"mat2.dst", 1}, {"mat2.amt", 0.5f},
            {"mat3.src", 1}, {"mat3.dst", 2}, {"mat3.amt", -0.35f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.5f}, {"fx.drive.mix", 0.2f},
        }},

        {"REESE BASS", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 4}, {"oscA.detune", 0.5f}, {"oscA.spread", 0.35f}, {"oscA.level", 0.85f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.semi", -12}, {"oscB.unison", 2}, {"oscB.detune", 0.28f}, {"oscB.level", 0.5f},
            {"sub.on", 1}, {"sub.level", 0.5f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 520}, {"filter.res", 0.26f}, {"filter.drive", 0.3f}, {"filter.key", 0.2f},
            {"env1.a", 0.005f}, {"env1.d", 0.4f}, {"env1.s", 0.85f}, {"env1.r", 0.18f},
            {"lfo1.shape", 0}, {"lfo1.rate", 0.16f},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.18f},
            {"mat2.src", 1}, {"mat2.dst", 1}, {"mat2.amt", 0.12f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.35f}, {"fx.drive.mix", 0.2f},
        }},

        {"POWER FIFTHS", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 3}, {"oscA.detune", 0.22f}, {"oscA.spread", 0.7f}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.semi", 7}, {"oscB.unison", 3}, {"oscB.detune", 0.22f}, {"oscB.spread", 0.7f}, {"oscB.level", 0.55f},
            {"sub.on", 1}, {"sub.level", 0.35f},
            {"filter.type", 0}, {"filter.cutoff", 6500}, {"filter.res", 0.12f}, {"filter.env", 0.3f}, {"filter.key", 0.3f},
            {"env1.a", 0.005f}, {"env1.d", 0.6f}, {"env1.s", 0.8f}, {"env1.r", 0.35f},
            {"env2.a", 0.005f}, {"env2.d", 0.3f}, {"env2.s", 0}, {"env2.r", 0.2f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.35f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.33f}, {"fx.delay.fb", 0.32f}, {"fx.delay.mix", 0.22f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.26f},
        }},

        {"GROWL BASS", {
            {"oscA.table", 3}, {"oscA.pos", 0.15f}, {"oscA.unison", 2}, {"oscA.detune", 0.14f}, {"oscA.spread", 0.5f}, {"oscA.level", 0.85f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.oct", -1}, {"oscB.level", 0.5f},
            {"sub.on", 1}, {"sub.level", 0.55f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 650}, {"filter.res", 0.45f}, {"filter.drive", 0.5f},
            {"env1.a", 0.004f}, {"env1.d", 0.3f}, {"env1.s", 0.9f}, {"env1.r", 0.12f},
            {"lfo1.shape", 1}, {"lfo1.rate", 5.5f},
            {"mat1.src", 1}, {"mat1.dst", 1}, {"mat1.amt", 0.7f},
            {"mat2.src", 1}, {"mat2.dst", 3}, {"mat2.amt", 0.4f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.55f}, {"fx.drive.mix", 0.2f},
        }},

        {"FUTURE CHORD", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 7}, {"oscA.detune", 0.32f}, {"oscA.spread", 1}, {"oscA.level", 0.72f},
            {"oscB.on", 1}, {"oscB.table", 1}, {"oscB.pos", 0.4f}, {"oscB.unison", 3}, {"oscB.detune", 0.24f}, {"oscB.spread", 0.8f}, {"oscB.level", 0.4f},
            {"filter.type", 0}, {"filter.cutoff", 8000}, {"filter.res", 0.08f}, {"filter.env", 0.25f}, {"filter.key", 0.2f},
            {"env1.a", 0.02f}, {"env1.d", 0.5f}, {"env1.s", 0.85f}, {"env1.r", 0.6f},
            {"lfo1.shape", 0}, {"lfo1.rate", 0.5f},
            {"mat1.src", 1}, {"mat1.dst", 1}, {"mat1.amt", 0.3f},
            {"mat2.src", 1}, {"mat2.dst", 3}, {"mat2.amt", 0.2f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.5f}, {"fx.chorus.mix", 0.5f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.68f}, {"fx.reverb.mix", 0.42f},
        }},

        {"SCREECH LEAD", {
            {"oscA.table", 2}, {"oscA.pos", 0.7f}, {"oscA.unison", 3}, {"oscA.detune", 0.3f}, {"oscA.spread", 0.7f}, {"oscA.level", 0.78f},
            {"oscB.on", 1}, {"oscB.table", 5}, {"oscB.pos", 0.4f}, {"oscB.level", 0.4f},
            {"filter.type", 2}, {"filter.cutoff", 1800}, {"filter.res", 0.6f}, {"filter.drive", 0.4f},
            {"env1.a", 0.004f}, {"env1.d", 0.4f}, {"env1.s", 0.85f}, {"env1.r", 0.15f},
            {"lfo1.shape", 3}, {"lfo1.rate", 8},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.5f},
            {"mat2.src", 1}, {"mat2.dst", 2}, {"mat2.amt", 0.4f},
            {"mat3.src", 4}, {"mat3.dst", 3}, {"mat3.amt", 0.3f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.6f}, {"fx.drive.mix", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.25f}, {"fx.delay.fb", 0.3f}, {"fx.delay.mix", 0.18f},
        }},

        {"DONK STAB", {
            {"oscA.table", 2}, {"oscA.pos", 0.25f}, {"oscA.level", 0.85f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 1}, {"oscB.oct", 1}, {"oscB.level", 0.28f},
            {"sub.on", 1}, {"sub.level", 0.4f},
            {"filter.type", 0}, {"filter.cutoff", 3500}, {"filter.res", 0.3f}, {"filter.env", 0.5f}, {"filter.key", 0.3f},
            {"env1.a", 0.002f}, {"env1.d", 0.16f}, {"env1.s", 0}, {"env1.r", 0.1f},
            {"env2.a", 0.002f}, {"env2.d", 0.05f}, {"env2.s", 0}, {"env2.r", 0.05f},
            {"mat1.src", 3}, {"mat1.dst", 4}, {"mat1.amt", 0.25f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.3f}, {"fx.drive.mix", 0.2f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.4f}, {"fx.reverb.mix", 0.2f},
        }},

        {"HOUSE PLUCK", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 3}, {"oscA.detune", 0.18f}, {"oscA.spread", 0.6f}, {"oscA.level", 0.78f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.3f}, {"oscB.semi", 12}, {"oscB.level", 0.3f},
            {"filter.type", 1}, {"filter.cutoff", 2200}, {"filter.res", 0.2f}, {"filter.env", 0.55f}, {"filter.key", 0.5f},
            {"env1.a", 0.003f}, {"env1.d", 0.28f}, {"env1.s", 0}, {"env1.r", 0.2f},
            {"env2.a", 0.003f}, {"env2.d", 0.18f}, {"env2.s", 0}, {"env2.r", 0.15f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.38f}, {"fx.delay.fb", 0.4f}, {"fx.delay.mix", 0.3f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.25f},
        }},

        {"TRAP BELL", {
            {"oscA.table", 4}, {"oscA.pos", 0.85f}, {"oscA.unison", 2}, {"oscA.detune", 0.12f}, {"oscA.spread", 0.5f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.6f}, {"oscB.semi", 12}, {"oscB.fine", 4}, {"oscB.level", 0.32f},
            {"filter.type", 0}, {"filter.cutoff", 7000}, {"filter.res", 0.1f}, {"filter.key", 0.5f},
            {"env1.a", 0.001f}, {"env1.d", 0.9f}, {"env1.s", 0.1f}, {"env1.r", 0.8f},
            {"env2.a", 0.001f}, {"env2.d", 0.4f}, {"env2.s", 0}, {"env2.r", 0.4f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", -0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.3f}, {"fx.delay.fb", 0.45f}, {"fx.delay.mix", 0.32f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.7f}, {"fx.reverb.mix", 0.4f},
        }},

        {"8-BIT LEAD", {
            {"oscA.table", 2}, {"oscA.pos", 0}, {"oscA.level", 0.8f},
            {"filter.on", 0},
            {"env1.a", 0.001f}, {"env1.d", 0.1f}, {"env1.s", 0.7f}, {"env1.r", 0.05f},
            {"lfo1.shape", 0}, {"lfo1.rate", 6},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.01f},
            {"mat2.src", 1}, {"mat2.dst", 1}, {"mat2.amt", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.25f}, {"fx.delay.fb", 0.25f}, {"fx.delay.mix", 0.2f},
        }},

        {"DARK DRONE", {
            {"oscA.table", 1}, {"oscA.pos", 0.2f}, {"oscA.unison", 4}, {"oscA.detune", 0.3f}, {"oscA.spread", 0.9f}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 3}, {"oscB.pos", 0.5f}, {"oscB.oct", -1}, {"oscB.unison", 2}, {"oscB.detune", 0.2f}, {"oscB.level", 0.45f},
            {"sub.on", 1}, {"sub.level", 0.35f}, {"sub.oct", -2},
            {"filter.type", 1}, {"filter.cutoff", 900}, {"filter.res", 0.15f},
            {"env1.a", 2.5f}, {"env1.d", 3}, {"env1.s", 0.7f}, {"env1.r", 4},
            {"lfo1.shape", 0}, {"lfo1.rate", 0.08f},
            {"lfo2.shape", 0}, {"lfo2.rate", 0.05f},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.4f},
            {"mat2.src", 2}, {"mat2.dst", 1}, {"mat2.amt", 0.5f},
            {"mat3.src", 2}, {"mat3.dst", 6}, {"mat3.amt", 0.4f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.5f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.9f}, {"fx.reverb.mix", 0.5f},
        }},

        {"WUB BASS", {
            {"oscA.table", 0}, {"oscA.pos", 1}, {"oscA.unison", 2}, {"oscA.detune", 0.1f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.5f}, {"oscB.oct", -1}, {"oscB.level", 0.5f},
            {"sub.on", 1}, {"sub.level", 0.6f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 400}, {"filter.res", 0.55f}, {"filter.drive", 0.45f},
            {"env1.a", 0.004f}, {"env1.d", 0.3f}, {"env1.s", 0.95f}, {"env1.r", 0.12f},
            {"lfo1.shape", 0}, {"lfo1.rate", 4},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.7f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.4f}, {"fx.drive.mix", 0.2f},
        }},

        {"GLIDE LEAD", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 2}, {"oscA.detune", 0.12f}, {"oscA.spread", 0.4f}, {"oscA.level", 0.82f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.4f}, {"oscB.semi", -12}, {"oscB.level", 0.4f},
            {"filter.type", 1}, {"filter.cutoff", 1600}, {"filter.res", 0.4f}, {"filter.env", 0.5f}, {"filter.key", 0.4f},
            {"env1.a", 0.005f}, {"env1.d", 0.5f}, {"env1.s", 0.6f}, {"env1.r", 0.25f},
            {"env2.a", 0.005f}, {"env2.d", 0.3f}, {"env2.s", 0.2f}, {"env2.r", 0.2f},
            {"master.glide", 0.12f},
            {"lfo1.shape", 0}, {"lfo1.rate", 5},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.008f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.3f}, {"fx.delay.fb", 0.35f}, {"fx.delay.mix", 0.25f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.45f}, {"fx.reverb.mix", 0.22f},
        }},
    };
    return presets;
}

ParamArray applyPreset(const Preset& preset) {
    ParamArray p = defaultParams();
    for (const auto& kv : preset.params) {
        int id = idFromString(kv.first);
        if (id >= 0) p[(size_t)id] = kv.second;
    }
    return p;
}

} // namespace fable
