// Offline audition through the real SQ-4 processor, including device FX,
// scene launches, faders and master limiter, using the factory session data.
// sq4_host_test --render-preset "TIDAL MEMORY" /absolute/path.wav [solo-track]
#pragma once

static int renderSessionPreset(const juce::String& name, const juce::File& output, int soloTrack = -1,
                               const fable::SessionData* draft = nullptr, int barsPerScene = 0) {
    const auto& presets = fable::factorySessionLibrary();
    const auto found = std::find_if(presets.begin(), presets.end(), [&](const auto& p) {
        return juce::String(p.name) == name;
    });
    if ((!draft && found == presets.end()) || soloTrack < -1 || soloTrack > 3) return 2;
    const auto& session = draft ? *draft : found->session;
    if (session.scenes.empty()) return 2;
    SeqAudioProcessor processor;
    processor.prepareToPlay(48000, 128);
    if (draft) {
        if (!processor.applySessionJson(fable::sessionToJson(session))) return 2;
    } else processor.setCurrentProgram((int)std::distance(presets.begin(), found));
    auto stream = output.createOutputStream();
    if (!stream || !stream->setPosition(0) || stream->truncate().failed()) return 2;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), 48000, 2, 24, {}, 0));
    if (!writer) return 2;

    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer midi;
    // Drain the program change before launching the first scene.
    processor.processBlock(buffer, midi);
    processor.drainAcks();
    if (soloTrack >= 0) processor.conductor().toggleSolo(soloTrack);
    processor.conductor().launchScene(0);
    const double anchor = processor.conductor().anchor();
    const double barFrames = 48000.0 * 240.0 / session.bpm;
    const auto& scenes = session.scenes;
    // Authored performance: give the two main grooves sixteen bars to settle.
    const auto sceneBars = [&](size_t scene) {
        if (barsPerScene > 0) return barsPerScene;
        return scenes[scene].name == "PRESSURE" || scenes[scene].name == "RETURN" ? 16 : 8;
    };
    size_t scene = 0;
    double next = anchor + sceneBars(0) * barFrames;
    double totalBars = 0;
    for (size_t s = 0; s < scenes.size(); ++s) totalBars += sceneBars(s);
    const double stop = anchor + totalBars * barFrames;
    double sum = 0, peak = 0, fullPeak = 0;
    long long samples = 0;
    bool stopped = false;
    // The first processed block advanced the processor clock to frame 128.
    for (double frame = 128; frame < stop + 48000 * 8; frame += 128) {
        // Queue each quantized launch in the block before its exact boundary.
        if (scene + 1 < scenes.size() && frame + 128 >= next) {
            std::printf("%s: RMS %.2f dBFS, peak %.2f dBFS\n", scenes[scene].name.c_str(),
                10 * std::log10(std::max(1e-15, sum / (double)std::max(1LL, samples))),
                20 * std::log10(std::max(1e-15, peak)));
            sum = peak = 0; samples = 0;
            processor.conductor().launchScene((int)++scene);
            next += sceneBars(scene) * barFrames;
        }
        if (!stopped && frame >= stop) {
            processor.conductor().stopTransport();
            stopped = true;
        }
        buffer.clear(); midi.clear();
        processor.processBlock(buffer, midi);
        processor.drainAcks();
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) {
            const double v = buffer.getSample(ch, i);
            if (!std::isfinite(v) || std::abs(v) >= 1) {
                std::fprintf(stderr, "Non-finite or clipped output at frame %.0f\n", frame + i);
                return 1;
            }
            sum += v * v; peak = std::max(peak, std::abs(v));
            fullPeak = std::max(fullPeak, std::abs(v)); ++samples;
        }
        if (!writer->writeFromAudioSampleBuffer(buffer, 0, 128)) return 2;
    }
    if (fullPeak < 1e-5) { std::fprintf(stderr, "Silent render\n"); return 1; }
    std::printf("Rendered %s: %.1f seconds, 48 kHz / 24-bit stereo, peak %.2f dBFS\n",
        name.toRawUTF8(), (stop / 48000) + 8, 20 * std::log10(fullPeak));
    return 0;
}

// Portable draft audition: play each four-bar cell once.
static int renderSessionFile(const juce::File& input, const juce::File& output) {
    fable::SessionData session;
    if (!input.existsAsFile() || !fable::sessionFromJson(input.loadFileAsString(), session)) return 2;
    return renderSessionPreset(session.name, output, -1, &session, 4);
}
