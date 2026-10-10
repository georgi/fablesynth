#include "PadGrid.h"
#include "PolyLanePanel.h"
#include "RemoteSampleSources.h"
#include "../../dsp/UserTables.h"
#include "../../agent/AgentPanel.h"

namespace fui {

static float parameterValue(DrumUiModel& model, const juce::String& id) {
    auto* p = model.parameters().parameter(id);
    return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
}

// Web timings/velocities (PadGrid.tsx, store.ts, useDrumKeys.ts).
static constexpr int    kLitMs    = 180;    // pad-led.lit window
static constexpr float  kClickVel = 0.8f;   // store.selectPad audition
static constexpr float  kKeyVel   = 0.85f;  // useDrumKeys trigger
static constexpr int    kGap      = 8;      // .pad-grid gap

// QWERTY map — useDrumKeys.ts KEYMAP flattened so indexOfChar == pad index:
// zxcv = pads 0-3, asdf = 4-7, qwer = 8-11, 1234 = 12-15.
static const juce::String kKeyOrder("zxcvasdfqwer1234");

class RemoteImportPopover final : public juce::Component {
public:
    explicit RemoteImportPopover(std::function<void(juce::String)> onImport)
        : onImport_(std::move(onImport)) {
        url_.setFont(monoFont(11.0f));
        url_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a0d13));
        url_.setColour(juce::TextEditor::textColourId, col::text);
        url_.setColour(juce::TextEditor::outlineColourId, col::line);
        url_.setTextToShowWhenEmpty("https://…/direct-audio-file.wav", col::textDim);
        url_.onReturnKey = [this] { submit(); };
        addAndMakeVisible(url_);

        for (int index = 0; index < (int) dr1Cc0SampleSources().size(); ++index)
            sources_.addItem(dr1Cc0SampleSources()[(size_t) index].name, index + 1);
        sources_.setSelectedId(1, juce::dontSendNotification);
        sources_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff11141c));
        sources_.setColour(juce::ComboBox::textColourId, col::text);
        sources_.setColour(juce::ComboBox::outlineColourId, col::line);
        addAndMakeVisible(sources_);

        auto style = [](juce::TextButton& button, juce::Colour text) {
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
            button.setColour(juce::TextButton::textColourOffId, text);
        };
        style(openSource_, col::textDim);
        style(import_, col::acA);
        openSource_.onClick = [this] {
            const int i = juce::jlimit(0, (int) dr1Cc0SampleSources().size() - 1, sources_.getSelectedId() - 1);
            juce::URL(dr1Cc0SampleSources()[(size_t) i].url).launchInDefaultBrowser();
        };
        import_.onClick = [this] { submit(); };
        addAndMakeVisible(openSource_);
        addAndMakeVisible(import_);
        setSize(480, 170);
    }

    void paint(juce::Graphics& g) override {
        drawPanel(g, getLocalBounds().toFloat(), 8.0f);
        g.setColour(col::acA); g.setFont(dispFont(9.0f));
        drawSpaced(g, "REMOTE AUDIO IMPORT", { 14, 10, getWidth() - 28, 15 }, 1.6f);
        g.setColour(col::textHint); g.setFont(monoFont(8.0f));
        g.drawText("CC0 SOURCES OPEN IN YOUR BROWSER. PASTE A DIRECT HTTPS AUDIO LINK.",
                   juce::Rectangle<int>{ 14, 30, getWidth() - 28, 16 }, juce::Justification::centredLeft, true);
        g.setColour(col::textDim); g.setFont(monoFont(7.0f));
        g.drawText("32 MB MAX · DIRECT FILES ONLY · IMPORTS AS A PAD WAVETABLE",
                   juce::Rectangle<int>{ 14, getHeight() - 28, getWidth() - 28, 14 }, juce::Justification::centredLeft, true);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 0);
        area.removeFromTop(51);
        url_.setBounds(area.removeFromTop(29));
        area.removeFromTop(8);
        auto row = area.removeFromTop(27);
        sources_.setBounds(row.removeFromLeft(244));
        row.removeFromLeft(8);
        openSource_.setBounds(row.removeFromLeft(100));
        row.removeFromLeft(8);
        import_.setBounds(row);
    }

private:
    void submit() {
        if (onImport_) onImport_(url_.getText().trim());
    }

    std::function<void(juce::String)> onImport_;
    juce::TextEditor url_;
    juce::ComboBox sources_;
    juce::TextButton openSource_ { "OPEN CC0" }, import_ { "IMPORT" };
};

PadGrid::PadGrid(DrumUiModel& p) : proc(p) {
    formatMgr.registerBasicFormats();
    for (int i = 0; i < fable::DR_NPADS; ++i) lastTag_[(size_t)i] = -1;
    setInterceptsMouseClicks(true, false);
    startTimerHz(fui::kAnimationHz);
}

#ifndef FABLE_HOSTED_UI
PadGrid::PadGrid(DrumAudioProcessor& p)
    : ownedModel(makeStandaloneDrumUiModel(p)), proc(*ownedModel) {
    formatMgr.registerBasicFormats();
    for (int i = 0; i < fable::DR_NPADS; ++i) lastTag_[(size_t)i] = -1;
    setInterceptsMouseClicks(true, false);
    startTimerHz(fui::kAnimationHz);
}
#endif

PadGrid::~PadGrid() {
    stopTimer();
    if (keyHost_ != nullptr) keyHost_->removeKeyListener(this);
}

// Register on the top-level component so pads play regardless of which child
// has keyboard focus (the web listens on window).
void PadGrid::parentHierarchyChanged() {
    auto* top = getTopLevelComponent();
    if (top == this) top = nullptr;
    if (top == keyHost_.getComponent()) return;
    if (keyHost_ != nullptr) keyHost_->removeKeyListener(this);
    keyHost_ = top;
    if (keyHost_ != nullptr) keyHost_->addKeyListener(this);
}

// ---- geometry --------------------------------------------------------------
// Panel box (.panel): padding 8px 12px 10px; head 15px + 8px margin; then the
// 4x4 grid with 8px gaps (352x369 panel -> 76px square tiles, like the web).
juce::Rectangle<int> PadGrid::tileBounds(int i) const {
    auto r = getLocalBounds();
    r.removeFromLeft(12);  r.removeFromRight(12);
    r.removeFromTop(8);    r.removeFromBottom(10);
    r.removeFromTop(15 + 8);
    const float tw = (float)(r.getWidth()  - 3 * kGap) / 4.0f;
    const float th = (float)(r.getHeight() - 3 * kGap) / 4.0f;
    const int row = 3 - i / 4, col = i % 4;  // pad 01 bottom-left
    return juce::Rectangle<float>((float)r.getX() + (float)col * (tw + kGap),
                                  (float)r.getY() + (float)row * (th + kGap),
                                  tw, th).toNearestInt();
}

int PadGrid::padAt(juce::Point<int> pos) const {
    for (int i = 0; i < fable::DR_NPADS; ++i)
        if (tileBounds(i).contains(pos)) return i;
    return -1;
}

// ---- interaction ------------------------------------------------------------
void PadGrid::flash(int i) {
    hitMs_[(size_t)i] = juce::Time::getMillisecondCounter();
    repaint(tileBounds(i));
}

void PadGrid::mouseDown(const juce::MouseEvent& e) {
    const int i = padAt(e.getPosition());
    if (i < 0) return;
    if (e.mods.isPopupMenu()) { showImportMenu(i, e.getScreenPosition()); return; }
    proc.selectPad(i);          // web store.selectPad: select + audition
    proc.triggerPad(i, kClickVel);
    flash(i);
    repaint();                       // selection ring moved
}

bool PadGrid::keyPressed(const juce::KeyPress& k, juce::Component*) {
    // Agent controls and text fields must never audition pads via shortcuts.
    if (auto* focus = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<PolyLanePanel*>(focus) != nullptr
            || focus->findParentComponentOfClass<PolyLanePanel>() != nullptr
            || dynamic_cast<juce::TextEditor*>(focus) != nullptr
            || focus->findParentComponentOfClass<juce::TextEditor>() != nullptr
            || dynamic_cast<fable::AgentPanel*>(focus) != nullptr
            || focus->findParentComponentOfClass<fable::AgentPanel>() != nullptr)
            return false;
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) {   // useDrumKeys: Escape = stop
        proc.setSequencerPlaying(false);
        return true;
    }
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    const int i = kKeyOrder.indexOfChar(c);
    if (i < 0) return false;
    if (heldKeys_.count(k.getKeyCode()) > 0) return true; // swallow OS auto-repeat
    heldKeys_.insert(k.getKeyCode());
    proc.triggerPad(i, kKeyVel);
    flash(i);
    return true;
}

// ---- drop-WAV import ---------------------------------------------------------
static bool isAudioFile(const juce::String& path) {
    const auto ext = path.fromLastOccurrenceOf(".", false, false).toLowerCase();
    return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac" || ext == "mp3";
}

bool PadGrid::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (isAudioFile(f)) return true;
    return false;
}

void PadGrid::fileDragEnter(const juce::StringArray&, int x, int y) {
    dragOver_ = padAt({ x, y });
    repaint();
}
void PadGrid::fileDragMove(const juce::StringArray&, int x, int y) {
    const int i = padAt({ x, y });
    if (i != dragOver_) { dragOver_ = i; repaint(); }
}
void PadGrid::fileDragExit(const juce::StringArray&) {
    if (dragOver_ != -1) { dragOver_ = -1; repaint(); }
}

void PadGrid::filesDropped(const juce::StringArray& files, int x, int y) {
    const int i = padAt({ x, y });
    dragOver_ = -1;
    repaint();
    if (i < 0 || files.isEmpty()) return;
    for (const auto& f : files)          // web takes the first (audio) file
        if (isAudioFile(f)) { importFile(juce::File(f), i); break; }
}

// Web PadGrid.dropFile: decode -> mixToMono -> detectCycleLength ->
// sliceToFrames -> buildUserTable(name = file name upper-cased, 14 chars).
bool PadGrid::importFile(const juce::File& file, int padIndex) {
    std::unique_ptr<juce::AudioFormatReader> reader(formatMgr.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;
    // Analysis only needs a bounded window; cap ~10 s so a long clip can't
    // blow up the allocation (WT-1 WavetableEditor precedent).
    const auto maxSamples = (juce::int64)(reader->sampleRate * 10.0);
    const int n = (int)juce::jmin(maxSamples, reader->lengthInSamples);
    juce::AudioBuffer<float> buf((int)reader->numChannels, juce::jmax(1, n));
    reader->read(&buf, 0, n, 0, true, true);

    const auto mono = fable::mixToMono(buf.getArrayOfReadPointers(), buf.getNumChannels(), n);
    auto frames = fable::sliceToFrames(mono, fable::detectCycleLength(mono, reader->sampleRate));
    if (frames.empty()) frames = fable::singleCycleFrame(mono);
    if (frames.empty()) return false;

    auto name = file.getFileName().toUpperCase().substring(0, 14); // web fileTableName
    if (name.isEmpty()) name = "USER";
    if (proc.addUserTableForPad(padIndex, fable::makeUserTable(name.toStdString(), frames)) < 0)
        return false;
    flash(padIndex);
    return true;
}

void PadGrid::showImportMenu(int padIndex, juce::Point<int> screenPosition) {
    juce::PopupMenu cc0;
    for (int index = 0; index < (int) dr1Cc0SampleSources().size(); ++index)
        cc0.addItem(100 + index, dr1Cc0SampleSources()[(size_t) index].name);
    juce::PopupMenu menu;
    menu.addItem(1, "IMPORT LOCAL AUDIO…");
    menu.addItem(2, "IMPORT HTTPS AUDIO…");
    menu.addSeparator();
    menu.addSubMenu("CC0 SOURCE LIBRARY", cc0, true);
    auto safe = juce::Component::SafePointer<PadGrid>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPosition.x, screenPosition.y, 1, 1 }),
                       [safe, padIndex](int choice) {
        if (safe == nullptr || choice == 0) return;
        if (choice == 1) {
            safe->localChooser_ = std::make_unique<juce::FileChooser>("Import audio into pad wavetable", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
            safe->localChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe, padIndex](const juce::FileChooser& chooser) {
                    if (safe != nullptr && chooser.getResult().existsAsFile()) safe->importFile(chooser.getResult(), padIndex);
                });
        } else if (choice == 2) {
            safe->showRemoteImport(padIndex);
        } else if (choice >= 100 && choice < 100 + (int) dr1Cc0SampleSources().size()) {
            juce::URL(dr1Cc0SampleSources()[(size_t) (choice - 100)].url).launchInDefaultBrowser();
        }
    });
}

void PadGrid::showRemoteImport(int padIndex) {
    auto safe = juce::Component::SafePointer<PadGrid>(this);
    auto* parent = getTopLevelComponent();
    const auto target = parent != nullptr ? parent->getLocalArea(this, getLocalBounds()) : getScreenBounds();
    juce::CallOutBox::launchAsynchronously(std::make_unique<RemoteImportPopover>([safe, padIndex](juce::String url) {
        if (safe != nullptr) safe->importRemoteUrl(std::move(url), padIndex);
    }), target, parent);
}

void PadGrid::importRemoteUrl(juce::String urlText, int padIndex) {
    urlText = urlText.trim();
    if (!urlText.startsWithIgnoreCase("https://") || !juce::URL::isProbablyAWebsiteURL(urlText)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "DR-1 remote import",
                                                "Paste a direct HTTPS link to an audio file.");
        return;
    }
    if (remoteImport_ != nullptr) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "DR-1 remote import",
                                                "An import is already in progress.");
        return;
    }
    auto safe = juce::Component::SafePointer<PadGrid>(this);
    remoteImport_ = std::make_unique<RemoteAudioDownload>(juce::URL(urlText), [safe, padIndex](RemoteAudioDownload::Result result) {
        if (safe != nullptr) safe->finishRemoteImport(std::move(result), padIndex);
        else if (result.file.existsAsFile()) result.file.deleteFile();
    });
    remoteImport_->begin();
}

void PadGrid::finishRemoteImport(RemoteAudioDownload::Result result, int padIndex) {
    const bool imported = result.succeeded() && importFile(result.file, padIndex);
    if (result.file.existsAsFile()) result.file.deleteFile();
    remoteImport_.reset();
    if (!imported) {
        const auto message = result.error.isNotEmpty() ? result.error
            : "The file could not be decoded as supported audio, or the user table pool is full.";
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "DR-1 remote import", message);
    }
}

// ---- animation --------------------------------------------------------------
void PadGrid::timerCallback() {
    // Hits reported by the audio thread (MIDI, sequencer, auditions).
    const juce::uint32 flags = proc.consumeHitFlags();
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < fable::DR_NPADS; ++i)
        if ((flags & (1u << i)) != 0) hitMs_[(size_t)i] = now;

    // Auto-repeat filter bookkeeping: physical key released -> allow re-trigger.
    for (auto it = heldKeys_.begin(); it != heldKeys_.end();)
        it = juce::KeyPress::isKeyCurrentlyDown(*it) ? std::next(it) : heldKeys_.erase(it);

    if (!isVisibleInHierarchy(*this)) return;

    // Repaint only when something visible changed (LED window, tag, selection).
    juce::uint32 lit = 0;
    bool tagChanged = false;
    for (int i = 0; i < fable::DR_NPADS; ++i) {
        if (now - hitMs_[(size_t)i] < (juce::uint32)kLitMs) lit |= 1u << i;
        const auto pre = "pad" + juce::String(i) + ".";
        const int choke = (int)std::lround(parameterValue(proc, pre + "choke"));
        const int out   = (int)std::lround(parameterValue(proc, pre + "out"));
        const int tag = (choke << 8) | out;
        if (tag != lastTag_[(size_t)i]) { lastTag_[(size_t)i] = tag; tagChanged = true; }
    }
    const int sel = proc.selectedPad();
    if (lit != lastLit_ || sel != lastSel_ || tagChanged) {
        lastLit_ = lit;
        lastSel_ = sel;
        repaint();
    }
}

// ---- paint -------------------------------------------------------------------
void PadGrid::paintPanel(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // .panel-head: h2 "PADS" (accent a), right-aligned dim hint
    auto head = getLocalBounds();
    head.removeFromLeft(12); head.removeFromRight(12); head.removeFromTop(8);
    head = head.removeFromTop(15);
    g.setColour(col::acA);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "PADS", head, 2.2f);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    // Right-click offers CC0 source bookmarks and a direct-HTTPS importer.
    // ASCII arrows avoid the default mono font's fallback box for U+2192.
    drawSpaced(g, "DROP WAV · RIGHT-CLICK SOURCE", head, 1.0f, juce::Justification::right);

}

void PadGrid::paintPadUnder(juce::Graphics& g, int i, int sel, int drag) {
    const auto t = tileBounds(i).toFloat();
    // .pad body: vertical gradient #1c212d -> #10131b, radius 10
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c212d), t.getX(), t.getY(),
                                           juce::Colour(0xff10131b), t.getX(), t.getBottom(),
                                           false));
    g.fillRoundedRectangle(t, 10.0f);

    if (i == sel) {          // .pad.sel: cyan border + halo
        g.setColour(col::acA.withAlpha(0.3f));
        g.drawRoundedRectangle(t.expanded(1.0f), 11.0f, 2.0f);
        g.setColour(col::acA);
        g.drawRoundedRectangle(t.reduced(0.5f), 10.0f, 1.0f);
    } else if (i == drag) {  // .pad.drag-over: amber
        g.setColour(col::acB.withAlpha(0.35f));
        g.drawRoundedRectangle(t.expanded(1.0f), 11.0f, 2.0f);
        g.setColour(col::acB);
        g.drawRoundedRectangle(t.reduced(0.5f), 10.0f, 1.0f);
    } else {
        g.setColour(col::line);
        g.drawRoundedRectangle(t.reduced(0.5f), 10.0f, 1.0f);
    }

    // .pad-num top-left
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    auto numArea = t.toNearestInt().withTrimmedLeft(8).withTrimmedTop(7);
    drawSpaced(g, juce::String(i + 1).paddedLeft('0', 2), numArea.withHeight(10), 1.0f);

}

void PadGrid::paintPadOver(juce::Graphics& g, int i, const juce::String& name, int choke, int out) {
    const auto t = tileBounds(i).toFloat();
    // .pad-meta: name over choke/out tag, anchored bottom-left
    const auto meta = t.toNearestInt().reduced(7, 0).withTrimmedBottom(7);
    g.setColour(col::text);
    g.setFont(monoFont(9.0f));
    g.drawText(name, meta.withTop(meta.getBottom() - 29).withHeight(16),
               juce::Justification::centredLeft, true);

    const auto& names = choke > 0 ? fable::CHOKE_NAMES : fable::OUT_NAMES;
    const int nameIdx = juce::jlimit(0, (int)names.size() - 1, choke > 0 ? choke : out);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, juce::String(names[(size_t)nameIdx]),
               meta.withTop(meta.getBottom() - 9), 0.9f);
}

void PadGrid::paintPadLed(juce::Graphics& g, int i, juce::uint32 now) {
    const auto t = tileBounds(i).toFloat();
    // .pad-led top-right, 7px, lit for 180 ms after a hit
    const bool litNow = now - hitMs_[(size_t)i] < (juce::uint32)kLitMs;
    juce::Rectangle<float> led(t.getRight() - 8.0f - 7.0f, t.getY() + 8.0f, 7.0f, 7.0f);
    if (litNow) {
        g.setColour(col::acA.withAlpha(0.5f));
        g.fillEllipse(led.expanded(2.5f));
        g.setColour(col::acA);
    } else {
        g.setColour(juce::Colour(0xff232936));
    }
    g.fillEllipse(led);

}

void PadGrid::paintDirect(juce::Graphics& g) {
    paintPanel(g);
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < fable::DR_NPADS; ++i) {
        paintPadUnder(g, i, proc.selectedPad(), dragOver_);
        paintPadLed(g, i, now);
        const auto pre = "pad" + juce::String(i) + ".";
        paintPadOver(g, i, proc.padName(i),
            (int)std::lround(parameterValue(proc, pre + "choke")),
            (int)std::lround(parameterValue(proc, pre + "out")));
    }
}

void PadGrid::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0 || !g.clipRegionIntersects(getLocalBounds())) return;
    const float physicalScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float scale = std::isfinite(physicalScale) && physicalScale > 0.0f ? physicalScale : 1.0f;
    // The Graphics API exposes physical scale but not the current device
    // translation. A cached raster would lose the subpixel phase of hosted
    // layouts (and shift glyph/rounded-edge coverage). Keep JUCE's original
    // vector paint whenever that phase cannot be represented exactly.
    bool aligned = scale == std::floor(scale);
    for (juce::Component* component = this; aligned && component != nullptr; component = component->getParentComponent()) {
        const auto transform = component->getTransform();
        aligned = transform.mat00 == std::floor(transform.mat00)
            && transform.mat11 == std::floor(transform.mat11)
            && transform.mat01 == 0.0f && transform.mat10 == 0.0f
            && transform.mat02 == std::floor(transform.mat02)
            && transform.mat12 == std::floor(transform.mat12);
    }
    if (!aligned) {
        paintDirect(g);
        return;
    }
    const bool geometryChanged = cacheSize_ != getLocalBounds() || cacheScale_ != scale;
    if (geometryChanged) {
        cacheSize_ = getLocalBounds();
        cacheScale_ = scale;
        panelLayer_ = {};
        for (auto& pad : padLayers_) { pad.under = {}; pad.over = {}; }
    }
    // Tile layer origins are snapped in physical pixels, retaining the same
    // raster phase as the component-wide panel, including fractional scales.
    auto build = [&](juce::Image& image, juce::Rectangle<int> region, auto&& draw) {
        const int x = (int)std::floor(region.getX() * scale);
        const int y = (int)std::floor(region.getY() * scale);
        const int right = (int)std::ceil(region.getRight() * scale);
        const int bottom = (int)std::ceil(region.getBottom() * scale);
        image = juce::Image(juce::Image::ARGB, juce::jmax(1, right - x), juce::jmax(1, bottom - y), true);
        juce::Graphics cached(image);
        cached.addTransform(juce::AffineTransform::scale(scale).translated((float)-x, (float)-y));
        cached.reduceClipRegion(getLocalBounds());
        draw(cached);
        ++cacheBuildCount_;
    };
    juce::Graphics::ScopedSaveState save(g);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    auto blit = [&](const juce::Image& image, juce::Rectangle<int> region) {
        g.setColour(juce::Colours::white);
        const float x = std::floor(region.getX() * scale) / scale;
        const float y = std::floor(region.getY() * scale) / scale;
        g.drawImageTransformed(image, juce::AffineTransform::scale(1.0f / scale).translated(x, y));
    };
    if (!panelLayer_.isValid()) build(panelLayer_, getLocalBounds(), [&](auto& cached) { paintPanel(cached); });
    blit(panelLayer_, getLocalBounds());
    const auto now = juce::Time::getMillisecondCounter();
    const int sel = proc.selectedPad();
    for (int i = 0; i < fable::DR_NPADS; ++i) {
        // Halo extends two logical pixels beyond the tile. Keep a spare pixel
        // for antialiasing, and rebuild clipped tiles fully rather than baking
        // the caller's partial repaint into the image.
        const auto tile = tileBounds(i);
        const auto meta = tile.reduced(7, 0).withTrimmedBottom(7);
        const auto region = tile.expanded(3)
            .getUnion(tile.withTrimmedLeft(8).withTrimmedTop(7).withHeight(10).withWidth(32))
            .getUnion(meta.withTop(meta.getBottom() - 29).withHeight(16))
            .getUnion(meta.withTop(meta.getBottom() - 9).withWidth(64))
            .getUnion(juce::Rectangle<float>((float)tile.getRight() - 15.0f, (float)tile.getY() + 8.0f, 7.0f, 7.0f).expanded(2.5f).getSmallestIntegerContainer())
            .expanded(1)
            .getIntersection(getLocalBounds());
        if (region.isEmpty() || !g.clipRegionIntersects(region)) continue;
        auto& pad = padLayers_[(size_t)i];
        const int border = i == sel ? 1 : i == dragOver_ ? 2 : 0;
        if (!pad.under.isValid() || pad.border != border) {
            pad.border = border;
            build(pad.under, region, [&](auto& cached) { paintPadUnder(cached, i, sel, dragOver_); });
        }
        blit(pad.under, region);
        paintPadLed(g, i, now);
        const auto pre = "pad" + juce::String(i) + ".";
        const int choke = (int)std::lround(parameterValue(proc, pre + "choke"));
        const int out = (int)std::lround(parameterValue(proc, pre + "out"));
        const auto name = proc.padName(i);
        if (!pad.over.isValid() || pad.name != name || pad.choke != choke || pad.out != out) {
            pad.name = name; pad.choke = choke; pad.out = out;
            build(pad.over, region, [&](auto& cached) { paintPadOver(cached, i, name, choke, out); });
        }
        blit(pad.over, region);
    }
}

} // namespace fui
