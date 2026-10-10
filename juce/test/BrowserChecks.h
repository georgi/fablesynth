#pragma once
// Preset browser checks shared by the three standalone host tests. They drive the
// real PresetBrowser through a machine adapter and verify what the web shell
// promises: a load -> capture -> load other -> restore round trip returns every
// parameter, the program index and the dirty flag; VARY moves only its allowed
// parameters and stays in range; favorites persist; audition respects transport.
#include "../source/ui/PresetBrowser.h"
#include "../source/ui/BrowserSupport.h"

#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace browser_test {

using CheckFn = std::function<void(bool, const char*, double)>;

struct Probe {
    std::string label;
    std::vector<fable::ParamInfo> infos;
    std::function<std::vector<float>()> values;        // every parameter, plain
    std::function<std::string()> extra;                // patterns / pad names: must restore exactly
    std::function<int()> program;                      // host program (kits, presets, patches)
    std::function<bool()> dirty;
    std::function<juce::String(int)> programName;
    std::map<int, float> varyAmounts;                  // parameter id -> allowed amount
    std::function<void(bool)> setPlaying;              // start / stop the internal sequencer
    bool programMatchesItem = true;                    // pad patches have no host program
};

inline double maxDeviation(const std::vector<float>& a, const std::vector<float>& b) {
    double worst = a.size() == b.size() ? 0.0 : 1.0e9;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, std::abs((double)a[i] - b[i]) / std::max(1.0, (double)std::abs(b[i])));
    return worst;
}

// Parameters that differ between two value sets, beyond float noise.
inline std::vector<int> changedIds(const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<int> out;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (std::abs((double)a[i] - b[i]) / std::max(1.0, (double)std::abs(b[i])) > 1.0e-4) out.push_back((int)i);
    return out;
}

inline void runChecks(fui::BrowserAdapter& a, fui::PresetBrowser& br, Probe& p, const CheckFn& check) {
    using fui::PresetBrowser;
    auto ck = [&](bool c, const char* m, double v = 0.0) { check(c, m, v); };
    const auto name = [&](const char* s) { static std::string buf; buf = p.label + ": " + s; return buf.c_str(); };
    const int n = (int)a.items().size();
    ck(n >= 4, name("browser lists the factory items"), n);

    int untagged = 0;
    for (const auto& e : a.items()) if (e.role.isEmpty()) ++untagged;
    ck(untagged == 0, name("every factory item carries generated tags"), untagged);
    ck(a.roles().size() > 0 && a.characters().size() > 0, name("role and character chips exist"), a.roles().size());

    // Pick rows that differ from what is loaded, so the first click is an audition.
    const int first = (a.currentIndex() + 1 + n) % n, second = (first + n / 2) % n, third = (first + n - 1) % n;
    br.open();
    ck(br.isOpen() && br.mode() == PresetBrowser::Mode::Kept, name("opening keeps the loaded sound"));
    ck(br.visibleCount() == n, name("all items listed when no filter is set"), br.visibleCount());

    // ---- load -> keep -> load other -> REVERT restores exactly ----
    br.selectItem(first);
    ck(a.currentIndex() == first && br.mode() == PresetBrowser::Mode::Audition, name("a row click loads it at once"), a.currentIndex());
    if (p.programMatchesItem) ck(p.program() == first, name("row click selects the host program"), p.program());
    br.keep();
    ck(br.mode() == PresetBrowser::Mode::Kept, name("KEEP keeps what you hear"));
    const auto keptValues = p.values();
    const auto keptExtra = p.extra();
    const int keptProgram = p.program();
    const bool keptDirty = p.dirty();

    br.selectItem(second);
    const auto otherValues = p.values();
    ck(!changedIds(otherValues, keptValues).empty(), name("another row changes the sound"), (double)changedIds(otherValues, keptValues).size());
    br.revert();
    const double revertDev = maxDeviation(p.values(), keptValues);
    ck(revertDev < 1.0e-4, name("REVERT restores every parameter"), revertDev);
    ck(p.extra() == keptExtra, name("REVERT restores patterns / names exactly"));
    ck(p.program() == keptProgram && p.dirty() == keptDirty, name("REVERT restores the program index and dirty flag"), p.program());
    ck(br.mode() == PresetBrowser::Mode::Kept && a.currentIndex() == first, name("REVERT returns to KEPT"));
    if (p.programMatchesItem)
        ck(p.programName(p.program()) == a.items()[(size_t)p.program()].name, name("program list and browser agree on the name"));

    // ---- A / B compares the kept sound with the audition ----
    br.selectItem(third);
    const auto thirdValues = p.values();
    const auto thirdExtra = p.extra();
    br.toggleAB();
    ck(br.mode() == PresetBrowser::Mode::B, name("A / B shows the kept sound"));
    ck(maxDeviation(p.values(), keptValues) < 1.0e-4 && p.extra() == keptExtra && a.currentIndex() == first, name("A shows the kept sound exactly"), maxDeviation(p.values(), keptValues));
    ck(br.shownItem() == third, name("the list marks the audition while B plays"), br.shownItem());
    br.toggleAB();
    ck(br.mode() == PresetBrowser::Mode::Audition, name("A / B returns to the audition"));
    ck(maxDeviation(p.values(), thirdValues) < 1.0e-4 && p.extra() == thirdExtra && a.currentIndex() == third, name("B restores the audition exactly"), maxDeviation(p.values(), thirdValues));
    br.revert();

    // ---- VARY ----
    if (a.canVary()) {
        br.selectItem(second);
        const auto base = p.values();
        const int baseProgram = p.program();
        bool moved = false, onlyAllowed = true, inRange = true, withinAmount = true;
        for (int round = 0; round < 3; ++round) {
            br.vary();
            const auto v = p.values();
            for (int id : changedIds(v, base)) {
                moved = true;
                auto it = p.varyAmounts.find(id);
                if (it == p.varyAmounts.end()) { onlyAllowed = false; continue; }
                const auto& d = p.infos[(size_t)id];
                if (v[(size_t)id] < d.min - 1.0e-4f || v[(size_t)id] > d.max + 1.0e-4f) inRange = false;
                const double amount = it->second, b = base[(size_t)id];
                if (d.curve == fable::Curve::Log) {
                    const double ratio = v[(size_t)id] / std::max(1.0e-9, b);
                    if (ratio > std::pow(2.0, amount) * 1.001 || ratio < std::pow(2.0, -amount) / 1.001) withinAmount = false;
                } else if (std::abs(v[(size_t)id] - b) > amount * (d.max - d.min) * 1.001 + 1.0e-4) withinAmount = false;
            }
            for (std::size_t i = 0; i < v.size(); ++i)
                if (v[i] < p.infos[i].min - 1.0e-4f || v[i] > p.infos[i].max + 1.0e-4f) inRange = false;
        }
        ck(moved, name("VARY changes the sound"));
        ck(onlyAllowed, name("VARY touches only its allowed parameters"));
        ck(inRange, name("VARY stays inside every parameter range"));
        ck(withinAmount, name("VARY stays within each parameter's amount"));
        ck(br.mode() == PresetBrowser::Mode::Variant && br.variantCount() == 3, name("VARY enters VARIANT mode"), br.variantCount());
        ck(p.dirty() && p.program() == baseProgram, name("a variant is unsaved and keeps the program"), p.program());
        br.revert();
        ck(maxDeviation(p.values(), keptValues) < 1.0e-4 && p.extra() == keptExtra, name("REVERT undoes a variant"), maxDeviation(p.values(), keptValues));
    }

    // ---- filters ----
    {
        const auto& e = a.items()[(size_t)first];
        br.setQuery(e.name.toLowerCase());
        ck(br.visibleCount() >= 1 && br.visibleCount() < n, name("search narrows the list"), br.visibleCount());
        br.setQuery({});
        br.toggleRole(e.role);
        const int withRole = br.visibleCount();
        ck(withRole >= 1 && withRole <= n, name("a role chip filters the list"), withRole);
        if (e.tags.size() > 0) {
            br.toggleCharacter(e.tags[0]);
            ck(br.visibleCount() >= 1 && br.visibleCount() <= withRole, name("a character chip narrows further"), br.visibleCount());
            br.toggleCharacter(e.tags[0]);
        }
        br.toggleRole(e.role);
        ck(br.visibleCount() == n, name("clearing chips restores the list"), br.visibleCount());
    }

    // ---- favorites persist ----
    {
        br.toggleFavorite(first);
        ck(br.isFavorite(first), name("favorite toggles on"));
        br.close();
        br.open();
        ck(br.isFavorite(first), name("favorites persist across opens"));
        br.toggleFavoritesOnly();
        ck(br.visibleCount() == 1, name("favorites filter lists only favorites"), br.visibleCount());
        br.toggleFavoritesOnly();
        br.toggleFavorite(first);
        ck(!br.isFavorite(first), name("favorite toggles off"));
    }

    // ---- audition: plan, lock-free hand-off, and transport guard ----
    {
        const auto plan = a.auditionPlan(first);
        ck(!plan.empty(), name("audition plan has events"), (double)plan.size());
        bool sane = true;
        for (const auto& e : plan) sane = sane && std::isfinite(e.ms) && e.ms >= 0.0 && e.velocity >= 0.0f && e.velocity <= 1.0f;
        ck(sane, name("audition events are finite and in range"));
        br.selectItem(first);
        ck(br.pendingAuditionCount() > 0, name("a row click schedules the phrase"), br.pendingAuditionCount());
        br.flushAudition();
        ck(br.pendingAuditionCount() == 0, name("the phrase plays out"));
        br.stopAudition();
        br.setPhrase(false);
        br.selectItem(second);
        ck(br.pendingAuditionCount() == 0, name("phrase off schedules nothing"));
        br.setPhrase(true);
        p.setPlaying(true);
        ck(a.auditionBlocked(), name("audition is blocked while the sequencer plays"));
        br.selectItem(first);
        ck(br.pendingAuditionCount() == 0, name("no audition while the sequencer plays"), br.pendingAuditionCount());
        p.setPlaying(false);
        ck(!a.auditionBlocked(), name("audition resumes when stopped"));
    }
    br.close();
    ck(!br.isOpen(), name("CLOSE hides the drawer"));
}

} // namespace browser_test
