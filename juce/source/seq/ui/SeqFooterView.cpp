#include "SeqFooterView.h"
#include "../../ui/Controls.h"
#include <cmath>

namespace fui {

SeqFooterView::SeqFooterView(SeqAudioProcessor& p) : proc(p) { startVisualTimer(*this, fui::kAnimationHz); }

// ---- actions (also the test handles) ---------------------------------------

void SeqFooterView::stopAllClick() { proc.conductor().stopAll(); }
void SeqFooterView::trackStopClick(int t) { proc.conductor().stopTrack(t); }

// ---- timer -------------------------------------------------------------------

SeqFooterView::PaintState SeqFooterView::paintState() const {
    PaintState state;
    const auto& cond = proc.conductor();
    const auto& session = cond.session();
    auto cr = chipsArea;
    cr.removeFromLeft(30);
    int x = cr.getX();
    for (int scene = 0; scene < (int)session.scenes.size(); ++scene) {
        bool live = false;
        for (int t = 0; t < 4; ++t) if (cond.ownerOf(t) == scene) live = true;
        if (!live) continue;
        state.anyLive = true;
        const auto text = juce::String(scene + 1).paddedLeft('0', 2) + " " + juce::String(session.scenes[(size_t)scene].name)
            + (cond.sceneMuted(scene) ? juce::String::fromUTF8(" \xc2\xb7M") : juce::String());
        int width = 0;
        for (const auto& chip : cachedState_.chips)
            if (chip.text == text) { width = chip.width; break; }
        if (width == 0) {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(monoFont(7.0f), text, 0.0f, 0.0f);
            width = (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 10;
        }
        if (x + width > cr.getRight()) break;
        state.chips.push_back({ text, width });
        x += width + 4;
    }
    state.tracks = juce::jmin(4, (int)session.tracks.size());
    for (int t = 0; t < state.tracks; ++t) {
        const int owner = cond.ownerOf(t);
        state.live[t] = owner != -2;
        state.audible[t] = state.live[t] && cond.trackAudible(t);
        state.colours[t] = session.tracks[(size_t)t].color;
        state.labels[t] = "-";
        if (state.live[t] && owner >= 0 && owner < (int)session.scenes.size()) {
            const auto& sc = session.scenes[(size_t)owner];
            const auto& clip = sc.clips[(size_t)t];
            juce::String pos;
            const int bar = proc.trackBar[t].load();
            if (bar >= 0 && clip.bars > 0)
                pos = juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(bar % clip.bars + 1) + "/" + juce::String(clip.bars);
            state.labels[t] = juce::String(owner + 1).paddedLeft('0', 2) + " " + juce::String(sc.name)
                + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(clip.name) + pos;
        }
    }
    return state;
}

// The VU decay must run on every tick, but a settled meter over a static
// now-playing state has nothing to redraw.
void SeqFooterView::timerCallback() {
    if (!isVisibleInHierarchy(*this)) {
        for (auto& level : vuLevel_) level = 0.0f;
        return;
    }
    constexpr float kVuEpsilon = 1.0e-3f;
    // Match the former 30 Hz release time at the animation cadence.
    const float decay = std::pow(0.92f, 30.0f / kAnimationHz);
    bool moved = false;
    for (int t = 0; t < 4; ++t) {
        const float rms = proc.trackRms[t].load();
        float v = juce::jmax(rms, vuLevel_[t] * decay);
        if (v < kVuEpsilon) v = 0.0f;   // snap, so the fall terminates
        if (v != vuLevel_[t]) moved = true;
        vuLevel_[t] = v;
    }
    const auto state = paintState();
    if (state != lastState_) { lastState_ = state; moved = true; }
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
// Component-local columns match the compact native rack grid: master col
// x=0 w=218, track cell i at x=218+9+i*(250+9) w=250 (i=0..3).

void SeqFooterView::resized() {
    const int h = getHeight();
    masterArea = { 0, 0, 218, h };
    auto m = masterArea.reduced(10, 8);
    stopAllBtn = m.removeFromTop(26);
    m.removeFromTop(6);
    chipsArea = m;

    for (int t = 0; t < 4; ++t) {
        auto r = juce::Rectangle<int>(218 + 9 + t * (250 + 9), 0, 250, h);
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
    if (getWidth() <= 0 || getHeight() <= 0 || !g.clipRegionIntersects(getLocalBounds())) return;
    const float physicalScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float scale = std::isfinite(physicalScale) && physicalScale > 0.0f ? physicalScale : 1.0f;
    const int width = (int)std::ceil(getWidth() * scale), height = (int)std::ceil(getHeight() * scale);
    const auto state = paintState();
    if (!artwork_.isValid() || artwork_.getWidth() != width || artwork_.getHeight() != height
        || artworkScale_ != scale || artworkWidth_ != getWidth() || artworkHeight_ != getHeight()
        || cachedState_ != state) {
        cachedState_ = state;
        artworkScale_ = scale;
        artworkWidth_ = getWidth();
        artworkHeight_ = getHeight();
        artwork_ = juce::Image(juce::Image::ARGB, width, height, true);
        juce::Graphics layer(artwork_);
        layer.addTransform(juce::AffineTransform::scale(scale));
        layer.reduceClipRegion(getLocalBounds());
        paintArtwork(layer, state);
        ++cacheBuildCount_;
    }
    {
        juce::Graphics::ScopedSaveState save(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.setColour(juce::Colours::white);
        g.drawImageTransformed(artwork_, juce::AffineTransform::scale(1.0f / scale));
    }
    paintMeters(g, state);
}

void SeqFooterView::paintArtwork(juce::Graphics& g, const PaintState& state) {
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
        for (const auto& entry : state.chips) {
            const auto& txt = entry.text;
            const int w = entry.width;
            g.setFont(monoFont(7.0f));
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
        if (!state.anyLive) {
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
    for (int t = 0; t < state.tracks; ++t) {
        const juce::Colour tc { state.colours[t] };
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

        const bool live = state.live[t];
        const bool audible = state.audible[t];

        // The tag/owner pair centers vertically in the cell (web .sq-foot-now
        // is a centered flex column), instead of hugging the top edge.
        auto na = nowArea[t].withSizeKeepingCentre(nowArea[t].getWidth(), 24);
        // NOW label brightens to the hint tone while a clip owns the track, so
        // "which scene is playing here" reads at a glance.
        g.setColour(live ? col::textHint : col::textDim);
        g.setFont(monoFont(7.0f));
        drawSpaced(g, "NOW", na.removeFromTop(10), 1.8f);

        // A brighter, near-white-tinted owner label when audible so it stands
        // out from an idle track (web parity: .sq-foot-owner.on color 92%).
        g.setColour(audible ? tc.interpolatedWith(juce::Colours::white, 0.12f)
                            : juce::Colour(0xff4a5266));
        g.setFont(monoFont(9.0f, audible));
        g.drawText(state.labels[t], na, juce::Justification::centredLeft);

        // VU
        auto vf = vuArea[t].toFloat();
        g.setColour(juce::Colour(0xff0a0d13));
        g.fillRoundedRectangle(vf, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(vf.reduced(0.5f), 3.0f, 1.0f);
    }
}

void SeqFooterView::paintMeters(juce::Graphics& g, const PaintState& state) {
    for (int t = 0; t < state.tracks; ++t) {
        const auto vf = vuArea[t].toFloat();
        const juce::Colour tc { state.colours[t] };
        const float frac = juce::jlimit(0.03f, 1.0f, vuLevel_[t] * 3.2f);
        auto lit = vf.withWidth(vf.getWidth() * frac);
        g.setColour(tc);
        g.fillRoundedRectangle(lit, 3.0f);
    }
}

} // namespace fui
