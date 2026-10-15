# TODO

## WT-1 modulation (Serum parity)

Done: LFO 3/4, MOD ENV 2, macros 1–4, MOD WHL, AFTERTCH, RANDOM, per-route
aux source (2026-10-08, web + JUCE). Also: base controls never move, outer mod rings, inner automation arc,
per-source remove items, drag readouts, per-route bipolar mode, and source
highlighting. The items below remain, in priority order.

- **Response curve per route.** The aux source is done (`mat{n}.aux`, scales
  the route by the aux's unipolar value). A per-route response curve remains.
- **More mod destinations.** Add LFO rate, envelope times, FX mix and send
  controls, and the depth of another route (mod of mod). Today there are 27
  oscillator, filter, and mixer targets.
- **Drawable LFO shapes.** Draw the shape with points and curves (reuse the
  automation lane editor geometry). Add a one-shot envelope mode.
- **Multiple rings at the knob top.** With more than one route on a
  parameter, the knob bounds clip the top of the outer rings (web and JUCE).
  Reserve space above the knob or shrink the body when rings exist.
- **JUCE tooltips.** No editor creates a `juce::TooltipWindow`, so every
  `setTooltip()` text is invisible. Ring info shows in the knob label instead.
- **Automation offset drag (optional).** Lanes replace the base value. If
  wanted, an Alt-drag on the inner automation arc could move the whole lane
  up or down.
