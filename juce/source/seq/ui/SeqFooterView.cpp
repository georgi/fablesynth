#include "SeqFooterView.h"
#include "../../ui/Controls.h"

namespace fui {

namespace {
constexpr const char* kStopGlyph = "S";
}

SeqFooterView::SeqFooterView(SeqAudioProcessor& p) : proc(p) { startTimerHz(30); }

// ---- actions (also the test handles) ---------------------------------------

void SeqFooterView::stopAllClick() { proc.conductor().stopAll(); }
void SeqFooterView::trackStopClick(int t) { proc.conductor().stopTrack(t); }

// ---- timer -------------------------------------------------------------------

void SeqFooterView::timerCallback() {
    for (int t = 0; t < 4; ++t) {
        const float rms = proc.trackRms[t].load();
        vuLevel_[t] = juce::jmax(rms, vuLevel_[t] * 0.92f);
    }
    repaint();
}

// ---- mouse -------------------------------------------------------------------

void SeqFooterView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (stopAllBtn.contains(pos)) { stopAllClick(); return; }
    for (int t = 0; t < 4; ++t)
        if (cellStopBtn[t].contains(pos)) { trackStopClick(t); return; }
}

// ---- layout --------------------------------------------------------------
// Component-local columns match the rack grid table (Task 9): master col
// x=0 w=218, track cell i at x=218+9+i*(292+9) w=292 (i=0..3).

void SeqFooterView::resized() {
    const int h = getHeight();
    masterArea = { 0, 0, 218, h };
    auto m = masterArea.reduced(10, 8);
    stopAllBtn = m.removeFromTop(26);
    m.removeFromTop(6);
    chipsArea = m;

    for (int t = 0; t < 4; ++t) {
        auto r = juce::Rectangle<int>(218 + 9 + t * (292 + 9), 0, 292, h);
        cellArea[t] = r;
        auto content = r.reduced(10, 7);
        cellStopBtn[t] = content.removeFromLeft(22).withSizeKeepingCentre(22, 22);
        content.removeFromLeft(8);
        vuArea[t] = content.removeFromRight(64).withSizeKeepingCentre(64, 5);
        content.removeFromRight(8);
        nowArea[t] = content;
    }
}

// ---- paint -----------------------------------------------------------------

void SeqFooterView::paint(juce::Graphics& g) {
    const auto& cond = proc.conductor();
    const auto& session = cond.session();

    // master: STOP ALL + live scene chips
    {
        auto rf = masterArea.toFloat();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff141824), rf.getX(), rf.getY(),
                                               juce::Colour(0xff0c0f16), rf.getX(), rf.getBottom(), false));
        g.fillRoundedRectangle(rf, 10.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);

        auto bf = stopAllBtn.toFloat();
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(bf, 6.0f);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRoundedRectangle(bf.reduced(0.5f), 6.0f, 1.0f);
        g.setColour(col::text);
        g.setFont(monoFont(8.5f));
        drawSpaced(g, "STOP ALL", stopAllBtn, 1.6f);

        auto cr = chipsArea;
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        auto tag = cr.removeFromLeft(30);
        drawSpaced(g, "LIVE", tag, 1.6f);

        int x = cr.getX();
        bool any = false;
        for (int s = 0; s < (int)session.scenes.size(); ++s) {
            bool live = false;
            for (int t = 0; t < 4; ++t) if (cond.ownerOf(t) == s) live = true;
            if (!live) continue;
            any = true;
            // web "01 DROP A ·M" -- ASCII middle dot substitute (BassHeader.cpp:175 convention)
            juce::String txt = juce::String(s + 1).paddedLeft('0', 2) + " " + juce::String(session.scenes[(size_t)s].name)
                              + (cond.sceneMuted(s) ? " -M" : "");
            g.setFont(monoFont(7.0f));
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(g.getCurrentFont(), txt, 0.0f, 0.0f);
            const int w = (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 10;
            juce::Rectangle<int> chip(x, cr.getY(), w, cr.getHeight());
            if (chip.getRight() > cr.getRight()) break;
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRoundedRectangle(chip.toFloat(), 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 3.0f, 1.0f);
            g.setColour(col::text);
            g.drawText(txt, chip, juce::Justification::centred);
            x = chip.getRight() + 4;
        }
        if (!any) {
            g.setColour(col::textDim);
            g.drawText("-", juce::Rectangle<int>(x, cr.getY(), 20, cr.getHeight()), juce::Justification::centredLeft);
        }
    }

    // per-track NOW cells
    for (int t = 0; t < 4 && t < (int)session.tracks.size(); ++t) {
        const juce::Colour tc { session.tracks[(size_t)t].color };
        auto rf = cellArea[t].toFloat();
        g.setGradientFill(juce::ColourGradient(col::panelHi, rf.getX(), rf.getY(),
                                               col::panelLo, rf.getX(), rf.getBottom(), false));
        g.fillRoundedRectangle(rf, 10.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);

        auto bf = cellStopBtn[t].toFloat();
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(bf, 5.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(bf.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(col::textDim);
        g.setFont(monoFont(9.0f, true));
        g.drawText(kStopGlyph, cellStopBtn[t], juce::Justification::centred);

        const int owner = cond.ownerOf(t);
        const bool live = owner != -2;
        const bool audible = live && cond.trackAudible(t);

        auto na = nowArea[t];
        // NOW label brightens to the hint tone while a clip owns the track, so
        // "which scene is playing here" reads at a glance.
        g.setColour(live ? col::textHint : col::textDim);
        g.setFont(monoFont(7.0f));
        drawSpaced(g, "NOW", na.removeFromTop(10), 1.8f);

        juce::String label = "-";
        if (live && owner >= 0 && owner < (int)session.scenes.size()) {
            const auto& sc = session.scenes[(size_t)owner];
            const auto& clip = sc.clips[(size_t)t];
            juce::String pos;
            const int bar = proc.trackBar[t].load();
            if (bar >= 0 && clip.bars > 0)
                pos = " - " + juce::String(bar % clip.bars + 1) + "/" + juce::String(clip.bars);
            label = juce::String(owner + 1).paddedLeft('0', 2) + " " + juce::String(sc.name)
                  + " - " + juce::String(clip.name) + pos;
        }
        // A brighter, near-white-tinted owner label when audible so it stands
        // out from an idle track (web parity: .sq-foot-owner.on color 92%).
        g.setColour(audible ? tc.interpolatedWith(juce::Colours::white, 0.12f)
                            : juce::Colour(0xff4a5266));
        g.setFont(monoFont(9.0f, audible));
        g.drawText(label, na, juce::Justification::centredLeft);

        // VU
        auto vf = vuArea[t].toFloat();
        g.setColour(juce::Colour(0xff0a0d13));
        g.fillRoundedRectangle(vf, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(vf.reduced(0.5f), 3.0f, 1.0f);
        const float frac = juce::jlimit(0.03f, 1.0f, vuLevel_[t] * 3.2f);
        auto lit = vf.withWidth(vf.getWidth() * frac);
        g.setColour(tc);
        g.fillRoundedRectangle(lit, 3.0f);
    }
}

} // namespace fui
