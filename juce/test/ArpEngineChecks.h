#pragma once
#include "../source/dsp/Arp.h"
#include "../source/dsp/Engine.h"
#include "../source/dsp/Presets.h"
#include "../source/bass/dsp/BassEngine.h"
#include "../source/bass/dsp/BassPatches.h"
#include <memory>
#include <type_traits>
#if FABLE_SQ4_TEST_JUCE
#include "../source/ui/ArpCodec.h"
#include "../source/seq/SessionCodec.h"
#include "../source/seq/ClipClipboardCodec.h"
#include "../source/seq/dsp/SeqFactory.h"
#endif

namespace arp_checks {
template<class Engine> struct Rig {
    static constexpr bool bass = std::is_same_v<Engine, fable::BassEngine>;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    double frame = 0;
    bool finite = true;
    float peak = 0;
    std::vector<fable::HostEvent> events;
    Rig(const std::vector<fable::TablePtr>& tables) {
        engine->prepare(48000); engine->setTables(tables);
        if constexpr (bass) {
            auto p = fable::applyBassPatch(fable::bassFactoryPatches()[0]);
            p[fable::BL_SEQ_BPM] = 120; p[fable::BL_MASTER_SWING] = 0;
            engine->setParams(p); engine->snapParams();
        } else {
            auto p = fable::applyPreset(fable::factoryPresets()[3]);
            p[fable::SEQ_BPM] = 120; p[fable::SEQ_SWING] = 0; p[fable::SEQ_ROOT] = 48;
            engine->setParams(p);
        }
    }
    void render(int samples, int block = 128) {
        std::array<float, 512> l {}, r {};
        while (samples > 0) {
            const int n = std::min(samples, block);
            engine->hostSetFrame(frame); engine->render(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) {
                finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
                peak = std::max(peak, std::max(std::abs(l[i]), std::abs(r[i])));
            }
            fable::HostEvent e[64]; int count;
            while ((count = engine->takeHostEvents(e, 64)) > 0)
                events.insert(events.end(), e, e + count);
            frame += n; samples -= n;
        }
    }
    bool gated() const {
        if constexpr (bass) return engine->vizGate;
        else return engine->seqPendingOffCount() > 0;
    }
    int pitch() const {
        if constexpr (bass) return engine->vizSemi + fable::BL_ROOT_MIDI;
        else return engine->seqCurrentNote();
    }
    void play() { if constexpr (bass) engine->play(); else engine->seqPlay(); }
    void stop() { if constexpr (bass) engine->stop(); else engine->seqStop(); }
};

template<class Engine, class Check>
void playback(const std::vector<fable::TablePtr>& tables, Check check) {
    using namespace fable;
    constexpr bool bass = Rig<Engine>::bass;
    const auto machine = bass ? Machine::BL1 : Machine::WT1;
    ArpSettings settings = bass ? bassArpDefaults() : ArpSettings{};
    settings.enabled = true;
    auto a = compileArp(settings);
    {
        Rig<Engine> r(tables); r.engine->setArp(a); r.play(); r.render(1);
        check(r.gated() && r.pitch() == a.notes[0], "arp starts with the compiled first pitch");
        r.render(4000); check(!r.gated(), "arp releases at its gate boundary");
        r.render(2000); check(r.gated() && r.pitch() == a.notes[1], "arp advances to second pitch");
        auto empty = a; empty.notes.fill(-1); r.engine->setArp(empty); r.render(1);
        check(!r.gated(), "cleared arp pool releases immediately");
        r.engine->setArp(a); r.render(6000); r.stop(); r.render(1);
        check(!r.gated(), "stopping arp releases its notes");
        check(r.finite && r.peak > .0001f, "arp renders finite audible output");
    }
    {
        Rig<Engine> r(tables);
        if constexpr (bass) {
            std::array<uint8_t, BL_PATTERN_BYTES> bytes {};
            bytes[0] = 5; bytes[1] = 7; bytes[2] = 1;
            r.engine->setPatterns(bytes.data(), (int)bytes.size());
        } else {
            std::array<uint8_t, SEQ_PATTERN_BYTES> bytes {};
            bytes[0] = 5; bytes[1] = 7; bytes[2] = 1;
            r.engine->setSeqPatterns(bytes.data(), (int)bytes.size());
        }
        r.engine->setArp(a); r.play(); r.render(1); r.stop();
        r.engine->setArp({}); r.play(); r.render(1);
        check(r.gated() && r.pitch() == (bass ? BL_ROOT_MIDI + 7 : 55), "disabling arp restores written sequence");
    }
    const auto bytes = sqEmptyClip(machine, 1);
    {
        Rig<Engine> r(tables); r.engine->setHostClipMode(true, 512);
        r.engine->hostTempo(120, 0, 512);
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 512, 0, a);
        r.play(); r.render(512);
        check(!r.gated(), "hosted arp cannot start its own clock before launch");
        r.render(1); check(r.gated(), "hosted arp starts on launch");
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 12032, 1, a);
        r.engine->hostClipUpdate(bytes.data(), (int)bytes.size(), 1, {});
        r.render(1); check(r.gated(), "queued clip edit does not release live arp");
        r.render(12032 - (int)r.frame + 1);
        check(!r.gated(), "queued sequence replaces arp at launch boundary");
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 0, 0, a);
        r.render(1); check(r.gated(), "hosted arp can relaunch");
        r.engine->hostClipStop(0); r.render(1);
        check(!r.gated(), "host stop releases hosted arp");
    }
    for (const int block : {64, 96, 128}) {
        Rig<Engine> r(tables); r.engine->setHostClipMode(true, block);
        r.engine->hostTempo(127, .2, 512);
        auto triplet = a; triplet.rate = 1.0 / 6;
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 512, 0, triplet);
        r.render(240000, block);
        int index = 0; bool timing = true;
        const double duration = 60.0 / 127 / 6 * 48000;
        for (const auto& e : r.events) if (e.t == HostEvent::T::Pos) {
            const double expected = 512 + index * duration + (index % 2 ? .2 * SQ_SWING_MAX * duration : 0);
            timing &= std::abs(e.frame - expected) <= 128.01;
            ++index;
        }
        check(index > 60 && timing, "host triplets and swing stay phase-locked across block sizes");
    }
    {
        Rig<Engine> r(tables); r.engine->setHostClipMode(true, 512);
        r.engine->hostTempo(120, 0, 0);
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 0, 0, a);
        r.render(4600);
        auto slower = a; slower.rate = .5;
        r.engine->hostClipUpdate(bytes.data(), (int)bytes.size(), 1, slower);
        r.render(1); check(r.gated(), "live rate edit rephases on the current host frame");
        r.render(5100);
        check(!r.gated(), "rephased gate uses remaining interval, not a whole new step");
    }
    {
        Rig<Engine> r(tables); constexpr int samples = 240000;
        r.engine->setHostClipMode(true, samples); r.engine->hostTempo(200, 0, 0);
        auto fast = a; fast.rate = .125;
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 0, 0, fast);
        std::vector<float> left(samples), right(samples);
        r.engine->hostSetFrame(0); r.engine->render(left.data(), right.data(), samples);
        HostEvent batch[64]; int count = 0, n;
        while ((n = r.engine->takeHostEvents(batch, 64)) > 0)
            for (int i = 0; i < n; ++i) if (batch[i].t == HostEvent::T::Pos) ++count;
        check(count == 134, "fast arp preserves every event in oversized offline blocks");
    }
    if constexpr (bass) {
        Rig<Engine> r(tables); auto slide = a; slide.slides.fill(true); slide.gate = .05;
        r.engine->setArp(slide); r.play(); r.render(6001);
        check(r.gated(), "bass destination slide holds through the gate");
        slide.hits[2] = false; r.engine->setArp(slide); r.render(6000);
        check(!r.gated(), "rest breaks the bass slide chain");
        r.render(6000); check(r.gated(), "bass retriggers after a rest");
        slide.hits.fill(false); r.engine->setArp(slide); r.render(1);
        check(!r.gated(), "all-rest edit releases a sustained bass slide");
    }
}
}

template<class Check> void runArpEngineChecks(Check check) {
    using namespace fable;
    ArpSettings a; a.enabled = true; a.count = 3; a.notes = {{60, 50, 57}}; a.order = 2;
    auto p = compileArp(a);
    check(std::equal(p.notes.begin(), p.notes.begin() + 8, std::array<int, 8>{{50,57,60,57,50,57,60,57}}.begin()),
          "up/down does not duplicate turnarounds");
    a.octaves = 2; p = compileArp(a);
    check(std::equal(p.notes.begin(), p.notes.begin() + 7, std::array<int, 7>{{50,57,60,62,69,72,69}}.begin()),
          "octave expansion matches browser arp");
    a.order = 3; a.octaves = 1; p = compileArp(a);
    check(p.notes[0] == 60 && p.notes[1] == 50 && p.notes[2] == 57, "played order retains insertion order");
    a.order = 4;
    check(compileArp(a).notes == std::array<int, 16>{{50,50,60,60,57,57,57,60,60,60,50,60,50,50,60,50}},
          "seeded random order matches the browser LCG sequence");
    a.count = 0; p = compileArp(a);
    check(std::all_of(p.notes.begin(), p.notes.end(), [](int n) { return n == -1; }), "empty pool compiles to rests");
    ArpMailbox mailbox; ArpSettings read; uint32_t version = 0;
    mailbox.publish(a); check(mailbox.consume(read, version) && read.count == 0 && read.order == 4,
                             "arp mailbox transfers a complete settings snapshot");
    check(!mailbox.consume(read, version), "unchanged mailbox does not reapply settings");
#if FABLE_SQ4_TEST_JUCE
    {
        const auto original = bassArpDefaults(); ArpSettings restored;
        check(arpFromVar(juce::JSON::parse(juce::JSON::toString(arpToVar(original))), restored, true)
              && compileArp(original).notes == compileArp(restored).notes,
              "portable arp JSON preserves disabled settings");
        for (const auto* key : {"rate", "gate", "octaves", "seed"}) {
            auto v = arpToVar(original);
            v["settings"].getDynamicObject()->setProperty(key, "1");
            check(!arpFromVar(v, restored, true), "arp JSON rejects string-coerced numeric fields");
        }
        {
            auto v = arpToVar(original);
            v["settings"]["notes"].getArray()->set(0, true);
            check(!arpFromVar(v, restored, true), "arp JSON rejects boolean notes");
        }
        auto keys = original; keys.keys = true; keys.latch = true;
        check(arpFromVar(arpToVar(keys), restored) && !arpFromVar(arpToVar(keys), restored, true),
              "live-key state is standalone only, not portable clip data");
        auto session = factorySession();
        auto& clip = session.scenes[2].clips[1];
        clip.hasArp = true; clip.arp = bassArpDefaults(); clip.arp.enabled = true;
        clip.arp.slides[3] = true; clip.arp.hits[5] = false; clip.arp.rate = 1.0 / 6;
        for (const double rate : {1.0 / 6, 1.0 / 3}) {
            clip.arp.rate = rate;
            const auto json = juce::JSON::parse(sessionToJson(session));
            check((double)json["scenes"][2]["clips"][1]["arp"]["settings"]["rate"] == rate,
                  "SQ export preserves exact web triplet constants");
        }
        SessionData recalled;
        const bool loaded = sessionFromJson(sessionToJson(session), recalled);
        check(loaded
              && recalled.scenes[2].clips[1].hasArp
              && juce::JSON::toString(arpToVar(recalled.scenes[2].clips[1].arp)) == juce::JSON::toString(arpToVar(clip.arp)),
              "SQ session round-trips per-clip arp settings");
        if (loaded) recalled.scenes[2].clips[1].arp.notes[0] = 99;
        check(clip.arp.notes[0] == 36, "SQ recalled arp data is value-owned");
        ClipClipboardData copied {{Machine::BL1}, {{clip}}, {{true}}}, pasted;
        check(clipClipboardFromJson(clipClipboardToJson(copied), pasted)
              && pasted.cells[0][0].hasArp && pasted.cells[0][0].arp.slides[3],
              "SQ clipboard preserves arp settings");
        check(sessionFromJson(sessionToJson(factorySession()), recalled) && !recalled.scenes[2].clips[1].hasArp,
              "legacy SQ sessions without arp still load");
        session.scenes[2].clips[0].hasArp = true;
        check(!sessionFromJson(sessionToJson(session), recalled), "SQ rejects arp on drum clips");
    }
#endif
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    arp_checks::playback<Engine>(tables, check);
    arp_checks::playback<BassEngine>(tables, check);
}
