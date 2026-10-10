#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "ParameterSource.h"

// Shared modulation plumbing for the VST editor: the 16-slot pool helpers (the
// single home for slot find/add/clear over the APVTS) plus the draggable source
// chip. Mirrors the web's slotHelpers.ts + ModSourceChip.tsx so the Serum-style
// drag-to-assign UX behaves identically across both codebases.
namespace fui {

using APVTS = juce::AudioProcessorValueTreeState;

// Default modulation depth (REAL value) applied when a route is created by drag
// or ADD ROUTE. One constant per language (TS mirrors this in slotHelpers.ts).
constexpr float MOD_DEFAULT_AMT = 0.3f;

// ---- slot pool helpers (single home; operate on REAL values via the APVTS) ----
// Slot predicates (contract §9):
//   findFreeSlot  : first slot 1..16 with src==0 AND dst==0 (fully empty).
//   isSlotActive  : src!=0 AND dst!=0 (engine applies it / matrix "live").
//   rowVisible    : src!=0 OR dst!=0  (matrix list shows the row).
int  findFreeSlot(APVTS&);                                       // -1 if none free
int  addRoute(APVTS&, int src, int dst, float amt = MOD_DEFAULT_AMT); // slot or -1
void clearSlot(APVTS&, int slot);                               // zeroes src, dst, amt
bool isSlotActive(APVTS&, int slot);
int  findFreeSlot(const ParameterSource&);                       // hosted/shared form
int  addRoute(const ParameterSource&, int src, int dst, float amt = MOD_DEFAULT_AMT);
void clearSlot(const ParameterSource&, int slot);
bool isSlotActive(const ParameterSource&, int slot);
// Effective route polarity (mat<n>.pol resolved against the source's native
// range). setRouteBipolar stores SRC when the choice matches the source.
bool routeBipolar(const ParameterSource&, int slot);
void setRouteBipolar(const ParameterSource&, int slot, bool bipolar);
// Ring/band label, e.g. "LFO 1 ±30%" (bipolar) or "VELO +30%" (unipolar).
juce::String routeLabel(int src, float amt, bool bipolar);

// Source chip under the mouse (MOD_SOURCES index, 0 = none). Mod targets
// emphasise that source's rings so the user sees where it goes. UI thread only.
int  highlightedModSource();
void setHighlightedModSource(int src);

// ---- draggable modulation source chip --------------------------------------
// A pill (grip glyph + optional label) tinted with modSourceColour(src). On a
// drag it starts a JUCE drag with description "mod-src:N" so any Knob / VSlider
// mod target (or panel drop zone) can pick it up. compact = grip-only.
class ModSourceChip : public juce::Component, public juce::SettableTooltipClient {
public:
    ModSourceChip(int src, juce::String label, bool compact = false);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
private:
    int          src_;
    juce::String label_;
    bool         compact_;
};

} // namespace fui
