#pragma once
#include "PresetBrowser.h"
#include "../dsp/Wavetables.h"
#include "../serumimport/ImportedLibrary.h"

// The device-bound half of a preset browser adapter. An adapter owns what is
// machine specific but device independent (items, tags, glyphs, phrases, VARY
// rules); a backend reads and writes one concrete sound source: a standalone
// plugin processor, or one SQ-4 track and its session patch.
namespace fui {

class BrowserBackend {
public:
    virtual ~BrowserBackend() = default;

    virtual float value(int id) const = 0;              // live flat parameter value
    virtual int loadedIndex() const = 0;                // factory item loaded now (-1 for none)
    virtual bool dirty() const = 0;                     // edited since the load
    virtual void load(int item) = 0;
    virtual std::unique_ptr<BrowserSnapshot> capture() const = 0;
    virtual void restore(const BrowserSnapshot&) = 0;
    // Apply `values` (a full flat parameter set derived from `item`) as an unsaved variant.
    virtual void applyVariant(int item, const std::vector<float>& values) = 0;

    virtual const fable::GeneratedTable* table(int /*index*/) const { return nullptr; }
    virtual int tableRevision() const { return 0; }
    virtual float bpm() const = 0;

    // Audition notes (WT-1, BL-1) or pad hits (DR-1) on a lock-free path to the audio thread.
    virtual void noteOn(int id, float velocity) = 0;
    virtual void noteOff(int id) = 0;
    virtual bool auditionBlocked() const = 0;
    // Shown while blocked; `noun` is "preset", "patch", "kit" or "pad".
    virtual juce::String blockedHint(const juce::String& noun) const = 0;
    virtual std::uint64_t transportRevision() const = 0;   // changes with whatever blocks audition

    // Imported presets (WT-1): a backend that can take their wavetables opts in.
    virtual bool supportsImported() const { return false; }
    virtual juce::String loadedImportedKey() const { return {}; }
    virtual bool loadImported(const serumimport::ImportedEntry&, juce::String& error) { error = "not supported"; return false; }

    // DR-1 pad patches act on the selected pad.
    virtual int selectedPad() const { return 0; }
    virtual void loadPadPatch(int /*item*/) {}
    virtual void applyPadFields(const std::vector<std::pair<int, float>>& /*absolute ids*/, bool /*dirty*/) {}
};

} // namespace fui
