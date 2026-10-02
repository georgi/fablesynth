#pragma once
#include "AutomationEdit.h"
#include "../../ui/ParameterSource.h"
#include "../../ui/LookAndFeel.h"
#include "../SeqProcessor.h"

namespace fui {
class ClipAutomationPanel final : public juce::Component, private juce::Timer {
public:
    enum Tool { Draw, Line, Point };
    explicit ClipAutomationPanel(SeqAudioProcessor&);
    ~ClipAutomationPanel() override;
    void setTarget(int scene,int track,ParameterSource source,std::function<int()> selectedPad);
    void refresh();
    bool expanded() const { return open_; }
    void setExpanded(bool);
    int preferredHeight(int width) const;
    std::function<void()> onLayoutChanged;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void addLane();
    void selectLane(int);
    void setTool(Tool);
    void setSnap(bool);
    void setTime(fable::AutoTime::Mode);
    void setSteps(int);
    void setCycle(int);
    void setLaneTarget(const std::string&);
    void setLearn(bool);
    bool learning() const { return learn_; }
    juce::Component& canvasForTest() { return canvas_; }
    juce::ComboBox& targetForTest() { return target_; }
    const fable::ClipData* clip() const;
    int selectedLane() const { return selected_; }
private:
    class Canvas final : public juce::Component {
    public:
        explicit Canvas(ClipAutomationPanel& p) : panel(p) { setWantsKeyboardFocus(true); setTitle("Automation curve"); }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress&) override;
        void cancel();
    private:
        struct Position { double t=0,v=0,s=0; float x=0; };
        Position locate(juce::Point<float>) const;
        int hit(double) const;
        float xOf(double) const; float yOf(double) const;
        double snapped(double) const;
        enum Drag { None,Paint,Ramp,Move,Bend } drag_ = None;
        int index_ = -1, focusedPoint_ = -1;
        double y0_=0,c0_=0,dir_=1;
        Position from_,to_,last_,hover_;
        bool hovering_=false;
        std::vector<fable::AutoPoint> base_,original_;
        ClipAutomationPanel& panel;
    };
    class Chip final : public juce::TextButton {
    public:
        std::vector<fable::AutoPoint> points; int len=16; juce::Colour accent; bool laneEnabled=true;
        void paintButton(juce::Graphics&,bool,bool) override;
    };
    struct Target { std::string id; int menu; };
    const fable::AutoLane* lane() const;
    fable::autoedit::Geometry geometry() const;
    void write(std::vector<fable::AutoLane>,bool history=true);
    void patchLane(std::function<void(fable::AutoLane&)>,bool history=true);
    void writePoints(std::vector<fable::AutoPoint>);
    void rebuildControls();
    void rebuildTargets();
    int layoutInspector(int width,int y,bool apply);
    void timerCallback() override;
    double elapsed() const;
    double baseline() const;
    juce::String timeReadout() const;
    juce::String valueReadout(double) const;
    static juce::String targetLabel(fable::Machine,const std::string&);
    juce::Colour color(int laneIndex) const;
    void style(juce::TextButton&,const juce::String& tooltip);
    void beginEdit();
    class PanelLookAndFeel final : public DarkLNF {
    public:
        juce::Font getTextButtonFont(juce::TextButton&,int) override { return monoFont(9); }
        juce::Font getComboBoxFont(juce::ComboBox&) override { return monoFont(9); }
        void positionComboBoxText(juce::ComboBox& box,juce::Label& label) override {
            label.setBounds(8,0,std::max(0,box.getWidth()-22),box.getHeight()); label.setFont(monoFont(9));
        }
    } lookAndFeel_;
    SeqAudioProcessor& proc_;
    ParameterSource source_;
    std::function<int()> selectedPad_;
    int scene_=-1,track_=-1,selected_=0,lastPad_=-1,lastFit_=4;
    bool open_=false,snap_=true,learn_=false,refreshing_=false;
    Tool tool_=Draw;
    juce::String signature_;
    std::unordered_map<std::string,float> learnValues_;
    std::vector<Target> targets_;
    juce::TextButton disclose_{"AUTOMATION"},add_{"+ LANE"},learnButton_{"LEARN"},power_{"ON"},clear_{"CLEAR"},remove_{"X"};
    std::array<juce::TextButton,3> tools_;
    std::array<juce::TextButton,2> snaps_,cycles_;
    std::array<juce::TextButton,4> times_;
    std::array<Chip,8> chips_;
    juce::ComboBox target_;
    juce::TextButton stepDown_{"<"},stepUp_{">"};
    juce::Label steps_,readout_;
    std::array<juce::Label,4> captions_;
    Canvas canvas_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAutomationPanel)
};
}
