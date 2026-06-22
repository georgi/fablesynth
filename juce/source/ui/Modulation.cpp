#include "Modulation.h"
#include "../dsp/Params.h"

namespace fui {

// Resolve a slot field (".src" / ".dst" / ".amt") to its APVTS RangedAudioParameter.
static juce::RangedAudioParameter* matParam(APVTS& s, int slot, const char* field) {
    return dynamic_cast<juce::RangedAudioParameter*>(
        s.getParameter("mat" + juce::String(slot) + field));
}

// Read a slot field as its REAL value (0 if missing).
static float matReal(APVTS& s, int slot, const char* field) {
    if (auto* p = matParam(s, slot, field)) return p->convertFrom0to1(p->getValue());
    return 0.0f;
}

// Write a slot field from a REAL value, notifying the host.
static void setMatReal(APVTS& s, int slot, const char* field, float real) {
    if (auto* p = matParam(s, slot, field))
        p->setValueNotifyingHost(p->convertTo0to1(real));
}

int findFreeSlot(APVTS& s) {
    for (int n = 1; n <= fable::MOD_MATRIX_SIZE; ++n) {
        // free = fully empty (src==0 AND dst==0) so a half-configured ADD-ROUTE
        // row (src set, dst not yet) is never clobbered.
        if ((int)matReal(s, n, ".src") == 0 && (int)matReal(s, n, ".dst") == 0)
            return n;
    }
    return -1;
}

int addRoute(APVTS& s, int src, int dst, float amt) {
    int slot = findFreeSlot(s);
    if (slot < 0) return -1;
    setMatReal(s, slot, ".src", (float)src);
    setMatReal(s, slot, ".dst", (float)dst);
    setMatReal(s, slot, ".amt", amt);
    return slot;
}

void clearSlot(APVTS& s, int slot) {
    if (slot < 1 || slot > fable::MOD_MATRIX_SIZE) return;
    setMatReal(s, slot, ".src", 0.0f);
    setMatReal(s, slot, ".dst", 0.0f);
    setMatReal(s, slot, ".amt", 0.0f);
}

bool isSlotActive(APVTS& s, int slot) {
    if (slot < 1 || slot > fable::MOD_MATRIX_SIZE) return false;
    return (int)matReal(s, slot, ".src") != 0 && (int)matReal(s, slot, ".dst") != 0;
}

// ======================= ModSourceChip =======================
ModSourceChip::ModSourceChip(int src, juce::String label, bool compact)
    : src_(src), label_(std::move(label)), compact_(compact) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("drag " + label_ + " onto a control");
}

void ModSourceChip::mouseDown(const juce::MouseEvent&) {}

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
