#pragma once
#include "Controls.h"
#include "Displays.h"
#include "Modulation.h"
#include "WtUiModel.h"
#include "../WavetableView.h"

// Rack panels — 1:1 ports of src/components/panels/*.tsx, laid out to match the
// CSS grid in src/index.css.
namespace fui {

// Shared: panel background + section header (power led + title + right widget).
void paintPanelBg(juce::Graphics&, juce::Component&);
void paintHeaderTitle(juce::Graphics&, juce::Rectangle<int>, const juce::String&, juce::Colour);

class OscPanel : public juce::Component {
public:
    OscPanel(WtUiModel&, int oscIndex, juce::String prefix, Accent, juce::String title);
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
    explicit UtilPanel(ParameterSource);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    PowerButton subPow, noisePow; Stepper subShape, noiseType;
    Knob subOct, subLevel, noiseLevel;
    juce::Rectangle<int> subHead, noiseHead;
};

class FilterPanel : public juce::Component {
public:
    explicit FilterPanel(ParameterSource);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct TabButton : public juce::Button, private juce::Timer {
        TabButton(ParameterSource, juce::String onId, juce::String label);
        ~TabButton() override { stopTimer(); }
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
        bool keyPressed(const juce::KeyPress&) override;
        void timerCallback() override;

        std::function<void(int)> onNavigate;
        ParameterSource parameters;
        juce::String onId, label;
        bool lastOn = false;
    };
    struct Block {
        // cutoffDest / resDest / driveDest / envDest / keyDest = mod-matrix dest
        // indices for this filter's CUTOFF/RES/DRIVE/ENV/KEY knobs (all continuous,
        // all mod targets).
        Block(ParameterSource, juce::String prefix,
              int cutoffDest, int resDest, int driveDest, int envDest, int keyDest);
        PowerButton power; Stepper type;
        juce::OwnedArray<Knob> knobs;
        void layout(juce::Rectangle<int>);
        void setVisible(bool);
    };
    void setActiveFilter(int index, bool moveKeyboardFocus = false);

    Stepper route; FilterView view;
    Block f1, f2;
    TabButton tab1, tab2;
    int activeFilter = 0;
    juce::Rectangle<int> titleArea;
};

// Small web-styled toggles (src/index.css):
//   Segment  .filter-tab   — a tab inside a dark segmented container (the owner paints the container)
//   Pick     .lfo-pick-btn — a small box; the selected one gets an accent outline
//   Tag      .lfo-tg / #mono-toggle — a pill; "on" is an accent outline with a soft glow
class PillButton : public juce::Button {
public:
    enum Style { Segment, Pick, Tag };
    PillButton(juce::String text, Style, juce::Colour accent);
    void setAccent(juce::Colour c) { accent_ = c; repaint(); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
private:
    Style style_;
    juce::Colour accent_;
};
// Paints the dark container behind a row of Segment buttons.
void paintSegmentContainer(juce::Graphics&, juce::Rectangle<int>);

class EnvPanel : public juce::Component {
public:
    // modSrc > 0 adds a draggable ModSourceChip in the header (e.g. MOD ENV = 3);
    // the env's knobs are never mod targets (modDest 0).
    // base2 adds a second envelope page (e.g. MOD ENV 2 = env3), picked by
    // header tabs "1" / "2"; modSrc2 is its mod-source index.
    EnvPanel(ParameterSource, juce::String base, juce::String title, juce::Colour viewAccent,
             Accent knobAccent, int modSrc = 0, juce::String base2 = {}, int modSrc2 = 0);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Page {
        std::unique_ptr<EnvView> view;
        juce::OwnedArray<Knob> knobs;
        std::unique_ptr<ModSourceChip> srcChip;
    };
    void showPage(int index);
    juce::String title;
    Page pages[2];
    int numPages = 1, activePage = 0;
    std::unique_ptr<PillButton> tabs[2];
    juce::Rectangle<int> titleArea, tabArea;
};

class LfoPanel : public juce::Component, private fui::VisualTimer {
public:
    LfoPanel(ParameterSource, std::function<HostTransport()> transportProvider);
    ~LfoPanel() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    struct Block {
        // modSrc = this LFO's mod-source index (1 = LFO 1, 2 = LFO 2) for the
        // draggable header chip.
        Block(ParameterSource, juce::String id, juce::String title, Accent,
              std::function<HostTransport()> transportProvider, int modSrc);
        juce::String title;
        ModSourceChip srcChip;
        Stepper shape;
        LfoView view;
        PillButton syncBtn, retrigBtn;
        std::unique_ptr<juce::ParameterAttachment> syncAtt, retrigAtt;
        Stepper syncRate;
        Knob rate, rise, phase;
        bool lastSync = false;
        juce::Rectangle<int> titleArea, slot0;   // slot0 = rate/div swap area
        void layout(juce::Rectangle<int>);
        void paintTitle(juce::Graphics&);
        void applySync(bool sync);                // toggle rate/syncRate visibility
    };
    ParameterSource parameters;
    Block l1, l2;
};

class MatrixPanel : public juce::Component, private juce::Timer {
public:
    explicit MatrixPanel(ParameterSource);
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
        Row(ParameterSource, int slot);
        void resized() override;
        void paint(juce::Graphics&) override; // ▸ arrow between src and dst
        int slot;
        juce::ComboBox src, dst, aux;
        std::unique_ptr<juce::ParameterAttachment> srcAtt, dstAtt, auxAtt;
        std::unique_ptr<Knob> amt;
        juce::TextButton remove{juce::String::fromUTF8("\xc3\x97")}; // ×
        juce::TextButton polarity{"UNI"};  // toggles the route between UNI and BI
        ParameterSource parameters;
        void refreshPolarity();
    };
    ParameterSource parameters;
    // Rows live in a four-route viewport beside a compact source/add rail. The
    // fixed 16-slot pool scrolls once a fifth visible route is present.
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;          // indexed [slot-1]; children of rowsHolder
    juce::OwnedArray<ModSourceChip> chips;
    // Macro 1-4 and MOD WHL knobs, each with its draggable source chip.
    juce::OwnedArray<Knob> macroKnobs;
    juce::OwnedArray<ModSourceChip> macroChips;
    juce::TextButton addBtn{"+ ADD ROUTE"};
    std::vector<int> lastVisible;        // cached visible-slot vector (diffed by timer)
    juce::Rectangle<int> titleArea, chipArea, hintArea, addArea, rowsArea;
};

class FxPanel : public juce::Component {
public:
    explicit FxPanel(ParameterSource);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Module {
        Module(ParameterSource, juce::String fx, juce::String title, juce::StringArray knobIds);
        juce::String title;
        PowerButton power;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> bounds, titleArea;
        void layout(juce::Rectangle<int>);
        void paintModule(juce::Graphics&);
    };
    juce::OwnedArray<Module> modules;
};

} // namespace fui
