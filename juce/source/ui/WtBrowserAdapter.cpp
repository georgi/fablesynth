#include "WtBrowserAdapter.h"
#include "BrowserSupport.h"
#include "Format.h"
#include "../dsp/Presets.h"

#include <algorithm>

namespace fui {

using namespace fable;

namespace {
// Phrases per role (src/presetBrowser.ts PHRASES): [semitones from the root, start beat, length in beats].
struct PhraseNote { int semi; float start, len; };
constexpr int kPhraseRoot = 45;   // A2

std::vector<PhraseNote> phraseFor(const juce::String& role) {
    if (role == "bass")  return { {0, 0, 0.5f}, {0, 0.75f, 0.25f}, {12, 1.5f, 0.25f}, {3, 2, 0.5f}, {5, 3, 0.5f} };
    if (role == "pad")   return { {12, 0, 3.5f}, {15, 0, 3.5f}, {19, 0, 3.5f}, {22, 0, 3.5f} };
    if (role == "keys")  return { {12, 0, 0.4f}, {15, 0, 0.4f}, {19, 0, 0.4f}, {12, 1.5f, 0.4f}, {15, 1.5f, 0.4f}, {19, 1.5f, 0.4f},
                                  {10, 2.5f, 0.8f}, {14, 2.5f, 0.8f}, {17, 2.5f, 0.8f} };
    if (role == "pluck") return { {24, 0, 0.25f}, {31, 0.5f, 0.25f}, {27, 1, 0.25f}, {34, 1.5f, 0.25f}, {31, 2, 0.25f}, {27, 2.5f, 0.25f}, {24, 3, 0.5f} };
    if (role == "fx")    return { {12, 0, 3} };
    return { {24, 0, 0.5f}, {27, 0.5f, 0.5f}, {31, 1, 1}, {29, 2.5f, 0.5f}, {27, 3, 1} };   // lead
}
} // namespace

const std::vector<browser::VaryRule>& WtBrowserAdapter::varyRules() {
    // Pitch, levels, and FX stay fixed so a variant keeps the preset's register and balance.
    static const std::vector<browser::VaryRule> rules = {
        { "oscA.pos", 0.15f }, { "oscB.pos", 0.15f },
        { "oscA.detune", 0.08f }, { "oscB.detune", 0.08f },
        { "filter.cutoff", 0.6f }, { "filter.res", 0.08f }, { "filter.env", 0.08f },
        { "env1.a", 0.4f }, { "env1.d", 0.4f }, { "env1.s", 0.08f }, { "env1.r", 0.4f },
        { "env2.a", 0.4f }, { "env2.d", 0.4f }, { "env2.s", 0.08f },
        { "lfo1.rate", 0.4f }, { "lfo2.rate", 0.4f },
    };
    return rules;
}

template <class Get>
glyph::WaveData WtBrowserAdapter::waveFrom(Get&& get) {
    glyph::WaveData d;
    d.table = (int)get(paramIndex(OSCA_BASE, OSC_TABLE));
    d.pos = get(paramIndex(OSCA_BASE, OSC_POS));
    d.filterType = get(paramIndex(FILTER1_BASE, FLT_ON)) == 0.0f ? -1 : (int)get(paramIndex(FILTER1_BASE, FLT_TYPE));
    d.cutoff = get(paramIndex(FILTER1_BASE, FLT_CUTOFF));
    d.res = get(paramIndex(FILTER1_BASE, FLT_RES));
    for (int i = 0; i < 4; ++i) d.env[i] = get(ENV1_BASE + i);
    float depth = 0;
    for (int s = 1; s <= MOD_MATRIX_SIZE; ++s) {
        const int src = (int)get(paramIndex((Pid)matBase(s), MAT_SRC));
        if (src == 1 || src == 2) depth += std::abs(get(paramIndex((Pid)matBase(s), MAT_AMT)));
    }
    d.lfoDepth = std::min(1.0f, depth);
    return d;
}

WtBrowserAdapter::WtBrowserAdapter(std::unique_ptr<BrowserBackend> backend) : backend_(std::move(backend)) {
    std::vector<juce::String> names;
    for (const auto& preset : factoryPresets()) names.emplace_back(preset.name);
    items_ = makeBrowserItems(browserTags::wt1, names);
    roles_ = browser::splitWords(browserTags::wt1.roles);
    characters_ = browser::splitWords(browserTags::wt1.characters);
    for (const auto& preset : factoryPresets()) {
        params_.push_back(applyPreset(preset));
        glyphs_.push_back(waveFrom([&](int id) { return params_.back()[(size_t)id]; }));
    }
    rng_.setSeedRandomly();
    factoryCount_ = (int)items_.size();
    reloadImported();
}

// ---- imported presets ----------------------------------------------------------

// Runs FableSerumImport as a child process (a Serum crash can never reach the
// host) and turns its progress lines into a status string.
class WtBrowserAdapter::ImportRun final : public juce::Thread {
public:
    explicit ImportRun(juce::File helper) : juce::Thread("FableSerumImport"), helper_(std::move(helper)) {}
    ~ImportRun() override { cancel(); stopThread(4000); }
    // start() and kill() share procLock_: juce::ChildProcess is not thread-safe,
    // and a cancel that lands before start() must still stop the child.
    void cancel() {
        cancelled_ = true;
        signalThreadShouldExit();
        const juce::ScopedLock l(procLock_);
        if (started_) proc_.kill();
    }
    juce::String status() const { const juce::ScopedLock l(lock_); return status_; }

    void run() override {
        {
            const juce::ScopedLock l(procLock_);
            if (!cancelled_)
                started_ = proc_.start(juce::StringArray{ helper_.getFullPathName() }, juce::ChildProcess::wantStdOut);
        }
        if (!started_) {
            set(cancelled_ ? cancelledText() : juce::String("The import helper could not start."));
            return;
        }
        set("Starting Serum...");
        juce::String pending;
        char buf[1024];
        // A kill closes the child's end of the pipe, so the blocking read returns.
        while (!threadShouldExit()) {
            const int n = proc_.readProcessOutput(buf, (int)sizeof(buf));
            if (n > 0) {
                pending += juce::String::fromUTF8(buf, n);
                int nl;
                while ((nl = pending.indexOfChar('\n')) >= 0) { line(pending.substring(0, nl).trim()); pending = pending.substring(nl + 1); }
                continue;
            }
            if (!proc_.isRunning() || threadShouldExit()) break;
            wait(20);
        }
        { const juce::ScopedLock l(procLock_); started_ = false; } // a late cancel must not kill a reused pid
        if (cancelled_) {
            proc_.waitForProcessToFinish(1000); // reap the killed child
            set(cancelledText());
        }
    }

private:
    static juce::String cancelledText() { return "Import cancelled. Converted presets stay listed."; }
    void set(const juce::String& s) { const juce::ScopedLock l(lock_); status_ = s; }
    void line(const juce::String& l) {
        if (l.startsWith("TOTAL ")) total_ = l.substring(6).getIntValue();
        else if (l.startsWith("PRESET ")) {
            const auto rest = l.substring(7);
            set("Importing " + rest.upToFirstOccurrenceOf(" ", false, false) + " of " + juce::String(total_) + ": "
                + rest.fromFirstOccurrenceOf(" ", false, false));
        } else if (l.startsWith("DONE ")) {
            const auto parts = juce::StringArray::fromTokens(l.substring(5), " ", "");
            set("Imported " + parts[0] + " presets" + (parts[1].getIntValue() > 0 ? " (" + parts[1] + " skipped)." : "."));
        } else if (l.startsWith("ERROR ")) set(l.substring(6));
    }

    juce::File helper_;
    juce::ChildProcess proc_;
    juce::CriticalSection procLock_, lock_;
    bool started_ = false; // guarded by procLock_
    juce::String status_;
    int total_ = 0;
    std::atomic<bool> cancelled_ { false };
};

WtBrowserAdapter::~WtBrowserAdapter() = default;

void WtBrowserAdapter::reloadImported() {
    if (!backend_->supportsImported()) return;
    items_.resize((size_t)factoryCount_);
    params_.resize((size_t)factoryCount_);
    glyphs_.resize((size_t)factoryCount_);
    imported_ = serumimport::loadIndex(serumimport::importedFolder());
    for (const auto& e : imported_) {
        BrowserItem item;
        item.name = e.name;
        item.role = serumimport::roleForCategory(e.category);
        if (const auto tag = serumimport::tagForCategory(e.category); tag.isNotEmpty()) item.tags.add(tag); // source category
        item.badge = "IMP";
        items_.push_back(item);
        ParamArray pv = defaultParams();
        for (const auto& [id, v] : e.params) if (const int i = idFromString(id); i >= 0) pv[(size_t)i] = v;
        params_.push_back(pv);
        glyphs_.push_back(waveFrom([&](int id) { return params_.back()[(size_t)id]; }));
    }
    ++itemsRevision_;
}

int WtBrowserAdapter::currentIndex() const {
    const auto key = backend_->loadedImportedKey();
    if (key.isEmpty()) return backend_->loadedIndex();
    for (size_t i = 0; i < imported_.size(); ++i)
        if (imported_[i].file.getFullPathName() == key) return factoryCount_ + (int)i;
    return -1;
}

void WtBrowserAdapter::load(int item) {
    if (item < factoryCount_) { backend_->load(item); return; }
    const auto k = (size_t)(item - factoryCount_);
    if (k >= imported_.size()) return;
    juce::String error;
    if (!backend_->loadImported(imported_[k], error)) status_ = error;
}

juce::String WtBrowserAdapter::importLabel() const {
    return backend_->supportsImported() ? juce::String::fromUTF8("IMPORT FROM SERUM\xe2\x80\xa6") : juce::String();
}

void WtBrowserAdapter::startImport() {
    if (importRunning()) return;
    juce::File helper(juce::SystemStats::getEnvironmentVariable("FABLE_SERUM_IMPORT", {}));
    if (!helper.existsAsFile()) {
        // Bundled next to the binary: <bundle>/Contents/Resources (macOS), else beside it.
        const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        for (auto f : { exe.getParentDirectory().getSiblingFile("Resources").getChildFile("FableSerumImport"),
                        exe.getSiblingFile("FableSerumImport"), exe.getSiblingFile("FableSerumImport.exe") })
            if (f.existsAsFile()) { helper = f; break; }
    }
    if (!helper.existsAsFile()) { status_ = "The import helper is missing from this installation."; return; }
    run_ = std::make_unique<ImportRun>(helper);
    status_.clear();
    run_->startThread();
}

void WtBrowserAdapter::cancelImport() { if (run_) run_->cancel(); }
bool WtBrowserAdapter::importRunning() const { return run_ != nullptr && run_->isThreadRunning(); }

juce::String WtBrowserAdapter::pollImport() {
    if (run_ != nullptr && !run_->isThreadRunning()) { // finished or cancelled: list what it wrote
        status_ = run_->status();
        run_.reset();
        reloadImported();
    }
    return run_ != nullptr ? run_->status() : status_;
}

glyph::WaveData WtBrowserAdapter::liveWave() const {
    return waveFrom([this](int id) { return backend_->value(id); });
}

std::vector<float> WtBrowserAdapter::presetValues(int item) const {
    return std::vector<float>(params_[(size_t)item].begin(), params_[(size_t)item].end());
}

void WtBrowserAdapter::vary(int item) {
    // Imported presets carry wavetables that a variant cannot restore.
    if (item < 0 || item >= factoryCount_) return;
    auto values = presetValues(item);
    for (const auto& rule : varyRules()) {
        const int id = idFromString(rule.id);
        if (id >= 0) values[(size_t)id] = browser::nudgeValue(paramInfo()[(size_t)id], values[(size_t)id], rule.amount, rng_.nextFloat() * 2 - 1);
    }
    for (int s = 1; s <= MOD_MATRIX_SIZE; ++s) {
        const int src = paramIndex((Pid)matBase(s), MAT_SRC), amt = paramIndex((Pid)matBase(s), MAT_AMT);
        if ((int)values[(size_t)src] != 0 && values[(size_t)amt] != 0.0f)
            values[(size_t)amt] = browser::nudgeValue(paramInfo()[(size_t)amt], values[(size_t)amt], 0.05f, rng_.nextFloat() * 2 - 1);
    }
    backend_->applyVariant(item, values);
}

std::vector<AuditionEvent> WtBrowserAdapter::auditionPlan(int item) const {
    const float bpm = backend_->bpm() > 0 ? backend_->bpm() : 120.0f;
    const double beat = 60.0 / bpm * 1000.0;
    const juce::String role = item >= 0 && item < (int)items_.size() && items_[(size_t)item].role.isNotEmpty() ? items_[(size_t)item].role : "lead";
    std::vector<AuditionEvent> out;
    for (const auto& n : phraseFor(role)) {
        const int note = kPhraseRoot + n.semi;
        out.push_back({ n.start * beat, true, note, 0.8f });
        out.push_back({ (n.start + n.len) * beat, false, note, 0.0f });
    }
    return out;
}

void WtBrowserAdapter::fire(const AuditionEvent& e) {
    if (e.on) held_.insert(e.id); else held_.erase(e.id);
    if (e.on) backend_->noteOn(e.id, e.velocity); else backend_->noteOff(e.id);
}

void WtBrowserAdapter::releaseAll() {
    for (int n : held_) backend_->noteOff(n);
    held_.clear();
}

juce::String WtBrowserAdapter::auditionHint(bool phraseOn) const {
    if (backend_->auditionBlocked()) return backend_->blockedHint("preset");
    return phraseOn ? "A short phrase plays on select." : "Play the keys to hear presets.";
}

void WtBrowserAdapter::drawItemGlyph(juce::Graphics& g, int item, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    const auto& d = glyphs_[(size_t)item];
    glyph::drawWave(g, r, c, fill, d, backend_->table(d.table));
}

void WtBrowserAdapter::drawLiveGlyph(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    const auto d = liveWave();
    glyph::drawWave(g, r, c, fill, d, backend_->table(d.table));
}

std::vector<std::pair<juce::String, juce::String>> WtBrowserAdapter::liveReadout() const {
    const auto d = liveWave();
    const auto& tables = TABLE_NAMES;
    const juce::String wave = d.table >= 0 && d.table < (int)tables.size() ? juce::String(tables[(size_t)d.table]) : juce::String("USER");
    const juce::String filter = d.filterType < 0 ? juce::String("off")
        : juce::String(FILTER_TYPES[(size_t)juce::jlimit(0, (int)FILTER_TYPES.size() - 1, d.filterType)]) + browser::kDot + fmtHz(d.cutoff);
    return {
        { "Wave", wave + browser::kDot + fmtPct(d.pos) },
        { "Filter", filter },
        { "Reso", fmtPct(d.res) },
        { "A / D / R", fmtSec(d.env[0]) + " / " + fmtSec(d.env[1]) + " / " + fmtSec(d.env[3]) },
        { "Sustain", fmtPct(d.env[2]) },
        { "LFO depth", d.lfoDepth > 0 ? fmtPct(d.lfoDepth) : juce::String("off") },
    };
}

std::uint64_t WtBrowserAdapter::liveRevision() const {
    const auto d = liveWave();
    return browser::hashFloats({ (float)d.table, d.pos, (float)d.filterType, d.cutoff, d.res, d.env[0], d.env[1], d.env[2], d.env[3],
                                 d.lfoDepth, (float)backend_->loadedIndex(), (float)backend_->tableRevision(),
                                 (float)backend_->transportRevision() });
}

} // namespace fui
