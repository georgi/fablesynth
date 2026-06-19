#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"
#include "../PluginProcessor.h"

// User-wavetable editor — JUCE port of src/components/WavetableEditor.tsx.
// A modal overlay with two creation modes:
//   IMPORT AUDIO: load a file, slice into SIZE-sample frames (single-cycle,
//                 auto-detect pitch, or fixed cycle length) and band-limit each.
//   DRAW:         sketch one cycle on a pad; band-limited on commit.
// The committed table is added to the processor's user pool and assigned to the
// oscillator that opened the editor.
namespace fui {

// Free-draw pad: one cycle of DRAW_N points in [-1, 1].
class DrawPad : public juce::Component {
public:
    static constexpr int DRAW_N = 256;
    enum class Brush { Pen, Smooth };
    explicit DrawPad(juce::Colour accent);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void clear();
    void setAccent(juce::Colour c) { accent = c; repaint(); }
    void setBrush(Brush b) { brush = b; }
    void setSnap(bool s) { snap = s; }
    void setReadOnly(bool ro) { readOnly = ro; repaint(); }
    void seed(int kind);                 // 0 sine 1 saw 2 square 3 tri
    void setPoints(const std::vector<float>& p); // load frame 0 (downsampled)
    const std::vector<float>& points() const { return pts; }
private:
    void paintAt(juce::Point<float>);
    void smoothAround(int idx, int rad = 7);
    std::vector<float> pts;
    int lastIdx = -1;
    juce::Colour accent;
    Brush brush = Brush::Pen;
    bool snap = false;
    bool readOnly = false;
};

// Read-only preview of the candidate wavetable (the frames the current settings
// would produce). Draws the frames as a small perspective terrain, like the
// rack's WavetableView, so you can see what you're about to create.
class TablePreview : public juce::Component {
public:
    explicit TablePreview(juce::Colour ac) : accent(ac) {}
    void setAccent(juce::Colour c) { accent = c; repaint(); }
    void setFrames(std::vector<std::vector<float>> f) { frames = std::move(f); repaint(); }
    void clear() { frames.clear(); repaint(); }
    void paint(juce::Graphics&) override;
private:
    std::vector<std::vector<float>> frames;
    juce::Colour accent;
};

class WavetableEditor : public juce::Component {
public:
    explicit WavetableEditor(FableAudioProcessor&);

    void openFor(int oscIndex);
    void close();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override; // backdrop click-to-close

private:
    enum class Tab { Audio, Draw };
    enum class AudioMode { Single, Auto, Fixed };

    void setTab(Tab);
    void setMode(AudioMode);
    void chooseFile();
    std::vector<std::vector<float>> framesFromCurrentSettings() const;
    void updatePreview();
    void createFromAudio();
    void createFromDraw();
    void commit(fable::UserTable);
    void refreshList();
    void layoutPanel();
    juce::Rectangle<int> panelBounds() const;
    juce::Colour accent() const;

    FableAudioProcessor& proc;
    int oscIndex = 0;
    Tab tab = Tab::Audio;
    AudioMode mode = AudioMode::Single;

    juce::AudioFormatManager formatMgr;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> audioSamples;
    double audioSr = 0;
    bool hasAudio = false;

    // header / tabs
    juce::TextButton closeBtn{"X"};
    juce::TextButton tabAudio{"IMPORT AUDIO"}, tabDraw{"DRAW"};
    juce::Label nameLabel{{}, "NAME"};
    juce::TextEditor nameField;

    // audio tab
    juce::TextButton fileBtn{"CHOOSE FILE..."};
    juce::TextButton modeSingle{"SINGLE CYCLE"}, modeAuto{"AUTO-DETECT"}, modeFixed{"FIXED LEN"};
    juce::Label fixedLabel{{}, "CYCLE"};
    juce::TextEditor fixedField;
    juce::Label statusLabel;
    juce::Label hintLabel;
    TablePreview audioPreview{col::acA};
    juce::TextButton createAudioBtn{"CREATE TABLE"};

    // draw tab
    DrawPad drawPad;
    juce::Label drawHint;
    juce::TextButton clearBtn{"CLEAR"};
    juce::TextButton createDrawBtn{"CREATE TABLE"};

    // user-table list — one Row per pooled table (name + frame count + delete).
    juce::Label listHeader{{}, "USER TABLES"};
    struct Row : public juce::Component {
        Row(const juce::String& text, std::function<void()> onDelete);
        void resized() override;
        juce::Label info;
        juce::TextButton del{"X"};
    };
    juce::OwnedArray<Row> listRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableEditor)
};

} // namespace fui
