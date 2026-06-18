---
name: FableSynth WT-1
description: Night-lab console for wavetable sound design, machined dark with signal-colored accents.
colors:
  signal-cyan: "#4de8ff"
  signal-amber: "#ffa14d"
  signal-violet: "#b18cff"
  signal-slate: "#9fb4d8"
  void: "#06070b"
  panel-high: "#181c26"
  panel-low: "#0d1017"
  display-well: "#07090e"
  knob-body: "#161b25"
  control-fill: "#11141c"
  value-well: "#0a0d13"
  text-ice: "#dfe6f3"
  text-dim: "#6b768c"
  pointer: "#e8edf7"
  hairline: "#ffffff12"
typography:
  display:
    fontFamily: "Michroma, 'Avenir Next', sans-serif"
    fontSize: "clamp(34px, 6vw, 64px)"
    fontWeight: 400
    lineHeight: 1
    letterSpacing: "0.14em"
  title:
    fontFamily: "Michroma, 'Avenir Next', sans-serif"
    fontSize: "17px"
    fontWeight: 400
    lineHeight: 1.2
    letterSpacing: "0.1em"
  body:
    fontFamily: "'IBM Plex Mono', 'SF Mono', Menlo, monospace"
    fontSize: "12px"
    fontWeight: 400
    lineHeight: 1.5
    letterSpacing: "normal"
  label:
    fontFamily: "'IBM Plex Mono', 'SF Mono', Menlo, monospace"
    fontSize: "8px"
    fontWeight: 600
    lineHeight: 1
    letterSpacing: "0.16em"
rounded:
  control: "4px"
  button: "6px"
  group: "8px"
  well: "9px"
  panel: "12px"
  full: "9999px"
spacing:
  hair: "1px"
  xs: "4px"
  sm: "8px"
  md: "16px"
components:
  panel:
    backgroundColor: "{colors.panel-high}"
    textColor: "{colors.text-ice}"
    rounded: "{rounded.panel}"
    padding: "14px"
  display-well:
    backgroundColor: "{colors.display-well}"
    rounded: "{rounded.well}"
    padding: "0"
  button-ghost:
    backgroundColor: "{colors.panel-low}"
    textColor: "{colors.text-ice}"
    typography: "{typography.body}"
    rounded: "{rounded.button}"
    padding: "6px 10px"
  button-ghost-hover:
    backgroundColor: "{colors.panel-low}"
    textColor: "{colors.signal-cyan}"
    rounded: "{rounded.button}"
  stepper-button:
    backgroundColor: "{colors.control-fill}"
    textColor: "{colors.text-dim}"
    rounded: "{rounded.control}"
    size: "18px"
  stepper-value:
    backgroundColor: "{colors.value-well}"
    textColor: "{colors.signal-cyan}"
    rounded: "{rounded.control}"
    padding: "3px 6px"
  led-on:
    backgroundColor: "{colors.signal-cyan}"
    rounded: "{rounded.full}"
    size: "8px"
---

# Design System: FableSynth WT-1

## 1. Overview

**Creative North Star: "The Night-Lab Console"**

FableSynth is a dark studio console seen in a low-lit room, an instrument lit
by its own signal. The chassis is near-black machined metal (`#06070b` washed
by a single cold radial glow from above); the only color comes from powered
elements, the way a real desk only lights up where current flows. Cyan, amber
and violet are not a palette in the decorative sense, they are voltage made
visible: osc A, osc B, and the filter each carry a fixed hue so your eye tracks
signal flow across the rack without reading a label. Type is two voices only,
the geometric Michroma for nameplates and IBM Plex Mono for every readout, set
in tight tracked capitals so the panel reads like calibrated equipment.

Density is deliberate and high. This is a working instrument for someone in
flow, not a spacious marketing surface, so controls sit close, grids are exact,
and every pixel of a panel earns its place. Depth is hybrid: base surfaces are
flat and tonal, while the controls you actually touch (knobs, value wells, the
display readouts) carry the tactile shadow and glow that invite the hand. The
feel of the controls is **weighted and exact**, machined gear you trust.

It explicitly rejects three things. No **skeuomorphic hardware**: no woodgrain,
no brushed-metal photography, no fake screws or beveled rack ears. No **generic
dark SaaS dashboard**: no rounded card grids, no Inter, no single-blue-accent
admin look, no oceans of padding. And no **flat unstyled Web Audio demo**: no
default browser sliders, no identity-free controls. The interface must read as
real gear the instant it loads.

**Key Characteristics:**
- Near-black blue-black chassis lit by one cold radial glow.
- Three fixed signal hues (cyan / amber / violet) that mean osc A / osc B / filter.
- Two type voices: Michroma nameplates, IBM Plex Mono readouts, tracked caps.
- High, deliberate density; exact grids; no wasted space.
- Hybrid depth: flat tonal surfaces, tactile glowing controls.

## 2. Colors

A near-monochrome blue-black field where saturated color appears only on powered, signal-carrying elements.

### Primary
- **Signal Cyan** (#4de8ff): Oscillator A's identity hue, and the system's default
  live color, power-on, focus outlines, the active LED, the wordmark spark. The
  most-seen accent because it doubles as "on".

### Secondary
- **Signal Amber** (#ffa14d): Oscillator B's identity hue and the voice-count
  readout. Warm counterweight to the cyan; the two reading against each other is
  how you tell the oscillators apart at a glance.

### Tertiary
- **Signal Violet** (#b18cff): The filter section's identity hue, on filter knobs,
  arcs and the response curve. Reserved strictly for filtering so it never
  competes with the oscillator pair.
- **Signal Slate** (#9fb4d8): The neutral accent for sections with no signal
  identity (utility, master, modulation), so they stay legible without claiming
  a signal color.

### Neutral
- **Void** (#06070b): The chassis and page base; a blue-black, never pure black,
  carrying a single radial top-glow toward `#11141d`.
- **Panel High / Panel Low** (#181c26 / #0d1017): The vertical gradient of every
  module panel, lighter at the top edge.
- **Display Well** (#07090e): The recessed background of scopes, the 3D wavetable
  view, and every visualization box.
- **Ice Text** (#dfe6f3): Primary readout text and the white knob pointer (#e8edf7).
- **Dim Text** (#6b768c): Labels, units, secondary copy, inactive controls.
- **Hairline** (rgba 255/255/255 .07): The 1px borders and dividers; structure by
  whisper, not by line weight.

### Named Rules
**The Signal-Color Rule.** A saturated hue must mean something. Cyan = osc A,
amber = osc B, violet = filter, slate = neutral sections. Never apply a signal
color decoratively, and never let two sections claim the same hue.

**The No Pure Black Rule.** The darkest surface is `#06070b`, a tinted blue-black.
Pure `#000` and pure `#fff` are forbidden; even the pointer is `#e8edf7`.

## 3. Typography

**Display Font:** Michroma (with Avenir Next, sans-serif)
**Body / Readout Font:** IBM Plex Mono (with SF Mono, Menlo, monospace)

**Character:** Michroma is a wide geometric techno-face used only as a nameplate,
all caps, heavily tracked, so headers feel laser-etched into the chassis. IBM
Plex Mono carries everything functional; its fixed advance keeps numeric readouts
and value columns mechanically aligned, which is the whole point of a console.

### Hierarchy
- **Display** (Michroma 400, clamp 34–64px, line-height 1, +0.14em): The power-on
  wordmark and any hero lockup only. Rare and large.
- **Title** (Michroma 400, 17px, +0.1em): Panel and section nameplates. The only
  other place Michroma appears.
- **Body** (IBM Plex Mono 400, 12px, line-height 1.5): Base readout text, status
  lines, preset names. Cap measure at 65–75ch in any prose context.
- **Label** (IBM Plex Mono 600, 8–9px, +0.16–0.3em, UPPERCASE): Knob labels,
  section sub-labels, units. Tiny, tracked, dimmed; the connective tissue of the rack.

### Named Rules
**The Two-Voice Rule.** Michroma for nameplates, IBM Plex Mono for everything else.
A third typeface is forbidden. Hierarchy comes from size, tracking and case, not
from new families.

**The Tracked-Caps Rule.** Labels are uppercase with positive letter-spacing
(0.14em and up). Lowercase label text reads as web-app, not instrument.

## 4. Elevation

Hybrid by doctrine. The page and panel surfaces are essentially flat and tonal,
distinguished by the panel gradient and a single 1px hairline rather than heavy
shadow. Real depth is spent only on the things you operate: knob bodies get a
radial gradient and their accent arc carries a colored glow, value wells sit
recessed, LEDs and the power button bloom when energized. Shadow is a response to
function and state, not an ambient texture everywhere.

### Shadow Vocabulary
- **Panel lift** (`box-shadow: inset 0 1px 0 rgba(255,255,255,0.05), 0 10px 26px rgba(0,0,0,0.45)`):
  The gentle seat of a module panel; an inner top highlight plus a soft drop.
- **Arc glow** (`filter: drop-shadow(0 0 4px <accent>/70%)`): The colored bloom on
  a knob's value arc; the signal "lighting up".
- **LED bloom** (`box-shadow: 0 0 8px <accent>`): An active status LED.
- **Power bloom** (`box-shadow: 0 0 10px <accent>/70%`): The powered-on transport button.
- **Socket inset** (`box-shadow: inset 0 0 2px rgba(0,0,0,0.7)`): The recessed seat
  of an unlit LED.

### Named Rules
**The Glow-Means-On Rule.** A colored glow signals energized state (powered,
active, focused), never decoration. An idle control does not glow.

## 5. Components

### Knobs (signature)
The defining control. Weighted and exact.
- **Shape:** Circular SVG body, radial gradient seating into `#161b25` (knob-body),
  1px white-alpha rim. Sizes lg/md/sm/xs (74 / 56 / 44 / 34px).
- **Arc:** A 5px rounded track in white-alpha with the live value arc stroked in the
  section accent and a `drop-shadow` glow. A white pointer (#e8edf7) marks position.
- **Label/Value:** 8px tracked-caps label below; the numeric value (9px) is hidden at
  rest and fades in on hover/drag/focus (0.15s).
- **Interaction:** `ns-resize`; vertical drag, shift = fine, double-click reset, wheel.
  Focus shows a 1px accent outline. The xs size drops its label.

### Steppers
Enum selectors (wave table, filter type, routing).
- **Layout:** tracked-caps label, an 18px −/+ button pair (4px radius, `#11141c` fill,
  dim glyph), and a centered value pill.
- **Value pill:** accent text on `#0a0d13`, 1px hairline, 4px radius, 3px 6px padding.
- **Hover:** buttons shift glyph and border to the section accent.

### Panels / Modules
- **Corner Style:** 12px radius.
- **Background:** vertical gradient `#181c26 → #0d1017` (panel-high to panel-low).
- **Shadow:** Panel lift (see Elevation); inset top highlight + soft drop.
- **Border:** 1px hairline.
- **Header:** Michroma title at 17px, +0.1em; a section may set its accent so child
  controls inherit the right signal hue.
- **Internal Padding:** 14px; inner grids gap at 8px (spacing.sm).

### Display Wells (scope / wavetable / views)
- **Style:** `#07090e` fill, 9px radius, 1px hairline, a faint top gradient sheen
  (white-alpha .02 fading by 30%).
- **Role:** Houses every live visualization (3D wavetable terrain, oscilloscope,
  spectrum, filter curve, env/LFO). The signal is drawn in the section accent.

### Buttons (ghost) & Preset Select
- **Shape:** 6px radius, 1px hairline, `#0d1017`-class fill, IBM Plex Mono 11px.
- **Default:** dim or ice text.
- **Hover:** border and text shift to the accent (cyan default). Preset select shows
  accent text and a 1px accent focus outline.

### LED & Power Button
- **LED:** an 8px circle; off is a dim disc with an inset socket shadow, on is the
  accent fill with an 8px glow.
- **Power button:** a circle with a radial-gradient face; powered-on becomes an
  accent radial with a 10px glow and accent-tinted border. It is also the audio
  unlock gesture in the browser build.

## 6. Do's and Don'ts

### Do:
- **Do** keep the chassis blue-black (`#06070b`) lit by a single cold radial glow;
  surfaces tonal and flat.
- **Do** reserve each saturated hue for its signal: cyan = osc A, amber = osc B,
  violet = filter, slate = neutral sections (The Signal-Color Rule).
- **Do** set all labels in tracked uppercase IBM Plex Mono (+0.14em or more);
  Michroma only for nameplates.
- **Do** spend depth on operated controls: radial knob bodies, glowing value arcs,
  recessed value wells, blooming LEDs.
- **Do** glow only to signal an energized state (The Glow-Means-On Rule).
- **Do** keep density high and grids exact; this is an instrument for someone in flow.

### Don't:
- **Don't** ship **skeuomorphic hardware**: no woodgrain, brushed-metal photos,
  fake screws or beveled rack ears.
- **Don't** drift toward a **generic dark SaaS dashboard**: no rounded card grids,
  no Inter, no single-blue admin accent, no oceans of empty padding.
- **Don't** let it look like a **flat unstyled Web Audio demo**: no default browser
  `<input type=range>`, no identity-free controls.
- **Don't** use a colored `border-left`/`border-right` stripe as an accent; use the
  full hairline, a tint, or the section accent on the control itself.
- **Don't** introduce a third typeface or a fourth saturated hue.
- **Don't** use pure `#000` or `#fff`, anywhere, ever.
- **Don't** glow decoratively; an idle control is dark.
