#pragma once
#include "Controls.h"
#include "Displays.h"
#include "Modulation.h"
#include "../WavetableView.h"
#include "../PluginProcessor.h"

// Rack panels — 1:1 ports of src/components/panels/*.tsx, laid out to match the
// CSS grid in src/index.css.
namespace fui {

using APVTS = juce::AudioProcessorValueTreeState;

// Shared: panel background + section header (power led + title + right widget).
void paintPanelBg(juce::Graphics&, juce::Component&);
void paintHeaderTitle(juce::Graphics&, juce::Rectangle<int>, const juce::String&, juce::Colour);

class OscPanel : public juce::Component {
public:
    OscPanel(APVTS&, FableAudioProcessor&, int oscIndex, juce::String prefix, Accent, juce::String title);
    void paint(juce::Graphics&) override;
    void resized() override;

    // Fired when the wavetable edit (✎) button is clicked; argument is the osc
    // index. The editor wires this to open the WavetableEditor overlay.
    std::function<void(int)> onEditTable;
private:
    int oscIndex;
    juce::String title, prefix; Accent accent;
    PowerButton power; juce::TextButton editBtn{"E"}; Stepper tableStep, unisonStep; WavetableView wt; VSlider pos;
    juce::OwnedArray<Knob> knobs;
    juce::Rectangle<int> titleArea;
    juce::Rectangle<int> uniLabelArea;   // UNI caption above the unison stepper
};

class UtilPanel : public juce::Component {
public:
    explicit UtilPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    PowerButton subPow, noisePow; Stepper subShape, noiseType;
    Knob subOct, subLevel, noiseLevel;
    juce::Rectangle<int> subHead, noiseHead;
};

class FilterPanel : public juce::Component {
public:
    explicit FilterPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Block {
        // cutoffDest / resDest / driveDest / envDest / keyDest = mod-matrix dest
        // indices for this filter's CUTOFF/RES/DRIVE/ENV/KEY knobs (all continuous,
        // all mod targets).
        Block(APVTS&, juce::String prefix, juce::String label,
              int cutoffDest, int resDest, int driveDest, int envDest, int keyDest);
        juce::String label;
        PowerButton power; Stepper type;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> labelArea;
        void layout(juce::Rectangle<int>);
        void paintLabel(juce::Graphics&);
    };
    Stepper route; FilterView view;
    Block f1, f2;
    juce::Rectangle<int> titleArea;
};

class EnvPanel : public juce::Component {
public:
    // modSrc > 0 adds a draggable ModSourceChip in the header (e.g. MOD ENV = 3);
    // the env's knobs are never mod targets (modDest 0).
    EnvPanel(APVTS&, juce::String base, juce::String title, juce::Colour viewAccent,
             Accent knobAccent, int modSrc = 0);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    juce::String title;
    EnvView view; juce::OwnedArray<Knob> knobs;
    std::unique_ptr<ModSourceChip> srcChip;
    juce::Rectangle<int> titleArea;
};

class LfoPanel : public juce::Component, private juce::Timer {
public:
    LfoPanel(APVTS&, std::function<HostTransport()> transportProvider);
    ~LfoPanel() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    struct Block {
        // modSrc = this LFO's mod-source index (1 = LFO 1, 2 = LFO 2) for the
        // draggable header chip.
        Block(APVTS&, juce::String id, juce::String title, Accent,
              std::function<HostTransport()> transportProvider, int modSrc);
        juce::String title;
        ModSourceChip srcChip;
        Stepper shape;
        LfoView view;
        juce::TextButton syncBtn{"SYNC"}, retrigBtn{"TRIG"};
        std::unique_ptr<APVTS::ButtonAttachment> syncAtt, retrigAtt;
        Stepper syncRate;
        Knob rate, rise, phase;
        bool lastSync = false;
        juce::Rectangle<int> titleArea, slot0;   // slot0 = rate/div swap area
        void layout(juce::Rectangle<int>);
        void paintTitle(juce::Graphics&);
        void applySync(bool sync);                // toggle rate/syncRate visibility
    };
    APVTS& apvts;
    Block l1, l2;
};

class MatrixPanel : public juce::Component, private juce::Timer {
public:
    explicit MatrixPanel(APVTS&);
    ~MatrixPanel() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void relayoutRows();                 // place only the currently-visible rows
    std::vector<int> visibleSlots() const; // slots with rowVisible() (§9), in order

    // One row per slot 1..16 (all constructed up front so combobox attachments
    // stay stable); only rows whose slot is rowVisible() are shown.
    struct Row : public juce::Component {
        Row(APVTS&, int slot);
        void resized() override;
        void paint(juce::Graphics&) override; // ▸ arrow between src and dst
        int slot;
        juce::ComboBox src, dst;
        std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt, dstAtt;
        std::unique_ptr<Knob> amt;
        juce::TextButton remove{juce::String::fromUTF8("\xc3\x97")}; // ×
    };
    APVTS& apvts;
    // Rows live in a scrollable viewport: fixed-height rows that scroll once the
    // active routes exceed the panel height (the 16-slot pool can fill the list).
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;          // indexed [slot-1]; children of rowsHolder
    juce::OwnedArray<ModSourceChip> chips;
    juce::TextButton addBtn{"+ ADD ROUTE"};
    std::vector<int> lastVisible;        // cached visible-slot vector (diffed by timer)
    juce::Rectangle<int> titleArea, chipArea, addArea, rowsArea;
};

class FxPanel : public juce::Component {
public:
    explicit FxPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Module {
        Module(APVTS&, juce::String fx, juce::String title, juce::StringArray knobIds);
        juce::String title;
        PowerButton power;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> bounds, titleArea;
        void layout(juce::Rectangle<int>);
        void paintModule(juce::Graphics&);
    };
    juce::OwnedArray<Module> modules;
};

class TopBar : public juce::Component, private juce::Timer {
public:
    TopBar(APVTS&, FableAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    FableAudioProcessor& proc;
    juce::TextButton prev{"<"}, next{">"}, save{"SAVE"};
    juce::ComboBox presets;
    ScopeView scope; SpectrumView spectrum;
    Knob master;
    juce::Rectangle<int> brandArea, scopeBox, specBox, statusArea;
    int lastVoices = -1; bool lastMidi = false;
};

} // namespace fui
