# TODO

## WT-1 modulation (Serum parity)

Done: LFO 3/4, MOD ENV 2, macros 1–4, MOD WHL, AFTERTCH, RANDOM, per-route
aux source (2026-10-08, web + JUCE). Also: base controls never move, outer mod rings, inner automation arc,
per-source remove items, drag readouts, per-route bipolar mode, and source
highlighting.

Done 2026-11-12 (web + JUCE): response curve per route (`mat{n}.crv`), more
mod destinations (warp amount, LFO rates, envelope times, six FX controls, the
amount of another route), drawn LFO shapes with a one-shot mode (`lfoN.p*`,
`lfoN.oneshot`), stacked rings inside the knob bounds, and JUCE tooltips. The
items below remain.

- **Automation offset drag (optional).** Lanes replace the base value. If
  wanted, an Alt-drag on the inner automation arc could move the whole lane
  up or down.
- **Larger LFO shape editor.** The CUSTOM shape is drawn in the 62 px LFO
  display. A pop-out editor with a finer grid would make detailed shapes
  easier.
- **Envelope sustain and more FX controls as destinations.** The envelope
  destinations are the times; the FX ones are drive amount, the stage mixes and
  delay feedback.

## WT-1 oscillators

Done 2026-11-12 (web + JUCE): warp modes SYNC, SYNC WIN, BEND, ASYM, PWM,
MIRROR, QUANTIZE, and FM / AM / RM from the other oscillator.

- **Serum import of warp and LFO shapes.** The importer still maps neither
  (`juce/source/serumimport/SerumPatch.cpp`, `tools/serum1/convert.py`). Measure
  the Serum warp curves and LFO point data, then map them to `oscX.warp` and
  the `lfoN.p*` points.
- **FM from the sub or the noise oscillator, and the modulator's own warp.**
  The FM / AM / RM modulator is the other oscillator's unwarped table.

## FX

Done 2026-11-12 (web + JUCE): PLATE reverb (`fx.reverb.type`, `fx.reverb.damp`)
beside the ROOM comb network.

- **PLATE for DR-1 and BL-1.** Their reverbs are still the comb network. The
  tank is a shared primitive (`PlateReverb` in `ott-worklet.js`,
  `juce/source/dsp/PlateReverb.h`).
