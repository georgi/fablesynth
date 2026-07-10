// DR-1 plugin-boundary verification: instantiates the real DrumAudioProcessor
// and drives it the way a DAW would — APVTS parameter surface, the 5-bus
// multi-out layout, MIDI pad triggers, per-pad AUX routing, the internal
// sequencer via kit program 0 (TR-VOID), and host-tempo sync through a mock
// AudioPlayHead. Modeled on plugin_host_test.cpp.
#include "../source/drum/DrumProcessor.h"
#include <array>
#include <cmath>
#include <cstdio>

static int g_fail = 0;
static void check(bool c, const char* msg, double val = 0) {
    printf("  [%s] %s (%.5f)\n", c ? "PASS" : "FAIL", msg, val);
    if (!c) g_fail++;
}

// Minimal host playhead reporting a fixed tempo (bpm-only sync, per spec).
struct MockPlayHead : juce::AudioPlayHead {
    double bpm = 90.0;
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override {
        juce::AudioPlayHead::PositionInfo pos;
        pos.setBpm(bpm);
        pos.setIsPlaying(true);
        return pos;
    }
};

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for the processor

    DrumAudioProcessor proc;
    const double sr = 48000;
    const int block = 512;

    printf("\n== DR-1 plugin-boundary test (DrumAudioProcessor) ==\n");

    // ---- 1. parameter surface: 788 params in the APVTS ----
    const int nParams = (int)proc.getParameters().size();
    check(nParams == fable::DR_NUM_PARAMS || nParams == fable::DR_NUM_PARAMS + 1, // + host bypass
          "788 host parameters", nParams);
    check(proc.getName() == "FableSynth DR-1", "plugin name");

    // ---- 2. bus layout: 5 stereo output buses ----
    check(proc.getBusCount(false) == 5, "5 output buses declared", proc.getBusCount(false));
    proc.enableAllBuses();
    check(proc.getTotalNumOutputChannels() == 10, "10 channels with all buses enabled",
          proc.getTotalNumOutputChannels());

    proc.prepareToPlay(sr, block);
    juce::AudioBuffer<float> buf(10, block);
    bool allFinite = true;

    // Render nBlocks, optionally firing a MIDI note at block 0; returns the
    // per-bus RMS and tracks step-counter changes + finiteness.
    int stepChanges = 0, lastStep = -1;
    auto render = [&](int nBlocks, int note, float vel) {
        std::array<double, 5> sumSq{};
        long cnt = 0;
        for (int b = 0; b < nBlocks; ++b) {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0 && note >= 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
            proc.processBlock(buf, midi);
            int s = proc.getCurrentStep();
            if (s != lastStep) { stepChanges++; lastStep = s; }
            for (int bus = 0; bus < 5; ++bus) {
                auto bb = proc.getBusBuffer(buf, false, bus);
                for (int ch = 0; ch < bb.getNumChannels(); ++ch) {
                    const float* d = bb.getReadPointer(ch);
                    for (int i = 0; i < block; ++i) {
                        if (!std::isfinite(d[i])) allFinite = false;
                        sumSq[(size_t)bus] += (double)d[i] * d[i];
                    }
                }
            }
            cnt += 2 * block;
        }
        std::array<double, 5> rms{};
        for (int bus = 0; bus < 5; ++bus) rms[(size_t)bus] = std::sqrt(sumSq[(size_t)bus] / (double)cnt);
        return rms;
    };

    // ---- 3. MIDI note 36 (pad 0) -> audio on MAIN within 2 blocks ----
    proc.consumeHitFlags(); // clear
    auto r3 = render(2, 36, 1.0f);
    check(r3[0] > 1e-5, "MIDI note 36 -> MAIN audible within 2 blocks", r3[0]);
    check((proc.consumeHitFlags() & 1u) != 0, "hit flag published for pad 0");
    check(proc.getMidiActive(), "MIDI activity LED glows");
    render(30, -1, 0); // let the pad decay

    // ---- 4. multi-out: pad 0 -> AUX 2 renders on bus 2, silent on MAIN ----
    // Reverb off while checking MAIN silence: test 3's MAIN hit leaves a long
    // Freeverb tail that would mask the routing assertion otherwise.
    if (auto* p = proc.apvts.getParameter("fx.reverb.on"))
        p->setValueNotifyingHost(0.0f);
    render(60, -1, 0); // flush the wet tail (equal-power dry ramp ~0.02 s)
    if (auto* p = proc.apvts.getParameter("pad0.out"))
        p->setValueNotifyingHost(p->convertTo0to1(2.0f)); // AUX 2
    auto r4 = render(20, 36, 1.0f);
    check(r4[2] > 1e-4, "pad 0 routed to AUX 2 audible on bus 2", r4[2]);
    check(r4[0] < 1e-6, "MAIN silent while pad routed to AUX 2", r4[0]);
    if (auto* p = proc.apvts.getParameter("pad0.out"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f)); // back to MAIN
    render(40, -1, 0); // decay (program 0 below restores fx.reverb.on)

    // ---- 5. sequencer with TR-VOID (program 0) ----
    check(proc.getNumPrograms() == 3, "3 kit programs", proc.getNumPrograms());
    check(proc.getProgramName(0) == "TR-VOID", "program 0 is TR-VOID");
    proc.setCurrentProgram(0);
    float bpmParam = proc.apvts.getRawParameterValue("seq.bpm")->load();
    check(std::abs(bpmParam - 126.0f) < 0.5f, "TR-VOID applies seq.bpm=126 to APVTS", bpmParam);

    proc.setSeqPlaying(true);
    stepChanges = 0; lastStep = -1;
    // one bar at 126 bpm = 16 * (60/126/4) * 48000 ~= 91429 samples ~= 179 blocks
    auto r5 = render(180, -1, 0);
    check(proc.isSeqPlaying(), "sequencer reports playing");
    check(r5[0] > 1e-4, "TR-VOID bar produces audio on MAIN", r5[0]);
    check(stepChanges >= 12, "step counter advances across the bar", stepChanges);
    check(proc.getCurrentPattern() == 0, "current pattern is chain[0] = A", proc.getCurrentPattern());
    proc.setSeqPlaying(false);
    render(4, -1, 0);
    check(proc.getCurrentStep() == -1, "stopped: currentStep reads -1", proc.getCurrentStep());
    check(!proc.isHostSynced(), "no playhead -> not host-synced");

    // ---- 6. host tempo: mock playhead at 90 bpm drives the step clock ----
    MockPlayHead ph;
    proc.setPlayHead(&ph);
    proc.setSeqPlaying(true);
    stepChanges = 0; lastStep = -1;
    // 4 s at 90 bpm = 90/60*4 = 6 steps/s -> ~24 step advances (vs 33.6 at 126).
    const int blocks4s = (int)(4.0 * sr / block);
    render(blocks4s, -1, 0);
    check(proc.isHostSynced(), "host tempo reported -> isHostSynced()");
    check(std::abs(proc.getHostBpm() - 90.0) < 0.01, "getHostBpm() = 90", proc.getHostBpm());
    check(stepChanges >= 21 && stepChanges <= 27,
          "step spacing follows host 90 bpm (~24 steps in 4 s)", stepChanges);
    proc.setSeqPlaying(false);
    proc.setPlayHead(nullptr);
    render(2, -1, 0);
    check(!proc.isHostSynced(), "playhead removed -> sync drops");

    // ---- 7. everything rendered finite ----
    check(allFinite, "all bus outputs finite");

    printf("%s\n", g_fail == 0 ? "DRUM PLUGIN CHECKS PASSED" : "DRUM PLUGIN CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
