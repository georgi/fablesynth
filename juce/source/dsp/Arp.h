#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace fable {
// Value-owned settings and compiled pattern: copying never allocates, including
// clip launches and audio-thread key capture. Notes retain insertion order.
struct ArpSettings {
    bool enabled = false, keys = false, latch = false;
    int order = 0, octaves = 1, count = 4;
    uint32_t seed = 1;
    double rate = .25, gate = .65;
    std::array<int, 128> notes {{50, 53, 57, 60}};
    std::array<bool, 16> hits, accents {}, slides {};
    ArpSettings() { hits.fill(true); for (int i = 0; i < 16; i += 4) accents[i] = true; }
};
struct ArpPattern {
    bool enabled = false;
    double rate = .25, gate = .65;
    std::array<int, 16> notes {};
    std::array<bool, 16> hits {}, accents {}, slides {};
};
inline bool validArpSettings(const ArpSettings& a, bool hosted = false) {
    if (a.count < 0 || a.count > 128 || a.order < 0 || a.order > 4 || a.octaves < 1 || a.octaves > 3
        || !std::isfinite(a.gate) || a.gate < .05 || a.gate > .95 || (hosted && (a.keys || a.latch))) return false;
    const std::array<double, 8> rates {{.125, .25, .5, 1, 1.0/3, 1.0/6, .375, .75}};
    if (std::find(rates.begin(), rates.end(), a.rate) == rates.end()) return false;
    for (int i = 0; i < a.count; ++i) if (a.notes[i] < 0 || a.notes[i] > 127) return false;
    return true;
}
inline bool arpHasNotes(const ArpPattern& a) {
    for (int i = 0; i < 16; ++i) if (a.hits[i] && a.notes[i] >= 0) return true;
    return false;
}
inline ArpPattern compileArp(const ArpSettings& a) {
    ArpPattern p; p.enabled = a.enabled;
    p.rate = std::isfinite(a.rate) ? std::clamp(a.rate, .125, 1.0) : .25;
    p.gate = std::isfinite(a.gate) ? std::clamp(a.gate, .05, .95) : .65;
    p.hits = a.hits; p.accents = a.accents; p.slides = a.slides;
    std::array<int, 768> pool {}; int count = 0;
    for (int i = 0; i < std::clamp(a.count, 0, 128); ++i) {
        const int n = a.notes[i];
        if (n >= 0 && n <= 127 && std::find(pool.begin(), pool.begin() + count, n) == pool.begin() + count)
            pool[count++] = n;
    }
    if (a.order != 3) std::sort(pool.begin(), pool.begin() + count);
    const int base = count;
    for (int oct = 1; oct < std::clamp(a.octaves, 1, 3); ++oct)
        for (int i = 0; i < base; ++i) if (pool[i] + oct * 12 <= 127) pool[count++] = pool[i] + oct * 12;
    if (a.order == 1) std::reverse(pool.begin(), pool.begin() + count);
    if (a.order == 2 && count > 2) for (int i = count - 2; i >= 1; --i) pool[count++] = pool[i];
    uint32_t seed = a.seed;
    for (int i = 0; i < 16; ++i) {
        seed = seed * 1664525u + 1013904223u;
        p.notes[i] = count ? pool[(a.order == 4 ? seed >> 8 : (uint32_t)i) % (uint32_t)count] : -1;
    }
    return p;
}
inline ArpSettings bassArpDefaults() {
    ArpSettings a; a.notes = {{36, 39, 43, 46}}; return a;
}
// Audio-owned key pool, independent of stored notes. Latch replaces the pool
// on the first key of a new gesture; releasing keys does not erase a latch.
class ArpInput {
public:
    ArpSettings settings;
    void set(const ArpSettings& a) {
        if (a.enabled != settings.enabled) clear();
        else if (a.keys != settings.keys || (settings.latch && !a.latch)) {
            int retained = 0;
            for (int i = 0; i < count; ++i) if (held[notes[i]]) notes[retained++] = notes[i];
            count = retained;
        }
        settings = a;
    }
    void clear() { held.fill(false); count = 0; }
    bool key(int n, bool on) {
        if (!settings.enabled || n < 0 || n > 127) return false;
        if (on) {
            if (settings.keys && settings.latch && std::none_of(held.begin(), held.end(), [](bool b) { return b; })) count = 0;
            held[n] = true;
            if (std::find(notes.begin(), notes.begin() + count, n) == notes.begin() + count && count < 128) notes[count++] = n;
        } else {
            held[n] = false;
            if (!settings.keys || !settings.latch) {
                auto end = std::remove(notes.begin(), notes.begin() + count, n);
                count = (int)(end - notes.begin());
            }
        }
        return settings.keys;
    }
    ArpPattern pattern() const {
        return compileArp(snapshot());
    }
    ArpSettings snapshot() const { auto a = settings; if (a.keys) { a.notes = notes; a.count = count; } return a; }
private:
    std::array<bool, 128> held {};
    std::array<int, 128> notes {};
    int count = 0;
};
// Single message-thread writer, bounded audio reader. Every payload word is
// atomic, so a concurrent edit is skipped safely until the following block.
class ArpMailbox {
public:
    void publish(const ArpSettings& a) {
        version.fetch_add(1, std::memory_order_seq_cst);
        int i = 0;
        put(i, a.enabled); put(i, a.keys); put(i, a.latch); put(i, a.order);
        put(i, a.octaves); put(i, a.count); put(i, a.seed);
        for (auto n : a.notes) put(i, n);
        for (auto b : a.hits) put(i, b);
        for (auto b : a.accents) put(i, b);
        for (auto b : a.slides) put(i, b);
        rate.store(a.rate); gate.store(a.gate);
        version.fetch_add(1, std::memory_order_seq_cst);
    }
    bool consume(ArpSettings& a, uint32_t& seen) const {
        const auto v = version.load(); if ((v & 1) || v == seen) return false;
        ArpSettings next; int i = 0;
        next.enabled = get(i); next.keys = get(i); next.latch = get(i); next.order = (int)get(i);
        next.octaves = (int)get(i); next.count = (int)get(i); next.seed = get(i);
        for (auto& n : next.notes) n = (int)get(i);
        for (auto& b : next.hits) b = get(i);
        for (auto& b : next.accents) b = get(i);
        for (auto& b : next.slides) b = get(i);
        next.rate = rate.load(); next.gate = gate.load();
        if (version.load() != v) return false;
        a = next; seen = v; return true;
    }
private:
    void put(int& i, uint32_t n) { words[(size_t)i++].store(n); }
    uint32_t get(int& i) const { return words[(size_t)i++].load(); }
    std::array<std::atomic<uint32_t>, 183> words {};
    std::atomic<double> rate {.25}, gate {.65};
    std::atomic<uint32_t> version {0};
};
}
