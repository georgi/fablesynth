#pragma once

// Friend access injects fixtures at the real processor's summing/master
// boundaries; production uses exactly these methods, with no test DSP path.
struct SeqOutputTestAccess {
    static std::array<float, 33> masterParams() {
        std::array<float, 33> values {};
        for (const auto& p : fable::masterFxParamInfo()) values[(size_t)p.id] = p.def;
        values[21] = values[26] = 0; // leave only the legacy master compressor
        return values;
    }

    static void safetyAndLegacy() {
        SeqAudioProcessor p;
        for (double sr : {44100.0, 48000.0, 96000.0}) {
            p.prepareToPlay(sr, 128);
            for (float ceilingDb : {-12.0f, -1.0f, -0.1f, 0.0f}) {
                const bool enabled = ceilingDb < 0;
                const double ceiling = std::pow(10.0, (enabled ? ceilingDb : -1.0f) / 20.0);
                for (int block : {1, 7, 128, 257}) {
                    auto params = masterParams();
                    params[31] = enabled ? 1.f : 0.f;
                    params[32] = enabled ? ceilingDb : -1.f;
                    p.masterFx_.setParams(params); p.masterFx_.reset();
                    p.limiter_.reset(); p.outputLimiter_.reset();
                    p.masterGain_.setCurrentAndTargetValue(1.f);
                    std::vector<float> l(2048), r(2048);
                    l[300] = 4.f; r[300] = -1.f; // silence -> isolated overload
                    for (int i = 1000; i < 1017; ++i) { l[(size_t)i] = -.6f; r[(size_t)i] = 3.f; }
                    float peak = 0;
                    bool finite = true, linked = true, meterMatches = true;
                    for (int at = 0; at < (int)l.size(); at += block) {
                        const int n = std::min(block, (int)l.size() - at);
                        p.processMaster(l.data() + at, r.data() + at, n);
                        float blockPeak = 0;
                        for (int i = at; i < at + n; ++i) {
                            finite &= std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]);
                            blockPeak = std::max(blockPeak, std::max(std::abs(l[(size_t)i]), std::abs(r[(size_t)i])));
                            if (std::abs(l[(size_t)i]) > 1.e-7f)
                                linked &= std::abs(r[(size_t)i] / l[(size_t)i] - (i < 900 ? -.25f : -5.f)) < 1.e-5f;
                        }
                        peak = std::max(peak, blockPeak);
                        meterMatches &= std::abs(p.masterLimiterTelemetry()[1] - fable::FxMeter::db(blockPeak)) < 1.e-5f;
                    }
                    check(finite && peak > .1 && peak <= ceiling + 1.e-6,
                          "final SQ output bounds impulses/bursts after legacy makeup, all rates/block sizes", peak);
                    check(linked && meterMatches, "final limiting preserves stereo ratio and meters final output");
                }
            }

            // Below the safety ceiling, preserve the exact legacy compressor
            // transfer and gain envelope, apart from the declared lookahead.
            auto params = masterParams();
            p.masterFx_.setParams(params); p.masterFx_.reset();
            p.outputLimiter_.reset(); p.limiter_.reset();
            p.masterGain_.setCurrentAndTargetValue(.75f);
            SeqAudioProcessor::Limiter reference;
            reference.prepare(sr);
            const int delay = p.outputLimiter_.latencySamples();
            std::vector<float> l(2048), r(2048), old(2048);
            for (int i = 0; i < 1700; ++i) {
                l[(size_t)i] = .03f * std::sin((float)i * .12f); r[(size_t)i] = l[(size_t)i];
                float a = l[(size_t)i] * .75f, b = a;
                reference.process(a, b); old[(size_t)i] = a;
            }
            p.processMaster(l.data(), r.data(), (int)l.size());
            double error = 0;
            for (int i = delay; i < (int)l.size(); ++i)
                error = std::max(error, std::abs((double)l[(size_t)i] - old[(size_t)(i - delay)]));
            check(error < 1.e-7, "sub-ceiling master audio retains legacy sound after latency alignment", error);

            // Lower the ceiling while an old louder sample is still queued.
            params[32] = -.1f;
            p.masterFx_.setParams(params); p.masterFx_.reset();
            p.outputLimiter_.reset(); p.limiter_.reset();
            p.masterGain_.setCurrentAndTargetValue(1.f);
            float burstL = 4.f, burstR = -1.f;
            p.processMaster(&burstL, &burstR, 1);
            params[32] = -12.f; p.masterFx_.setParams(params);
            std::fill(l.begin(), l.end(), 0.f); std::fill(r.begin(), r.end(), 0.f);
            p.processMaster(l.data(), r.data(), (int)l.size());
            const float peak = *std::max_element(l.begin(), l.end());
            check(peak > .1f && peak <= std::pow(10.f, -12.f / 20.f) + 1.e-6f,
                  "lowered final ceiling also bounds audio already in lookahead", peak);
        }
    }

    template<class Params, class Catalog>
    static void bypassFx(Params& params, const Catalog& catalog) {
        for (const auto& p : catalog)
            if (p.pid.find("fx.") != std::string::npos && p.pid.size() >= 3
                && p.pid.substr(p.pid.size() - 3) == ".on") params[(size_t)p.id] = 0;
    }

    static void alignment() {
        SeqAudioProcessor p;
        for (double sr : {44100.0, 48000.0, 96000.0}) {
            p.prepareToPlay(sr, 128);
            fable::DrumFx pad, group;
            fable::DrumBusOut bus;
            fable::BassFx bass;
            fable::Fx wt;
            pad.prepare(sr); group.prepare(sr); bus.prepare(sr); bass.prepare(sr); wt.prepare(sr);
            auto dp = fable::defaultDrumParams(); auto bp = fable::defaultBassParams(); auto wp = fable::defaultParams();
            bypassFx(dp, fable::drumParamInfo()); bypassFx(bp, fable::bassParamInfo()); bypassFx(wp, fable::paramInfo());
            pad.setParams(dp, 0); group.setGroupParams(dp); bus.setParams(dp); bass.setParams(bp); wt.setParams(wp);
            const int longest = pad.latencySamples() + group.latencySamples() + bus.latencySamples();
            check(p.getLatencySamples() == longest + p.outputLimiter_.latencySamples(),
                  "host latency includes aligned tracks plus final lookahead", p.getLatencySamples());
            for (int block : {1, 7, 128, 257}) {
                pad.reset(); group.reset(); bus.reset(); bass.reset(); wt.reset();
                for (auto& track : p.trackDelay_) for (auto& channel : track) channel.reset();
                for (int track = 0; track < 4; ++track) {
                    if (track == 3) wt.reset();
                    std::vector<float> l(1024), r(1024);
                    l[0] = .01f; r[0] = -.005f;
                    for (int at = 0; at < 1024; at += block) {
                        const int n = std::min(block, 1024 - at);
                        auto* left = l.data() + at; auto* right = r.data() + at;
                        if (track == 0) { pad.process(left, right, n); group.process(left, right, n); bus.process(left, right, n); }
                        else if (track == 1) bass.process(left, right, n);
                        else wt.process(left, right, n);
                        p.alignTrack(track, left, right, n);
                    }
                    const int peak = (int)std::distance(l.begin(), std::max_element(l.begin(), l.end(),
                        [](float a, float b) { return std::abs(a) < std::abs(b); }));
                    check(peak == longest && std::abs(l[(size_t)peak]) > .001f,
                          "actual drum/bass/WT FX impulses align at summing junction", peak);
                    p.masterFx_.setParams(masterParams()); p.masterFx_.reset();
                    p.limiter_.reset(); p.outputLimiter_.reset();
                    p.masterGain_.setCurrentAndTargetValue(1.f);
                    p.processMaster(l.data(), r.data(), (int)l.size());
                    const int finalPeak = (int)std::distance(l.begin(), std::max_element(l.begin(), l.end(),
                        [](float a, float b) { return std::abs(a) < std::abs(b); }));
                    check(finalPeak == p.getLatencySamples(), "rendered total delay matches host declaration", finalPeak);
                }
            }
            // Reprepare must discard delay history and recompute for the new rate.
            float pendingL = 1.f, pendingR = -.5f;
            p.alignTrack(1, &pendingL, &pendingR, 1);
            p.prepareToPlay(sr, 7);
            float l[128] {}, r[128] {};
            p.alignTrack(1, l, r, 128);
            check(std::all_of(std::begin(l), std::end(l), [](float x) { return std::abs(x) < 1.e-20f; }),
                  "reprepare clears latency compensation history");
        }
    }

    static void mono() {
        auto render = [](bool mono, bool effects) {
            SeqAudioProcessor p;
            auto layout = p.getBusesLayout();
            layout.outputBuses.set(0, mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            check(p.setBusesLayout(layout), "SQ supports requested mono/stereo layout");
            p.prepareToPlay(48000, 128);
            auto set = [&](const char* id, float value) {
                auto* param = p.apvts.getParameter(id);
                param->setValueNotifyingHost(param->convertTo0to1(value));
            };
            set("master", .5f);
            auto fxSet = [&](const char* id, float value) {
                auto* param = p.masterFxParameter(id);
                param->setValueNotifyingHost(param->convertTo0to1(value));
            };
            fxSet("fx.eq.on", effects ? 1.f : 0.f); fxSet("fx.eq.low", -15.f); fxSet("fx.eq.lfreq", 2000.f);
            fxSet("fx.ott.on", effects ? 1.f : 0.f); fxSet("fx.comp.on", effects ? 1.f : 0.f);
            p.setTrackFactoryPatch(1, 0);
            p.auditionBassOn(0, .7f);
            juce::AudioBuffer<float> buffer(mono ? 1 : 2, 333); // larger than prepared capacity
            juce::MidiBuffer midi;
            std::vector<float> result;
            for (int b = 0; b < 30; ++b) {
                p.processBlock(buffer, midi);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    result.push_back(mono ? buffer.getSample(0, i)
                        : .5f * (buffer.getSample(0, i) + buffer.getSample(1, i)));
            }
            return result;
        };
        const auto stereo = render(false, true), mono = render(true, true), dry = render(true, false);
        double error = 0, difference = 0, energy = 0;
        for (size_t i = 0; i < mono.size(); ++i) {
            error = std::max(error, std::abs((double)mono[i] - stereo[i]));
            difference += std::abs(mono[i] - dry[i]); energy += mono[i] * mono[i];
        }
        check(error < 1.e-6 && energy > 1.e-6, "mono output equals post-master stereo downmix", error);
        check(difference > .01, "configurable master effects change mono render", difference);
    }

    static void tail() {
        SeqAudioProcessor p;
        const double declared = p.getTailLengthSeconds();
        // Actual BL delay: short mid-band burst, longest time and feedback.
        fable::BassFx fx;
        fx.prepare(48000);
        auto params = fable::defaultBassParams();
        bypassFx(params, fable::bassParamInfo());
        params[fable::BL_FXDELAY_ON] = 1; params[fable::BL_FXDELAY_TIME] = 1.5f;
        params[fable::BL_FXDELAY_FB] = .92f; params[fable::BL_FXDELAY_MIX] = 1;
        fx.setParams(params); fx.reset();
        double afterSix = 0;
        std::array<float, 128> l {}, r {};
        for (int at = 0; at < 48000 * 8; at += 128) {
            for (int i = 0; i < 128; ++i)
                l[(size_t)i] = r[(size_t)i] = at + i < 2400 ? .1f * std::sin((float)(at + i) * .13f) : 0.f;
            fx.process(l.data(), r.data(), 128);
            if (at >= 48000 * 6)
                for (float sample : l) afterSix += sample * sample;
        }
        check(afterSix > 1.e-6, "valid maximum-feedback delay remains audible after six seconds", afterSix);
        // Two serial feedback chains require the convolution's (n+1) factor.
        const double repeats = std::floor((declared - 12) / 1.5);
        check(declared > 8 && (repeats + 1) * std::pow(.92, repeats) < 1.e-8,
              "declared tail covers serial maximum-feedback decay with headroom", declared);
        p.prepareToPlay(96000, 128);
        check(std::abs(p.getTailLengthSeconds() - declared) < 1.e-9,
              "tail bound is available before prepare and rate independent");
    }

    static void run() { safetyAndLegacy(); alignment(); mono(); tail(); }
};
