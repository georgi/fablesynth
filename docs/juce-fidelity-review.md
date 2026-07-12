# JUCE Audio Fidelity Review

Scope: the JUCE wavetable (WT-1), bass (BL-1), drum (DR-1), shared FX, processor,
transport, and sequencer DSP paths under `juce/source/`.

The core architecture is solid — band-limited wavetable mipmaps, double-precision
phase/filter state, ADAA filter drive, denormal protection, and sample-accurate
internal sequencing — but several design choices prevent "highest audio fidelity."
All code citations below were verified against the current tree.

## Findings

### 1. High — MIDI events are not sample-accurate

All MIDI messages are applied before rendering the entire block;
`meta.samplePosition` is ignored. Notes, releases, drum hits, and pitch bends
therefore occur at the block start, up to one buffer early. At 1024 samples /
48 kHz, the error can approach 21 ms.

Evidence:

- `juce/source/PluginProcessor.cpp:273`
- `juce/source/bass/BassProcessor.cpp:250`
- `juce/source/drum/DrumProcessor.cpp:339`

Fix: split rendering at each MIDI event's sample offset, apply the event, then
continue. Pitch bend should also be interpolated or smoothed after the event.

### 2. High — Wavetable updates can produce an intentional full-block dropout

Each engine takes a table mutex with `try_to_lock`; contention causes the entire
output block to become silent. A single table import, deletion, or preset
operation can therefore create an obvious click or dropout.

Evidence:

- `juce/source/dsp/Engine.cpp:879`
- `juce/source/bass/dsp/BassEngine.cpp:604`
- `juce/source/drum/dsp/DrumEngine.cpp:542`

Fix: publish immutable table snapshots atomically and retain the snapshot for
the render call. Another option is an audio-thread-owned pointer swap through a
lock-free command queue. Never substitute silence because a UI operation holds
a lock.

### 3. High — The "safety limiter" is not a limiter

The final stage uses a 2 ms attack envelope without lookahead. Transients pass
before gain reduction develops, and there is no final sample-peak or true-peak
ceiling. Makeup gain makes overshoot more likely.

Evidence:

- `juce/source/dsp/Fx.cpp:78` (attack/release coefficients, WebAudio-matched makeup gain)
- `juce/source/dsp/Fx.cpp:259` (feed-forward peak compressor loop)
- Equivalent code exists in `BassFx.cpp` and `DrumFx.cpp`.

Fix: use a lookahead limiter with a delayed signal path, linked stereo peak
detector, controlled release, and a ceiling around −1 dBTP. For
maximum-fidelity export, detect intersample peaks at 4x. If WebAudio parity is
required, expose the current compressor as "legacy/parity" behavior and offer a
high-quality output mode separately.

### 4. High — Nonlinear FX oversampling has insufficient rejection

The tanh drive uses 2x zero-stuffing with one RBJ biquad before and after the
nonlinearity. A second-order filter does not provide enough stop-band
attenuation for strong nonlinear distortion. Additionally, `shape()` hard-clamps
its input before `tanh`, introducing another nonsmooth nonlinearity.

Evidence:

- `juce/source/dsp/Fx.cpp:71` (single biquad up/down filters at 0.45 · fs)
- `juce/source/dsp/Fx.cpp:150` (`shape()` hard clamp before tanh)
- `juce/source/dsp/Fx.cpp:190` (zero-stuff 2x path)

This implementation is duplicated across WT-1, BL-1, and DR-1.

Fix: use `juce::dsp::Oversampling` or a shared polyphase half-band
implementation at 4x, optionally 8x for offline rendering. Remove the hard
input clamp or replace it with an intentionally smooth pre-saturation curve.
ADAA is already used for filter drive and is preferable where practical.

### 5. Medium-high — Parameter automation is mostly block-rate and discontinuous

APVTS values are sampled once per host block. Most oscillator parameters,
filter resonance/drive, pan, unison controls, FX drive, chorus rate/depth, and
routing state then change abruptly. Only selected FX mix/time values have
per-sample smoothing.

Evidence:

- `juce/source/PluginProcessor.cpp:213`
- `juce/source/bass/BassProcessor.cpp:193`
- `juce/source/drum/DrumProcessor.cpp:283`

Fix:

- Smooth gain/pan/position linearly or exponentially.
- Smooth pitch in the log-frequency domain.
- Smooth filter cutoff in log Hz and resonance conservatively.
- Crossfade filter type, routing, oscillator enable, and wavetable changes.
- Recalculate coefficients per sample or in short sub-blocks using ramped values.

### 6. Medium — Several smoothers are sample-rate and chunk-boundary dependent

Wavetable position uses a fixed `0.35` update and filter cutoff uses `0.5` per
render chunk. Since updates occur every 128 samples — or sooner when transport
boundaries split a chunk — the effective smoothing time changes with sample rate
and musical event placement.

Evidence:

- `juce/source/dsp/Engine.cpp:409` (`o.posSm += (pos - o.posSm) * 0.35`)
- `juce/source/dsp/Engine.cpp:531` (`fs.cutSm += (fc - fs.cutSm) * 0.5`)
- `juce/source/drum/dsp/DrumEngine.cpp:252`
- Bass uses the same cutoff scheme.

Fix: derive coefficients from elapsed samples:

```cpp
coef = 1.0 - std::exp(-numSamples / (tauSeconds * sampleRate));
```

For modulation-sensitive controls, interpolate across the chunk rather than
holding one value.

### 7. Medium — Modulation and glide can produce staircase sidebands

WT-1 oscillator frequency, wavetable position, LFO modulation, and filter
coefficients are held for chunks up to 128 samples. BL-1 improves pitch updates
to 16-sample chunks, but still holds oscillator increments within each
sub-block. This creates staircase FM and audible sidebands at aggressive
modulation settings.

Evidence:

- `juce/source/dsp/Engine.cpp:465`
- `juce/source/dsp/Engine.cpp:473`
- `juce/source/dsp/Engine.cpp:844`
- `juce/source/bass/dsp/BassEngine.cpp:542`

Fix: calculate start/end modulation values and ramp oscillator phase
increments, wavetable morph, pan, and cutoff over each chunk. Audio-rate
modulation routes should be evaluated per sample.

### 8. Medium — Delay and chorus interpolation is only linear

The fractional delay reader uses two-point linear interpolation. This is
inexpensive but introduces frequency-dependent attenuation and more modulation
artifacts than a higher-quality interpolator.

Evidence: `juce/source/dsp/Fx.h:45` (`DelayLine::read`)

Fix: use third-order Lagrange or Hermite interpolation for chorus and modulated
delays. For static delay-time changes, a dual-read-head crossfade avoids
Doppler pitch sweeps unless those are desired.

### 9. Medium — Noise and DC behavior varies with sample rate

Several coefficients are fixed per sample rather than derived from sample rate:

- WT-1 pink-noise filter coefficients.
- DR-1 noise-color one-pole coefficient.
- Per-voice `DC_R = 0.9998`.

At 44.1, 48, 96, and 192 kHz these produce different spectra and cutoff
frequencies.

Evidence:

- `juce/source/dsp/Engine.cpp:779`
- `juce/source/drum/dsp/DrumEngine.cpp:474`
- `juce/source/dsp/Engine.cpp:9` (`static const double DC_R = 0.9998`)

Fix: derive pole coefficients from cutoff and sample rate, or generate noise at
a canonical rate and resample appropriately.

### 10. Medium-low — Wavetable playback uses linear interpolation

The mipmapped tables correctly control aliasing, but two-point interpolation
adds high-frequency droop and residual interpolation images.

Evidence: `juce/source/dsp/Engine.cpp:482`

Fix: use cubic Hermite interpolation, preferably with guard samples to avoid
extra wrapping branches. This is less urgent than MIDI timing and nonlinear
oversampling because the current 2048-sample, band-limited mipmaps are already
a good foundation.

### 11. Medium-low — Re-prepare/reset semantics are incomplete

`Fx::prepare()` redesigns filters but does not comprehensively clear all
existing Biquad, smoother, chorus-phase, and gate state — an `Fx::reset()`
method exists (`juce/source/dsp/Fx.cpp:89`) but is never called from
`prepareToPlay`. WT-1 `Engine::prepare()` also does not reset active voices. A
host sample-rate/device change can retain state calculated for the previous
configuration.

Evidence:

- `juce/source/dsp/Fx.cpp:42` (`prepare` — no state clear)
- `juce/source/dsp/Fx.cpp:89` (`reset` — exists, not wired to prepare)
- `juce/source/dsp/Engine.cpp:109`
- `juce/source/PluginProcessor.cpp:53` (prepareToPlay calls `prepare` only)

Fix: define an explicit reset contract and call it from every `prepareToPlay`.
Reset all recursive state, phase as appropriate, limiter/compressor envelopes,
smoothers, and voices.

### 12. Low — Emergency buffer allocation remains possible on the audio thread

Buffers are preallocated in `prepareToPlay`, but the processors call
`setSize()` inside `processBlock` if the host supplies a larger block than
announced.

Evidence:

- `juce/source/PluginProcessor.cpp:289`
- `juce/source/bass/BassProcessor.cpp:273`
- `juce/source/drum/DrumProcessor.cpp:355`

Fix: render unexpected oversized buffers in preallocated chunks, or allocate to
a documented maximum during preparation.

## Recommended implementation order

1. Sample-accurate MIDI rendering.
2. Lock-free immutable wavetable publication.
3. Real lookahead/true-peak output limiter.
4. Shared high-quality oversampling implementation for all nonlinear FX.
5. Central parameter smoothing/ramping layer.
6. Sample-rate-correct noise, DC, and smoothing coefficients.
7. Cubic delay and wavetable interpolation.
8. Comprehensive reset and high-sample-rate/offline quality modes.

## Testing notes

The existing tests appear heavily focused on WebAudio parity. That is useful,
but strict parity conflicts with several fidelity improvements (notably the
limiter's spec-matched makeup gain and the 2x drive path). Recommendation:
retain a legacy parity mode and add spectral tests for alias energy, automation
discontinuities, true-peak overshoot, sample-rate consistency, MIDI offset
accuracy, and block-size invariance.
