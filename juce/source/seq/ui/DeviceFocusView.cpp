#include "DeviceFocusView.h"
#include "../dsp/ClipLibrary.gen.h"

#include <algorithm>

namespace fui {
namespace {

constexpr int kSaveAction = 1;
constexpr int kImportAction = 2;
constexpr int kExportAction = 3;
constexpr int kTransformActionBase = 100;

const std::array<const char*, 10> kTransformNames {{
    "ROTATE +1", "REVERSE", "DENSITY x2", "DENSITY /2", "SHIFT ACCENTS",
    "HUMANIZE", "EXTRACT BAR 1", "REPEAT 2 BARS", "TRANSPOSE +1", "REMAP DRUMS"
}};

std::function<HostTransport()> wtTransport(WtUiModel& model) {
    return [&model] {
        return HostTransport { model.hostBpm(), 0.0, model.sequencerPlaying() };
    };
}

} // namespace

DeviceFocusView::DeviceFocusView(SeqAudioProcessor& proc)
    : proc_(proc), drumModel_(proc), bassModel_(proc), wt2Model_(proc, 2), wt3Model_(proc, 3),
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

    patchLabel_.setText("PATCH", juce::dontSendNotification);
    patchLabel_.setJustificationType(juce::Justification::centredRight);
    patchLabel_.setColour(juce::Label::textColourId, col::textDim);
    addChildComponent(patchLabel_);

    auto addPatchButton = [this](juce::TextButton& button, int direction) {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        button.setColour(juce::TextButton::textColourOffId, col::text);
        button.onClick = [this, direction] { stepActiveProgram(direction); };
        addChildComponent(button);
    };
    addPatchButton(previousPatchButton_, -1);
    addPatchButton(nextPatchButton_, 1);
    patchSelector_.onChange = [this] {
        const int selected = patchSelector_.getSelectedId() - 1;
        if (selected >= 0 && selected < activeNumPrograms()) selectActiveProgram(selected);
        shownPatchProgram_ = -2;
        refreshPatchSelector();
    };
    addChildComponent(patchSelector_);

    clipTargetLabel_.setJustificationType(juce::Justification::centredRight);
    clipTargetLabel_.setColour(juce::Label::textColourId, col::textDim);
    addChildComponent(clipTargetLabel_);
    clipSourceSelector_.addItem("ALL", 1);
    clipSourceSelector_.addItem("FACTORY", 2);
    clipSourceSelector_.addItem("USER", 3);
    clipSourceSelector_.addItem("IMPORTED", 4);
    clipSourceSelector_.setSelectedId(1, juce::dontSendNotification);
    clipSourceSelector_.onChange = [this] { refreshClipBrowser(true); };
    addChildComponent(clipSourceSelector_);
    clipSelector_.setTextWhenNothingSelected("SELECT CLIP");
    clipSelector_.onChange = [this] {
        selectedClipIndex_ = clipSelector_.getSelectedId() - 1;
        refreshClipMetadata();
        loadSelectedClip();
    };
    addChildComponent(clipSelector_);
    clipMetadataLabel_.setJustificationType(juce::Justification::centredLeft);
    clipMetadataLabel_.setColour(juce::Label::textColourId, col::textDim);
    addChildComponent(clipMetadataLabel_);
    clipActionsSelector_.setTextWhenNothingSelected("CLIP ACTIONS");
    clipActionsSelector_.onChange = [this] {
        const int action = clipActionsSelector_.getSelectedId();
        clipActionsSelector_.setSelectedId(0, juce::dontSendNotification);
        if (action == kSaveAction) saveCurrentClip();
        else if (action == kImportAction) chooseImportClip();
        else if (action == kExportAction) chooseExportClip();
        else if (action >= kTransformActionBase)
            saveSelectedTransform(action - kTransformActionBase);
    };
    addChildComponent(clipActionsSelector_);

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

void DeviceFocusView::reloadPatchesFromSession() {
    drumModel_.reloadPatchFromSession();
    bassModel_.reloadPatchFromSession();
    wt2Model_.reloadPatchFromSession();
    wt3Model_.reloadPatchFromSession();
    shownPatchProgram_ = -2;
    refreshPatchSelector();
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
    refreshPatchSelector();
    refreshClipBrowser(true);
    resized();
    repaint();
}

const fable::ClipLibraryEntry* DeviceFocusView::selectedClip() const {
    return selectedClipIndex_ >= 0 && selectedClipIndex_ < (int)visibleClips_.size()
        ? &visibleClips_[(size_t)selectedClipIndex_].entry : nullptr;
}

void DeviceFocusView::refreshClipBrowser(bool rebuild) {
    const bool visible = activeBody_ != ActiveBody::none;
    for (auto* component : std::initializer_list<juce::Component*> {
             &clipTargetLabel_, &clipSelector_, &clipMetadataLabel_,
             &clipSourceSelector_, &clipActionsSelector_ })
        component->setVisible(visible);
    if (!visible) return;

    const auto& session = proc_.conductor().session();
    if (targetScene_ >= 0 && targetScene_ < (int)session.scenes.size()
        && targetTrack_ >= 0 && targetTrack_ < (int)session.tracks.size()) {
        clipTargetLabel_.setText(
            juce::String(session.scenes[(size_t)targetScene_].name) + " / "
                + juce::String(session.tracks[(size_t)targetTrack_].name),
            juce::dontSendNotification);
    }

    if (rebuild) {
        const auto wanted = session.tracks[(size_t)targetTrack_].machine;
        clipActionsSelector_.clear(juce::dontSendNotification);
        clipActionsSelector_.addItem("SAVE CURRENT", kSaveAction);
        clipActionsSelector_.addItem("IMPORT...", kImportAction);
        clipActionsSelector_.addItem("EXPORT...", kExportAction);
        clipActionsSelector_.addSeparator();
        clipActionsSelector_.addSectionHeading("SAVE TRANSFORM");
        for (int i = 0; i < (int)kTransformNames.size(); ++i) {
            if ((i == 8 && wanted == fable::Machine::DR1)
                || (i == 9 && wanted != fable::Machine::DR1)) continue;
            clipActionsSelector_.addItem(kTransformNames[(size_t)i], kTransformActionBase + i);
        }
        clipSelector_.clear(juce::dontSendNotification);
        visibleClips_.clear();
        selectedClipIndex_ = -1;
        const int sourceFilter = clipSourceSelector_.getSelectedId();
        for (const auto& clip : clipStorage_.all()) {
            if (clip.entry.machine != wanted) continue;
            if ((sourceFilter == 2 && clip.source != fable::ClipSource::factory)
                || (sourceFilter == 3 && clip.source != fable::ClipSource::user)
                || (sourceFilter == 4 && clip.source != fable::ClipSource::imported)) continue;
            visibleClips_.push_back(clip);
            const int index = (int)visibleClips_.size() - 1;
            clipSelector_.addItem(juce::String(clip.entry.name), index + 1);
        }
        clipSelector_.setSelectedId(0, juce::dontSendNotification);
    }
    refreshClipMetadata();
}

void DeviceFocusView::refreshClipMetadata() {
    const auto* entry = selectedClip();
    clipActionsSelector_.setItemEnabled(kSaveAction, activeHasTargetClip());
    clipActionsSelector_.setItemEnabled(kExportAction, entry != nullptr);
    for (int i = 0; i < (int)kTransformNames.size(); ++i)
        clipActionsSelector_.setItemEnabled(kTransformActionBase + i, entry != nullptr);
    if (entry == nullptr) {
        clipMetadataLabel_.setText(visibleClips_.empty() ? "NO COMPATIBLE CLIPS"
                                                         : "SELECT A CLIP TO LOAD",
                                   juce::dontSendNotification);
        return;
    }
    clipMetadataLabel_.setText(
        fable::ClipLibraryStorage::sourceName(visibleClips_[(size_t)selectedClipIndex_].source) + " / "
            + juce::String(entry->family).toUpperCase() + " / "
            + juce::String(entry->role).toUpperCase() + " / E"
            + juce::String(entry->energy) + " / " + juce::String(entry->bars)
            + (entry->bars == 1 ? " BAR" : " BARS"),
        juce::dontSendNotification);
}

void DeviceFocusView::loadSelectedClip() {
    const auto* entry = selectedClip();
    if (entry == nullptr) return;
    flushActiveModel();
    if (!proc_.loadClipLibraryEntry(targetScene_, targetTrack_, *entry, 0)) return;
    retargetModelsAfterLoad();
}

void DeviceFocusView::retargetModelsAfterLoad() {
    // Hosted models read clip bytes directly from the conductor. Retargeting
    // clamps their selected bar after a shorter replacement.
    switch (activeBody_) {
        case ActiveBody::drum: drumModel_.setTargetScene(targetScene_); break;
        case ActiveBody::bass: bassModel_.setTargetScene(targetScene_); break;
        case ActiveBody::wt2: wt2Model_.setTarget(targetScene_); break;
        case ActiveBody::wt3: wt3Model_.setTarget(targetScene_); break;
        case ActiveBody::none: break;
    }
    updateCreateClipButton();
    refreshClipMetadata();
    repaint();
}

void DeviceFocusView::saveCurrentClip() {
    if (!activeHasTargetClip()) return;
    flushActiveModel();
    const auto& session = proc_.conductor().session();
    const auto& clip = session.scenes[(size_t)targetScene_].clips[(size_t)targetTrack_];
    const auto machine = session.tracks[(size_t)targetTrack_].machine;
    fable::ClipLibraryEntry entry;
    entry.id = "user-" + std::to_string(juce::Time::currentTimeMillis()) + "-" + std::to_string(targetTrack_);
    entry.name = clip.name + " COPY";
    int suffix = 2;
    auto all = clipStorage_.all();
    auto collides = [&] { return std::any_of(all.begin(), all.end(), [&](const auto& c) {
        return juce::String(c.entry.name).equalsIgnoreCase(juce::String(entry.name)); }); };
    const auto base = entry.name;
    while (collides()) entry.name = base + " " + std::to_string(suffix++);
    entry.machine = machine; entry.bars = clip.bars; entry.bytes = clip.bytes;
    entry.family = "experimental"; entry.role = fable::sqClipRoles(machine).front();
    entry.energy = 3; entry.transpose = false;
    juce::String error;
    if (clipStorage_.addUser(std::move(entry), &error)) {
        clipSourceSelector_.setSelectedId(3, juce::dontSendNotification);
        refreshClipBrowser(true);
    } else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   "Could not save clip", error);
}

void DeviceFocusView::saveSelectedTransform(int chosen) {
    const auto* selected = selectedClip();
    if (selected == nullptr) return;
    static const std::array<fable::ClipTransformKind, 10> kinds {{
        fable::ClipTransformKind::rotate, fable::ClipTransformKind::reverse,
        fable::ClipTransformKind::densityDouble, fable::ClipTransformKind::densityHalf,
        fable::ClipTransformKind::accentShift, fable::ClipTransformKind::humanize,
        fable::ClipTransformKind::extractBar, fable::ClipTransformKind::repeat,
        fable::ClipTransformKind::transpose, fable::ClipTransformKind::drumLaneRemap }};
    if (chosen < 0 || chosen >= (int)kinds.size()) return;
    try {
        const int amount = chosen == 6 ? 0 : chosen == 7 ? 2 : 1;
        auto entry = fable::transformClipLibraryEntry(*selected, kinds[(size_t)chosen], amount);
        entry.id = "user-" + std::to_string(juce::Time::currentTimeMillis()) + "-transform";
        entry.name += " · " + std::string(kTransformNames[(size_t)chosen]);
        int suffix = 2;
        const auto base = entry.name;
        auto all = clipStorage_.all();
        while (std::any_of(all.begin(), all.end(), [&](const auto& c) {
                   return juce::String(c.entry.name).equalsIgnoreCase(juce::String(entry.name)); }))
            entry.name = base + " " + std::to_string(suffix++);
        juce::String error;
        if (clipStorage_.addUser(std::move(entry), &error)) {
            clipSourceSelector_.setSelectedId(3, juce::dontSendNotification);
            refreshClipBrowser(true);
        } else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Could not transform clip", error);
    } catch (const std::exception& e) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Could not transform clip", e.what());
    }
}

void DeviceFocusView::chooseImportClip() {
    clipChooser_ = std::make_unique<juce::FileChooser>("Import SQ-4 clip", juce::File{}, "*.sqclip");
    juce::Component::SafePointer<DeviceFocusView> safe(this);
    clipChooser_->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File{}) return;
        juce::String error;
        if (safe->clipStorage_.importSqclip(fc.getResult().loadFileAsString(), &error)) {
            safe->clipSourceSelector_.setSelectedId(4, juce::dontSendNotification);
            safe->refreshClipBrowser(true);
        } else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Could not import clip", error);
    });
}

void DeviceFocusView::chooseExportClip() {
    const auto* selected = selectedClip();
    if (selected == nullptr) return;
    const auto copy = *selected;
    clipChooser_ = std::make_unique<juce::FileChooser>("Export SQ-4 clip",
        juce::File(juce::String(copy.id) + ".sqclip"), "*.sqclip");
    juce::Component::SafePointer<DeviceFocusView> safe(this);
    clipChooser_->launchAsync(juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::warnAboutOverwriting, [safe, copy](const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File{}) return;
        juce::String error;
        if (!safe->clipStorage_.exportSqclip(copy, fc.getResult(), &error))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   "Could not export clip", error);
    });
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
    refreshClipMetadata();
    repaint();
}

void DeviceFocusView::updateCreateClipButton() {
    const bool show = activeBody_ != ActiveBody::none && !activeHasTargetClip();
    createClipButton_.setVisible(show);
    if (show) createClipButton_.toFront(false);
}

int DeviceFocusView::activeCurrentProgram() const {
    switch (activeBody_) {
        case ActiveBody::drum: return drumModel_.currentProgram();
        case ActiveBody::bass: return bassModel_.currentProgram();
        case ActiveBody::wt2:  return wt2Model_.currentProgram();
        case ActiveBody::wt3:  return wt3Model_.currentProgram();
        case ActiveBody::none: return -1;
    }
    return -1;
}

int DeviceFocusView::activeNumPrograms() const {
    switch (activeBody_) {
        case ActiveBody::drum: return drumModel_.numPrograms();
        case ActiveBody::bass: return bassModel_.numPrograms();
        case ActiveBody::wt2:  return wt2Model_.numPrograms();
        case ActiveBody::wt3:  return wt3Model_.numPrograms();
        case ActiveBody::none: return 0;
    }
    return 0;
}

juce::String DeviceFocusView::activeProgramName(int index) const {
    switch (activeBody_) {
        case ActiveBody::drum: return drumModel_.programName(index);
        case ActiveBody::bass: return bassModel_.programName(index);
        case ActiveBody::wt2:  return wt2Model_.programName(index);
        case ActiveBody::wt3:  return wt3Model_.programName(index);
        case ActiveBody::none: return {};
    }
    return {};
}

void DeviceFocusView::selectActiveProgram(int index) {
    switch (activeBody_) {
        case ActiveBody::drum: drumModel_.selectProgram(index); break;
        case ActiveBody::bass: bassModel_.selectProgram(index); break;
        case ActiveBody::wt2:  wt2Model_.selectProgram(index); break;
        case ActiveBody::wt3:  wt3Model_.selectProgram(index); break;
        case ActiveBody::none: return;
    }
    shownPatchProgram_ = -2;
}

void DeviceFocusView::stepActiveProgram(int direction) {
    const int count = activeNumPrograms();
    if (count <= 0) return;
    const int current = activeCurrentProgram();
    const int next = current < 0
        ? (direction < 0 ? count - 1 : 0)
        : ((current + direction) % count + count) % count;
    selectActiveProgram(next);
    refreshPatchSelector();
}

void DeviceFocusView::refreshPatchSelector() {
    const bool visible = activeBody_ != ActiveBody::none;
    patchLabel_.setVisible(visible);
    previousPatchButton_.setVisible(visible);
    patchSelector_.setVisible(visible);
    nextPatchButton_.setVisible(visible);
    if (!visible) return;

    const int program = activeCurrentProgram();
    if (shownPatchTrack_ != targetTrack_) {
        patchSelector_.clear(juce::dontSendNotification);
        const int count = activeNumPrograms();
        for (int i = 0; i < count; ++i)
            patchSelector_.addItem(activeProgramName(i), i + 1);
        patchSelector_.addItem("CUSTOM", count + 1);
        patchSelector_.setItemEnabled(count + 1, false);
        shownPatchTrack_ = targetTrack_;
        shownPatchProgram_ = -2;
    }
    if (shownPatchProgram_ != program) {
        const int id = program >= 0 ? program + 1 : activeNumPrograms() + 1;
        patchSelector_.setSelectedId(id, juce::dontSendNotification);
        shownPatchProgram_ = program;
    }
    patchLabel_.toFront(false);
    previousPatchButton_.toFront(false);
    patchSelector_.toFront(false);
    nextPatchButton_.toFront(false);
}

void DeviceFocusView::timerCallback() {
    updateCreateClipButton();
    refreshPatchSelector();
}

void DeviceFocusView::layoutBody(juce::Component& body, int logicalWidth,
                                 int logicalHeight, int contentTop) {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    constexpr int selectorHeight = 38;
    const int contentHeight = logicalHeight - contentTop;
    const float scale = std::min(static_cast<float>(getWidth()) / static_cast<float>(logicalWidth),
                                 static_cast<float>(juce::jmax(1, getHeight() - selectorHeight))
                                     / static_cast<float>(contentHeight));
    const float dx = (static_cast<float>(getWidth()) - static_cast<float>(logicalWidth) * scale) * 0.5f;
    const float dy = selectorHeight
        + (static_cast<float>(getHeight() - selectorHeight)
           - static_cast<float>(contentHeight) * scale) * 0.5f;
    body.setTransform(juce::AffineTransform::translation(0.0f, (float)-contentTop)
                          .scaled(scale).translated(dx, dy));
}

void DeviceFocusView::resized() {
    layoutBody(drumBody_, kDrumWidth, kDrumHeight, 103);
    layoutBody(bassBody_, kBassWidth, kBassHeight, 103);
    layoutBody(wt2Body_, WtDeviceBody::LW, WtDeviceBody::LH);
    layoutBody(wt3Body_, WtDeviceBody::LW, WtDeviceBody::LH);

    constexpr int controlHeight = 28, patchWidth = 205, patchButtonWidth = 26;
    auto toolbar = juce::Rectangle<int>(12, 5, juce::jmax(0, getWidth() - 24), controlHeight);
    patchLabel_.setBounds(toolbar.removeFromLeft(48));
    toolbar.removeFromLeft(6);
    previousPatchButton_.setBounds(toolbar.removeFromLeft(patchButtonWidth));
    toolbar.removeFromLeft(6);
    patchSelector_.setBounds(toolbar.removeFromLeft(patchWidth));
    toolbar.removeFromLeft(6);
    nextPatchButton_.setBounds(toolbar.removeFromLeft(patchButtonWidth));
    toolbar.removeFromLeft(16);

    clipTargetLabel_.setBounds(toolbar.removeFromLeft(155));
    toolbar.removeFromLeft(8);
    clipSourceSelector_.setBounds(toolbar.removeFromLeft(96));
    toolbar.removeFromLeft(6);
    clipSelector_.setBounds(toolbar.removeFromLeft(215));
    toolbar.removeFromLeft(8);
    clipActionsSelector_.setBounds(toolbar.removeFromRight(130));
    toolbar.removeFromRight(8);
    clipMetadataLabel_.setBounds(toolbar);

    constexpr int buttonWidth = 180, buttonHeight = 38;
    createClipButton_.setBounds((getWidth() - buttonWidth) / 2,
                                (getHeight() - buttonHeight) / 2,
                                buttonWidth, buttonHeight);
    if (createClipButton_.isVisible()) createClipButton_.toFront(false);
}

} // namespace fui
