#include "PresetBrowser.h"

#include <algorithm>

namespace fui {

namespace {
constexpr int kFilterW = 200, kDetailW = 320, kRowH = 44, kHeadH = 33, kPad = 14;
constexpr int kGlyphW = 112, kGlyphH = 22, kHeartW = 34;

juce::Colour modeColour(PresetBrowser::Mode m) {
    switch (m) {
        case PresetBrowser::Mode::Kept:
        case PresetBrowser::Mode::Audition: return col::acN;
        case PresetBrowser::Mode::Variant:  return col::acF;
        case PresetBrowser::Mode::B:        return col::acB;
    }
    return col::acN;
}
float textWidth(const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth(f, s); }
const char* modeLabel(PresetBrowser::Mode m) {
    switch (m) {
        case PresetBrowser::Mode::Kept:      return "KEPT";
        case PresetBrowser::Mode::Audition:  return "AUDITION";
        case PresetBrowser::Mode::Variant:   return "VARIANT";
        case PresetBrowser::Mode::B:         return "B \xc2\xb7 KEPT";
    }
    return "";
}

juce::String joinMeta(const BrowserItem& e) {
    juce::StringArray parts;
    if (e.user) return "user";
    if (e.badge.isNotEmpty()) parts.add("imported");
    if (e.role.isNotEmpty()) parts.add(e.role);
    parts.addArray(e.tags);
    return parts.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 "));
}

juce::File favoritesFile() { return BrowserFavorites::overrideFile(); }

std::unique_ptr<juce::PropertiesFile> openSettings() {
    if (favoritesFile() != juce::File())
        return std::make_unique<juce::PropertiesFile>(favoritesFile(), juce::PropertiesFile::Options{});
    juce::PropertiesFile::Options o;
    o.applicationName = "FableSynth";
    o.filenameSuffix = ".settings";
    o.folderName = "FableSynth";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return std::make_unique<juce::PropertiesFile>(o);
}

juce::String favKey(const BrowserItem& e) { return (e.user ? "u:" : "f:") + e.name; }
} // namespace

// ---- helpers -----------------------------------------------------------------

juce::StringArray tagWords(const char* s) {
    juce::StringArray out;
    out.addTokens(juce::String(s), " ", "");
    out.removeEmptyStrings();
    return out;
}

std::vector<BrowserItem> makeBrowserItems(const fable::browserTags::Table& table, const std::vector<juce::String>& names) {
    std::vector<BrowserItem> out;
    out.reserve(names.size());
    for (const auto& name : names) {
        BrowserItem item;
        item.name = name;
        for (std::size_t i = 0; i < table.count; ++i) {
            if (name == table.entries[i].name) {
                item.role = table.entries[i].role;
                item.tags = tagWords(table.entries[i].tags);
                break;
            }
        }
        out.push_back(std::move(item));
    }
    return out;
}

juce::File& BrowserFavorites::overrideFile() { static juce::File f; return f; }

std::set<juce::String> BrowserFavorites::load(const juce::String& key) {
    std::set<juce::String> out;
    if (auto pf = openSettings()) {
        juce::StringArray lines;
        lines.addLines(pf->getValue(key));
        for (const auto& l : lines) if (l.isNotEmpty()) out.insert(l);
    }
    return out;
}

void BrowserFavorites::save(const juce::String& key, const std::set<juce::String>& values) {
    if (auto pf = openSettings()) {
        juce::StringArray lines;
        for (const auto& v : values) lines.add(v);
        pf->setValue(key, lines.joinIntoString("\n"));
        pf->saveIfNeeded();
    }
}

// ---- PresetBrowser -----------------------------------------------------------

PresetBrowser::PresetBrowser(BrowserAdapter& a) : a_(a), list_("browser-list", this) {
    setOpaque(false);
    setVisible(false);

    search_.setMultiLine(false);
    search_.setTextToShowWhenEmpty("Search", col::textDim);
    search_.setFont(monoFont(12.0f));
    search_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    search_.setColour(juce::TextEditor::textColourId, col::text);
    search_.setColour(juce::TextEditor::outlineColourId, col::line);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, accentA());
    search_.setColour(juce::CaretComponent::caretColourId, accentA());
    search_.setIndents(9, 6);
    search_.onTextChange = [this] { setQuery(search_.getText()); };
    addAndMakeVisible(search_);

    auto makeChip = [this](const juce::String& label, std::function<void()> action) {
        auto chip = std::make_unique<BrowserButton>(label, BrowserButton::Chip);
        chip->setClickingTogglesState(true);
        chip->onClick = std::move(action);
        addAndMakeVisible(*chip);
        return chip;
    };
    for (const auto& r : a_.roles()) roleChips_.push_back(makeChip(r, [this, r] { toggleRole(r); }));
    for (const auto& c : a_.characters()) charChips_.push_back(makeChip(c, [this, c] { toggleCharacter(c); }));
    favoritesChip_.setClickingTogglesState(true);
    favoritesChip_.onClick = [this] { toggleFavoritesOnly(); };
    addAndMakeVisible(favoritesChip_);
    if (a_.importLabel().isNotEmpty()) {
        importBtn_.setButtonText(a_.importLabel());
        importBtn_.setName(a_.importLabel());
        importBtn_.onClick = [this] { if (a_.importRunning()) a_.cancelImport(); else a_.startImport(); };
        addAndMakeVisible(importBtn_);
    }
    itemsRevision_ = a_.itemsRevision();

    list_.setRowHeight(kRowH);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list_.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list_.setWantsKeyboardFocus(false);   // no keyboard shortcuts
    list_.getViewport()->setScrollBarThickness(8);
    addAndMakeVisible(list_);

    keepBtn_.onClick = [this] { keep(); };
    revertBtn_.onClick = [this] { revert(); };
    abBtn_.onClick = [this] { toggleAB(); };
    varyBtn_.onClick = [this] { vary(); };
    phraseBtn_.setClickingTogglesState(true);
    phraseBtn_.onClick = [this] { setPhrase(phraseBtn_.getToggleState()); };
    closeBtn_.onClick = [this] { close(); };
    for (auto* b : { &keepBtn_, &revertBtn_, &abBtn_, &varyBtn_, &phraseBtn_, &closeBtn_ }) addAndMakeVisible(*b);
    varyBtn_.setVisible(a_.canVary());
    abBtn_.setClickingTogglesState(false);

    scheduler_.tick = [this] { tickAudition(); };
    startVisualTimer(*this, 15);
}

PresetBrowser::~PresetBrowser() { stopAudition(); scheduler_.stopTimer(); }

int PresetBrowser::shownItem() const {
    return mode_ == Mode::B && audition_ ? audition_->index : a_.currentIndex();
}

void PresetBrowser::setAccent(juce::Colour c) {
    accent_ = c;
    for (auto& chip : roleChips_) chip->setAccent(c);
    for (auto& chip : charChips_) chip->setAccent(c);
    for (auto* b : { &favoritesChip_, &keepBtn_, &revertBtn_, &abBtn_, &varyBtn_, &phraseBtn_, &closeBtn_ }) b->setAccent(c);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, c);
    search_.setColour(juce::CaretComponent::caretColourId, c);
    glyphCache_[0].clear(); glyphCache_[1].clear();
    repaint();
}

void PresetBrowser::open() {
    favorites_ = BrowserFavorites::load(a_.favoritesKey());
    query_.clear();
    search_.setText({}, false);
    roleFilter_.clear(); charFilter_.clear();
    favoritesOnly_ = false; phraseOn_ = true;
    kept_ = a_.capture();
    audition_.reset();
    mode_ = Mode::Kept;
    variantN_ = 0;
    glyphCache_[0].clear(); glyphCache_[1].clear();
    rebuildVisible();
    refreshChips();
    setVisible(true);
    toFront(false);
    lastRevision_ = ~0ull; lastShown_ = -2;
    const auto it = std::find(visible_.begin(), visible_.end(), shownItem());
    if (it != visible_.end()) list_.scrollToEnsureRowIsOnscreen((int)(it - visible_.begin()));
    refreshDetail();
    repaint();
}

void PresetBrowser::close() {
    stopAudition();
    setVisible(false);
    if (onClose) onClose();
}

// ---- state machine (mirrors PresetBrowserShell.tsx) ---------------------------

void PresetBrowser::selectItem(int item) {
    if (item < 0 || item >= (int)a_.items().size()) return;
    a_.load(item);
    audition_.reset();
    variantN_ = 0;
    mode_ = (kept_ && item == kept_->index && !kept_->dirty) ? Mode::Kept : Mode::Audition;
    playAudition(item);
    refreshDetail();
    list_.repaint();
}

void PresetBrowser::keep() {
    kept_ = a_.capture();
    audition_.reset();
    mode_ = Mode::Kept;
    refreshDetail();
    list_.repaint();
}

void PresetBrowser::revert() {
    if (!kept_) return;
    a_.restore(*kept_);
    audition_.reset();
    variantN_ = 0;
    mode_ = Mode::Kept;
    playAudition(kept_->index);
    refreshDetail();
    list_.repaint();
}

void PresetBrowser::toggleAB() {
    if (!kept_) return;
    if (mode_ == Mode::B && audition_) {
        const int index = audition_->index;
        a_.restore(*audition_);
        mode_ = variantN_ ? Mode::Variant : Mode::Audition;
        audition_.reset();
        playAudition(index);
    } else {
        audition_ = a_.capture();
        a_.restore(*kept_);
        mode_ = Mode::B;
        playAudition(kept_->index);
    }
    refreshDetail();
    list_.repaint();
}

void PresetBrowser::vary() {
    const int current = a_.currentIndex();
    if (current < 0 || !a_.canVary()) return;
    a_.vary(current);
    ++variantN_;
    mode_ = Mode::Variant;
    playAudition(current);
    refreshDetail();
}

void PresetBrowser::setPhrase(bool on) {
    phraseOn_ = on;
    stopAudition();
    phraseBtn_.setToggleState(on, juce::dontSendNotification);
    repaint(detailArea_);
}

// ---- filters -----------------------------------------------------------------

void PresetBrowser::setQuery(const juce::String& q) { query_ = q; rebuildVisible(); }
void PresetBrowser::toggleRole(const juce::String& r) {
    if (!roleFilter_.erase(r)) roleFilter_.insert(r);
    rebuildVisible(); refreshChips();
}
void PresetBrowser::toggleCharacter(const juce::String& c) {
    if (!charFilter_.erase(c)) charFilter_.insert(c);
    rebuildVisible(); refreshChips();
}
void PresetBrowser::toggleFavoritesOnly() { favoritesOnly_ = !favoritesOnly_; rebuildVisible(); refreshChips(); }

bool PresetBrowser::isFavorite(int item) const {
    return item >= 0 && item < (int)a_.items().size() && favorites_.count(favKey(a_.items()[(size_t)item])) > 0;
}

void PresetBrowser::toggleFavorite(int item) {
    if (item < 0 || item >= (int)a_.items().size()) return;
    const auto key = favKey(a_.items()[(size_t)item]);
    if (!favorites_.erase(key)) favorites_.insert(key);
    BrowserFavorites::save(a_.favoritesKey(), favorites_);
    rebuildVisible();
}

void PresetBrowser::rebuildVisible() {
    const auto q = query_.trim().toLowerCase();
    visible_.clear();
    const auto& items = a_.items();
    for (int i = 0; i < (int)items.size(); ++i) {
        const auto& e = items[(size_t)i];
        if (!roleFilter_.empty() && (e.role.isEmpty() || !roleFilter_.count(e.role))) continue;
        bool tagsOk = true;
        for (const auto& t : charFilter_) if (!e.tags.contains(t)) { tagsOk = false; break; }
        if (!tagsOk) continue;
        if (favoritesOnly_ && !favorites_.count(favKey(e))) continue;
        if (q.isNotEmpty()) {
            bool hit = e.name.toLowerCase().contains(q) || e.role.contains(q);
            for (const auto& t : e.tags) hit = hit || t.contains(q);
            if (!hit) continue;
        }
        visible_.push_back(i);
    }
    list_.updateContent();
    list_.repaint();
    repaint(filterArea_);
}

void PresetBrowser::refreshChips() {
    for (std::size_t i = 0; i < roleChips_.size(); ++i)
        roleChips_[i]->setToggleState(roleFilter_.count(a_.roles()[(int)i]) > 0, juce::dontSendNotification);
    for (std::size_t i = 0; i < charChips_.size(); ++i)
        charChips_[i]->setToggleState(charFilter_.count(a_.characters()[(int)i]) > 0, juce::dontSendNotification);
    favoritesChip_.setToggleState(favoritesOnly_, juce::dontSendNotification);
}

void PresetBrowser::refreshDetail() {
    if (onChanged) onChanged();
    const bool isKept = mode_ == Mode::Kept || mode_ == Mode::B;
    keepBtn_.setEnabled(!isKept);
    revertBtn_.setEnabled(!isKept);
    abBtn_.setEnabled(mode_ != Mode::Kept);
    abBtn_.setToggleState(mode_ == Mode::B, juce::dontSendNotification);
    varyBtn_.setEnabled(a_.currentIndex() >= 0 && mode_ != Mode::B);
    phraseBtn_.setToggleState(phraseOn_, juce::dontSendNotification);
    repaint(detailArea_);
}

void PresetBrowser::timerCallback() {
    if (importBtn_.isVisible()) {
        const auto status = a_.pollImport();
        const auto text = a_.importRunning() ? juce::String("CANCEL IMPORT") : a_.importLabel();
        if (importBtn_.getButtonText() != text) importBtn_.setButtonText(text);
        if (status != importStatus_) { importStatus_ = status; repaint(importStatusArea_); }
    }
    if (a_.itemsRevision() != itemsRevision_) { // imported presets added or removed
        itemsRevision_ = a_.itemsRevision();
        glyphCache_[0].clear(); glyphCache_[1].clear();
        rebuildVisible();
        repaint();
    }
    const auto revision = a_.liveRevision();
    const int shown = shownItem();
    if (revision != lastRevision_ || shown != lastShown_) {
        if (shown != lastShown_) list_.repaint();
        lastRevision_ = revision; lastShown_ = shown;
        varyBtn_.setEnabled(a_.currentIndex() >= 0 && mode_ != Mode::B);
        repaint(detailArea_);
    }
}

// ---- audition ----------------------------------------------------------------

void PresetBrowser::playAudition(int item) {
    stopAudition();
    if (!phraseOn_ || item < 0 || a_.auditionBlocked()) return;
    pending_ = a_.auditionPlan(item);
    for (auto& e : pending_) e.ms += 30.0;
    std::stable_sort(pending_.begin(), pending_.end(), [](const auto& l, const auto& r) { return l.ms < r.ms; });
    std::reverse(pending_.begin(), pending_.end());    // pop from the back
    auditionStartMs_ = juce::Time::getMillisecondCounterHiRes();
    scheduler_.startTimer(4);
}

void PresetBrowser::tickAudition() {
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - auditionStartMs_;
    while (!pending_.empty() && pending_.back().ms <= elapsed) {
        const auto e = pending_.back();
        pending_.pop_back();
        a_.fire(e);
    }
    if (pending_.empty()) scheduler_.stopTimer();
}

void PresetBrowser::flushAudition() {
    while (!pending_.empty()) { const auto e = pending_.back(); pending_.pop_back(); a_.fire(e); }
    scheduler_.stopTimer();
}

void PresetBrowser::stopAudition() {
    scheduler_.stopTimer();
    pending_.clear();
    a_.releaseAll();
}

// ---- layout & painting ---------------------------------------------------------

void PresetBrowser::resized() {
    const int W = getWidth(), H = getHeight();
    filterArea_ = { 0, 0, kFilterW, H };
    detailArea_ = { W - kDetailW, 0, kDetailW, H };
    mainArea_ = { kFilterW, 0, W - kFilterW - kDetailW, H };
    listHeadArea_ = mainArea_.withHeight(kHeadH);
    list_.setBounds(mainArea_.withTrimmedTop(kHeadH));

    // filters: search, then three labelled chip groups
    const int fx = kPad, fw = kFilterW - 2 * kPad;
    search_.setBounds(fx, kPad, fw, 28);
    int y = kPad + 28 + 18;
    auto flow = [&](auto& chips) {
        int x = 0, rowY = y;
        for (auto& chip : chips) {
            const int w = juce::roundToInt(textWidth(monoFont(11.0f), chip->getButtonText())) + 16;
            if (x > 0 && x + w > fw) { x = 0; rowY += 22 + 5; }
            chip->setBounds(fx + x, rowY, w, 22);
            x += w + 5;
        }
        y = rowY + 22 + 18;
    };
    y += 12 + 7; flow(roleChips_);
    y += 12 + 7; flow(charChips_);
    y += 12 + 7;
    favoritesChip_.setBounds(fx, y, 96, 22);
    y += 22 + 18;
    countArea_ = { fx, y, fw, 14 };
    y += 14 + 18;
    importBtn_.setBounds(fx, y, fw, 28);
    importStatusArea_ = { fx, y + 28 + 6, fw, 30 };

    // detail column
    const int dx = detailArea_.getX() + kPad, dw = kDetailW - 2 * kPad;
    int dy = kPad;
    nameArea_ = { dx, dy, dw - 80, 40 };
    dy += 40 + 12;
    stripArea_ = { dx, dy, dw, 64 };
    dy += 64 + 6;
    legendArea_ = { dx, dy, dw, 12 };
    dy += 12 + 12;
    kvArea_ = { dx, dy, dw, 6 * 17 };
    dy += 6 * 17 + 12;
    const int bw = (dw - 6) / 2;
    keepBtn_.setBounds(dx, dy, bw, 30);
    revertBtn_.setBounds(dx + bw + 6, dy, bw, 30);
    abBtn_.setBounds(dx, dy + 36, bw, 30);
    varyBtn_.setBounds(dx + bw + 6, dy + 36, bw, 30);
    const int footY = H - kPad - 38;
    phraseBtn_.setBounds(dx, footY + 8, 74, 22);
    closeBtn_.setBounds(dx + dw - 64, footY + 4, 64, 30);
    hintArea_ = { dx + 74 + 8, footY, dw - 74 - 64 - 16, 38 };
    glyphCache_[0].clear(); glyphCache_[1].clear();
}

const juce::Image& PresetBrowser::rowGlyph(int item, bool selected, float scale) {
    if (std::abs(glyphScale_ - scale) > 1.0e-4f || glyphAccent_ != accent()) {
        glyphCache_[0].clear(); glyphCache_[1].clear();
        glyphScale_ = scale; glyphAccent_ = accent();
    }
    auto& cache = glyphCache_[selected ? 1 : 0];
    if (cache.size() != a_.items().size()) cache.assign(a_.items().size(), juce::Image());
    auto& img = cache[(size_t)item];
    if (!img.isValid()) {
        img = juce::Image(juce::Image::ARGB, juce::roundToInt(kGlyphW * scale), juce::roundToInt(kGlyphH * scale), true);
        juce::Graphics ig(img);
        ig.addTransform(juce::AffineTransform::scale(scale));
        a_.drawItemGlyph(ig, item, { 0, 0, (float)kGlyphW, (float)kGlyphH },
                         selected ? accent() : col::textHint.withAlpha(0.38f).withMultipliedAlpha(1.0f), false);
    }
    return img;
}

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) {
    if (row < 0 || row >= (int)visible_.size()) return;
    const int item = visible_[(size_t)row];
    const auto& e = a_.items()[(size_t)item];
    const bool sel = item == shownItem();
    if (sel) {
        g.setColour(accent().withAlpha(0.10f));
        g.fillRect(0, 0, w, h);
        g.setColour(accent());
        g.fillRect(0, 0, 2, h);
    }
    g.setColour(col::line);
    g.fillRect(0, h - 1, w, 1);

    g.setColour(col::textDim);
    g.setFont(monoFont(10.5f));
    g.drawText(e.user ? juce::String("USR") : e.badge.isNotEmpty() ? e.badge : juce::String(item + 1).paddedLeft('0', 3),
               kPad, 0, 30, h, juce::Justification::centredLeft);

    const int textX = kPad + 30 + 10, textR = w - kHeartW - 4 - kGlyphW - 10;
    g.setColour(col::text);
    g.setFont(dispFont(10.5f));
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(textX, 0, textR - textX, h);
        drawSpaced(g, e.name, { textX, 6, 400, 17 }, 0.6f);
    }
    g.setColour(col::textDim);
    g.setFont(monoFont(10.5f));
    g.drawText(joinMeta(e), textX, 22, textR - textX, 16, juce::Justification::centredLeft, true);

    const float scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    {
        juce::Graphics::ScopedSaveState save(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.setColour(juce::Colours::white);
        g.drawImageTransformed(rowGlyph(item, sel, scale),
            juce::AffineTransform::scale(1.0f / scale).translated((float)(w - kHeartW - 4 - kGlyphW), (float)((h - kGlyphH) / 2)));
    }

    const bool fav = isFavorite(item);
    const auto heart = juce::Rectangle<float>(0, 0, 13, 12).withCentre({ (float)(w - kHeartW / 2), (float)h * 0.5f });
    g.setColour(fav ? col::acB : col::textDim);
    if (fav) g.fillPath(browserHeartPath(heart));
    else g.strokePath(browserHeartPath(heart), juce::PathStrokeType(1.2f));
}

void PresetBrowser::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= (int)visible_.size()) return;
    const int item = visible_[(size_t)row];
    const int rowW = e.eventComponent ? e.eventComponent->getWidth() : list_.getRowContainingPosition(0, 0);
    if (e.x >= rowW - kHeartW) { toggleFavorite(item); return; }
    selectItem(item);
}

void PresetBrowser::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(r.translated(0, 6).expanded(2), 14.0f);
    g.setColour(col::panelLo);
    g.fillRoundedRectangle(r, 12.0f);
    g.setColour(col::line);
    g.fillRect(kFilterW, 0, 1, getHeight());
    g.fillRect(detailArea_.getX(), 0, 1, getHeight());
    g.fillRect(kFilterW, kHeadH - 1, mainArea_.getWidth(), 1);

    auto label = [&](const juce::String& text, juce::Rectangle<int> area, juce::Justification j = juce::Justification::centredLeft) {
        g.setColour(col::textDim);
        g.setFont(monoFont(9.5f));
        drawSpaced(g, text.toUpperCase(), area, 1.5f, j);
    };

    // filter column labels
    {
        auto labelAt = [&](const juce::String& t, const juce::Component& firstChip) {
            label(t, { kPad, firstChip.getY() - 7 - 12, 172, 12 });
        };
        if (!roleChips_.empty()) labelAt("Role", *roleChips_.front());
        if (!charChips_.empty()) labelAt("Character", *charChips_.front());
        labelAt("Collection", favoritesChip_);
        g.setColour(col::textDim);
        g.setFont(monoFont(11.0f));
        g.drawText(juce::String((int)visible_.size()) + " of " + juce::String((int)a_.items().size()), countArea_,
                   juce::Justification::centredLeft);
        if (importBtn_.isVisible() && importStatus_.isNotEmpty()) {
            g.setFont(monoFont(10.0f));
            g.drawFittedText(importStatus_, importStatusArea_, juce::Justification::topLeft, 2);
        }
    }

    // list head
    label(a_.title(), listHeadArea_.withTrimmedLeft(kPad).withTrimmedRight(kPad).reduced(0, 0),
          juce::Justification::centredLeft);
    label(a_.glyphLegend(), listHeadArea_.withTrimmedLeft(kPad).withTrimmedRight(kPad), juce::Justification::centredRight);
    if (visible_.empty()) {
        g.setColour(col::textDim);
        g.setFont(monoFont(12.0f));
        g.drawFittedText("Nothing matches these filters. Clear a chip to see more.",
                         mainArea_.withTrimmedTop(kHeadH + 8).reduced(kPad, 8).withHeight(40),
                         juce::Justification::topLeft, 2, 1.0f);
    }

    // detail
    const int current = a_.currentIndex();
    const BrowserItem* item = current >= 0 ? &a_.items()[(size_t)current] : nullptr;
    const auto accentNow = modeColour(mode_);
    g.setColour(col::text);
    g.setFont(dispFont(14.0f));
    juce::String name = item ? item->name : juce::String::fromUTF8("\xe2\x80\x94");
    if (mode_ == Mode::Variant && item) name << " ~" << variantN_;
    g.drawFittedText(name, nameArea_.withHeight(24), juce::Justification::topLeft, 1, 1.0f);
    g.setColour(col::textHint);
    g.setFont(monoFont(11.0f));
    g.drawText(item && !(joinMeta(*item).isEmpty()) ? joinMeta(*item) : juce::String("untagged"),
               nameArea_.withTrimmedTop(24), juce::Justification::topLeft, true);

    // state chip
    {
        g.setFont(monoFont(9.5f));
        const juce::String text = juce::String::fromUTF8(modeLabel(mode_));
        const int tw = juce::roundToInt(textWidth(monoFont(9.5f), text)) + (int)text.length() + 12;
        const auto chip = juce::Rectangle<int>(detailArea_.getRight() - kPad - tw, kPad, tw, 18);
        if (mode_ == Mode::Kept) {
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 3.0f, 1.0f);
        } else {
            g.setColour((mode_ == Mode::Audition ? accent() : accentNow).withAlpha(0.12f));
            g.fillRoundedRectangle(chip.toFloat(), 3.0f);
        }
        g.setColour(mode_ == Mode::Kept ? col::textDim : mode_ == Mode::Audition ? accent() : accentNow);
        drawSpaced(g, text, chip, 1.1f, juce::Justification::centred);
    }

    // glyph strip: a black well with the live fingerprint
    {
        const auto well = stripArea_.toFloat();
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(well, 6.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(well.reduced(0.5f), 6.0f, 1.0f);
        a_.drawLiveGlyph(g, well, mode_ == Mode::Audition ? accent() : accentNow, true);
    }
    const auto legend = a_.detailLegend();
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    for (int i = 0; i < 4 && i < legend.size(); ++i)
        drawSpaced(g, legend[i], { legendArea_.getX() + i * legendArea_.getWidth() / 4, legendArea_.getY(), legendArea_.getWidth() / 4, legendArea_.getHeight() },
                   1.1f, juce::Justification::centred);

    // value readout
    {
        const auto kv = a_.liveReadout();
        float dtW = 0;
        for (const auto& row : kv) dtW = std::max(dtW, textWidth(monoFont(11.0f), row.first));
        int y = kvArea_.getY();
        g.setFont(monoFont(11.0f));
        for (const auto& row : kv) {
            g.setColour(col::textDim);
            g.drawText(row.first, kvArea_.getX(), y, (int)dtW + 2, 17, juce::Justification::centredLeft);
            g.setColour(col::text);
            g.drawText(row.second, kvArea_.getX() + (int)dtW + 12, y, kvArea_.getWidth() - (int)dtW - 12, 17,
                       juce::Justification::centredLeft, true);
            y += 17;
        }
    }

    g.setColour(col::textDim);
    g.setFont(monoFont(10.5f));
    g.drawFittedText(a_.auditionHint(phraseOn_), hintArea_, juce::Justification::centredLeft, 3, 1.0f);

    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
}

} // namespace fui
