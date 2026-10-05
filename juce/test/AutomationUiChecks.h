#pragma once

static void testAutomationUi() {
    using namespace fable;
    SeqAudioProcessor p; p.prepareToPlay(48000,512);
    if (!p.conductor().session().scenes[0].hasClip[2]) p.conductor().createClip(0,2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* seq=dynamic_cast<SeqEditor*>(editor.get());
    check(seq!=nullptr,"automation UI has a native editor"); if (!seq) return;
    seq->enterFocus(2,0);
    auto& focus=seq->deviceFocus(); auto& panel=focus.automationForTest();
    const auto notes=panel.clip()->bytes;
    panel.addLane(); panel.setTime(AutoTime::Grid); panel.setSteps(7);
    check(panel.expanded() && panel.clip()->automation.size()==1,"add lane opens the native panel");
    auto& canvas=panel.canvasForTest();
    auto event=[&](double t,double v,int mods) {
        const auto pos=juce::Point<float>((float)(t/(panel.clip()->bars*16)*canvas.getWidth()),(float)(6+(1-v)*(canvas.getHeight()-12)));
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),pos,juce::ModifierKeys(mods),1,0,0,0,0,&canvas,&canvas,juce::Time::getCurrentTime(),pos,juce::Time::getCurrentTime(),1,false);
    };
    auto gesture=[&](double t0,double v0,double t1,double v1,int extra=0) {
        canvas.mouseDown(event(t0,v0,juce::ModifierKeys::leftButtonModifier|extra));
        canvas.mouseDrag(event(t1,v1,juce::ModifierKeys::leftButtonModifier|extra));
        canvas.mouseUp(event(t1,v1,extra));
    };
    p.clearHistory(); gesture(.4,.2,3.4,.8);
    auto points=panel.clip()->automation[0].points;
    check(points.size()>=4 && points.front().hold,"DRAW fills crossed cells with held values");
    check(p.undo() && panel.clip()->automation[0].points.empty() && !p.canUndo(),"one drag is one undo entry");
    check(p.redo(),"automation drag can be redone"); panel.refresh();
    panel.setTool(fui::ClipAutomationPanel::Line); gesture(0,.2,5,.8);
    points=panel.clip()->automation[0].points;
    check(std::abs(evalLane(points,7,3)-.5)<.001,"LINE creates a continuous ramp");
    panel.setTool(fui::ClipAutomationPanel::Point);
    gesture(2,.4,3,.6);
    points=panel.clip()->automation[0].points;
    auto moved=std::find_if(points.begin(),points.end(),[](const auto& point){ return point.t==3 && std::abs(point.v-.6)<.001; });
    check(moved!=points.end(),"POINT adds and moves a breakpoint");
    gesture(3,.6,4,.7,juce::ModifierKeys::shiftModifier);
    points=panel.clip()->automation[0].points;
    check(std::any_of(points.begin(),points.end(),[](const auto& point){return point.t==3 && std::abs(point.v-.7)<.001;}),"Shift drag locks point time");
    gesture(3.4,.5,3.4,.9,juce::ModifierKeys::altModifier);
    points=panel.clip()->automation[0].points;
    check(std::any_of(points.begin(),points.end(),[](const auto& point){return point.t==3 && std::abs(point.c)>.1 && !point.hold;}),"Alt drag bends a segment");
    canvas.mouseDown(event(3,.7,juce::ModifierKeys::rightButtonModifier));
    check(panel.clip()->automation[0].points.size()+1==points.size(),"right click removes a point");
    panel.setSnap(false); gesture(2.3,.5,2.3,.5);
    check(std::any_of(panel.clip()->automation[0].points.begin(),panel.clip()->automation[0].points.end(),[](const auto& point){return point.t==2.25;}),"FREE uses quarter-step positions");
    const auto beforeCancelJson=juce::JSON::toString(automationToVar(panel.clip()->automation));
    panel.setTool(fui::ClipAutomationPanel::Draw); canvas.mouseDown(event(1,.9,juce::ModifierKeys::leftButtonModifier));
    check(canvas.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Escape cancels an automation gesture");
    check(juce::JSON::toString(automationToVar(panel.clip()->automation))==beforeCancelJson,"cancel restores the pre-drag curve");
    panel.setTool(fui::ClipAutomationPanel::Point); canvas.mouseDown(event(4.5,.5,juce::ModifierKeys::leftButtonModifier));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    check(juce::JSON::toString(automationToVar(panel.clip()->automation))==beforeCancelJson,"cancel also removes a newly added point");
    panel.setLearn(true);
    auto* knob=focus.wt2ModelForTest().parameters().parameter("fx.delay.mix");
    check(knob!=nullptr,"LEARN has the focused device parameter source");
    if (knob) knob->setValueNotifyingHost(knob->getValue()>.5f ? .2f : .8f);
    panel.refresh();
    check(!panel.learning() && panel.clip()->automation[0].target=="fx.delay.mix","LEARN assigns the moved knob");
    panel.addLane(); panel.setLaneTarget("fx.delay.mix");
    check(panel.clip()->automation[1].target!="fx.delay.mix","target selection prevents duplicate lanes");
    panel.setTime(AutoTime::Fit); panel.setSteps(3); panel.setCycle(8);
    check(panel.clip()->automation[1].time.mode==AutoTime::Fit && panel.clip()->automation[1].time.steps==3 && panel.clip()->automation[1].time.cycleBeats==8,"FIT timing controls update the clip");
    panel.setSteps(100); check(panel.clip()->automation[1].time.steps==64,"step count is bounded"); panel.setSteps(3);
    check(panel.clip()->bytes==notes,"automation UI preserves the note clip");
    SeqAudioProcessor restored; restored.prepareToPlay(48000,512);
    check(restored.applySessionJson(p.currentSessionJson()),"UI automation survives session persistence");
    check(juce::JSON::toString(automationToVar(restored.conductor().session().scenes[0].clips[2].automation))==juce::JSON::toString(automationToVar(panel.clip()->automation)),"persisted UI lanes retain points and timing");
    const auto output=juce::File::getCurrentWorkingDirectory().getChildFile("build/auditions"); output.createDirectory();
    auto save=[&](juce::Component& component,const char* name) {
        juce::Image image(juce::Image::ARGB,component.getWidth(),component.getHeight(),true);
        { juce::Graphics g(image); component.paintEntireComponent(g,true); }
        const auto file=output.getChildFile(name); file.deleteFile();
        auto stream=file.createOutputStream();
        check(stream && juce::PNGImageFormat().writeImageToStream(image,*stream),"automation UI screenshot rendered");
    };
    panel.selectLane(0); save(panel,"native-automation-wt.png");
    focus.deviceViewportForTest().setViewPosition(0,100000); save(*editor,"native-automation-focus.png");
    check(focus.deviceViewportForTest().getViewPositionY()+focus.deviceViewportForTest().getHeight()>=panel.getBottom(),"expanded automation is visible or accessible by scrolling");
    for (const char* page : {"SEQUENCER","ARP","EDIT"}) {
        if (auto* button=findFxComponent<juce::TextButton>(focus.wt2BodyForTest(),page)) {
            button->onClick(); focus.resized();
            save(*editor,(juce::String("native-automation-focus-")+page+".png").toRawUTF8());
        }
    }
    panel.setExpanded(false);
    check(panel.getParentComponent()==&focus && panel.getBottom()<=focus.getHeight(),"collapsed automation strip stays visible");
    seq->enterFocus(1,2); auto& bass=focus.automationForTest(); bass.addLane(); bass.setTime(AutoTime::Fit); bass.setSteps(3); bass.setCycle(8); save(bass,"native-automation-bass.png");
    seq->enterFocus(0,2); focus.drumModelForTest().selectPad(5); auto& drums=focus.automationForTest(); drums.refresh(); drums.addLane(); drums.setTime(AutoTime::Pad);
    check(drums.clip()->automation[0].target=="pad5.flt.cut" && drums.clip()->automation[0].time.mode==AutoTime::Pad,"DR-1 lane targets the selected pad and supports PAD timing");
    drums.setLaneTarget("fx.reverb.mix"); check(drums.clip()->automation[0].time.mode==AutoTime::Clip,"group target resets incompatible PAD mode");
    drums.setLaneTarget("pad5.flt.cut"); drums.setTime(AutoTime::Fit); drums.setSteps(3); save(drums,"native-automation-drums.png");
    seq->enterFocus(2,0);
    const auto laneCount=focus.automationForTest().clip()->automation.size();
    check(focus.canShowAutomation("fx.delay.mix") && !focus.canShowAutomation("seq.bpm"),"native control menu enables valid automation targets");
    focus.showAutomation("fx.delay.mix");
    check(focus.automationForTest().expanded() && focus.automationForTest().selectedLane()==0 &&
        focus.automationForTest().clip()->automation.size()==laneCount,"show in automation reuses and reveals the existing lane");
    focus.showAutomation("oscA.level");
    check(focus.automationForTest().clip()->automation.size()==laneCount+1 &&
        focus.automationForTest().clip()->automation.back().target=="oscA.level","show in automation creates a missing lane");

    // The hosted sources must expose processor telemetry for every machine.
    juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
    const char* targets[]{"pad5.flt.cut","flt.cut","filter.cutoff"};
    for (int track=0;track<3;++track) {
        seq->enterFocus(track,0);
        if (!p.conductor().session().scenes[0].hasClip[(size_t)track]) p.conductor().createClip(0,track);
        p.conductor().updateClipAutomation(0,track,{{targets[track],true,{AutoTime::Grid,4,4},{{0,.3,0,true},{2,.7,0,true}}}});
    }
    p.processBlock(audio,midi); p.drainAcks(); p.conductor().launchScene(0);
    p.processBlock(audio,midi); p.drainAcks();
    fui::ParameterSource sources[]{focus.drumModelForTest().parameters(),focus.bassModelForTest().parameters(),focus.wt2ModelForTest().parameters()};
    for (int track=0;track<3;++track) {
        fui::Knob knob(sources[track],targets[track],fui::Knob::Md,fui::Accent::N);
        check(std::isfinite(sources[track].liveAutomation(targets[track])) && std::abs(knob.automationNorm()-.3f)<.001,
            "hosted native controls show actual automation for every machine");
    }

}
