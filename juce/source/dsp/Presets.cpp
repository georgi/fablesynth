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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -2.0f}, {"fx.eq.mid", -1.1f}, {"fx.eq.mfreq", 405}, {"fx.eq.high", 3.1f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 263}, {"fx.eq.high", 6.0f},
        }},

        {"CRYSTAL PLUCK", {
            {"oscA.table", 4}, {"oscA.pos", 0.72f}, {"oscA.oct", 1}, {"oscA.level", 0.8f}, {"oscA.unison", 2}, {"oscA.detune", 0.1f}, {"oscA.spread", 0.5f},
            {"filter.type", 0}, {"filter.cutoff", 3200}, {"filter.res", 0.2f}, {"filter.env", 0.6f}, {"filter.key", 0.6f},
            {"env1.a", 0.001f}, {"env1.d", 0.5f}, {"env1.s", 0}, {"env1.r", 0.6f},
            {"env2.a", 0.001f}, {"env2.d", 0.32f}, {"env2.s", 0}, {"env2.r", 0.3f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.35f},
            {"mat2.src", 3}, {"mat2.dst", 1}, {"mat2.amt", -0.4f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.42f}, {"fx.delay.fb", 0.42f}, {"fx.delay.mix", 0.3f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.55f}, {"fx.reverb.mix", 0.35f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 4.7f}, {"fx.eq.mfreq", 709}, {"fx.eq.high", -6.0f},
        }},

        {"HYPER SAW", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 7}, {"oscA.detune", 0.42f}, {"oscA.spread", 1}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.oct", 1}, {"oscB.unison", 5}, {"oscB.detune", 0.35f}, {"oscB.spread", 0.9f}, {"oscB.level", 0.45f},
            {"sub.on", 1}, {"sub.level", 0.45f},
            {"filter.type", 0}, {"filter.cutoff", 12000}, {"filter.res", 0.05f},
            {"env1.a", 0.01f}, {"env1.d", 0.4f}, {"env1.s", 0.9f}, {"env1.r", 0.5f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.4f}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.4f}, {"fx.reverb.mix", 0.22f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 5.1f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 598}, {"fx.eq.high", -6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", 4.5f}, {"fx.eq.mid", 5.2f}, {"fx.eq.mfreq", 916}, {"fx.eq.high", -6.0f},
        }},

        {"CATHEDRAL BELL", {
            {"oscA.table", 4}, {"oscA.pos", 1}, {"oscA.oct", 1}, {"oscA.level", 0.75f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.5f}, {"oscB.oct", 2}, {"oscB.fine", 9}, {"oscB.level", 0.3f},
            {"filter.type", 0}, {"filter.cutoff", 9000}, {"filter.res", 0.05f}, {"filter.key", 0.5f},
            {"env1.a", 0.001f}, {"env1.d", 2.8f}, {"env1.s", 0.12f}, {"env1.r", 3.5f},
            {"env2.a", 0.001f}, {"env2.d", 1.8f}, {"env2.s", 0}, {"env2.r", 1.5f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", -0.5f},
            {"lfo2.rate", 4.6f}, {"mat2.src", 2}, {"mat2.dst", 4}, {"mat2.amt", 0.015f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.9f}, {"fx.reverb.mix", 0.5f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.6f}, {"fx.delay.fb", 0.35f}, {"fx.delay.mix", 0.18f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 5.2f}, {"fx.eq.mfreq", 693}, {"fx.eq.high", -6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -3.8f}, {"fx.eq.mid", 3.0f}, {"fx.eq.mfreq", 356}, {"fx.eq.high", 0.8f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 320}, {"fx.eq.high", 6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", 5.3f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 601}, {"fx.eq.high", -6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -4.8f}, {"fx.eq.mfreq", 620}, {"fx.eq.high", 6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", 5.7f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 431}, {"fx.eq.high", -6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 2.5f}, {"fx.eq.mfreq", 734}, {"fx.eq.high", -6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 900}, {"fx.eq.high", -6.0f},
        }},

        {"HOUSE PLUCK", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.oct", 1}, {"oscA.unison", 3}, {"oscA.detune", 0.18f}, {"oscA.spread", 0.6f}, {"oscA.level", 0.78f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.3f}, {"oscB.oct", 1}, {"oscB.semi", 12}, {"oscB.level", 0.3f},
            {"filter.type", 1}, {"filter.cutoff", 2200}, {"filter.res", 0.2f}, {"filter.env", 0.55f}, {"filter.key", 0.5f},
            {"env1.a", 0.003f}, {"env1.d", 0.28f}, {"env1.s", 0}, {"env1.r", 0.2f},
            {"env2.a", 0.003f}, {"env2.d", 0.18f}, {"env2.s", 0}, {"env2.r", 0.15f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.38f}, {"fx.delay.fb", 0.4f}, {"fx.delay.mix", 0.3f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.25f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 4.0f}, {"fx.eq.mid", -5.8f}, {"fx.eq.mfreq", 504}, {"fx.eq.high", 1.7f},
        }},

        {"TRAP BELL", {
            {"oscA.table", 4}, {"oscA.pos", 0.85f}, {"oscA.oct", 1}, {"oscA.unison", 2}, {"oscA.detune", 0.12f}, {"oscA.spread", 0.5f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.6f}, {"oscB.oct", 1}, {"oscB.semi", 12}, {"oscB.fine", 4}, {"oscB.level", 0.32f},
            {"filter.type", 0}, {"filter.cutoff", 7000}, {"filter.res", 0.1f}, {"filter.key", 0.5f},
            {"env1.a", 0.001f}, {"env1.d", 0.9f}, {"env1.s", 0.1f}, {"env1.r", 0.8f},
            {"env2.a", 0.001f}, {"env2.d", 0.4f}, {"env2.s", 0}, {"env2.r", 0.4f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", -0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.3f}, {"fx.delay.fb", 0.45f}, {"fx.delay.mix", 0.32f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.7f}, {"fx.reverb.mix", 0.4f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 4.2f}, {"fx.eq.mfreq", 680}, {"fx.eq.high", -6.0f},
        }},

        {"8-BIT LEAD", {
            {"oscA.table", 2}, {"oscA.pos", 0}, {"oscA.level", 0.8f},
            {"filter.on", 0},
            {"env1.a", 0.001f}, {"env1.d", 0.1f}, {"env1.s", 0.7f}, {"env1.r", 0.05f},
            {"lfo1.shape", 0}, {"lfo1.rate", 6},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.01f},
            {"mat2.src", 1}, {"mat2.dst", 1}, {"mat2.amt", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.25f}, {"fx.delay.fb", 0.25f}, {"fx.delay.mix", 0.2f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 507}, {"fx.eq.high", -6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -4.1f}, {"fx.eq.mfreq", 361}, {"fx.eq.high", 6.0f},
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
        
            {"fx.eq.on", 1}, {"fx.eq.low", -0.4f}, {"fx.eq.mid", 5.7f}, {"fx.eq.mfreq", 493}, {"fx.eq.high", -5.3f},
        }},

        {"GLIDE LEAD", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.oct", 1}, {"oscA.unison", 2}, {"oscA.detune", 0.12f}, {"oscA.spread", 0.4f}, {"oscA.level", 0.82f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.4f}, {"oscB.oct", 1}, {"oscB.semi", -12}, {"oscB.level", 0.4f},
            {"filter.type", 1}, {"filter.cutoff", 1600}, {"filter.res", 0.4f}, {"filter.env", 0.5f}, {"filter.key", 0.4f},
            {"env1.a", 0.005f}, {"env1.d", 0.5f}, {"env1.s", 0.6f}, {"env1.r", 0.25f},
            {"env2.a", 0.005f}, {"env2.d", 0.3f}, {"env2.s", 0.2f}, {"env2.r", 0.2f},
            {"master.glide", 0.12f},
            {"lfo1.shape", 0}, {"lfo1.rate", 5},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.008f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.3f}, {"fx.delay.fb", 0.35f}, {"fx.delay.mix", 0.25f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.45f}, {"fx.reverb.mix", 0.22f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 1.3f}, {"fx.eq.mid", -4.5f}, {"fx.eq.mfreq", 546}, {"fx.eq.high", 3.2f},
        }},

        {"MELLOW RHODES", {
            {"oscA.table", 0}, {"oscA.pos", 0.08f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.12f}, {"oscB.oct", 1}, {"oscB.level", 0.18f},
            {"filter.type", 0}, {"filter.cutoff", 1200}, {"filter.res", 0.1f}, {"filter.env", 0.45f}, {"filter.key", 0.4f},
            {"env1.a", 0.002f}, {"env1.d", 1.4f}, {"env1.s", 0.42f}, {"env1.r", 0.5f},
            {"env2.a", 0.001f}, {"env2.d", 0.35f}, {"env2.s", 0}, {"env2.r", 0.3f},
            {"lfo1.rate", 0.9f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.4f},
            {"mat2.src", 4}, {"mat2.dst", 8}, {"mat2.amt", 0.35f},
            {"mat3.src", 1}, {"mat3.dst", 6}, {"mat3.amt", 0.3f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.35f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.35f}, {"fx.reverb.mix", 0.25f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 302}, {"fx.eq.high", 6.0f},
        }},

        {"DYNO EPIANO", {
            {"oscA.table", 0}, {"oscA.pos", 0.05f}, {"oscA.level", 0.75f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.3f}, {"oscB.oct", 2}, {"oscB.level", 0.15f},
            {"filter.type", 0}, {"filter.cutoff", 2500}, {"filter.res", 0.08f}, {"filter.env", 0.3f}, {"filter.key", 0.5f},
            {"env1.a", 0.001f}, {"env1.d", 1.8f}, {"env1.s", 0.3f}, {"env1.r", 0.6f},
            {"env2.a", 0.001f}, {"env2.d", 0.5f}, {"env2.s", 0}, {"env2.r", 0.4f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.45f},
            {"mat2.src", 4}, {"mat2.dst", 8}, {"mat2.amt", 0.4f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.7f}, {"fx.chorus.mix", 0.5f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.4f}, {"fx.reverb.mix", 0.24f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 1.4f}, {"fx.eq.mid", 2.3f}, {"fx.eq.mfreq", 443}, {"fx.eq.high", -3.7f},
        }},

        {"DRAWBAR ORGAN", {
            {"oscA.table", 0}, {"oscA.pos", 0}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0}, {"oscB.oct", 1}, {"oscB.level", 0.5f},
            {"sub.on", 1}, {"sub.level", 0.6f}, {"sub.oct", -1},
            {"filter.on", 0},
            {"env1.a", 0.003f}, {"env1.d", 0.1f}, {"env1.s", 1}, {"env1.r", 0.05f},
            {"lfo1.rate", 6.5f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.004f},
            {"mat2.src", 1}, {"mat2.dst", 6}, {"mat2.amt", 0.25f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.8f}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.35f}, {"fx.reverb.mix", 0.18f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 309}, {"fx.eq.high", 6.0f},
        }},

        {"ROCK ORGAN", {
            {"oscA.table", 2}, {"oscA.pos", 0}, {"oscA.unison", 2}, {"oscA.detune", 0.08f}, {"oscA.level", 0.75f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0}, {"oscB.oct", 1}, {"oscB.level", 0.45f},
            {"sub.on", 1}, {"sub.level", 0.5f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 5000}, {"filter.res", 0.1f},
            {"env1.a", 0.002f}, {"env1.d", 0.1f}, {"env1.s", 1}, {"env1.r", 0.06f},
            {"lfo1.rate", 5.5f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.005f},
            {"mat2.src", 1}, {"mat2.dst", 6}, {"mat2.amt", 0.35f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.5f}, {"fx.drive.mix", 0.2f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 1.2f}, {"fx.chorus.mix", 0.45f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -3.4f}, {"fx.eq.mid", 3.4f}, {"fx.eq.mfreq", 410}, {"fx.eq.high", 0.0f},
        }},

        {"ANALOG STRINGS", {
            {"oscA.table", 0}, {"oscA.pos", 0.62f}, {"oscA.unison", 7}, {"oscA.detune", 0.3f}, {"oscA.spread", 1}, {"oscA.level", 0.65f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.oct", 1}, {"oscB.unison", 3}, {"oscB.detune", 0.22f}, {"oscB.spread", 0.8f}, {"oscB.level", 0.3f},
            {"filter.type", 0}, {"filter.cutoff", 2800}, {"filter.res", 0.08f}, {"filter.key", 0.2f},
            {"env1.a", 0.5f}, {"env1.d", 1}, {"env1.s", 0.85f}, {"env1.r", 1.2f},
            {"lfo1.rate", 0.25f}, {"lfo2.rate", 0.11f},
            {"mat1.src", 1}, {"mat1.dst", 1}, {"mat1.amt", 0.12f},
            {"mat2.src", 2}, {"mat2.dst", 6}, {"mat2.amt", 0.2f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.5f}, {"fx.chorus.mix", 0.55f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.7f}, {"fx.reverb.mix", 0.35f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 2.6f}, {"fx.eq.mid", 2.9f}, {"fx.eq.mfreq", 467}, {"fx.eq.high", -5.5f},
        }},

        {"CINEMA STRINGS", {
            {"oscA.table", 1}, {"oscA.pos", 0.45f}, {"oscA.unison", 5}, {"oscA.detune", 0.25f}, {"oscA.spread", 0.9f}, {"oscA.level", 0.68f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.6f}, {"oscB.oct", -1}, {"oscB.level", 0.4f},
            {"filter.type", 0}, {"filter.cutoff", 1500}, {"filter.res", 0.1f}, {"filter.env", 0.3f}, {"filter.key", 0.2f},
            {"env1.a", 1.2f}, {"env1.d", 2}, {"env1.s", 0.75f}, {"env1.r", 2.5f},
            {"env2.a", 2.5f}, {"env2.d", 3}, {"env2.s", 0.4f}, {"env2.r", 2},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", 0.3f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.85f}, {"fx.reverb.mix", 0.45f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -0.6f}, {"fx.eq.mid", 1.2f}, {"fx.eq.mfreq", 392}, {"fx.eq.high", -0.6f},
        }},

        {"BRASS SECTION", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 3}, {"oscA.detune", 0.15f}, {"oscA.spread", 0.5f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.fine", 8}, {"oscB.level", 0.5f},
            {"sub.on", 1}, {"sub.level", 0.3f},
            {"filter.type", 0}, {"filter.cutoff", 900}, {"filter.res", 0.15f}, {"filter.env", 0.7f}, {"filter.key", 0.3f},
            {"env1.a", 0.03f}, {"env1.d", 0.3f}, {"env1.s", 0.85f}, {"env1.r", 0.2f},
            {"env2.a", 0.06f}, {"env2.d", 0.35f}, {"env2.s", 0.55f}, {"env2.r", 0.2f},
            {"lfo1.rate", 5},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.3f},
            {"mat2.src", 1}, {"mat2.dst", 4}, {"mat2.amt", 0.003f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.25f}, {"fx.drive.mix", 0.2f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.4f}, {"fx.reverb.mix", 0.22f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 5.7f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 547}, {"fx.eq.high", -6.0f},
        }},

        {"SOFT BRASS", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 5}, {"oscA.detune", 0.28f}, {"oscA.spread", 0.9f}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.15f}, {"oscB.oct", -1}, {"oscB.level", 0.45f},
            {"filter.type", 0}, {"filter.cutoff", 1200}, {"filter.res", 0.1f}, {"filter.env", 0.5f}, {"filter.key", 0.25f},
            {"env1.a", 0.12f}, {"env1.d", 0.6f}, {"env1.s", 0.8f}, {"env1.r", 0.8f},
            {"env2.a", 0.15f}, {"env2.d", 0.8f}, {"env2.s", 0.4f}, {"env2.r", 0.6f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.45f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.55f}, {"fx.reverb.mix", 0.3f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 1.2f}, {"fx.eq.mid", 2.0f}, {"fx.eq.mfreq", 443}, {"fx.eq.high", -3.2f},
        }},

        {"NYLON PLUCK", {
            {"oscA.table", 0}, {"oscA.pos", 0.3f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.05f}, {"oscB.oct", 1}, {"oscB.level", 0.25f},
            {"filter.type", 0}, {"filter.cutoff", 1800}, {"filter.res", 0.12f}, {"filter.env", 0.5f}, {"filter.key", 0.6f},
            {"env1.a", 0.001f}, {"env1.d", 0.7f}, {"env1.s", 0}, {"env1.r", 0.35f},
            {"env2.a", 0.001f}, {"env2.d", 0.25f}, {"env2.s", 0}, {"env2.r", 0.2f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.35f},
            {"mat2.src", 3}, {"mat2.dst", 1}, {"mat2.amt", 0.25f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.32f}, {"fx.delay.fb", 0.28f}, {"fx.delay.mix", 0.18f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.45f}, {"fx.reverb.mix", 0.25f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -5.6f}, {"fx.eq.mid", -2.5f}, {"fx.eq.mfreq", 276}, {"fx.eq.high", 6.0f},
        }},

        {"KALIMBA PLUCK", {
            {"oscA.table", 4}, {"oscA.pos", 0.15f}, {"oscA.level", 0.85f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0}, {"oscB.oct", 1}, {"oscB.level", 0.2f},
            {"filter.type", 0}, {"filter.cutoff", 2600}, {"filter.res", 0.1f}, {"filter.env", 0.4f}, {"filter.key", 0.7f},
            {"env1.a", 0.001f}, {"env1.d", 0.45f}, {"env1.s", 0}, {"env1.r", 0.4f},
            {"env2.a", 0.001f}, {"env2.d", 0.12f}, {"env2.s", 0}, {"env2.r", 0.1f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", 0.35f},
            {"mat2.src", 4}, {"mat2.dst", 3}, {"mat2.amt", 0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.28f}, {"fx.delay.fb", 0.3f}, {"fx.delay.mix", 0.22f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.28f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 277}, {"fx.eq.high", 6.0f},
        }},

        {"CELTIC HARP", {
            {"oscA.table", 0}, {"oscA.pos", 0.22f}, {"oscA.unison", 2}, {"oscA.detune", 0.06f}, {"oscA.spread", 0.4f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.08f}, {"oscB.oct", 1}, {"oscB.level", 0.15f},
            {"filter.type", 0}, {"filter.cutoff", 2200}, {"filter.res", 0.1f}, {"filter.env", 0.45f}, {"filter.key", 0.65f},
            {"env1.a", 0.001f}, {"env1.d", 1.3f}, {"env1.s", 0}, {"env1.r", 1.1f},
            {"env2.a", 0.001f}, {"env2.d", 0.3f}, {"env2.s", 0}, {"env2.r", 0.25f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.35f},
            {"mat2.src", 3}, {"mat2.dst", 1}, {"mat2.amt", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.36f}, {"fx.delay.fb", 0.3f}, {"fx.delay.mix", 0.15f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.75f}, {"fx.reverb.mix", 0.38f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -0.6f}, {"fx.eq.mfreq", 273}, {"fx.eq.high", 6.0f},
        }},

        {"HARPSI COMB", {
            {"oscA.table", 0}, {"oscA.pos", 0}, {"oscA.level", 0.45f},
            {"noise.on", 1}, {"noise.level", 0.85f},
            {"filter.type", 5}, {"filter.cutoff", 262}, {"filter.res", 0.88f}, {"filter.key", 1},
            {"env1.a", 0.001f}, {"env1.d", 1.1f}, {"env1.s", 0}, {"env1.r", 0.9f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.34f}, {"fx.delay.fb", 0.3f}, {"fx.delay.mix", 0.2f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.28f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 1165}, {"fx.eq.high", -6.0f},
        }},

        {"GHOST CHOIR", {
            {"oscA.table", 1}, {"oscA.pos", 0.5f}, {"oscA.unison", 5}, {"oscA.detune", 0.22f}, {"oscA.spread", 0.9f}, {"oscA.level", 0.9f},
            {"oscB.on", 1}, {"oscB.table", 3}, {"oscB.pos", 0.35f}, {"oscB.fine", -7}, {"oscB.level", 0.5f},
            {"filter.type", 6}, {"filter.cutoff", 700}, {"filter.res", 0.4f},
            {"env1.a", 1.4f}, {"env1.d", 2}, {"env1.s", 0.8f}, {"env1.r", 2.4f},
            {"lfo1.rate", 0.09f}, {"lfo2.rate", 0.13f},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.35f},
            {"mat2.src", 2}, {"mat2.dst", 6}, {"mat2.amt", 0.25f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.45f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.85f}, {"fx.reverb.mix", 0.45f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", -3.0f}, {"fx.eq.mfreq", 582}, {"fx.eq.high", -6.0f},
        }},

        {"DATA STREAM", {
            {"oscA.table", 5}, {"oscA.pos", 0.55f}, {"oscA.level", 0.8f},
            {"filter.type", 1}, {"filter.cutoff", 1400}, {"filter.res", 0.5f},
            {"env1.a", 0.002f}, {"env1.d", 0.2f}, {"env1.s", 0.8f}, {"env1.r", 0.08f},
            {"lfo1.shape", 4}, {"lfo1.rate", 9},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.55f},
            {"mat2.src", 1}, {"mat2.dst", 1}, {"mat2.amt", 0.6f},
            {"mat3.src", 1}, {"mat3.dst", 6}, {"mat3.amt", 0.5f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.22f}, {"fx.delay.fb", 0.4f}, {"fx.delay.mix", 0.28f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 1.9f}, {"fx.eq.mid", 4.5f}, {"fx.eq.mfreq", 356}, {"fx.eq.high", -6.0f},
        }},

        {"OCEAN AIR", {
            {"oscA.table", 1}, {"oscA.pos", 0.25f}, {"oscA.unison", 4}, {"oscA.detune", 0.25f}, {"oscA.spread", 0.9f}, {"oscA.level", 0.6f},
            {"noise.on", 1}, {"noise.type", 1}, {"noise.level", 0.35f},
            {"filter.type", 0}, {"filter.cutoff", 1900}, {"filter.res", 0.1f},
            {"env1.a", 1.8f}, {"env1.d", 2.5f}, {"env1.s", 0.8f}, {"env1.r", 3},
            {"lfo1.rate", 0.07f}, {"lfo2.rate", 0.05f},
            {"mat1.src", 1}, {"mat1.dst", 25}, {"mat1.amt", 0.3f},
            {"mat2.src", 1}, {"mat2.dst", 3}, {"mat2.amt", 0.3f},
            {"mat3.src", 2}, {"mat3.dst", 6}, {"mat3.amt", 0.3f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.9f}, {"fx.reverb.mix", 0.5f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 3.5f}, {"fx.eq.mid", 4.9f}, {"fx.eq.mfreq", 450}, {"fx.eq.high", -6.0f},
        }},

        {"TWIN SKY", {
            {"oscA.table", 0}, {"oscA.pos", 0.62f}, {"oscA.unison", 6}, {"oscA.detune", 0.28f}, {"oscA.spread", 1}, {"oscA.level", 0.65f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.oct", 1}, {"oscB.unison", 3}, {"oscB.detune", 0.2f}, {"oscB.spread", 0.8f}, {"oscB.level", 0.4f},
            {"filter.route", 2}, {"filter.type", 0}, {"filter.cutoff", 900}, {"filter.res", 0.2f},
            {"filter2.on", 1}, {"filter2.type", 3}, {"filter2.cutoff", 2500}, {"filter2.res", 0.25f},
            {"env1.a", 0.8f}, {"env1.d", 1.5f}, {"env1.s", 0.85f}, {"env1.r", 1.8f},
            {"lfo1.rate", 0.15f}, {"lfo2.rate", 0.11f},
            {"mat1.src", 1}, {"mat1.dst", 3}, {"mat1.amt", 0.4f},
            {"mat2.src", 2}, {"mat2.dst", 9}, {"mat2.amt", -0.4f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.5f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.7f}, {"fx.reverb.mix", 0.35f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 5.7f}, {"fx.eq.mid", 6.0f}, {"fx.eq.mfreq", 507}, {"fx.eq.high", -6.0f},
        }},

        {"TAPE KEYS", {
            {"oscA.table", 0}, {"oscA.pos", 0.12f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 5}, {"oscB.pos", 0.15f}, {"oscB.level", 0.15f},
            {"noise.on", 1}, {"noise.type", 1}, {"noise.level", 0.08f},
            {"filter.type", 0}, {"filter.cutoff", 1500}, {"filter.res", 0.12f}, {"filter.env", 0.3f}, {"filter.key", 0.35f},
            {"env1.a", 0.002f}, {"env1.d", 1.2f}, {"env1.s", 0.35f}, {"env1.r", 0.5f},
            {"env2.a", 0.001f}, {"env2.d", 0.3f}, {"env2.s", 0}, {"env2.r", 0.25f},
            {"lfo1.shape", 4}, {"lfo1.rate", 3.2f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.006f},
            {"mat2.src", 4}, {"mat2.dst", 3}, {"mat2.amt", 0.35f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.4f}, {"fx.reverb.mix", 0.28f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -4.8f}, {"fx.eq.mfreq", 264}, {"fx.eq.high", 6.0f},
        }},

        {"TALKBOX BASS", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.level", 1},
            {"sub.on", 1}, {"sub.shape", 1}, {"sub.level", 0.65f}, {"sub.oct", -1},
            {"filter.type", 6}, {"filter.cutoff", 500}, {"filter.res", 0.45f},
            {"env1.a", 0.003f}, {"env1.d", 0.3f}, {"env1.s", 0.9f}, {"env1.r", 0.12f},
            {"env2.a", 0.005f}, {"env2.d", 0.28f}, {"env2.s", 0}, {"env2.r", 0.15f},
            {"lfo1.shape", 1}, {"lfo1.rate", 4.5f},
            {"mat1.src", 3}, {"mat1.dst", 3}, {"mat1.amt", 0.5f},
            {"mat2.src", 1}, {"mat2.dst", 3}, {"mat2.amt", 0.25f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.4f}, {"fx.drive.mix", 0.2f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 0.2f}, {"fx.eq.mfreq", 577}, {"fx.eq.high", -6.0f},
        }},

        {"AURORA RISER", {
            {"oscA.table", 1}, {"oscA.pos", 0.3f}, {"oscA.unison", 6}, {"oscA.detune", 0.3f}, {"oscA.spread", 1}, {"oscA.level", 0.65f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0}, {"oscB.oct", 1}, {"oscB.fine", 8}, {"oscB.level", 0.3f},
            {"filter.type", 0}, {"filter.cutoff", 1000}, {"filter.res", 0.12f},
            {"env1.a", 2.5f}, {"env1.d", 3}, {"env1.s", 1}, {"env1.r", 3.5f},
            {"env2.a", 4}, {"env2.d", 5}, {"env2.s", 1}, {"env2.r", 3},
            {"lfo1.rate", 5.5f}, {"lfo1.rise", 3.5f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", 0.6f},
            {"mat2.src", 3}, {"mat2.dst", 3}, {"mat2.amt", 0.5f},
            {"mat3.src", 1}, {"mat3.dst", 4}, {"mat3.amt", 0.006f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.45f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.9f}, {"fx.reverb.mix", 0.5f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -1.5f}, {"fx.eq.mid", -0.5f}, {"fx.eq.mfreq", 372}, {"fx.eq.high", 2.0f},
        }},

        {"GAMELAN POT", {
            {"oscA.table", 4}, {"oscA.pos", 0.65f}, {"oscA.level", 1},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.4f}, {"oscB.semi", 5}, {"oscB.fine", -12}, {"oscB.level", 0.45f},
            {"filter.type", 2}, {"filter.cutoff", 1500}, {"filter.res", 0.3f}, {"filter.key", 0.6f},
            {"env1.a", 0.001f}, {"env1.d", 1.6f}, {"env1.s", 0}, {"env1.r", 1.4f},
            {"env2.a", 0.001f}, {"env2.d", 0.5f}, {"env2.s", 0}, {"env2.r", 0.4f},
            {"mat1.src", 3}, {"mat1.dst", 2}, {"mat1.amt", -0.4f},
            {"mat2.src", 4}, {"mat2.dst", 3}, {"mat2.amt", 0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.45f}, {"fx.delay.fb", 0.35f}, {"fx.delay.mix", 0.22f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.65f}, {"fx.reverb.mix", 0.32f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 5.9f}, {"fx.eq.mfreq", 1183}, {"fx.eq.high", -6.0f},
        }},

        {"PUMP PAD", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 7}, {"oscA.detune", 0.35f}, {"oscA.spread", 1}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.2f}, {"oscB.oct", -1}, {"oscB.level", 0.4f},
            {"filter.type", 0}, {"filter.cutoff", 3000}, {"filter.res", 0.1f},
            {"env1.a", 0.05f}, {"env1.d", 0.5f}, {"env1.s", 0.9f}, {"env1.r", 0.4f},
            {"lfo1.shape", 2}, {"lfo1.sync", 1}, {"lfo1.syncrate", 2}, {"lfo1.retrig", 0},
            {"mat1.src", 1}, {"mat1.dst", 5}, {"mat1.amt", -0.45f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.3f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 3.3f}, {"fx.eq.mid", 3.5f}, {"fx.eq.mfreq", 475}, {"fx.eq.high", -6.0f},
        }},

        {"MINI LEAD", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 2}, {"oscA.detune", 0.12f}, {"oscA.spread", 0.2f}, {"oscA.level", 0.85f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.oct", -1}, {"oscB.fine", 6}, {"oscB.level", 0.5f},
            {"filter.type", 1}, {"filter.cutoff", 1300}, {"filter.res", 0.3f}, {"filter.env", 0.55f}, {"filter.key", 0.35f}, {"filter.drive", 0.2f},
            {"env1.a", 0.004f}, {"env1.d", 0.35f}, {"env1.s", 0.75f}, {"env1.r", 0.15f},
            {"env2.a", 0.002f}, {"env2.d", 0.4f}, {"env2.s", 0.3f}, {"env2.r", 0.2f},
            {"master.glide", 0.05f},
            {"lfo1.rate", 5.2f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.005f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.3f}, {"fx.drive.mix", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.28f}, {"fx.delay.fb", 0.25f}, {"fx.delay.mix", 0.15f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -3.9f}, {"fx.eq.mid", -3.9f}, {"fx.eq.mfreq", 430}, {"fx.eq.high", 6.0f},
        }},

        {"JUNO DREAM", {
            {"oscA.table", 2}, {"oscA.pos", 0.35f}, {"oscA.unison", 3}, {"oscA.detune", 0.15f}, {"oscA.spread", 0.7f}, {"oscA.level", 0.75f},
            {"sub.on", 1}, {"sub.shape", 1}, {"sub.level", 0.45f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 2000}, {"filter.res", 0.12f}, {"filter.env", 0.25f}, {"filter.key", 0.25f},
            {"env1.a", 0.4f}, {"env1.d", 1}, {"env1.s", 0.8f}, {"env1.r", 1.4f},
            {"lfo1.rate", 0.4f}, {"lfo2.rate", 0.17f},
            {"mat1.src", 1}, {"mat1.dst", 1}, {"mat1.amt", 0.3f},
            {"mat2.src", 2}, {"mat2.dst", 6}, {"mat2.amt", 0.2f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.5f}, {"fx.chorus.mix", 0.6f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.6f}, {"fx.reverb.mix", 0.3f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -1.0f}, {"fx.eq.mfreq", 448}, {"fx.eq.high", 6.0f},
        }},

        {"JUMP BRASS", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 2}, {"oscA.detune", 0.18f}, {"oscA.spread", 0.6f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.1f}, {"oscB.fine", -7}, {"oscB.level", 0.6f},
            {"filter.type", 0}, {"filter.cutoff", 4500}, {"filter.res", 0.08f}, {"filter.env", 0.15f}, {"filter.key", 0.3f},
            {"env1.a", 0.005f}, {"env1.d", 0.4f}, {"env1.s", 0.9f}, {"env1.r", 0.3f},
            {"env2.a", 0.003f}, {"env2.d", 0.25f}, {"env2.s", 0.4f}, {"env2.r", 0.25f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.35f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.5f}, {"fx.reverb.mix", 0.28f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 4.6f}, {"fx.eq.mid", 5.7f}, {"fx.eq.mfreq", 553}, {"fx.eq.high", -6.0f},
        }},

        {"BLADE BRASS", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 2}, {"oscA.detune", 0.1f}, {"oscA.spread", 0.4f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.25f}, {"oscB.fine", 9}, {"oscB.level", 0.5f},
            {"filter.type", 0}, {"filter.cutoff", 900}, {"filter.res", 0.2f}, {"filter.env", 0.6f}, {"filter.key", 0.3f},
            {"env1.a", 0.15f}, {"env1.d", 0.8f}, {"env1.s", 0.85f}, {"env1.r", 1.6f},
            {"env2.a", 0.25f}, {"env2.d", 1.2f}, {"env2.s", 0.6f}, {"env2.r", 1},
            {"master.glide", 0.09f},
            {"lfo1.rate", 4.2f}, {"lfo1.rise", 1.2f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.006f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.3f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.8f}, {"fx.reverb.mix", 0.42f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 3.6f}, {"fx.eq.mid", 2.8f}, {"fx.eq.mfreq", 499}, {"fx.eq.high", -6.0f},
        }},

        {"FANTA BELLS", {
            {"oscA.table", 4}, {"oscA.pos", 0.6f}, {"oscA.oct", 1}, {"oscA.level", 0.15f},
            {"oscB.on", 1}, {"oscB.table", 3}, {"oscB.pos", 0.4f}, {"oscB.unison", 3}, {"oscB.detune", 0.18f}, {"oscB.spread", 0.8f}, {"oscB.level", 0.5f},
            {"noise.on", 1}, {"noise.type", 1}, {"noise.level", 0.12f},
            {"filter.type", 0}, {"filter.cutoff", 3500}, {"filter.res", 0.08f}, {"filter.key", 0.4f},
            {"env1.a", 0.005f}, {"env1.d", 1.5f}, {"env1.s", 0.75f}, {"env1.r", 2},
            {"env2.a", 0.001f}, {"env2.d", 1.2f}, {"env2.s", 0}, {"env2.r", 1},
            {"lfo1.rate", 0.3f},
            {"mat1.src", 3}, {"mat1.dst", 7}, {"mat1.amt", 0.5f},
            {"mat2.src", 1}, {"mat2.dst", 6}, {"mat2.amt", 0.2f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.35f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.85f}, {"fx.reverb.mix", 0.45f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", 0.9f}, {"fx.eq.mfreq", 624}, {"fx.eq.high", -6.0f},
        }},

        {"TAURUS PEDAL", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.level", 0.8f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 1}, {"oscB.oct", -1}, {"oscB.level", 0.5f},
            {"sub.on", 1}, {"sub.level", 0.7f}, {"sub.oct", -1},
            {"filter.type", 1}, {"filter.cutoff", 380}, {"filter.res", 0.2f}, {"filter.env", 0.45f}, {"filter.drive", 0.3f},
            {"env1.a", 0.004f}, {"env1.d", 0.5f}, {"env1.s", 0.8f}, {"env1.r", 0.25f},
            {"env2.a", 0.003f}, {"env2.d", 0.4f}, {"env2.s", 0.2f}, {"env2.r", 0.2f},
            {"master.glide", 0.07f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.35f}, {"fx.drive.mix", 0.2f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 288}, {"fx.eq.high", 6.0f},
        }},

        {"WAVE DANCER", {
            {"oscA.table", 3}, {"oscA.pos", 0.2f}, {"oscA.level", 0.7f},
            {"oscB.on", 1}, {"oscB.table", 5}, {"oscB.pos", 0.35f}, {"oscB.oct", 1}, {"oscB.level", 0.3f},
            {"filter.type", 0}, {"filter.cutoff", 3800}, {"filter.res", 0.15f}, {"filter.key", 0.3f},
            {"env1.a", 0.004f}, {"env1.d", 0.6f}, {"env1.s", 0.7f}, {"env1.r", 0.4f},
            {"env2.a", 0.01f}, {"env2.d", 0.6f}, {"env2.s", 0.25f}, {"env2.r", 0.3f},
            {"mat1.src", 3}, {"mat1.dst", 1}, {"mat1.amt", 0.55f},
            {"mat2.src", 4}, {"mat2.dst", 1}, {"mat2.amt", 0.3f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.3f}, {"fx.delay.fb", 0.3f}, {"fx.delay.mix", 0.2f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 6.0f}, {"fx.eq.mid", -1.4f}, {"fx.eq.mfreq", 694}, {"fx.eq.high", -6.0f},
        }},

        {"FUNKY WORM", {
            {"oscA.table", 0}, {"oscA.pos", 0}, {"oscA.oct", 1}, {"oscA.level", 0.9f},
            {"filter.type", 0}, {"filter.cutoff", 4000}, {"filter.res", 0.1f}, {"filter.key", 0.5f},
            {"env1.a", 0.002f}, {"env1.d", 0.3f}, {"env1.s", 0.85f}, {"env1.r", 0.15f},
            {"master.glide", 0.22f},
            {"lfo1.rate", 5.8f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.007f},
            {"fx.drive.on", 1}, {"fx.drive.amt", 0.2f}, {"fx.drive.mix", 0.2f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.25f}, {"fx.delay.fb", 0.2f}, {"fx.delay.mix", 0.15f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -6.0f}, {"fx.eq.mid", -6.0f}, {"fx.eq.mfreq", 396}, {"fx.eq.high", 6.0f},
        }},

        {"LATELY BASS", {
            {"oscA.table", 0}, {"oscA.pos", 0.05f}, {"oscA.level", 0.9f},
            {"oscB.on", 1}, {"oscB.table", 0}, {"oscB.pos", 0.66f}, {"oscB.level", 0.45f},
            {"sub.on", 1}, {"sub.level", 0.4f}, {"sub.oct", -1},
            {"filter.type", 0}, {"filter.cutoff", 700}, {"filter.res", 0.12f}, {"filter.env", 0.65f}, {"filter.key", 0.4f},
            {"env1.a", 0.001f}, {"env1.d", 0.4f}, {"env1.s", 0.6f}, {"env1.r", 0.1f},
            {"env2.a", 0.001f}, {"env2.d", 0.18f}, {"env2.s", 0}, {"env2.r", 0.1f},
            {"mat1.src", 4}, {"mat1.dst", 3}, {"mat1.amt", 0.4f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", -4.2f}, {"fx.eq.mid", -0.5f}, {"fx.eq.mfreq", 271}, {"fx.eq.high", 4.7f},
        }},

        {"PROPHET STAB", {
            {"oscA.table", 0}, {"oscA.pos", 0.66f}, {"oscA.unison", 2}, {"oscA.detune", 0.1f}, {"oscA.spread", 0.5f}, {"oscA.level", 0.75f},
            {"oscB.on", 1}, {"oscB.table", 2}, {"oscB.pos", 0.3f}, {"oscB.fine", 5}, {"oscB.level", 0.55f},
            {"filter.type", 0}, {"filter.cutoff", 1700}, {"filter.res", 0.25f}, {"filter.env", 0.55f}, {"filter.key", 0.4f},
            {"env1.a", 0.003f}, {"env1.d", 0.5f}, {"env1.s", 0.35f}, {"env1.r", 0.3f},
            {"env2.a", 0.002f}, {"env2.d", 0.35f}, {"env2.s", 0.1f}, {"env2.r", 0.25f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.25f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.45f}, {"fx.reverb.mix", 0.22f},
        
            {"fx.eq.on", 1}, {"fx.eq.low", 1.0f}, {"fx.eq.mid", 1.1f}, {"fx.eq.mfreq", 458}, {"fx.eq.high", -2.1f},
        }},

        // ---- Ambient glide leads (51+) — smooth near-sine voices with slow
        // attacks, long releases, master.glide portamento and long delays.
        // Copied verbatim from src/presets.ts; all stay "clean" (no drive).

        {"FOG LIGHT", {
            {"oscA.table", 0}, {"oscA.pos", 0.02f}, {"oscA.level", 0.8f},
            {"sub.on", 1}, {"sub.shape", 0}, {"sub.oct", -1}, {"sub.level", 0.45f},
            {"filter.type", 1}, {"filter.cutoff", 1100}, {"filter.res", 0.08f}, {"filter.env", 0.15f}, {"filter.key", 0.3f},
            {"env1.a", 0.25f}, {"env1.d", 1.5f}, {"env1.s", 0.85f}, {"env1.r", 2.5f},
            {"env2.a", 0.4f}, {"env2.d", 1.2f}, {"env2.s", 0.3f}, {"env2.r", 1.5f},
            {"master.glide", 0.22f},
            {"lfo1.shape", 0}, {"lfo1.rate", 4.5f}, {"lfo1.rise", 1.2f},
            {"lfo2.rate", 0.08f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.006f},
            {"mat2.src", 2}, {"mat2.dst", 3}, {"mat2.amt", 0.15f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.8f}, {"fx.delay.fb", 0.55f}, {"fx.delay.mix", 0.4f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.85f}, {"fx.reverb.mix", 0.45f},
            {"fx.eq.on", 1}, {"fx.eq.low", 1.5f}, {"fx.eq.high", -4.0f},
        }},

        {"GLASS RIBBON", {
            {"oscA.table", 0}, {"oscA.pos", 0.0f}, {"oscA.level", 0.75f},
            {"oscB.on", 1}, {"oscB.table", 4}, {"oscB.pos", 0.2f}, {"oscB.oct", 1}, {"oscB.level", 0.22f},
            {"filter.type", 0}, {"filter.cutoff", 3500}, {"filter.res", 0.1f},
            {"env1.a", 0.12f}, {"env1.d", 1.2f}, {"env1.s", 0.8f}, {"env1.r", 2.0f},
            {"master.glide", 0.14f},
            {"lfo1.shape", 0}, {"lfo1.rate", 5.0f}, {"lfo1.rise", 1.0f},
            {"lfo2.rate", 0.15f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.007f},
            {"mat2.src", 2}, {"mat2.dst", 8}, {"mat2.amt", 0.15f},
            {"fx.chorus.on", 1}, {"fx.chorus.rate", 0.4f}, {"fx.chorus.mix", 0.4f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.7f}, {"fx.delay.fb", 0.5f}, {"fx.delay.mix", 0.38f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.7f}, {"fx.reverb.mix", 0.35f},
            {"fx.eq.on", 1}, {"fx.eq.low", 0.5f}, {"fx.eq.high", 1.0f},
        }},

        {"NORTH WIRE", {
            {"oscA.table", 0}, {"oscA.pos", 0.0f}, {"oscA.oct", 1}, {"oscA.unison", 2}, {"oscA.detune", 0.06f}, {"oscA.spread", 0.3f}, {"oscA.level", 0.8f},
            {"filter.type", 0}, {"filter.cutoff", 6000}, {"filter.res", 0.05f},
            {"env1.a", 0.06f}, {"env1.d", 1.0f}, {"env1.s", 0.75f}, {"env1.r", 1.8f},
            {"master.glide", 0.18f},
            {"lfo1.shape", 0}, {"lfo1.rate", 5.5f}, {"lfo1.rise", 1.5f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.005f},
            {"fx.delay.on", 1}, {"fx.delay.time", 1.0f}, {"fx.delay.fb", 0.65f}, {"fx.delay.mix", 0.5f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.8f}, {"fx.reverb.mix", 0.35f},
            {"fx.eq.on", 1}, {"fx.eq.low", -2.0f}, {"fx.eq.high", 2.0f},
        }},

        {"TEMPLE BREATH", {
            {"oscA.table", 0}, {"oscA.pos", 0.05f}, {"oscA.level", 0.78f},
            {"noise.on", 1}, {"noise.type", 1}, {"noise.level", 0.12f},
            {"filter.type", 0}, {"filter.cutoff", 2400}, {"filter.res", 0.12f}, {"filter.key", 0.3f},
            {"env1.a", 0.18f}, {"env1.d", 1.4f}, {"env1.s", 0.85f}, {"env1.r", 2.2f},
            {"master.glide", 0.16f},
            {"lfo1.shape", 0}, {"lfo1.rate", 4.2f}, {"lfo1.rise", 1.4f},
            {"lfo2.rate", 0.1f},
            {"mat1.src", 1}, {"mat1.dst", 4}, {"mat1.amt", 0.008f},
            {"mat2.src", 2}, {"mat2.dst", 25}, {"mat2.amt", 0.1f},
            {"fx.chorus.on", 1}, {"fx.chorus.mix", 0.3f},
            {"fx.delay.on", 1}, {"fx.delay.time", 0.7f}, {"fx.delay.fb", 0.52f}, {"fx.delay.mix", 0.36f},
            {"fx.reverb.on", 1}, {"fx.reverb.size", 0.75f}, {"fx.reverb.mix", 0.4f},
            {"fx.eq.on", 1}, {"fx.eq.low", 2.0f}, {"fx.eq.mid", 1.5f}, {"fx.eq.mfreq", 500}, {"fx.eq.high", -3.0f},
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
