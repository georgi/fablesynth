#pragma once
#include "../source/seq/AutomationCodec.h"

static void testAutomationHost() {
    using namespace fable;
    // Library imports/exports and loads keep lanes, including disabled ones.
    auto entry = factoryClipLibrary().front();
    const std::string target = entry.machine == Machine::DR1 ? "pad5.aenv.dec" : entry.machine == Machine::BL1 ? "flt.cut" : "filter.cutoff";
    entry.automation = {{target, true, {AutoTime::Grid,7}, {{0,.2},{3,.6,0,true}}}};
    std::vector<ClipLibraryEntry> back; juce::String error;
    check(ClipLibraryStorage::decodeSqclip(ClipLibraryStorage::encodeSqclip({entry}),back,error), "clip library automation round trip");
    check(back.size() == 1 && back[0].automation.size() == 1, "clip library keeps automation");
    SeqAudioProcessor loader; loader.prepareToPlay(48000,512);
    const int t = entry.machine == Machine::DR1 ? 0 : entry.machine == Machine::BL1 ? 1 : 2;
    check(loader.loadClipLibraryEntry(0,t,entry,0), "load library clip with automation");
    check(loader.conductor().session().scenes[0].clips[(size_t)t].hasAutomation, "library automation reaches session");

    juce::AudioBuffer<float> loaderBlock(2,512); juce::MidiBuffer loaderMidi;
    loader.processBlock(loaderBlock,loaderMidi); loader.drainAcks(); loader.conductor().launchScene(0);
    loader.processBlock(loaderBlock,loaderMidi); loader.drainAcks();
    const auto originalBytes = loader.conductor().session().scenes[0].clips[(size_t)t].bytes;
    auto replacement = entry.automation; replacement[0].points = {{0,.55,0,true}};
    check(loader.conductor().updateClipAutomation(0,t,replacement), "automation-only edit updates the playing clip");
    loader.processBlock(loaderBlock,loaderMidi);
    const auto* def = autoParamDef(entry.machine,target);
    check(std::abs(loader.debugTrackParams(t)[(size_t)def->id] - normToValue(*def,.55f)) < .01, "automation-only edit reaches the engine");
    check(std::abs(loader.liveAutomation(t,juce::String(target))-normToValue(*def,.55f))<.01,
        "native control telemetry reports the applied automation value");
    check(!std::isfinite(loader.liveAutomation(t,"missing")),"unknown control automation telemetry is idle");
    check(loader.conductor().session().scenes[0].clips[(size_t)t].bytes == originalBytes, "automation edit preserves notes");
    auto updatedPatch = loader.conductor().session().tracks[(size_t)t].patch;
    updatedPatch.factory = false; updatedPatch.params[target] = .12f;
    loader.conductor().setTrackPatch(t,updatedPatch); loader.applyTrackPatch(t);
    loader.processBlock(loaderBlock,loaderMidi);
    check(std::abs(loader.debugTrackParams(t)[(size_t)def->id] - normToValue(*def,.55f)) < .01, "patch load keeps automation live");
    loader.conductor().stopTransport(); loader.processBlock(loaderBlock,loaderMidi);
    check(std::abs(loader.debugTrackParams(t)[(size_t)def->id] - .12f) < 1e-6, "stop restores the loaded patch value");

    auto session = factorySession(); session.bpm = 120; session.swing = 0; session.quant = Quant::Off;
    session.scenes.resize(1);
    auto& scene = session.scenes[0]; scene.hasClip = {false,false,true,false}; scene.pass.clear();
    auto& clip = scene.clips[2]; clip = {"FX TRACE",2,sqEmptyClip(Machine::WT1,2)};
    for (int step = 0; step < 32; step += 8) {
        const int offset = sqWtNoteIdx(step/16,step%16,0);
        clip.bytes[(size_t)offset] = 5; clip.bytes[(size_t)offset+1] = 0; clip.bytes[(size_t)offset+2] = 1;
    }
    auto& patch = session.tracks[2].patch; patch.factory = false; patch.params.clear();
    const auto defaults = defaultParams();
    for (const auto& info : paramInfo()) patch.params[info.pid] = defaults[(size_t)info.id];
    patch.params["env1.att"] = .001f; patch.params["env1.dec"] = .04f; patch.params["env1.sus"] = 0; patch.params["env1.rel"] = .01f;
    patch.params["fx.delay.on"] = 1; patch.params["fx.delay.time"] = .125f; patch.params["fx.delay.fb"] = .8f; patch.params["fx.delay.mix"] = 0;
    patch.params["fx.reverb.on"] = 0; patch.params["fx.chorus.on"] = 0;
    clip.hasAutomation = true; clip.automation = {{"fx.delay.mix",true,{AutoTime::Grid,8},{{0,0,0,true},{4,1,0,true}}}};
    const auto fixture = juce::File(__FILE__).getSiblingFile("fixtures/web-automation-tables.json");
    const auto goldens = juce::JSON::parse(fixture.loadFileAsString());
    auto portable = session;
    // A single portable audition exercises all machines and DR-1 group FX.
    portable.scenes[0].hasClip = {true,true,true,false};
    portable.scenes[0].clips[1] = {"BASS LOCKS",2,sqEmptyClip(Machine::BL1,2)};
    portable.scenes[0].clips[1].bytes[0] = 1; portable.scenes[0].clips[1].bytes[2] = 1;
    for (const auto& c : *goldens.getArray()) {
        const auto machine = c["machine"].toString(); const int track = machine == "DR1" ? 0 : machine == "BL1" ? 1 : 2;
        auto& cell = portable.scenes[0].clips[(size_t)track]; cell.hasAutomation = true;
        check(automationFromVar(c["automation"],cell.automation,portable.tracks[(size_t)track].machine), "portable automation lane fixture");
        if (c.hasProperty("drumRhythm")) check(drumRhythmFromVar(c["drumRhythm"],cell,Machine::DR1), "portable automation drum rhythm");
    }
    check(validateSession(portable).empty(), "portable automation session is valid");
    SessionData roundTrip; check(sessionFromJson(sessionToJson(portable), roundTrip), "portable automation session round trip");
    auto auditions = juce::File::getCurrentWorkingDirectory().getChildFile("build/auditions"); auditions.createDirectory();
    check(auditions.getChildFile("native-clip-automation.json").replaceWithText(sessionToJson(portable)), "portable automation audition saved");

    auto render = [&](bool automated) {
        auto doc = session;
        if (!automated) { doc.scenes[0].clips[2].automation.clear(); doc.scenes[0].clips[2].hasAutomation = false; }
        SeqAudioProcessor p; p.prepareToPlay(48000,512);
        check(p.applySessionJson(sessionToJson(doc)), "FX automation session loads");
        juce::AudioBuffer<float> block(2,512); juce::MidiBuffer midi;
        p.processBlock(block,midi); p.drainAcks(); p.conductor().launchScene(0);
        const double anchor = p.conductor().anchor();
        std::array<double,2> wetEnergy{};
        bool valuesMatch = true;
        for (int frame = 512; frame < anchor + 16 * 6000; frame += 512) {
            p.processBlock(block,midi); p.drainAcks();
            const auto params = p.debugTrackParams(2);
            const double chunkFrame = frame + 384; // final quantum in this host block
            const double step = std::max(0.0,chunkFrame-anchor)/6000;
            const float expected = std::fmod(step,8.0) >= 4 ? 1 : 0;
            // Skip the single table sample that interpolates across a hold edge.
            const double phase = std::fmod(step,4.0);
            if (phase < 3.9) valuesMatch &= std::abs(params[(size_t)idFromString("fx.delay.mix")] - (automated ? expected : 0)) < 1e-5;
            for (int i = 0; i < 512; ++i) {
                const double s = (frame+i-anchor)/6000; const int cycle = (int)(s/8);
                if (cycle >= 0 && cycle < 2 && std::fmod(s,8.0) >= 5 && std::fmod(s,8.0) < 7)
                    wetEnergy[(size_t)cycle] += block.getSample(0,i)*block.getSample(0,i)+block.getSample(1,i)*block.getSample(1,i);
            }
        }
        check(valuesMatch,"FX automation reaches processor params inside 512-sample blocks");
        return wetEnergy;
    };
    const auto dry = render(false), wet = render(true);
    check(wet[0] > 1e-8 && wet[0] > dry[0]*2 && wet[1] > dry[1]*2, "delay wet energy follows lane over two cycles");
}
