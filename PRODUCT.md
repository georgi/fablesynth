# Product

## Register

product

## Users

Bedroom producers and hobbyist musicians making tracks at home, mostly in a DAW
(VST3/AU) and sometimes in the browser demo. They are creative, not engineers:
they want to land an inspiring sound fast, tweak by ear, and stay in flow. They
appreciate depth and pro-grade capability but won't read a manual to get there.
The job on any given screen is *sculpt a sound* — reach for an oscillator, push
a filter, dial in modulation, hear the result immediately.

## Product Purpose

FableSynth WT-1 is a real wavetable synthesizer instrument (VST3 · AU ·
Standalone, plus an in-browser prototype). It exists to make Serum-class
wavetable sound design feel immediate and trustworthy without a hardware budget
or a learning wall. Success is a producer loading it, getting to a sound they
love within a minute, and reaching for it again on the next track — not admiring
a tech demo and closing the tab.

## Brand Personality

Precise, machined, confident. It reads like pro studio hardware: engineered,
deliberate, a little futuristic. Voice is terse and technical, labels are
uppercase and units are shown, nothing is flimsy or apologetic. The emotional
goal is the quiet confidence of gear you trust, capability without intimidation.

## Anti-references

- **Skeuomorphic hardware** — woodgrain, brushed-metal photos, fake screws and
  bezels (the Arturia/IK pastiche). FableSynth is machined and modern, not a
  photo of a rack unit.
- **Generic dark SaaS dashboard** — rounded card grids, Inter, a single blue
  accent, oceans of empty padding. This is an instrument, not an admin panel.
- **Flat unstyled Web Audio demo** — default browser sliders, no identity, no
  craft. The interface must earn trust as a tool the moment it loads.

## Design Principles

- **Instrument first, demo never.** Every surface has to feel like real gear you
  reach for. Craft in the DSP is mirrored by craft in the UI; nothing reads as a
  toy or a proof of concept.
- **Inspiring without a manual.** Bedroom producers reach a great sound fast.
  Depth (mod matrix, dual filter, unison) is available but never a wall; defaults
  are musical and feedback is immediate.
- **Show the signal.** The synth makes its internal state visible — the live 3D
  wavetable terrain, oscilloscope, spectrum, filter curve. Visualization is core
  identity and a sound-design aid, not decoration.
- **Machined confidence.** Tight alignment, deliberate density, hardware-grade
  precision. Controls feel weighted and exact; layout is engineered, not padded.
- **Color is function.** Accent hues carry meaning and wayfinding — cyan = osc A,
  amber = osc B, violet = filter, neutral elsewhere. Color is never arbitrary.

## Accessibility & Inclusion

Target WCAG AA for text. The dark palette is intentional (a focused, low-light
studio surface), so watch contrast on the dimmed/secondary text against darker
panels and bump it where small text drops below AA. The osc/filter identity is
encoded by hue (cyan/amber/violet) — never rely on color alone; pair it with
position and labels so color-blind users can still tell A from B from filter.
Respect `prefers-reduced-motion` for the animated terrain, scope and spectrum.
Knobs stay operable by keyboard, scroll wheel and fine-drag, not just coarse
mouse drag.
