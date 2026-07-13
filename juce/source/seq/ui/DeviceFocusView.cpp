#include "DeviceFocusView.h"

#include <algorithm>

namespace fui {
namespace {

std::function<HostTransport()> wtTransport(WtUiModel& model) {
    return [&model] {
        return HostTransport { model.hostBpm(), 0.0, model.sequencerPlaying() };
    };
}

} // namespace

DeviceFocusView::DeviceFocusView(SeqAudioProcessor& proc)
    : drumModel_(proc), bassModel_(proc), wt2Model_(proc, 2), wt3Model_(proc, 3),
      drumBody_(drumModel_), bassBody_(bassModel_),
      wt2Body_(wt2Model_, wtTransport(wt2Model_)),
      wt3Body_(wt3Model_, wtTransport(wt3Model_)) {
    drumBody_.setBounds(0, 0, kDrumWidth, kDrumHeight);
    bassBody_.setBounds(0, 0, kBassWidth, kBassHeight);
    wt2Body_.setBounds(0, 0, WtDeviceBody::LW, WtDeviceBody::LH);
    wt3Body_.setBounds(0, 0, WtDeviceBody::LW, WtDeviceBody::LH);

    addChildComponent(drumBody_);
    addChildComponent(bassBody_);
    addChildComponent(wt2Body_);
    addChildComponent(wt3Body_);

    createClipButton_.setTooltip("Create an empty clip for this scene and device");
    createClipButton_.onClick = [this] { createActiveClip(); };
    addChildComponent(createClipButton_);

    startTimerHz(10);
}

DeviceFocusView::~DeviceFocusView() {
    stopTimer();
    drumModel_.flushPendingPatch();
    bassModel_.flushPendingPatch();
    wt2Model_.flushPendingPatch();
    wt3Model_.flushPendingPatch();
}

void DeviceFocusView::setTarget(int scene, int track) {
    flushActiveModel();

    for (auto* body : std::initializer_list<juce::Component*> {
             &drumBody_, &bassBody_, &wt2Body_, &wt3Body_ })
        body->setVisible(false);

    targetScene_ = scene;
    targetTrack_ = track;
    activeBody_ = ActiveBody::none;

    if (scene >= 0) {
        switch (track) {
            case 0:
                drumModel_.setTargetScene(scene);
                activeBody_ = ActiveBody::drum;
                drumBody_.setVisible(true);
                break;
            case 1:
                bassModel_.setTargetScene(scene);
                activeBody_ = ActiveBody::bass;
                bassBody_.setVisible(true);
                break;
            case 2:
                wt2Model_.setTarget(scene);
                activeBody_ = ActiveBody::wt2;
                wt2Body_.setVisible(true);
                break;
            case 3:
                wt3Model_.setTarget(scene);
                activeBody_ = ActiveBody::wt3;
                wt3Body_.setVisible(true);
                break;
            default:
                targetScene_ = targetTrack_ = -1;
                break;
        }
    } else {
        targetScene_ = targetTrack_ = -1;
    }

    updateCreateClipButton();
    resized();
    repaint();
}

juce::Component* DeviceFocusView::activeBodyComponent() {
    return const_cast<juce::Component*>(std::as_const(*this).activeBodyComponent());
}

const juce::Component* DeviceFocusView::activeBodyComponent() const {
    switch (activeBody_) {
        case ActiveBody::drum: return &drumBody_;
        case ActiveBody::bass: return &bassBody_;
        case ActiveBody::wt2:  return &wt2Body_;
        case ActiveBody::wt3:  return &wt3Body_;
        case ActiveBody::none: return nullptr;
    }
    return nullptr;
}

void DeviceFocusView::flushActiveModel() {
    switch (activeBody_) {
        case ActiveBody::drum: drumModel_.flushPendingPatch(); break;
        case ActiveBody::bass: bassModel_.flushPendingPatch(); break;
        case ActiveBody::wt2:  wt2Model_.flushPendingPatch(); break;
        case ActiveBody::wt3:  wt3Model_.flushPendingPatch(); break;
        case ActiveBody::none: break;
    }
}

bool DeviceFocusView::activeHasTargetClip() const {
    switch (activeBody_) {
        case ActiveBody::drum: return drumModel_.hasTargetClip();
        case ActiveBody::bass: return bassModel_.hasTargetClip();
        case ActiveBody::wt2:  return wt2Model_.hasTargetClip();
        case ActiveBody::wt3:  return wt3Model_.hasTargetClip();
        case ActiveBody::none: return false;
    }
    return false;
}

void DeviceFocusView::createActiveClip() {
    switch (activeBody_) {
        case ActiveBody::drum: drumModel_.createTargetClip(); break;
        case ActiveBody::bass: bassModel_.createTargetClip(); break;
        case ActiveBody::wt2:  wt2Model_.createTargetClip(); break;
        case ActiveBody::wt3:  wt3Model_.createTargetClip(); break;
        case ActiveBody::none: return;
    }
    updateCreateClipButton();
    repaint();
}

void DeviceFocusView::updateCreateClipButton() {
    const bool show = activeBody_ != ActiveBody::none && !activeHasTargetClip();
    createClipButton_.setVisible(show);
    if (show) createClipButton_.toFront(false);
}

void DeviceFocusView::timerCallback() { updateCreateClipButton(); }

void DeviceFocusView::layoutBody(juce::Component& body, int logicalWidth,
                                 int logicalHeight, int contentTop) {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const int contentHeight = logicalHeight - contentTop;
    const float scale = std::min(static_cast<float>(getWidth()) / static_cast<float>(logicalWidth),
                                 static_cast<float>(getHeight()) / static_cast<float>(contentHeight));
    const float dx = (static_cast<float>(getWidth()) - static_cast<float>(logicalWidth) * scale) * 0.5f;
    const float dy = (static_cast<float>(getHeight()) - static_cast<float>(contentHeight) * scale) * 0.5f;
    body.setTransform(juce::AffineTransform::translation(0.0f, (float)-contentTop)
                          .scaled(scale).translated(dx, dy));
}

void DeviceFocusView::resized() {
    layoutBody(drumBody_, kDrumWidth, kDrumHeight, 103);
    layoutBody(bassBody_, kBassWidth, kBassHeight, 103);
    layoutBody(wt2Body_, WtDeviceBody::LW, WtDeviceBody::LH);
    layoutBody(wt3Body_, WtDeviceBody::LW, WtDeviceBody::LH);

    constexpr int buttonWidth = 180, buttonHeight = 38;
    createClipButton_.setBounds((getWidth() - buttonWidth) / 2,
                                (getHeight() - buttonHeight) / 2,
                                buttonWidth, buttonHeight);
    if (createClipButton_.isVisible()) createClipButton_.toFront(false);
}

} // namespace fui
