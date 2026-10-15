#include "Modulation.h"
#include "../dsp/Params.h"
#include <cmath>

namespace fui {

// Resolve a slot field (".src" / ".dst" / ".amt") through the shared source.
static juce::RangedAudioParameter* matParam(const ParameterSource& s, int slot, const char* field) {
    return s.parameter("mat" + juce::String(slot) + field);
}

// Read a slot field as its REAL value (0 if missing).
static float matReal(const ParameterSource& s, int slot, const char* field) {
    if (auto* p = matParam(s, slot, field)) return p->convertFrom0to1(p->getValue());
    return 0.0f;
}

// Write a slot field from a REAL value, notifying the host.
static void setMatReal(const ParameterSource& s, int slot, const char* field, float real) {
    if (auto* p = matParam(s, slot, field))
        p->setValueNotifyingHost(p->convertTo0to1(real));
}

int findFreeSlot(const ParameterSource& s) {
    for (int n = 1; n <= fable::MOD_MATRIX_SIZE; ++n) {
        // free = fully empty (src==0 AND dst==0) so a half-configured ADD-ROUTE
        // row (src set, dst not yet) is never clobbered.
        if ((int)matReal(s, n, ".src") == 0 && (int)matReal(s, n, ".dst") == 0)
            return n;
    }
    return -1;
}

int addRoute(const ParameterSource& s, int src, int dst, float amt) {
    int slot = findFreeSlot(s);
    if (slot < 0) return -1;
    setMatReal(s, slot, ".src", (float)src);
    setMatReal(s, slot, ".dst", (float)dst);
    setMatReal(s, slot, ".amt", amt);
    return slot;
}

void clearSlot(const ParameterSource& s, int slot) {
    if (slot < 1 || slot > fable::MOD_MATRIX_SIZE) return;
    setMatReal(s, slot, ".src", 0.0f);
    setMatReal(s, slot, ".dst", 0.0f);
    setMatReal(s, slot, ".amt", 0.0f);
    setMatReal(s, slot, ".pol", 0.0f);
    setMatReal(s, slot, ".aux", 0.0f);
}

bool routeBipolar(const ParameterSource& s, int slot) {
    return fable::routeBipolar((int)matReal(s, slot, ".src"), (int)matReal(s, slot, ".pol"));
}

void setRouteBipolar(const ParameterSource& s, int slot, bool bipolar) {
    if (slot < 1 || slot > fable::MOD_MATRIX_SIZE) return;
    const bool native = fable::sourceBipolar((int)matReal(s, slot, ".src"));
    setMatReal(s, slot, ".pol", bipolar == native ? 0.0f : bipolar ? 2.0f : 1.0f);
}

juce::String routeLabel(int src, float amt, bool bipolar) {
    const auto name = src > 0 && src < (int)fable::MOD_SOURCES.size()
        ? juce::String(fable::MOD_SOURCES[(size_t)src]) : juce::String("MOD");
    const int pct = (int)std::lround(amt * 100.0f);
    const auto sign = bipolar ? juce::String::fromUTF8("\xc2\xb1") : juce::String(pct < 0 ? "" : "+");
    return name + " " + sign + juce::String(bipolar ? std::abs(pct) : pct) + "%";
}

static int g_highlightedSource = 0;
int highlightedModSource() { return g_highlightedSource; }
void setHighlightedModSource(int src) { g_highlightedSource = src; }

bool isSlotActive(const ParameterSource& s, int slot) {
    if (slot < 1 || slot > fable::MOD_MATRIX_SIZE) return false;
    return (int)matReal(s, slot, ".src") != 0 && (int)matReal(s, slot, ".dst") != 0;
}

static ParameterSource sourceFor(APVTS& s) { return ParameterSource::fromApvts(s); }
int findFreeSlot(APVTS& s) { return findFreeSlot(sourceFor(s)); }
int addRoute(APVTS& s, int src, int dst, float amt) { return addRoute(sourceFor(s), src, dst, amt); }
void clearSlot(APVTS& s, int slot) { clearSlot(sourceFor(s), slot); }
bool isSlotActive(APVTS& s, int slot) { return isSlotActive(sourceFor(s), slot); }

// ======================= ModSourceChip =======================
ModSourceChip::ModSourceChip(int src, juce::String label, bool compact)
    : src_(src), label_(std::move(label)), compact_(compact) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("drag " + label_ + " onto a control");
}

void ModSourceChip::mouseDown(const juce::MouseEvent&) {}
void ModSourceChip::mouseEnter(const juce::MouseEvent&) { setHighlightedModSource(src_); }
void ModSourceChip::mouseExit(const juce::MouseEvent&) {
    if (highlightedModSource() == src_) setHighlightedModSource(0);
}

void ModSourceChip::mouseDrag(const juce::MouseEvent&) {
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this))
        if (!c->isDragAndDropActive())
            c->startDragging("mod-src:" + juce::String(src_), this);
}

void ModSourceChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    auto tint = modSourceColour(src_);

    g.setColour(tint.withAlpha(0.16f));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(tint.withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);

    auto inner = r.toNearestInt().reduced(5, 0);
    g.setColour(tint.withAlpha(0.75f));
    g.setFont(monoFont(9.0f));
    g.drawText(juce::String::fromUTF8("\xe2\xa0\xbf"), // ⠿ grip glyph
               inner.removeFromLeft(compact_ ? inner.getWidth() : 12),
               juce::Justification::centred);
    if (!compact_) {
        g.setColour(tint);
        g.setFont(monoFont(8.0f));
        drawSpaced(g, label_.toUpperCase(), inner, 0.8f, juce::Justification::centredLeft);
    }
}

} // namespace fui
