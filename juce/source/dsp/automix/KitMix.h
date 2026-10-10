#pragma once
#include "AutoEq.h"
#include <optional>
#include <vector>

// DR-1 kit auto-mix. Port of src/drum/kitMix.ts: learn each pad's post-FX
// spectrum, then move the pad levels so the summed kit follows a reference
// tonal curve (penalised Gauss-Newton over per-pad dB moves).
namespace fable::automix {

/** Accumulates per-pad band power from capture blocks. */
class KitCapture {
public:
    explicit KitCapture(double sr, int pads = 16) : sampleRate(sr), bands((size_t)pads, Bands{}) {}
    /** `data` is one pad's block (a power-of-two length); `frames` the samples since capture began. */
    void add(int pad, const float* data, int size, int frames);
    double seconds() const { return frames / sampleRate; }
    /** Mean band power per pad over the capture window. */
    std::vector<Bands> meanBands() const;

    double sampleRate;
    std::vector<Bands> bands;
    /** Samples since capture began, the averaging window for every pad. */
    int frames = 0;
};

struct KitMixFit {
    /** Pads with enough energy to balance. */
    std::vector<bool> heard;
    /** Planned level move per pad in dB (0 for pads left alone). */
    std::vector<double> moves;
    /** New `lvl` value per pad, or empty when the pad does not move. */
    std::vector<std::optional<double>> levels;
    /** Kit band levels before and after, aligned to the reference scale. */
    std::vector<double> before, after, target;
    std::vector<bool> present;
    /** RMS band error against the reference, dB, before and after. */
    double errorBefore = 0, errorAfter = 0;
    /** Each heard pad's share of the kit energy, 0..1. */
    std::vector<double> share;
};

/**
 * `pads` holds each pad's mean band power, `lvl` its level param (gain =
 * lvl^2). `amount` scales the fitted moves. Empty when fewer than two pads play.
 */
std::optional<KitMixFit> fitKitMix(const std::vector<Bands>& pads, const std::vector<double>& lvl,
                                   const EqReference& ref, double amount);

}
