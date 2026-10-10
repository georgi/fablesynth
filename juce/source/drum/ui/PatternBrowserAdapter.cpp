#include "PatternBrowserAdapter.h"
#include "../../ui/BrowserSupport.h"
#include "../../ui/Theme.h"
#include "../dsp/PatternLibrary.h"

#include <algorithm>

namespace fui {

using namespace fable;

namespace {
constexpr int kSteps = 16, kPads = 16, kBarBytes = kPads * kSteps;
// Glyph row of each pad: kick/sub, backbeat (snare, clap, rim), hats and ride, the rest.
constexpr int kRowOfPad[kPads] = { 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3 };

// Greedy word wrap of `text` into at most `lines` lines of `width` px.
juce::StringArray wrap(const juce::String& text, const juce::Font& font, float width, int lines) {
    juce::StringArray words, out;
    words.addTokens(text, " ", "");
    juce::String line;
    for (const auto& w : words) {
        const auto next = line.isEmpty() ? w : line + " " + w;
        if (line.isNotEmpty() && juce::GlyphArrangement::getStringWidth(font, next) > width) {
            out.add(line);
            line = w;
        } else line = next;
    }
    if (line.isNotEmpty()) out.add(line);
    if (out.size() > lines) {
        out.removeRange(lines, out.size() - lines);
        out.set(lines - 1, out[lines - 1] + juce::String::fromUTF8("\xe2\x80\xa6"));
    }
    return out;
}
} // namespace

PatternBrowserAdapter::PatternBrowserAdapter(DrumUiModel& model, Hooks hooks)
    : model_(model), hooks_(std::move(hooks)) {
    roles_ = browser::splitWords(patternlib::kUses);
    characters_ = browser::splitWords(patternlib::kFamilies);
    for (const auto& e : patternLibrary()) {
        BrowserItem item;
        item.name = e.name;
        item.role = e.role;
        item.tags.add(e.family);
        items_.push_back(item);
        glyphs_.push_back(rowsOf(e.steps, 1));   // rows show the first bar
    }
}

juce::String PatternBrowserAdapter::glyphLegend() const {
    return juce::String::fromUTF8("kick \xc2\xb7 backbeat \xc2\xb7 hats \xc2\xb7 perc");
}
juce::StringArray PatternBrowserAdapter::detailLegend() const {
    return { "KICK", "BACKBEAT", "HATS", juce::String::fromUTF8("TOMS \xc2\xb7 PERC") };
}

// ---- load / snapshot (store.ts loadPatterns / captureSequence / restoreSequence) ----

void PatternBrowserAdapter::load(int item) {
    const auto& lib = patternLibrary();
    if (item < 0 || item >= (int)lib.size() || !model_.hasTargetClip()) return;
    const auto& e = lib[(size_t)item];
    auto next = model_.sequence();
    next.steps = e.steps;
    // Library chains are bars 1..N, which is also what a hosted clip takes
    // (web: sequenceChain(chain.length)); the clip's length becomes N.
    next.chain = e.chain;
    next.hasRhythm = false;
    next.rhythm = DrumRhythm{};
    next.configuredLanes = 0;
    if (!model_.commitSequence(next)) return;
    model_.setEditPattern(0);
    loaded_ = item;
    if (hooks_.loaded) hooks_.loaded();
}

bool PatternBrowserAdapter::liveDiffersFrom(int item) const {
    const auto& lib = patternLibrary();
    if (item < 0 || item >= (int)lib.size()) return true;
    const auto live = model_.sequence();
    return live.steps != lib[(size_t)item].steps || live.chain != lib[(size_t)item].chain || live.hasRhythm;
}

std::unique_ptr<BrowserSnapshot> PatternBrowserAdapter::capture() const {
    auto s = std::make_unique<Snap>();
    s->index = loaded_;
    s->dirty = loaded_ >= 0 && liveDiffersFrom(loaded_);
    s->sequence = model_.sequence();
    s->editPattern = model_.editPattern();
    return s;
}

void PatternBrowserAdapter::restore(const BrowserSnapshot& snap) {
    const auto& s = static_cast<const Snap&>(snap);
    if (!model_.commitSequence(s.sequence)) return;
    model_.setEditPattern(s.editPattern);
    loaded_ = s.index;
    if (hooks_.loaded) hooks_.loaded();
}

// ---- audition ----

std::vector<AuditionEvent> PatternBrowserAdapter::auditionPlan(int item) const {
    std::vector<AuditionEvent> out;
    const auto& lib = patternLibrary();
    if (item < 0 || item >= (int)lib.size()) return out;
    const float bpm = hooks_.bpm && hooks_.bpm() > 0 ? hooks_.bpm() : 120.0f;
    const double step = 60.0 / bpm / 4.0 * 1000.0;
    const auto& steps = lib[(size_t)item].steps;
    for (int s = 0; s < kSteps; ++s)
        for (int p = 0; p < kPads; ++p)
            if (const auto v = steps[(size_t)(p * kSteps + s)])
                out.push_back({ s * step, true, p, v == 2 ? 1.0f : 0.72f });
    return out;
}

bool PatternBrowserAdapter::auditionBlocked() const {
    // A hosted clip owns the transport, so it always plays the loaded pattern in place.
    return model_.capabilities().hosted || model_.sequencerPlaying() || (hooks_.hostPlaying && hooks_.hostPlaying());
}

juce::String PatternBrowserAdapter::auditionHint(bool phraseOn) const {
    if (auditionBlocked()) return "The sequencer plays the selected pattern.";
    return phraseOn ? "The first bar plays on select." : "Start the sequencer to hear patterns in place.";
}

// ---- glyphs ----

PatternBrowserAdapter::Rows PatternBrowserAdapter::rowsOf(const std::vector<uint8_t>& steps, int bars) {
    Rows rows((size_t)juce::jlimit(1, 4, bars));
    for (size_t bar = 0; bar < rows.size(); ++bar) {
        auto& r = rows[bar];
        r.fill(0);
        for (int p = 0; p < kPads; ++p)
            for (int s = 0; s < kSteps; ++s) {
                const size_t i = bar * kBarBytes + (size_t)(p * kSteps + s);
                if (i < steps.size()) {
                    auto& cell = r[(size_t)(kRowOfPad[p] * kSteps + s)];
                    cell = std::max(cell, steps[i]);
                }
            }
    }
    return rows;
}

void PatternBrowserAdapter::drawRows(juce::Graphics& g, const Rows& rows, juce::Rectangle<float> r, juce::Colour c, bool fill) {
    if (fill) r = r.reduced(10.0f, 8.0f);
    const int cols = (int)rows.size() * kSteps;
    const float cw = r.getWidth() / (float)cols, ch = r.getHeight() / 4.0f;
    for (size_t bar = 1; bar < rows.size(); ++bar) {
        g.setColour(c.withMultipliedAlpha(0.35f));
        g.fillRect(r.getX() + (float)(bar * kSteps) * cw - 0.5f, r.getY(), 1.0f, r.getHeight());
    }
    for (size_t bar = 0; bar < rows.size(); ++bar)
        for (int row = 0; row < 4; ++row)
            for (int s = 0; s < kSteps; ++s) {
                const auto v = rows[bar][(size_t)(row * kSteps + s)];
                if (!v) continue;
                const float x = r.getX() + (float)((int)bar * kSteps + s) * cw;
                g.setColour(c.withMultipliedAlpha(v == 2 ? 1.0f : 0.55f));
                g.fillRect(x + cw * 0.12f, r.getY() + (float)row * ch + ch * 0.15f, cw * 0.76f, ch * 0.7f);
            }
}

void PatternBrowserAdapter::drawItemGlyph(juce::Graphics& g, int item, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    if (item >= 0 && item < (int)glyphs_.size()) drawRows(g, glyphs_[(size_t)item], r, c, fill);
}

void PatternBrowserAdapter::drawLiveGlyph(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    const auto s = model_.sequence();
    drawRows(g, rowsOf(s.steps, juce::jmax(1, (int)s.chain.size())), r, c, fill);
}

std::vector<std::pair<juce::String, juce::String>> PatternBrowserAdapter::liveReadout() const {
    const auto& lib = patternLibrary();
    const auto* e = loaded_ >= 0 && loaded_ < (int)lib.size() ? &lib[(size_t)loaded_] : nullptr;
    const auto dash = juce::String::fromUTF8("\xe2\x80\x94");
    // The detail name already reads "GENRE · SECTION", so the rows give the
    // arrangement note room to wrap over three lines.
    std::vector<std::pair<juce::String, juce::String>> kv = {
        { "Bars", juce::String(juce::jmax(1, (int)model_.chain().size())) },
        { "Typical tempo", e ? juce::String(e->bpm) + " BPM" : dash },
        { "Written swing", e ? juce::String(juce::roundToInt(e->swing * 100.0f)) + "%" : dash },
    };
    const auto font = monoFont(11.0f);
    const float keyW = juce::GlyphArrangement::getStringWidth(font, "Typical tempo");
    const auto lines = e ? wrap(e->note, font, 320.0f - 28.0f - keyW - 12.0f - 4.0f, 3) : juce::StringArray{ dash };
    for (int i = 0; i < lines.size(); ++i) kv.push_back({ i == 0 ? juce::String("Arrangement") : juce::String(), lines[i] });
    return kv;
}

std::uint64_t PatternBrowserAdapter::liveRevision() const {
    const auto s = model_.sequence();
    std::uint64_t h = 1469598103934665603ull;
    for (auto v : s.steps) h = (h ^ v) * 1099511628211ull;
    return browser::hashFloats({ (float)loaded_, (float)s.chain.size(), auditionBlocked() ? 1.0f : 0.0f }, h);
}

} // namespace fui
