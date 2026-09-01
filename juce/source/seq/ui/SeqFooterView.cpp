#include "SeqFooterView.h"
#include "../../ui/Controls.h"

namespace fui {

SeqFooterView::SeqFooterView(SeqAudioProcessor& p) : proc(p) { startTimerHz(30); }

// ---- actions (also the test handles) ---------------------------------------

void SeqFooterView::stopAllClick() { proc.conductor().stopAll(); }
void SeqFooterView::trackStopClick(int t) { proc.conductor().stopTrack(t); }

// ---- timer -------------------------------------------------------------------

juce::uint32 SeqFooterView::paintSignature() const {
    const auto& cond = proc.conductor();
    const auto& sess = cond.session();

    juce::uint32 sig = 17;
    auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
    auto mixStr = [&mix](const std::string& s) {
        mix((int)s.size());
        for (char c : s) mix((int)(unsigned char)c);
    };

    mix((int)sess.scenes.size());
    mix((int)sess.tracks.size());
    for (int s = 0; s < (int)sess.scenes.size(); ++s) {
        mixStr(sess.scenes[(size_t)s].name);        // live chip text
        mix(cond.sceneMuted(s) ? 1 : 0);
    }
    for (int t = 0; t < 4 && t < (int)sess.tracks.size(); ++t) {
        const int owner = cond.ownerOf(t);
        mix(owner);
        mix(cond.trackAudible(t) ? 1 : 0);
        mix((int)(sess.tracks[(size_t)t].color & 0xffffu));
        mix((int)(sess.tracks[(size_t)t].color >> 16));
        mix(proc.trackBar[t].load());               // NOW bar counter
        if (owner >= 0 && owner < (int)sess.scenes.size()) {
            const auto& clip = sess.scenes[(size_t)owner].clips[(size_t)t];
            mixStr(clip.name);
            mix(clip.bars);
        }
    }
    return sig;
}

// The VU decay must run on every tick, but a settled meter over a static
// now-playing state has nothing to redraw.
void SeqFooterView::timerCallback() {
    constexpr float kVuEpsilon = 1.0e-3f;
    bool moved = false;
    for (int t = 0; t < 4; ++t) {
        const float rms = proc.trackRms[t].load();
        float v = juce::jmax(rms, vuLevel_[t] * 0.92f);
        if (v < kVuEpsilon) v = 0.0f;   // snap, so the fall terminates
        if (v != vuLevel_[t]) moved = true;
        vuLevel_[t] = v;
    }
    const juce::uint32 sig = paintSignature();
    if (sig != lastSig_) { lastSig_ = sig; moved = true; }
    if (moved) repaint();
}

// ---- mouse -------------------------------------------------------------------

void SeqFooterView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (stopAllBtn.contains(pos)) { stopAllClick(); return; }
    for (int t = 0; t < 4; ++t)
        if (cellStopBtn[t].contains(pos)) { trackStopClick(t); return; }
}

void SeqFooterView::mouseMove(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    bool clickable = stopAllBtn.contains(pos);
    for (int t = 0; t < 4 && !clickable; ++t) clickable = cellStopBtn[t].contains(pos);
    setMouseCursor(clickable ? juce::MouseCursor::PointingHandCursor
                             : juce::MouseCursor::NormalCursor);
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
        // Web .sq-stopall: "■ STOP ALL" centered in the button.
        g.setColour(col::text);
        g.setFont(monoFont(8.5f));
        {
            const juce::String txt("STOP ALL");
            const auto f = g.getCurrentFont();
            float tw = 0;
            for (int i = 0; i < txt.length(); ++i)
                tw += juce::GlyphArrangement::getStringWidth(f, txt.substring(i, i + 1)) + 1.6f;
            tw -= 1.6f;
            const float iconW = 7.0f, gap = 6.0f;
            const int x0 = stopAllBtn.getCentreX() - (int)((iconW + gap + tw) * 0.5f);
            g.fillPath(iconStop(juce::Rectangle<float>((float)x0, (float)stopAllBtn.getCentreY() - 3.5f,
                                                       iconW, iconW)));
            drawSpaced(g, txt, stopAllBtn.withLeft(x0 + (int)(iconW + gap)), 1.6f);
        }

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
            juce::String txt = juce::String(s + 1).paddedLeft('0', 2) + " " + juce::String(session.scenes[(size_t)s].name)
                              + (cond.sceneMuted(s) ? juce::String::fromUTF8(" \xc2\xb7M") : juce::String());
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
            // Even the "nothing live" placeholder sits in a chip (web wraps
            // the em-dash in .sq-live-chip).
            juce::Rectangle<int> chip(x, cr.getY(), 18, cr.getHeight());
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRoundedRectangle(chip.toFloat(), 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 3.0f, 1.0f);
            g.setColour(col::textDim);
            g.drawText(juce::String::fromUTF8("\xe2\x80\x94"), chip, juce::Justification::centred);
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
        g.fillPath(iconStop(cellStopBtn[t].toFloat().withSizeKeepingCentre(7.0f, 7.0f)));

        const int owner = cond.ownerOf(t);
        const bool live = owner != -2;
        const bool audible = live && cond.trackAudible(t);

        // The tag/owner pair centers vertically in the cell (web .sq-foot-now
        // is a centered flex column), instead of hugging the top edge.
        auto na = nowArea[t].withSizeKeepingCentre(nowArea[t].getWidth(), 24);
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
                pos = juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(bar % clip.bars + 1) + "/" + juce::String(clip.bars);
            label = juce::String(owner + 1).paddedLeft('0', 2) + " " + juce::String(sc.name)
                  + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(clip.name) + pos;
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
