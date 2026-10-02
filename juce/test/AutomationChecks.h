#pragma once
#include "../source/dsp/ClipAutomation.h"
#include "../source/seq/ui/AutomationEdit.h"
#if FABLE_SQ4_TEST_JUCE
#include "../source/seq/AutomationCodec.h"
#include "../source/seq/DrumRhythmCodec.h"
#endif

static void testAutomation() {
    using namespace fable;
    const std::vector<AutoLane> lanes {{ "filter.cutoff", true, { AutoTime::Grid, 4, 4 }, {{0, .2, 0, true}, {2, .8, 0, true}} }};
    CHECK(validateAutomation(lanes, Machine::WT1));
    auto bad = lanes; bad[0].target = "filter.type"; CHECK(!validateAutomation(bad, Machine::WT1));
    bad = lanes; bad.resize(9, lanes[0]); CHECK(!validateAutomation(bad, Machine::WT1));
    bad = lanes; bad[0].time.mode = AutoTime::Pad; CHECK(!validateAutomation(bad, Machine::WT1));
    bad[0].target = "fx.delay.mix"; CHECK(!validateAutomation(bad, Machine::DR1));
    bad = lanes; bad[0].points[0].c = NAN; CHECK(!validateAutomation(bad, Machine::WT1));
    auto bank = std::make_unique<AutoBank>(); compileAutomation(*bank, lanes, 2, nullptr, Machine::WT1);
    auto engine = std::make_unique<Engine>(); engine->prepare(48000); engine->setHostClipMode(true, 512);
    engine->hostTempo(120, .35, 256);
    const auto bytes = sqEmptyClip(Machine::WT1, 2);
    const int id = idFromString("filter.cutoff");
    engine->setParam(id, 1234);
    const auto capacity = engine->automationCapacity();
    float L[512]{}, R[512]{};
    engine->hostClip(bytes.data(), (int)bytes.size(), 2, 1024, 0, {}, bank.get());
    engine->hostSetFrame(0); engine->render(L, R, 512);
    CHECK(engine->params()[(size_t)id] == 1234); // pending bank survives a silent block
    engine->hostSetFrame(1024); engine->render(L, R, 128);
    CHECK(engine->params()[(size_t)id] == bank->lanes[0].table[0]);
    engine->setParam(id, 4321);
    CHECK(engine->params()[(size_t)id] == bank->lanes[0].table[0]);
    engine->hostClipStop(0); engine->render(L, R, 128);
    CHECK(engine->params()[(size_t)id] == 4321);
    engine->hostClip(bytes.data(), (int)bytes.size(), 2, 0, 0, {}, bank.get()); engine->render(L, R, 128);
    auto patch = defaultParams(); patch[(size_t)id] = 2468; engine->setParams(patch);
    engine->hostClipUpdate(bytes.data(), (int)bytes.size(), 2); engine->render(L, R, 128);
    CHECK(engine->params()[(size_t)id] == 2468); // patch updates held values, lane removal restores
    CHECK(engine->automationCapacity() == capacity);
    // Pending updates must never replace the outgoing playing bank.
    engine->hostClip(bytes.data(), (int)bytes.size(), 2, 0, 0, {}, bank.get()); engine->render(L, R, 128);
    engine->hostClip(bytes.data(), (int)bytes.size(), 2, 48000);
    engine->hostClipUpdate(bytes.data(), (int)bytes.size(), 2, {}, bank.get());
    CHECK(engine->params()[(size_t)id] != 2468);
    engine->hostClipStop(0); engine->render(L, R, 128);
    CHECK(engine->params()[(size_t)id] == 2468);
    CHECK(engine->automationCapacity() == capacity);
    engine->hostClip(bytes.data(), (int)bytes.size(), 2, 0, 0, {}, bank.get()); engine->render(L,R,128);
    engine->prepare(48000); CHECK(engine->params()[(size_t)id] == 2468);
    CHECK(!engine->hasClipAutomation() && engine->automationCapacity() == capacity);

#if FABLE_SQ4_TEST_JUCE
    // Every time mode survives session and clipboard round trips.
    auto session = factorySession(); auto& clip = session.scenes[0].clips[0];
    clip.hasAutomation = true; clip.automation = {
        {"pad5.aenv.dec", true, {AutoTime::Pad}, {{0,.2,.3,false},{1,.8,0,true}}},
        {"pad0.lvl", true, {AutoTime::Clip}, {{0,.4}}},
        {"pad1.lvl", false, {AutoTime::Grid,7}, {{0,.5}}},
        {"fx.reverb.mix", true, {AutoTime::Fit,3,8}, {{0,.3},{1,.6}}}
    };
    SessionData decoded; CHECK(sessionFromJson(sessionToJson(session), decoded));
    CHECK(juce::JSON::toString(automationToVar(decoded.scenes[0].clips[0].automation)) == juce::JSON::toString(automationToVar(clip.automation)));
    ClipClipboardData cb; cb.machines = {Machine::DR1}; cb.cells = {{clip}}; cb.hasCell = {{true}};
    ClipClipboardData cbBack; CHECK(clipClipboardFromJson(clipClipboardToJson(cb), cbBack));
    CHECK(cbBack.cells[0][0].hasAutomation && cbBack.cells[0][0].automation.size() == 4);
    auto invalid = juce::JSON::parse(sessionToJson(session));
    auto& jsonClip = invalid["scenes"].getArray()->getReference(0)["clips"].getArray()->getReference(0);
    jsonClip["automation"].getArray()->getReference(0).getDynamicObject()->setProperty("target", "pad5.out");
    CHECK(!sessionFromJson(juce::JSON::toString(invalid), decoded));

    const auto fixture = juce::File(__FILE__).getSiblingFile("fixtures/web-automation-tables.json");
    const auto goldens = juce::JSON::parse(fixture.loadFileAsString());
    CHECK(goldens.isArray());
    if (!goldens.isArray()) return;
    for (const auto& c : *goldens.getArray()) {
        if (const auto* edits=c["edits"].getArray()) for (const auto& edit : *edits) {
            auto readPoints=[](const juce::var& data) {
                std::vector<AutoPoint> points;
                if (const auto* list=data.getArray()) for (const auto& p : *list)
                    points.push_back({(double)p["t"],(double)p["v"],(double)p["c"],(bool)p["hold"]});
                return points;
            };
            const auto points=readPoints(edit["points"]), expected=readPoints(edit["result"]);
            const auto op=edit["op"].toString(); const int len=(int)edit["len"];
            auto arg=[&](int n){ return (double)edit["args"].getArray()->getReference(n); };
            std::vector<AutoPoint> actual;
            if (op=="paint") actual=autoedit::paintCell(points,len,arg(0),arg(1),arg(2));
            else if (op=="line") actual=autoedit::drawLine(points,len,arg(0),arg(1),arg(2),arg(3));
            else if (op=="add") actual=autoedit::addPoint(points,arg(0),arg(1)).first;
            else if (op=="move") actual=autoedit::movePoint(points,len,(int)arg(0),arg(1),arg(2));
            else if (op=="bend") actual=autoedit::bendSegment(points,(int)arg(0),arg(1));
            else if (op=="remove") actual=autoedit::removePoint(points,(int)arg(0));
            else if (op=="tidy") actual=autoedit::tidy(points);
            bool equal=actual.size()==expected.size();
            for (size_t i=0; equal && i<actual.size();++i) equal=std::abs(actual[i].t-expected[i].t)<1e-12 && std::abs(actual[i].v-expected[i].v)<1e-12 && std::abs(actual[i].c-expected[i].c)<1e-12 && actual[i].hold==expected[i].hold;
            CHECK(equal);
        }
        const auto name = c["machine"].toString();
        const Machine machine = name == "WT1" ? Machine::WT1 : name == "BL1" ? Machine::BL1 : Machine::DR1;
        ClipData data; data.bars = (int)c["bars"]; data.bytes = sqEmptyClip(machine, data.bars);
        CHECK(automationFromVar(c["automation"], data.automation, machine));
        if (c.hasProperty("drumRhythm")) CHECK(drumRhythmFromVar(c["drumRhythm"], data, machine));
        compileAutomation(*bank, data.automation, data.bars, data.hasDrumRhythm ? &data.drumRhythm : nullptr, machine);
        const auto* tables = c["tables"].getArray(); CHECK(tables && tables->size() == bank->count);
        for (int i = 0; tables && i < tables->size() && i < bank->count; ++i) {
            const auto& expected = tables->getReference(i); const auto& actual = bank->lanes[(size_t)i];
            CHECK(actual.len == (int)expected["len"] && actual.fit == (int)expected["fit"] && actual.rot == (int)expected["rot"]);
            const auto* samples = expected["table"].getArray(); CHECK(samples && samples->size() == actual.len * AUTO_RES);
            bool equal = true;
            for (int j = 0; samples && j < samples->size(); ++j) equal &= std::abs(actual.table[(size_t)j] - (double)samples->getReference(j)) < 1e-5;
            CHECK(equal);
        }
        struct TraceContext { const juce::Array<juce::var>* trace; AutoBank* bank; size_t samples = 0; bool equal = true; } context {c["trace"].getArray(), bank.get()};
        auto observe = [](void* ptr, double frame, int id, float value) {
            auto& ctx = *static_cast<TraceContext*>(ptr);
            if ((int64_t)frame % 128 != 0) return;
            const size_t chunk = ctx.samples / (size_t)ctx.bank->count, lane = ctx.samples % (size_t)ctx.bank->count;
            ++ctx.samples;
            if (!ctx.trace || chunk >= (size_t)ctx.trace->size()) { ctx.equal = false; return; }
            const auto& expected = ctx.trace->getReference((int)chunk);
            const double want = (double)expected["values"].getArray()->getReference((int)lane);
            const bool equal = frame == (double)expected["frame"] && id == ctx.bank->lanes[lane].paramIndex && std::abs(want - value) < 1e-4;
            if (!equal && ctx.equal) std::printf("First trace mismatch chunk=%zu frame=%.0f expectedFrame=%.0f id=%d value=%.9g expected=%.9g delta=%.9g\n", chunk, frame, (double)expected["frame"], id, value, want, value-want);
            ctx.equal &= equal;
        };
        auto wt = std::make_unique<Engine>(); auto bass = std::make_unique<BassEngine>(); auto drum = std::make_unique<DrumEngine>();
        wt->prepare(48000); bass->prepare(48000); drum->prepare(48000);
        wt->setHostClipMode(true,512); bass->setHostClipMode(true,512); drum->setHostClipMode(true,512);
        wt->hostTempo(120,.35,256); bass->hostTempo(120,.35,256); drum->hostTempo(120,.35,256);
        if (machine == Machine::WT1) { wt->setAutomationTrace(observe,&context); wt->hostClip(data.bytes.data(),(int)data.bytes.size(),data.bars,256,0,{},bank.get()); }
        if (machine == Machine::BL1) { bass->setAutomationTrace(observe,&context); bass->hostClip(data.bytes.data(),(int)data.bytes.size(),data.bars,256,0,{},bank.get()); }
        if (machine == Machine::DR1) { drum->setAutomationTrace(observe,&context); drum->hostClip(data.bytes.data(),(int)data.bytes.size(),data.bars,256,0,&data.drumRhythm,bank.get()); }
        float audio[DR_NBUSES][2][512]{}; float* outs[DR_NBUSES][2];
        for (int b = 0; b < DR_NBUSES; ++b) for (int ch = 0; ch < 2; ++ch) outs[b][ch] = audio[b][ch];
        for (int frame = 0; frame < 384512; frame += 512) {
            if (machine == Machine::WT1) { wt->hostSetFrame(frame); wt->render(L,R,512); }
            if (machine == Machine::BL1) { bass->hostSetFrame(frame); bass->render(L,R,512); }
            if (machine == Machine::DR1) { drum->hostSetFrame(frame); drum->render(outs,512); }
        }
        if (!context.equal) std::printf("Automation trace mismatch: %s\n", name.toRawUTF8());
        CHECK(context.equal);
        CHECK(context.trace && context.samples == (size_t)context.trace->size() * (size_t)bank->count);
        CHECK(wt->automationCapacity() == capacity && bass->automationCapacity() == capacity && drum->automationCapacity() == capacity);
        // Each machine routes knob and full-patch writes through held values.
        auto storedWrites = [&](auto& device, auto patch, auto launch, auto render) {
            device.setAutomationTrace(nullptr,nullptr);
            const int id = bank->lanes[0].paramIndex;
            patch[(size_t)id] *= .8f;
            const float stored = patch[(size_t)id];
            device.setParam(id,stored); device.hostClipStop(0); render();
            CHECK(device.params()[(size_t)id] == stored);
            launch(); render(); patch[(size_t)id] *= .9f; device.setParams(patch);
            device.hostClipStop(0); render();
            CHECK(device.params()[(size_t)id] == patch[(size_t)id]);
            CHECK(device.automationCapacity() == capacity);
        };
        if (machine == Machine::WT1) storedWrites(*wt,defaultParams(),[&]{ wt->hostClip(data.bytes.data(),(int)data.bytes.size(),data.bars,0,0,{},bank.get()); },[&]{ wt->render(L,R,512); });
        if (machine == Machine::BL1) storedWrites(*bass,defaultBassParams(),[&]{ bass->hostClip(data.bytes.data(),(int)data.bytes.size(),data.bars,0,0,{},bank.get()); },[&]{ bass->render(L,R,512); });
        if (machine == Machine::DR1) storedWrites(*drum,defaultDrumParams(),[&]{ drum->hostClip(data.bytes.data(),(int)data.bytes.size(),data.bars,0,0,&data.drumRhythm,bank.get()); },[&]{ drum->render(outs,512); });
    }
#endif
}
