#pragma once
#include "AutoMixUiChecks.h"
#include "LiveSpectrumChecks.h"
#include "../source/seq/SeqProcessor.h"
#include "../source/seq/ui/MasterFxView.h"
#include "../source/seq/ui/HostedDrumModel.h"
#include "../source/seq/ui/HostedBassModel.h"
#include "../source/seq/ui/HostedWtModel.h"
#include "../source/drum/DrumDeviceBody.h"
#include "../source/ui/LookAndFeel.h"

// SQ-4: ASSIST (LISTEN -> plan -> APPLY -> REVERT), master EQ/COMP AUTO and
// side-chain, and the hosted devices' auto-mix sources (DR-1 KIT MIX in the
// hosted body, BL-1 auto-EQ, WT-1 side-chain names). Offline renders only.
inline int runSeqAutoMixChecks() {
    automixchecks::Checker check{"sq4"};
    std::printf("\n== SQ-4 auto-mix UI ==\n");
    SeqAudioProcessor p;
    p.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    auto render = [&] {
        buffer.clear();
        midi.clear();
        p.processBlock(buffer, midi);
        p.drainAcks();
    };
    p.conductor().launchScene(2);   // DROP A: all four tracks
    for (int i = 0; i < 400; ++i) render();
    check(p.conductor().playing(), "the session plays for the assistant");

    fui::DarkLNF lnf;   // the SQ-4 editor's look and feel
    fui::MasterFxView view(p);
    view.setLookAndFeel(&lnf);
    view.setSize(1254, 556);
    view.setVisible(true);
    auto& assist = view.assistPanel();
    check(assist.isVisible() && assist.getWidth() > 600 && assist.getHeight() > 180, "MASTER FX shows the ASSIST panel");
    auto& s = assist.session();
    assist.refresh();
    automixchecks::writeSnapshot(view, "sq4-assist-ready.png");

    // ---- ASSIST: LISTEN 12 s over the pre-FX sum, output and track taps ----
    const auto before = s.current();
    s.listen();
    for (int i = 0; i < 3000 && s.listening(); ++i) {
        render();
        if (i % 4 == 3) s.poll();
    }
    s.stop(true);
    const auto* analysis = s.analysis();
    const auto* plan = s.plan();
    check(analysis && analysis->seconds >= 11.9, "LISTEN analyses 12 s of the running session");
    if (analysis) {
        int heard = 0;
        for (const auto& t : analysis->tracks) heard += t ? 1 : 0;
        check(std::isfinite(analysis->lufsIn) && std::isfinite(analysis->lufsOut) && heard == 4,
              "ASSIST measures IN/OUT LUFS and hears all four tracks: IN " + fui::toFixed(analysis->lufsIn, 1)
                  + " OUT " + fui::toFixed(analysis->lufsOut, 1) + ", tracks " + juce::String(heard));
    }
    check(plan && !plan->moves.empty() && plan->levels.size() == 4, "ASSIST proposes moves and a four-track LEVELS table");
    // Role override and loudness target re-plan without listening again.
    if (plan) {
        s.setLoudness(-9);
        const auto clubMoves = s.plan()->moves.size();
        s.setRole(2, fable::automix::MixRole::Pad);
        check(s.plan() && s.plan()->levels[2].role == fable::automix::MixRole::Pad && clubMoves > 0,
              "a role override and the CLUB target re-plan");
        s.setRole(2, std::nullopt);
        s.setLoudness(-11);
    }
    assist.updateLive(juce::Time::getMillisecondCounterHiRes());
    assist.refresh();
    automixchecks::writeSnapshot(view, "sq4-assist-plan.png");
    if ((plan = s.plan())) {
        s.apply();
        bool written = true;
        for (const auto& [id, v] : plan->masterFx) {
            auto* param = p.apvts.getParameter(id);
            written &= param && std::abs(param->convertFrom0to1(param->getValue()) - v) < 0.02f * std::max(1.0f, std::abs(v));
        }
        for (size_t t = 0; t < plan->trackVol.size(); ++t)
            if (plan->trackVol[t]) {
                auto* vol = p.apvts.getParameter("vol" + juce::String((int)t));
                written &= std::abs(vol->convertFrom0to1(vol->getValue()) - *plan->trackVol[t]) < 1e-3f;
            }
        if (plan->masterVol) {
            auto* master = p.apvts.getParameter("master");
            written &= std::abs(master->convertFrom0to1(master->getValue()) - *plan->masterVol) < 1e-3f;
        }
        check(written && s.applied() && s.caption() == "APPLIED", "APPLY sets the master FX, track faders and master fader");
        // The applied state reaches the session's master section.
        const auto json = p.exportSessionJson();
        check(json.contains("masterFx") && json.contains("masterVol"), "the applied master section exports with the session");
        assist.refresh();
        automixchecks::writeSnapshot(view, "sq4-assist-applied.png");
        s.revert();
        const auto after = s.current();
        bool restored = !s.applied() && std::abs(after.masterVol - before.masterVol) < 1e-4;
        for (const auto& [id, v] : before.masterFx) restored &= std::abs(after.masterFx.at(id) - v) < 1e-4f * std::max(1.0f, std::abs(v));
        for (size_t t = 0; t < before.trackVol.size(); ++t) restored &= std::abs(after.trackVol[t] - before.trackVol[t]) < 1e-4;
        check(restored, "REVERT restores the snapshot taken at APPLY");
    }

    // ---- master EQ / COMP AUTO and side-chain ----
    fui::MasterFxUiModel model(p);
    auto params = model.parameters();
    auto chainRender = render;
    {
        auto& eq = view.eqModule();
        eq.setAutoOpen(true);
        auto& el = eq.autoEqStrip()->learner();
        check(el.available(), "master EQ learns from the pre-FX sum");
        automixchecks::learn(el, chainRender);
        const auto* fit = el.fit();
        check(fit != nullptr, "master EQ LEARN fits the session: " + el.status());
        if (fit) {
            const float lowBefore = automixchecks::paramValue(params, "fx.eq.low");
            el.apply();
            check(std::abs(automixchecks::paramValue(params, "fx.eq.low") - fit->bands[0].gain) < 0.05
                      && automixchecks::paramValue(params, "fx.eq.on") > 0.5f, "master EQ APPLY writes master.fx.eq.*");
            eq.autoEqStrip()->refresh();
            automixchecks::writeSnapshot(view, "sq4-master-eq-auto.png");
            el.undo();
            check(std::abs(automixchecks::paramValue(params, "fx.eq.low") - lowBefore) < 1e-3f, "master EQ UNDO restores");
        }
        eq.setAutoOpen(false);
        auto& comp = view.compModule();
        comp.setAutoOpen(true);
        auto& cl = comp.autoCompStrip()->learner();
        const float thrBefore = automixchecks::paramValue(params, "fx.comp.thr");
        automixchecks::learn(cl, chainRender);
        check(cl.fit() && cl.fit()->exact, "master COMP LEARN fits the master comp input: " + cl.status());
        check(cl.fit() && cl.fit()->bpm && std::abs(*cl.fit()->bpm - p.conductor().session().bpm) < 1e-6,
              "master COMP release uses the session tempo");
        if (cl.fit()) {
            cl.apply();
            check(std::abs(automixchecks::paramValue(params, "fx.comp.thr") - cl.fit()->threshold) < 0.05, "master COMP APPLY writes the threshold");
            comp.autoCompStrip()->refresh();
            automixchecks::writeSnapshot(view, "sq4-master-comp-auto.png");
            cl.undo();
            check(std::abs(automixchecks::paramValue(params, "fx.comp.thr") - thrBefore) < 1e-3f, "master COMP UNDO restores");
        }
        comp.setAutoOpen(false);
        auto* row = comp.sideChainRow();
        check(row != nullptr, "master COMP has the SIDECHAIN row");
        if (row) {
            const auto name = juce::String(p.conductor().session().tracks[1].name);
            check(row->menu().getItemText(3) == "2 " + name, "track keys show SQ-4 track names: " + row->menu().getItemText(3));
            check(row->menu().isItemEnabled(4), "hosted track keys are enabled");
            row->menu().setSelectedId(4, juce::sendNotificationSync);   // TRACK 2
            auto* sc = p.apvts.getParameter("master.fx.comp.sc");
            check(sc && std::lround(sc->convertFrom0to1(sc->getValue())) == 3, "SIDECHAIN writes master.fx.comp.sc");
            automixchecks::writeSnapshot(view, "sq4-master-sidechain.png");
            fui::writeParam(params, "fx.comp.sc", 0);
            row->poll();
        }
    }

    // ---- hosted devices ----
    {
        fui::HostedWtModel wt(p, 2);
        const auto info = wt.sideChain();
        check(info.hosted && info.label(2) == "1 " + juce::String(p.conductor().session().tracks[0].name),
              "hosted WT-1 side-chain lists SQ-4 track names");
        check(wt.autoEqSource(-1).tap == &p.trackTap(2) && wt.autoCompSource(-1).trace == &p.compTrace(2),
              "hosted WT-1 learns from its track tap and comp trace");
        fui::HostedBassModel bass(p);
        fui::AutoEqLearner el(bass.autoEqSource(-1), bass.parameters(), {}, bass.analysisSampleRate());
        automixchecks::learn(el, render);
        check(el.fit() != nullptr && el.reference() == "bass", "hosted BL-1 EQ learns its track: " + el.status());
        fui::AutoCompLearner cl(bass.autoCompSource(-1), bass.parameters(), {}, bass.analysisSampleRate(),
                                [&bass] { return bass.analysisTempo(); });
        automixchecks::learn(cl, render);
        check(cl.fit() && cl.fit()->exact, "hosted BL-1 COMP learns its trace: " + cl.status());
        if (cl.fit()) {
            const float before = automixchecks::paramValue(bass.parameters(), "fx.comp.thr");
            cl.apply();
            const float applied = automixchecks::paramValue(bass.parameters(), "fx.comp.thr");
            const auto session = p.trackParameterValues(1);
            const auto stored = session.find("fx.comp.thr");
            check(std::abs(applied - cl.fit()->threshold) < 0.05 && stored != session.end()
                      && std::abs(stored->second - applied) < 1e-3f, "hosted BL-1 COMP APPLY reaches the session patch");
            cl.undo();
            check(std::abs(automixchecks::paramValue(bass.parameters(), "fx.comp.thr") - before) < 1e-3f, "hosted BL-1 COMP UNDO restores");
        }
    }
    {
        fui::HostedDrumModel drum(p);
        DrumDeviceBody body(drum);
        body.setLookAndFeel(&lnf);
        body.setSize(1460, DrumDeviceBody::workspaceHeight);
        body.setVisible(true);
        auto* groupTab = findFxComponent<juce::TextButton>(body, "GROUP FX");
        check(groupTab != nullptr, "hosted DR-1 has GROUP FX");
        if (groupTab) {
            groupTab->setToggleState(true, juce::dontSendNotification);
            groupTab->onClick();
        }
        body.fxChain().showGroupPanel(true);
        auto& kl = body.kitMixPanel().learner();
        auto params = drum.parameters();
        std::vector<float> lvl;
        for (int i = 0; i < 16; ++i) lvl.push_back(automixchecks::paramValue(params, "pad" + juce::String(i) + ".lvl"));
        automixchecks::learn(kl, render);
        check(kl.fit() != nullptr, "hosted KIT MIX learns the drum track: " + kl.status());
        body.kitMixPanel().refresh();
        automixchecks::writeSnapshot(body.fxChain(), "sq4-dr1-kitmix.png");
        if (kl.fit()) {
            kl.apply();
            bool written = true;
            for (int i = 0; i < 16; ++i) {
                const auto& level = kl.fit()->levels[(size_t)i];
                written &= std::abs(automixchecks::paramValue(params, "pad" + juce::String(i) + ".lvl")
                                    - (level ? (float)*level : lvl[(size_t)i])) < 1e-3f;
            }
            check(written, "hosted KIT MIX APPLY writes pad levels");
            kl.undo();
            bool restored = true;
            for (int i = 0; i < 16; ++i)
                restored &= std::abs(automixchecks::paramValue(params, "pad" + juce::String(i) + ".lvl") - lvl[(size_t)i]) < 1e-3f;
            check(restored, "hosted KIT MIX UNDO restores");
        }
        body.setLookAndFeel(nullptr);
        const auto info = drum.sideChain();
        check(info.hosted && info.label(1).startsWith("01 ") && info.label(17) == "2 " + juce::String(p.conductor().session().tracks[1].name),
              "hosted DR-1 side-chain lists pads and tracks: " + info.label(1) + ", " + info.label(17));
    }
    // ---- live spectrum: master EQ (master output), hosted DR-1 EQ + KIT MIX (track), ASSIST ----
    {
        namespace ls = livespectrumchecks;
        fui::HostedBassModel bass(p);
        check(model.liveSpectrumTap(-1) == &p.masterOutputTap() && bass.liveSpectrumTap(-1) == &p.trackTap(1),
              "master EQ shows the master output, hosted EQs their track");
        fui::HostedDrumModel drum(p);
        DrumDeviceBody body(drum);
        body.setLookAndFeel(&lnf);
        body.setSize(1460, DrumDeviceBody::workspaceHeight);
        body.setVisible(true);
        if (auto* groupTab = findFxComponent<juce::TextButton>(body, "GROUP FX")) {
            groupTab->setToggleState(true, juce::dontSendNotification);
            groupTab->onClick();
        }
        auto& masterEq = view.eqModule();
        auto* hostedEq = body.fxChain().module(fui::FxModuleView::Eq);
        auto& kit = body.kitMixPanel();
        check(masterEq.liveSpectrum() && hostedEq && hostedEq->liveSpectrum(), "master and hosted EQs have a live spectrum");
        if (masterEq.liveSpectrum() && hostedEq && hostedEq->liveSpectrum()) {
            if (!p.conductor().playing()) p.conductor().launchScene(2);
            const auto update = [&] {
                const double now = ls::nowMs();
                masterEq.updateLiveSpectrum(now);
                hostedEq->updateLiveSpectrum(now);
                kit.updateLiveSpectrum(now);
                assist.updateLive(now);
            };
            const std::vector<const fui::LiveSpectrum*> lives{masterEq.liveSpectrum(), hostedEq->liveSpectrum(),
                                                               &kit.liveSpectrum(), &assist.liveSpectrum()};
            check(ls::untilDrawn(lives, render, update),
                  "master EQ, hosted DR-1 EQ, KIT MIX and ASSIST draw live paths while the session plays");
            ls::writeSnapshot(view, "sq4-master-eq-live.png");
            ls::writeSnapshot(body.fxChain(), "sq4-dr1-eq-live.png");
            body.fxChain().showGroupPanel(true);
            ls::writeSnapshot(body.fxChain(), "sq4-dr1-kitmix-live.png");
            body.fxChain().showGroupPanel(false);
            p.conductor().stopTransport();
            check(ls::untilCleared(lives, render, update), "every live path clears after the session stops");
            ls::writeSnapshot(view, "sq4-master-eq-silent.png");
        }
        body.setLookAndFeel(nullptr);
    }
    view.setLookAndFeel(nullptr);
    return check.fails;
}
