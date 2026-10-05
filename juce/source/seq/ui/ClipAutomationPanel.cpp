#include "ClipAutomationPanel.h"
#include "../AutomationCodec.h"
#include "../DrumRhythmCodec.h"
#include <cmath>
namespace fui {
using namespace fable;
namespace {
const std::array<uint32_t,8> colors {0xff4de8ff,0xffffa14d,0xffb18cff,0xff7ef0a6,0xffff72b0,0xffffdd6b,0xff6f9dff,0xffff8466};
void curve(juce::Graphics& g,const std::vector<AutoPoint>& points,int len,juce::Rectangle<float> area,juce::Colour color) {
    if (!autoedit::audible(points,len)) { g.setColour(col::textDim); g.drawHorizontalLine((int)area.getCentreY(),area.getX(),area.getRight()); return; }
    juce::Path path;
    for (int i=0;i<=40;++i) {
        const float x=area.getX()+area.getWidth()*i/40.0f;
        const float y=area.getBottom()-area.getHeight()*(float)evalLane(points,len,i/40.0*len*.9999);
        if (!i) path.startNewSubPath(x,y); else path.lineTo(x,y);
    }
    g.setColour(color); g.strokePath(path,juce::PathStrokeType(1.3f));
}
}
void ClipAutomationPanel::Chip::paintButton(juce::Graphics& g,bool over,bool down) {
    auto r=getLocalBounds().toFloat().reduced(.5f); auto c=laneEnabled ? accent : col::textDim;
    g.setColour(getToggleState() ? c.withAlpha(.12f) : col::panelLo); g.fillRoundedRectangle(r,5);
    g.setColour(getToggleState() || over || down ? c.withAlpha(.6f) : col::line); g.drawRoundedRectangle(r,5,1);
    g.setColour(c); g.fillEllipse(8,10,6,6);
    g.setColour(laneEnabled ? col::text : col::textDim); g.setFont(monoFontMedium(9));
    g.drawFittedText(getButtonText(),20,0,getWidth()-70,getHeight(),juce::Justification::centredLeft,1);
    curve(g,points,len,{(float)getWidth()-46,7,38,12},c);
}
ClipAutomationPanel::ClipAutomationPanel() : canvas_(*this) {
    setLookAndFeel(&lookAndFeel_);
    setTitle("Clip automation"); setWantsKeyboardFocus(true);
    for (auto* b : {&disclose_,&add_,&learnButton_,&power_,&clear_,&remove_,&stepDown_,&stepUp_}) { style(*b,{}); addAndMakeVisible(*b); }
    disclose_.onClick=[this]{ if (!open_ && clip() && clip()->automation.empty()) addLane(); else setExpanded(!open_); }; add_.onClick=[this]{ addLane(); };
    learnButton_.onClick=[this]{ setLearn(!learn_); };
    power_.onClick=[this]{ patchLane([](auto& l){ l.enabled=!l.enabled; }); };
    clear_.onClick=[this]{ canvas_.cancel(); patchLane([](auto& l){ l.points.clear(); }); };
    remove_.onClick=[this]{ canvas_.cancel(); if (const auto* c=clip()) { auto lanes=c->automation; if (selected_<(int)lanes.size()) { lanes.erase(lanes.begin()+selected_); selected_=std::max(0,selected_-1); setLearn(false); write(std::move(lanes)); } } };
    const char* names[] {"DRAW","LINE","POINT"};
    const char* tips[] {"Paint one held value per cell", "Drag a straight ramp", "Click to add, drag to move, Alt-drag to bend, double-click or right-click to remove. Shift locks time."};
    for (int i=0;i<3;++i) { auto& b=tools_[(size_t)i]; b.setButtonText(names[i]); style(b,tips[i]); addAndMakeVisible(b); b.onClick=[this,i]{ setTool((Tool)i); }; }
    for (int i=0;i<2;++i) {
        auto& s=snaps_[(size_t)i]; s.setButtonText(i ? "FREE" : "1/16"); style(s,i ? "Quarter-step precision" : "Snap to lane steps"); addAndMakeVisible(s); s.onClick=[this,i]{ setSnap(i==0); };
        auto& c=cycles_[(size_t)i]; c.setButtonText(i ? "2 BARS" : "1 BAR"); style(c,"FIT cycle length"); addAndMakeVisible(c); c.onClick=[this,i]{ setCycle(i ? 8 : 4); };
    }
    const char* modes[] {"CLIP","GRID","FIT","PAD"};
    for (int i=0;i<4;++i) { auto& b=times_[(size_t)i]; b.setButtonText(modes[i]); style(b,"Lane timebase"); addAndMakeVisible(b); b.onClick=[this,i]{ setTime((AutoTime::Mode)i); }; }
    for (int i=0;i<8;++i) { style(chips_[(size_t)i],"Select automation lane"); addChildComponent(chips_[(size_t)i]); chips_[(size_t)i].onClick=[this,i]{ selectLane(i); }; }
    const char* captions[]{"TARGET","TIME","STEPS","CYCLE"};
    for (int i=0;i<4;++i) { auto& c=captions_[(size_t)i]; c.setText(captions[i],juce::dontSendNotification); c.setFont(monoFont(8)); c.setColour(juce::Label::textColourId,col::textDim); addAndMakeVisible(c); }
    target_.setTooltip("Continuous device parameter"); addAndMakeVisible(target_);
    target_.onChange=[this]{ if (refreshing_) return; for (const auto& t : targets_) if (t.menu==target_.getSelectedId()) { setLaneTarget(t.id); break; } };
    stepDown_.onClick=[this]{ if (lane()) setSteps(lane()->time.steps-1); }; stepUp_.onClick=[this]{ if (lane()) setSteps(lane()->time.steps+1); };
    steps_.setEditable(false,true,false); steps_.setJustificationType(juce::Justification::centred); steps_.setFont(monoFontMedium(10));
    steps_.setColour(juce::Label::backgroundColourId,col::display); steps_.setColour(juce::Label::textColourId,col::text); steps_.setTooltip("STEPS: 1 to 64"); addAndMakeVisible(steps_);
    steps_.onTextChange=[this]{ if (!refreshing_) setSteps(steps_.getText().getIntValue()); };
    readout_.setFont(monoFont(9)); readout_.setColour(juce::Label::textColourId,col::textHint); addAndMakeVisible(readout_); addAndMakeVisible(canvas_);
    learnButton_.setTooltip("Move a continuous knob on this device to set the target"); remove_.setTooltip("Delete lane");
    canvas_.setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30); rebuildControls();
}
ClipAutomationPanel::~ClipAutomationPanel() { stopTimer(); setLookAndFeel(nullptr); }
void ClipAutomationPanel::style(juce::TextButton& b,const juce::String& tooltip) {
    b.setColour(juce::TextButton::buttonColourId,col::panelLo); b.setColour(juce::TextButton::buttonOnColourId,col::acA.withAlpha(.16f));
    b.setColour(juce::TextButton::textColourOffId,col::textHint); b.setColour(juce::TextButton::textColourOnId,col::acA); b.setTooltip(tooltip);
}
juce::Colour ClipAutomationPanel::color(int i) const { return juce::Colour(colors[(size_t)juce::jlimit(0,7,i)]); }
const ClipData* ClipAutomationPanel::clip() const {
    return automationSource_.clip ? automationSource_.clip() : nullptr;
}
const AutoLane* ClipAutomationPanel::lane() const { const auto* c=clip(); return c && selected_>=0 && selected_<(int)c->automation.size() ? &c->automation[(size_t)selected_] : nullptr; }
autoedit::Geometry ClipAutomationPanel::geometry() const {
    const auto* c=clip(); const auto* l=lane(); return {l && c ? laneCycle(*l,c->bars,c->hasDrumRhythm ? &c->drumRhythm : nullptr) : AutoCycle{},c ? c->bars*16 : 16};
}
void ClipAutomationPanel::setSource(Source source,ParameterSource parameters,std::function<int()> pad) {
    const bool changed=automationSource_.identity!=source.identity;
    if (changed) { canvas_.cancel(); selected_=0; learn_=false; }
    machine_=source.machine; automationSource_=std::move(source);
    source_=std::move(parameters); selectedPad_=std::move(pad); lastPad_=-1;
    if (changed) open_=clip() && !clip()->automation.empty();
    times_[0].setButtonText(automationSource_.unit);
    signature_.clear(); refresh(); if (onLayoutChanged) onLayoutChanged();
}
bool ClipAutomationPanel::canShowAutomation(const juce::String& target) const {
    const auto* c=clip();
    return c && autoParamDef(machine_,target.toStdString()) &&
        (c->automation.size()<AUTO_MAX_LANES || std::any_of(c->automation.begin(),c->automation.end(),
            [&](const auto& l){ return l.target==target.toStdString(); }));
}
void ClipAutomationPanel::showAutomation(const juce::String& target) {
    if (!canShowAutomation(target)) return;
    canvas_.cancel(); auto lanes=clip()->automation;
    auto it=std::find_if(lanes.begin(),lanes.end(),[&](const auto& l){ return l.target==target.toStdString(); });
    selected_=(int)std::distance(lanes.begin(),it);
    if (it==lanes.end()) { lanes.push_back({target.toStdString(),true,{}, {}}); write(std::move(lanes)); }
    setLearn(false); setExpanded(true); signature_.clear(); refresh();
}
void ClipAutomationPanel::setExpanded(bool open) { if (open_==open) return; canvas_.cancel(); open_=open; rebuildControls(); if (onLayoutChanged) onLayoutChanged(); }
void ClipAutomationPanel::selectLane(int i) { canvas_.cancel(); selected_=i; setLearn(false); signature_.clear(); refresh(); }
void ClipAutomationPanel::setTool(Tool tool) { canvas_.cancel(); tool_=tool; canvas_.setMouseCursor(tool==Point ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor); rebuildControls(); }
void ClipAutomationPanel::setSnap(bool snap) { snap_=snap; rebuildControls(); }
void ClipAutomationPanel::beginEdit() { if (automationSource_.beginEdit) automationSource_.beginEdit(); }
void ClipAutomationPanel::write(std::vector<AutoLane> lanes,bool history) {
    if (!clip() || !validateAutomation(lanes,machine_)) return;
    if (history) beginEdit();
    if (automationSource_.write) automationSource_.write(lanes); signature_.clear(); refresh();
}
void ClipAutomationPanel::patchLane(std::function<void(AutoLane&)> edit,bool history) {
    if (!lane()) return; auto lanes=clip()->automation; edit(lanes[(size_t)selected_]); write(std::move(lanes),history);
}
void ClipAutomationPanel::writePoints(std::vector<AutoPoint> points) { patchLane([&](auto& l){ l.points=std::move(points); },false); }
void ClipAutomationPanel::addLane() {
    const auto* c=clip(); if (!c || c->automation.size()>=AUTO_MAX_LANES) return;
    const auto machine=machine_;
    const std::string preferred=machine==Machine::WT1 ? "filter.cutoff" : machine==Machine::BL1 ? "flt.cut" : "pad"+std::to_string(selectedPad_ ? selectedPad_() : 0)+".flt.cut";
    auto used=[&](const std::string& id){ return std::any_of(c->automation.begin(),c->automation.end(),[&](const auto& l){ return l.target==id; }); };
    std::string id=preferred; if (used(id)) { id.clear(); for (const auto& t : targets_) if (!used(t.id)) { id=t.id; break; } }
    if (id.empty()) return;
    canvas_.cancel(); auto lanes=c->automation; selected_=(int)lanes.size(); lanes.push_back({id,true,{}, {}}); write(std::move(lanes)); setExpanded(true);
}
void ClipAutomationPanel::setLaneTarget(const std::string& id) {
    if (!lane() || lane()->target==id) return;
    const auto& lanes=clip()->automation;
    for (int i=0;i<(int)lanes.size();++i) if (i!=selected_ && lanes[(size_t)i].target==id) return;
    if (!autoParamDef(machine_,id)) return;
    canvas_.cancel(); patchLane([&](auto& l){ l.target=id; if (l.time.mode==AutoTime::Pad && autoPadOf(id)<0) l.time.mode=AutoTime::Clip; });
}
void ClipAutomationPanel::setTime(AutoTime::Mode mode) {
    if (!lane() || lane()->time.mode==mode) return;
    const int steps=std::min(AUTO_MAX_STEPS,geometry().cycle.len);
    if (mode==AutoTime::Pad && (machine_!=Machine::DR1 || autoPadOf(lane()->target)<0)) return;
    canvas_.cancel(); patchLane([&](auto& l){
        if (l.time.mode==AutoTime::Fit) lastFit_=l.time.cycleBeats;
        const int n=l.time.mode==AutoTime::Grid || l.time.mode==AutoTime::Fit ? l.time.steps : steps;
        l.time={mode,mode==AutoTime::Fit ? std::min(16,n) : n,lastFit_};
    });
}
void ClipAutomationPanel::setSteps(int n) { if (lane() && (lane()->time.mode==AutoTime::Grid || lane()->time.mode==AutoTime::Fit)) { n=juce::jlimit(1,64,n); if (n!=lane()->time.steps) { canvas_.cancel(); patchLane([&](auto& l){ l.time.steps=n; }); } else rebuildControls(); } }
void ClipAutomationPanel::setCycle(int n) { if (lane() && lane()->time.mode==AutoTime::Fit && (n==4 || n==8) && lane()->time.cycleBeats!=n) { lastFit_=n; canvas_.cancel(); patchLane([&](auto& l){ l.time.cycleBeats=n; }); } }
void ClipAutomationPanel::setLearn(bool on) {
    learn_=on && lane(); learnValues_.clear();
    if (learn_) {
        const auto machine=machine_;
        auto remember=[&](const auto& defs){ for (const auto& d : defs) if (autoParamDef(machine,d.pid)) if (auto* p=source_.parameter(d.pid)) learnValues_[d.pid]=p->getValue(); };
        if (machine==Machine::WT1) remember(paramInfo()); else if (machine==Machine::BL1) remember(bassParamInfo()); else remember(drumParamInfo());
    }
    rebuildControls();
}
juce::String ClipAutomationPanel::targetLabel(Machine machine,const std::string& id) {
    const auto* d=autoParamDef(machine,id);
    const std::map<juce::String,juce::String> prefixes {
        {"oscA","OSC A"},{"oscB","OSC B"},{"filter","F1"},{"filter2","F2"},{"flt","FILTER"},
        {"env1","AMP ENV"},{"env2","MOD ENV"},{"lfo1","LFO 1"},{"lfo2","LFO 2"},{"modenv","MOD ENV"}
    };
    auto parts=juce::StringArray::fromTokens(juce::String(id),".",""); juce::StringArray labels;
    const int pad=autoPadOf(id);
    for (int i=0;i<parts.size();++i) {
        if (!i && pad>=0) labels.add("P"+juce::String(pad+1).paddedLeft('0',2));
        else if (i==parts.size()-1 && d) labels.add(juce::String(d->label).toUpperCase());
        else { const auto p=prefixes.find(parts[i]); labels.add(p==prefixes.end() ? parts[i].toUpperCase() : p->second); }
    }
    return labels.joinIntoString(" ");
}
void ClipAutomationPanel::rebuildTargets() {
    targets_.clear(); target_.clear(juce::dontSendNotification); if (!clip()) return;
    const auto machine=machine_; const int pad=selectedPad_ ? selectedPad_() : 0;
    int menu=0; juce::String group;
    auto add=[&](const auto& defs){ for (const auto& d : defs) {
        if (!autoParamDef(machine,d.pid)) continue;
        const int targetPad=autoPadOf(d.pid);
        if (machine==Machine::DR1 && targetPad>=0 && targetPad!=pad && (!lane() || lane()->target!=d.pid)) continue;
        juce::String next(d.pid); next=next.upToLastOccurrenceOf(".",false,false).toUpperCase();
        if (next!=group) { group=next; target_.addSectionHeading(group.replace("."," ")); }
        targets_.push_back({d.pid,++menu}); target_.addItem(targetLabel(machine,d.pid),menu);
        bool used=false; for (int i=0;i<(int)clip()->automation.size();++i) used |= i!=selected_ && clip()->automation[(size_t)i].target==d.pid;
        target_.setItemEnabled(menu,!used);
        if (lane() && lane()->target==d.pid) target_.setSelectedId(menu,juce::dontSendNotification);
    } };
    if (machine==Machine::WT1) add(paramInfo()); else if (machine==Machine::BL1) add(bassParamInfo()); else add(drumParamInfo());
}
void ClipAutomationPanel::refresh() {
    if (learn_ && lane()) {
        std::string moved;
        for (auto& [id,v] : learnValues_) if (auto* p=source_.parameter(id)) { const float next=p->getValue(); if (next!=v && moved.empty()) moved=id; v=next; }
        if (!moved.empty()) { setLearn(false); setLaneTarget(moved); }
    }
    const auto* c=clip(); const int pad=selectedPad_ ? selectedPad_() : 0;
    juce::String sig=c ? juce::JSON::toString(automationToVar(c->automation))+juce::String(c->bars)+(c->hasDrumRhythm ? juce::JSON::toString(drumRhythmToVar(*c)) : "") : "NONE";
    if (sig!=signature_ || pad!=lastPad_) {
        signature_=sig; lastPad_=pad; if (!c) { canvas_.cancel(); learn_=false; }
        selected_=juce::jlimit(0,c ? std::max(0,(int)c->automation.size()-1) : 0,selected_);
        rebuildControls(); if (onLayoutChanged) onLayoutChanged();
    }
    canvas_.repaint();
}
void ClipAutomationPanel::rebuildControls() {
    refreshing_=true; const auto* c=clip(); const auto* l=lane(); const auto machine=machine_;
    const bool editing=open_ && l; const auto accent=color(selected_);
    disclose_.setButtonText(open_ ? "v AUTOMATION" : "> AUTOMATION"); add_.setEnabled(c && c->automation.size()<AUTO_MAX_LANES);
    for (int i=0;i<8;++i) {
        auto& b=chips_[(size_t)i]; const bool show=open_ && c && i<(int)c->automation.size(); b.setVisible(show);
        if (show) { const auto& a=c->automation[(size_t)i]; b.setButtonText(targetLabel(machine,a.target)); b.setToggleState(i==selected_,juce::dontSendNotification); b.accent=color(i); b.laneEnabled=a.enabled; b.points=a.points; b.len=laneCycle(a,c->bars,c->hasDrumRhythm ? &c->drumRhythm : nullptr).len; }
    }
    for (int i=0;i<3;++i) { tools_[(size_t)i].setVisible(open_); tools_[(size_t)i].setToggleState(i==(int)tool_,juce::dontSendNotification); }
    for (int i=0;i<2;++i) { snaps_[(size_t)i].setVisible(open_); snaps_[(size_t)i].setToggleState(i==(snap_ ? 0 : 1),juce::dontSendNotification); cycles_[(size_t)i].setVisible(editing && l->time.mode==AutoTime::Fit); cycles_[(size_t)i].setToggleState(l && l->time.cycleBeats==(i ? 8 : 4),juce::dontSendNotification); }
    for (int i=0;i<4;++i) { times_[(size_t)i].setVisible(editing && (i<3 || (machine_==Machine::DR1 && autoPadOf(l->target)>=0))); times_[(size_t)i].setToggleState(l && i==(int)l->time.mode,juce::dontSendNotification); }
    for (auto* b : {&learnButton_,&power_,&clear_,&remove_}) b->setVisible(editing);
    captions_[0].setVisible(editing); captions_[1].setVisible(editing); captions_[2].setVisible(editing && (l->time.mode==AutoTime::Grid || l->time.mode==AutoTime::Fit)); captions_[3].setVisible(editing && l->time.mode==AutoTime::Fit);
    target_.setVisible(editing); canvas_.setVisible(editing); readout_.setVisible(editing);
    const bool steps=editing && (l->time.mode==AutoTime::Grid || l->time.mode==AutoTime::Fit);
    stepDown_.setVisible(steps); stepUp_.setVisible(steps); steps_.setVisible(steps);
    if (l) { steps_.setText(juce::String(l->time.steps),juce::dontSendNotification); stepDown_.setEnabled(l->time.steps>1); stepUp_.setEnabled(l->time.steps<64); clear_.setEnabled(!l->points.empty()); power_.setButtonText(l->enabled ? "ON" : "OFF"); power_.setToggleState(l->enabled,juce::dontSendNotification); if (l->time.mode==AutoTime::Fit) lastFit_=l->time.cycleBeats; }
    learnButton_.setButtonText(learn_ ? "MOVE A KNOB" : "LEARN"); learnButton_.setToggleState(learn_,juce::dontSendNotification);
    for (auto* b : {&learnButton_,&power_}) { b->setColour(juce::TextButton::buttonOnColourId,accent.withAlpha(.16f)); b->setColour(juce::TextButton::textColourOnId,accent); }
    auto accentButton=[&](juce::TextButton& b){ b.setColour(juce::TextButton::buttonOnColourId,accent.withAlpha(.16f)); b.setColour(juce::TextButton::textColourOnId,accent); };
    for (auto& b : tools_) accentButton(b); for (auto& b : snaps_) accentButton(b); for (auto& b : times_) accentButton(b); for (auto& b : cycles_) accentButton(b);
    target_.setColour(juce::ComboBox::textColourId,accent); readout_.setText(timeReadout(),juce::dontSendNotification);
    rebuildTargets(); refreshing_=false; resized(); repaint();
}
int ClipAutomationPanel::preferredHeight(int width) const {
    if (!open_) return 40;
    const int count=clip() ? (int)clip()->automation.size() : 0;
    if (!count) return 106;
    const int per=std::max(1,(width-16)/210),rows=(count+per-1)/per;
    return 40+rows*32+const_cast<ClipAutomationPanel*>(this)->layoutInspector(width,0,false)+24+132+10;
}
int ClipAutomationPanel::layoutInspector(int width,int y,bool apply) {
    const int available=std::max(1,width-16); int x=8,row=0;
    auto group=[&](int w) { if (x>8 && x+w>width-8) { x=8; ++row; } auto r=juce::Rectangle<int>(x,y+row*32,w,26); x+=w+12; return r; };
    auto bounds=[&](juce::Component& c,juce::Rectangle<int> r){ if (apply) c.setBounds(r); };
    auto r=group(std::min(333,available)); bounds(captions_[0],r.removeFromLeft(47)); bounds(target_,r.removeFromLeft(std::max(1,r.getWidth()-96))); r.removeFromLeft(6); bounds(learnButton_,r);
    int modes=0; for (const auto& b : times_) modes+=b.isVisible() ? 1 : 0;
    r=group(36+modes*42); bounds(captions_[1],r.removeFromLeft(36)); for (auto& b : times_) if (b.isVisible()) bounds(b,r.removeFromLeft(42));
    if (steps_.isVisible()) { r=group(114); bounds(captions_[2],r.removeFromLeft(42)); bounds(stepDown_,r.removeFromLeft(20)); bounds(steps_,r.removeFromLeft(32)); bounds(stepUp_,r.removeFromLeft(20)); }
    if (cycles_[0].isVisible()) { r=group(146); bounds(captions_[3],r.removeFromLeft(44)); for (auto& b : cycles_) bounds(b,r.removeFromLeft(51)); }
    r=group(131); r.setX(width-8-131); bounds(power_,r.removeFromLeft(42)); r.removeFromLeft(4); bounds(clear_,r.removeFromLeft(50)); r.removeFromLeft(4); bounds(remove_,r.removeFromLeft(27));
    return (row+1)*32;
}
void ClipAutomationPanel::resized() {
    auto row=juce::Rectangle<int>(8,7,std::max(0,getWidth()-16-(!open_ ? 90 : 0)),26);
    disclose_.setBounds(row.removeFromLeft(158)); add_.setBounds(row.removeFromRight(76)); row.removeFromRight(12);
    if (open_) { for (int i=1;i>=0;--i) snaps_[(size_t)i].setBounds(row.removeFromRight(46)); row.removeFromRight(12); for (int i=2;i>=0;--i) tools_[(size_t)i].setBounds(row.removeFromRight(60)); }
    if (!open_ || !clip() || !lane()) return;
    const int count=(int)clip()->automation.size(),per=std::max(1,(getWidth()-16)/210),rows=(count+per-1)/per;
    for (int i=0;i<count;++i) chips_[(size_t)i].setBounds(8+(i%per)*210,40+(i/per)*32,202,26);
    const int y=40+rows*32+layoutInspector(getWidth(),40+rows*32,true);
    readout_.setBounds(8,y,getWidth()-16,22); canvas_.setBounds(8,y+24,getWidth()-16,std::max(0,getHeight()-(y+24)-10));
}
void ClipAutomationPanel::paint(juce::Graphics& g) {
    auto r=getLocalBounds().toFloat().reduced(.5f); g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0f131b),0,0,juce::Colour(0xff0b0e14),0,(float)getHeight(),false)); g.fillRoundedRectangle(r,9); g.setColour(col::line); g.drawRoundedRectangle(r,9,1);
    if (!clip()) return;
    if (!open_) { g.setColour(col::textHint); g.setFont(monoFont(9)); g.drawText(clip()->automation.empty() ? "DRAW A PARAMETER OVER THE CLIP" : juce::String((int)clip()->automation.size())+" LANES",172,7,std::max(0,getWidth()-360),26,juce::Justification::centredLeft); }
    else if (!lane()) { g.setColour(col::textHint); g.setFont(monoFont(10)); g.drawText("ADD AN AUTOMATION LANE TO DRAW A PARAMETER",getLocalBounds().withTrimmedTop(40),juce::Justification::centred); }
}
double ClipAutomationPanel::baseline() const {
    if (!lane()) return .5;
    if (auto* p=source_.parameter(lane()->target)) return p->getValue();
    return .5;
}
double ClipAutomationPanel::elapsed() const {
    return clip() && automationSource_.elapsed ? automationSource_.elapsed() : -1;
}
juce::String ClipAutomationPanel::timeReadout() const {
    if (!lane() || !clip()) return {};
    const auto c=geometry().cycle; const auto mode=lane()->time.mode; juce::String bars=juce::String(clip()->bars)+" BAR"+(clip()->bars>1 ? "S" : "");
    if (mode==AutoTime::Grid) return "LOOP "+juce::String(c.len)+" x 1/16  /  AGAINST "+bars;
    if (mode==AutoTime::Fit) return juce::String(c.len)+" IN "+juce::String(c.fit/4)+" BAR"+(c.fit>4 ? "S" : "")+"  /  STRAIGHT";
    if (mode==AutoTime::Pad) {
        const int pad=autoPadOf(lane()->target); const bool poly=clip()->hasDrumRhythm && pad>=0 && clip()->drumRhythm.lanes[(size_t)pad].enabled;
        if (!poly) return "PAD IS NOT POLY  /  FOLLOWS "+automationSource_.unit;
        return "PAD POLY  /  "+juce::String(c.len)+(c.fit ? " IN "+juce::String(c.fit/4)+" BAR"+(c.fit>4 ? "S" : "") : " STEPS")+(c.rot ? "  /  ROT "+juce::String(c.rot) : "");
    }
    return "FOLLOWS "+automationSource_.unit+"  /  "+bars;
}
juce::String ClipAutomationPanel::valueReadout(double norm) const {
    if (!lane()) return {};
    const auto* d=autoParamDef(machine_,lane()->target);
    const float value=normToValue(*d,(float)norm);
    if (d->curve==Curve::Log && (lane()->target.find("cut")!=std::string::npos || lane()->target.find("freq")!=std::string::npos)) return value>=1000 ? juce::String(value/1000,2)+" kHz" : juce::String(value,1)+" Hz";
    if (lane()->target.find(".att")!=std::string::npos || lane()->target.find(".dec")!=std::string::npos || lane()->target.find(".rel")!=std::string::npos || lane()->target.find(".time")!=std::string::npos) return value<1 ? juce::String(value*1000,1)+" ms" : juce::String(value,2)+" s";
    return juce::String(value,2);
}
void ClipAutomationPanel::timerCallback() {
    if (isShowing() || learn_) refresh();
}
bool ClipAutomationPanel::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() && (key.getKeyCode()=='Z' || key.getKeyCode()=='z')) {
        auto& action=key.getModifiers().isShiftDown() ? automationSource_.redo : automationSource_.undo;
        if (action) { canvas_.cancel(); action(); signature_.clear(); refresh(); return true; }
    }
    if (key==juce::KeyPress::escapeKey) { canvas_.cancel(); setLearn(false); return true; }
    // Device panel keys must not launch scenes while editing a lane.
    return key.getKeyCode()>=32 && key.getKeyCode()<127 && !key.getModifiers().isCommandDown();
}
} // namespace fui
namespace fui {
float ClipAutomationPanel::Canvas::xOf(double s) const { return (float)(s/panel.geometry().axis*getWidth()); }
float ClipAutomationPanel::Canvas::yOf(double v) const { return (float)(6+(1-v)*(getHeight()-12)); }
ClipAutomationPanel::Canvas::Position ClipAutomationPanel::Canvas::locate(juce::Point<float> p) const {
    const auto geo=panel.geometry(); Position at; at.x=juce::jlimit(0.0f,std::max(0.0f,(float)getWidth()-.01f),p.x);
    at.s=(double)at.x/std::max(1,getWidth())*geo.axis; at.t=geo.toLane(at.s); at.v=std::clamp(1-((double)p.y-6)/std::max(1.0,(double)getHeight()-12),0.0,1.0); return at;
}
double ClipAutomationPanel::Canvas::snapped(double t) const { const double grid=panel.snap_ ? 1 : autoedit::tick; return std::min(panel.geometry().cycle.len-autoedit::tick,std::round(t/grid)*grid); }
int ClipAutomationPanel::Canvas::hit(double s) const {
    if (!panel.lane()) return -1; const auto geo=panel.geometry(); int best=-1; float distance=9;
    for (int i=0;i<(int)panel.lane()->points.size();++i) {
        const auto& p=panel.lane()->points[(size_t)i]; if (p.t>=geo.cycle.len) continue;
        const float d=std::abs(xOf(geo.toClip(p.t))-xOf(autoMod(s,geo.span)));
        if (d<distance) { distance=d; best=i; }
    }
    return best;
}
void ClipAutomationPanel::Canvas::paint(juce::Graphics& g) {
    const auto* lane=panel.lane(); if (!lane) return;
    const auto geo=panel.geometry(); const auto color=panel.color(panel.selected_).withAlpha(lane->enabled ? 1.0f : .4f);
    auto bounds=getLocalBounds().toFloat(); g.setColour(col::display); g.fillRoundedRectangle(bounds,6);
    g.saveState(); g.reduceClipRegion(getLocalBounds());
    g.setColour(col::text.withAlpha(.02f)); g.fillRect(0.0f,0.0f,xOf(geo.span),(float)getHeight());
    if (geo.cycle.fit) for (int i=0;i<geo.cycle.len;++i) if (i%2) { g.setColour(color.withAlpha(.05f)); g.fillRect(xOf(geo.toClip(i)),0.0f,xOf(geo.unit),(float)getHeight()); }
    for (int step=1;step<geo.axis;++step) { g.setColour(col::text.withAlpha(step%16==0 ? .16f : step%4==0 ? .06f : .025f)); g.drawVerticalLine((int)xOf(step),0,(float)getHeight()); }
    for (double value : {.25,.5,.75}) { g.setColour(col::text.withAlpha(.04f)); g.drawHorizontalLine((int)yOf(value),0,(float)getWidth()); }
    const bool audible=autoedit::audible(lane->points,geo.cycle.len);
    if (!audible) {
        const float dash[]{4,4}; juce::Path p; p.startNewSubPath(0,yOf(panel.baseline())); p.lineTo((float)getWidth(),yOf(panel.baseline()));
        g.setColour(col::textDim.withAlpha(.6f)); juce::Path dashed; juce::PathStrokeType(1).createDashedStroke(dashed,p,dash,2); g.fillPath(dashed);
    } else {
        for (int repeat=0;repeat<(int)std::ceil(geo.axis/geo.span);++repeat) {
            const double from=repeat*geo.span,to=std::min(geo.axis,from+geo.span); const int samples=std::max(64,(int)((to-from)/geo.axis*getWidth()/2));
            juce::Path p;
            for (int i=0;i<=samples;++i) { const double s=from+(to-from)*i/samples; const float x=xOf(s),y=yOf(evalLane(lane->points,geo.cycle.len,geo.toLane(std::min(s,to-1e-6)))); if (!i) p.startNewSubPath(x,y); else p.lineTo(x,y); }
            auto area=p; area.lineTo(xOf(to),(float)getHeight()); area.lineTo(xOf(from),(float)getHeight()); area.closeSubPath();
            g.setColour(color.withAlpha(repeat ? .025f : .10f)); g.fillPath(area); g.setColour(color.withAlpha(repeat ? .28f : lane->enabled ? 1.0f : .4f)); g.strokePath(p,juce::PathStrokeType(1.8f));
        }
        for (int i=0;i<(int)lane->points.size();++i) {
            const auto& p=lane->points[(size_t)i]; if (p.t>=geo.cycle.len) continue;
            const float x=xOf(geo.toClip(p.t)),y=yOf(p.v),radius=i==focusedPoint_ ? 5 : 3.5f;
            g.setColour(p.hold || i==focusedPoint_ ? color : col::display); g.fillEllipse(x-radius,y-radius,radius*2,radius*2); g.setColour(color); g.drawEllipse(x-radius,y-radius,radius*2,radius*2,1.5f);
        }
    }
    if (geo.axis>geo.span || geo.span>geo.clipSteps) { g.setColour(color.withAlpha(.5f)); const float x=xOf(geo.axis>geo.span ? geo.span : geo.clipSteps); for (int y=0;y<getHeight();y+=6) g.drawLine(x,(float)y,x,(float)y+3,1); }
    if (drag_==Ramp) { g.setColour(col::text); g.drawLine(xOf(geo.toClip(from_.t)),yOf(from_.v),xOf(geo.toClip(std::min(to_.t,geo.cycle.len-autoedit::tick))),yOf(to_.v),1.5f); }
    const double elapsed=panel.elapsed();
    if (elapsed>=0) {
        g.setColour(col::text.withAlpha(.28f)); g.drawVerticalLine((int)xOf(autoMod(elapsed,geo.clipSteps)),0,(float)getHeight());
        if (lane->enabled) { const double phase=geo.phase(elapsed); const float x=xOf(geo.toClip(phase)); g.setColour(color.withAlpha(.75f)); g.drawVerticalLine((int)x,0,(float)getHeight()); if (audible) { const float y=yOf(evalLane(lane->points,geo.cycle.len,phase)); g.setColour(col::text); g.fillEllipse(x-3,y-3,6,6); } }
    }
    if (hovering_) {
        g.setColour(col::text.withAlpha(.15f)); g.drawVerticalLine((int)hover_.x,0,(float)getHeight()); g.drawHorizontalLine((int)yOf(hover_.v),0,(float)getWidth());
        const double value=drag_!=None || !audible ? hover_.v : evalLane(lane->points,geo.cycle.len,hover_.t);
        auto tip=juce::Rectangle<float>(std::clamp(hover_.x+10,4.0f,std::max(4.0f,(float)getWidth()-140)),6,132,36);
        g.setColour(col::display.withAlpha(.94f)); g.fillRoundedRectangle(tip,4); g.setColour(color.withAlpha(.4f)); g.drawRoundedRectangle(tip,4,1);
        g.setFont(monoFontMedium(10)); g.setColour(color); g.drawText(panel.valueReadout(value),tip.removeFromTop(19),juce::Justification::centred);
        g.setFont(monoFont(8)); g.setColour(col::textHint); g.drawText("STEP "+juce::String(hover_.t+1,2),tip,juce::Justification::centred);
    } else if (!audible) { g.setFont(monoFont(9)); g.setColour(col::textHint.withAlpha(.8f)); g.drawText("DRAW A CURVE OR CLICK TO ADD POINTS",getLocalBounds(),juce::Justification::centred); }
    g.restoreState();
    if (hasKeyboardFocus(true)) { g.setColour(color.withAlpha(.5f)); g.drawRoundedRectangle(bounds.reduced(.5f),6,1); }
}
void ClipAutomationPanel::Canvas::mouseDown(const juce::MouseEvent& e) {
    if (!panel.lane()) return;
    grabKeyboardFocus(); const auto at=locate(e.position);
    if (e.mods.isPopupMenu()) { const int i=hit(at.s); if (i>=0) { panel.beginEdit(); panel.writePoints(autoedit::removePoint(panel.lane()->points,i)); focusedPoint_=-1; } return; }
    if (!e.mods.isLeftButtonDown()) return;
    original_=base_=panel.lane()->points; panel.beginEdit(); const int len=panel.geometry().cycle.len; const double grid=panel.snap_ ? 1 : autoedit::tick;
    from_=to_=last_=at;
    if (panel.tool_==Draw) { drag_=Paint; panel.writePoints(autoedit::paintCell(base_,len,at.t,at.v,grid)); }
    else if (panel.tool_==Line) { drag_=Ramp; from_.t=snapped(at.t); to_=from_; }
    else {
        index_=hit(at.s);
        if (e.mods.isAltDown() && base_.size()>1) {
            drag_=Bend; index_=-1; for (int i=0;i<(int)base_.size();++i) if (base_[(size_t)i].t<=at.t) index_=i;
            if (index_<0) index_=(int)base_.size()-1;
            y0_=e.position.y; c0_=base_[(size_t)index_].c; const double delta=base_[((size_t)index_+1)%base_.size()].v-base_[(size_t)index_].v; dir_=delta<0 ? -1 : 1;
        } else {
            drag_=Move; if (index_<0) { auto added=autoedit::addPoint(base_,snapped(at.t),at.v); base_=std::move(added.first); index_=added.second; panel.writePoints(base_); }
            focusedPoint_=index_;
        }
    }
    repaint();
}
void ClipAutomationPanel::Canvas::mouseDrag(const juce::MouseEvent& e) {
    if (drag_==None || !panel.lane()) return;
    const auto at=locate(e.position); hover_=at; hovering_=true; const int len=panel.geometry().cycle.len; const double grid=panel.snap_ ? 1 : autoedit::tick;
    if (drag_==Paint) {
        auto points=panel.lane()->points;
        const int cells=std::max(1,(int)std::ceil(std::abs(at.t-last_.t)/grid));
        if (std::abs(at.t-last_.t)<len/2.0) for (int i=1;i<=cells;++i) { const double f=(double)i/cells; points=autoedit::paintCell(points,len,last_.t+(at.t-last_.t)*f,last_.v+(at.v-last_.v)*f,grid); }
        else points=autoedit::paintCell(points,len,at.t,at.v,grid);
        panel.writePoints(std::move(points)); last_=at;
    } else if (drag_==Ramp) { to_=at; to_.t=snapped(at.t)+(snapped(at.t)>=from_.t ? grid : 0); }
    else if (drag_==Move && index_>=0) panel.writePoints(autoedit::movePoint(base_,len,index_,e.mods.isShiftDown() ? base_[(size_t)index_].t : snapped(at.t),at.v));
    else if (drag_==Bend) panel.writePoints(autoedit::bendSegment(base_,index_,c0_-(y0_-e.position.y)/70*dir_));
    repaint();
}
void ClipAutomationPanel::Canvas::mouseUp(const juce::MouseEvent&) {
    if (drag_==Ramp && panel.lane()) panel.writePoints(autoedit::drawLine(base_,panel.geometry().cycle.len,from_.t,from_.v,std::min(panel.geometry().cycle.len-autoedit::tick,to_.t),to_.v));
    drag_=None; base_.clear(); original_.clear(); repaint();
}
void ClipAutomationPanel::Canvas::mouseDoubleClick(const juce::MouseEvent& e) {
    if (panel.tool_!=Point || !panel.lane()) return;
    const int i=hit(locate(e.position).s); if (i<0) return;
    if (drag_==None) panel.beginEdit(); drag_=None; base_.clear(); original_.clear(); focusedPoint_=-1; panel.writePoints(autoedit::removePoint(panel.lane()->points,i));
}
void ClipAutomationPanel::Canvas::mouseMove(const juce::MouseEvent& e) { hover_=locate(e.position); hovering_=true; repaint(); }
void ClipAutomationPanel::Canvas::mouseExit(const juce::MouseEvent&) { if (drag_==None) hovering_=false; repaint(); }
void ClipAutomationPanel::Canvas::cancel() { if (drag_!=None && panel.lane()) { drag_=None; panel.writePoints(original_); } drag_=None; base_.clear(); original_.clear(); focusedPoint_=-1; hovering_=false; }
bool ClipAutomationPanel::Canvas::keyPressed(const juce::KeyPress& k) {
    if (k==juce::KeyPress::escapeKey) { cancel(); panel.setLearn(false); return true; }
    if (!panel.lane()) return false;
    if (focusedPoint_>=0 && focusedPoint_<(int)panel.lane()->points.size()) {
        if (k==juce::KeyPress::deleteKey || k==juce::KeyPress::backspaceKey) { panel.beginEdit(); panel.writePoints(autoedit::removePoint(panel.lane()->points,focusedPoint_)); focusedPoint_=-1; return true; }
        const auto p=panel.lane()->points[(size_t)focusedPoint_]; double t=p.t,v=p.v;
        if (k.getKeyCode()==juce::KeyPress::leftKey) t-=panel.snap_ ? 1 : .25;
        else if (k.getKeyCode()==juce::KeyPress::rightKey) t+=panel.snap_ ? 1 : .25;
        else if (k.getKeyCode()==juce::KeyPress::upKey) v+=k.getModifiers().isShiftDown() ? .001 : .01;
        else if (k.getKeyCode()==juce::KeyPress::downKey) v-=k.getModifiers().isShiftDown() ? .001 : .01;
        else return panel.keyPressed(k);
        panel.beginEdit(); panel.writePoints(autoedit::movePoint(panel.lane()->points,panel.geometry().cycle.len,focusedPoint_,t,v)); return true;
    }
    return panel.keyPressed(k);
}
}
