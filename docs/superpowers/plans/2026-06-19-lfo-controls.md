# LFO Controls Expansion — Implementation Plan

> Exact-code plan. Two independent tracks (Web, JUCE) touch **disjoint files** and can be implemented in parallel. Both MUST use the identical param ids, division list/factors, defaults, and DSP formula below. Do **not** commit; the orchestrator commits.

**Goal:** Add tempo sync, rise/fade-in, and phase+retrig to both LFOs in the web app and the JUCE plugin.

**Spec:** `docs/superpowers/specs/2026-06-19-lfo-controls-design.md`

## Shared constants (MUST match exactly on both platforms)

- New per-LFO params (`lfo1` and `lfo2`), added after `lfoN.rate`:
  - `lfoN.sync` — bool, default 0
  - `lfoN.syncrate` — enum over `LFO_DIVS`, default index 2 (`1/4`)
  - `lfoN.rise` — float lin 0..5 s, default 0, `fmtSec`
  - `lfoN.phase` — float lin 0..1, default 0, `fmtPct`
  - `lfoN.retrig` — bool, default 1
- `LFO_DIVS = ['1/1','1/2','1/4','1/4.','1/4T','1/8','1/8.','1/8T','1/16','1/16T','1/32']`
- `LFO_DIV_F = [0.25, 0.5, 1, 2/3, 1.5, 2, 4/3, 3, 4, 6, 8]`  (cycles per beat)
- Rate: `hz = sync ? (bpm/60) * LFO_DIV_F[syncrate] : rate`
- Rise gain (per-voice): `rise<=0 ? 1 : min(1, elapsed/(rise*sr))`
- Phase read: `valueOff(shape, off)` uses `frac(phase + off)`
- Retrig=0 → read a single **global** LFO phase; Retrig=1 → per-voice phase (reset on note-on). Rise is per-voice in both modes.

---

# Track WEB

## W1 — `src/params.ts`

Add `LFO_DIVS` next to the other option lists (after `LFO_SHAPES`):

```ts
export const LFO_DIVS = ['1/1', '1/2', '1/4', '1/4.', '1/4T', '1/8', '1/8.', '1/8T', '1/16', '1/16T', '1/32'];
```

Replace the four existing LFO param lines:

```ts
  { id: 'lfo1.shape', type: 'enum', options: LFO_SHAPES, def: 0 },
  { id: 'lfo1.rate', label: 'RATE', min: 0.02, max: 30, def: 2, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
  { id: 'lfo2.shape', type: 'enum', options: LFO_SHAPES, def: 0 },
  { id: 'lfo2.rate', label: 'RATE', min: 0.02, max: 30, def: 5, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
```

with (helper keeps the two blocks identical):

```ts
  ...lfoParams('lfo1', 2),
  ...lfoParams('lfo2', 5),
```

and add this factory above `PARAM_DEFS` (near `matSlot`):

```ts
function lfoParams(prefix: string, defRate: number): ParamDef[] {
  return [
    { id: `${prefix}.shape`, type: 'enum', options: LFO_SHAPES, def: 0 },
    { id: `${prefix}.rate`, label: 'RATE', min: 0.02, max: 30, def: defRate, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
    { id: `${prefix}.sync`, type: 'bool', def: 0 },
    { id: `${prefix}.syncrate`, type: 'enum', options: LFO_DIVS, def: 2 },
    { id: `${prefix}.rise`, label: 'RISE', min: 0, max: 5, def: 0, curve: 'lin', fmt: fmtSec },
    { id: `${prefix}.phase`, label: 'PHASE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.retrig`, type: 'bool', def: 1 },
  ];
}
```

## W2 — `src/engine/worklet.js`

Add the factor table near the top constants (after `COMB_MAX`):

```js
// LFO note-division factors (cycles per beat, beat = quarter note). Index maps
// to params.ts LFO_DIVS.
const LFO_DIV_F = [0.25, 0.5, 1, 2 / 3, 1.5, 2, 4 / 3, 3, 4, 6, 8];
```

Replace the whole `LFO` class with:

```js
class LFO {
  constructor() { this.phase = 0; this.hold = 0; this.elapsed = 0; }
  reset() { this.phase = 0; this.hold = Math.random() * 2 - 1; this.elapsed = 0; }
  // Read the shape at a wrapped phase offset (for the start-phase control).
  valueOff(shape, off) {
    let p = this.phase + off; p -= Math.floor(p);
    switch (shape | 0) {
      case 0: return Math.sin(2 * Math.PI * p);
      case 1: return 1 - 4 * Math.abs(p - 0.5);
      case 2: return 1 - 2 * p;
      case 3: return p < 0.5 ? 1 : -1;
      default: return this.hold;
    }
  }
  // Fade-in gain, per-voice, keyed off note-on (samples since reset).
  riseGain(riseSec) { return riseSec <= 0 ? 1 : Math.min(1, this.elapsed / (riseSec * sampleRate)); }
  advance(rate, n) {
    this.elapsed += n;
    this.phase += (rate * n) / sampleRate;
    if (this.phase >= 1) { this.phase -= Math.floor(this.phase); this.hold = Math.random() * 2 - 1; }
  }
}
```

In `FableProcessor` constructor add (near `this.bend = 0;`):

```js
    this.bpm = 120;
    this.gLfo1 = new LFO(); this.gLfo2 = new LFO();
```

In `onMsg`, add a case (after `case 'bend'`):

```js
      case 'bpm': this.bpm = d.v > 0 ? d.v : 120; break;
```

Add a helper method on `FableProcessor` (e.g. right above `renderVoice`):

```js
  lfoHz(pre) {
    if (this.p[pre + '.sync']) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[pre + '.syncrate'] | 0));
      return (this.bpm / 60) * LFO_DIV_F[i];
    }
    return this.p[pre + '.rate'];
  }
```

In `renderVoice`, replace:

```js
    const l1 = v.lfo1.value(p['lfo1.shape']);
    const l2 = v.lfo2.value(p['lfo2.shape']);
```

with:

```js
    const rt1 = !!p['lfo1.retrig'], rt2 = !!p['lfo2.retrig'];
    const l1 = (rt1 ? v.lfo1 : this.gLfo1).valueOff(p['lfo1.shape'], p['lfo1.phase']) * v.lfo1.riseGain(p['lfo1.rise']);
    const l2 = (rt2 ? v.lfo2 : this.gLfo2).valueOff(p['lfo2.shape'], p['lfo2.phase']) * v.lfo2.riseGain(p['lfo2.rise']);
```

In `renderVoice`, replace the advance lines:

```js
    v.lfo1.advance(p['lfo1.rate'], n);
    v.lfo2.advance(p['lfo2.rate'], n);
```

with:

```js
    v.lfo1.advance(this.lfoHz('lfo1'), n);
    v.lfo2.advance(this.lfoHz('lfo2'), n);
```

In `process`, after the `for (const v of this.voices)` loop (right after it, before `this.vizCount += n;`), advance the global LFOs once per block:

```js
    // Free-running LFOs advance once per block regardless of active voices.
    this.gLfo1.advance(this.lfoHz('lfo1'), n);
    this.gLfo2.advance(this.lfoHz('lfo2'), n);
```

(`value(shape)` is now unused; leaving or removing it is fine — prefer removing it to avoid dead code.)

## W3 — `src/components/panels/LfoPanel.tsx`

Replace the file with:

```tsx
import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { LFOView } from '../displays/LFOView';
import { ACCENTS } from '../../constants';
import { useStore } from '../../store';

function Toggle({ id, label }: { id: string; label: string }) {
  const v = useStore((s) => s.params[id]);
  const setParam = useStore((s) => s.setParam);
  return (
    <button className={`lfo-tg${v ? ' on' : ''}`} aria-pressed={!!v} onClick={() => setParam(id, v ? 0 : 1)}>
      {label}
    </button>
  );
}

function LfoBlock({ id, title, accentKey }: { id: 'lfo1' | 'lfo2'; title: string; accentKey: 'a' | 'b' }) {
  const shape = useStore((s) => s.params[`${id}.shape`]);
  const rate = useStore((s) => s.params[`${id}.rate`]);
  const sync = useStore((s) => s.params[`${id}.sync`]);
  return (
    <div className="lfo-block">
      <div className="panel-head">
        <h2>{title}</h2>
        <span className="ph-stepper"><Stepper paramId={`${id}.shape`} /></span>
      </div>
      <LFOView className="lfo-curve" shape={shape} rate={rate} accent={ACCENTS[accentKey]} />
      <div className="lfo-toggles">
        <Toggle id={`${id}.sync`} label="SYNC" />
        <Toggle id={`${id}.retrig`} label="TRIG" />
      </div>
      <div className="knob-row lfo-knobs">
        {sync
          ? <span className="lfo-div"><Stepper paramId={`${id}.syncrate`} accent={ACCENTS[accentKey]} /></span>
          : <Knob paramId={`${id}.rate`} size="sm" accent={accentKey} />}
        <Knob paramId={`${id}.rise`} size="sm" accent={accentKey} />
        <Knob paramId={`${id}.phase`} size="sm" accent={accentKey} />
      </div>
    </div>
  );
}

export function LfoPanel() {
  return (
    <section className="panel panel-lfos" style={{ gridArea: 'lfos' }}>
      <LfoBlock id="lfo1" title="LFO 1" accentKey="a" />
      <LfoBlock id="lfo2" title="LFO 2" accentKey="b" />
    </section>
  );
}
```

Confirm `Knob`'s `size` prop accepts `"sm"` and `accent` accepts `'a'|'b'` (it does — see existing usage). Confirm `Stepper`'s `accent` prop type (string) matches passing `ACCENTS[accentKey]`; if `Stepper` expects an accent key rather than a colour, pass `accentKey`-compatible value per that component's signature.

## W4 — `src/index.css`

Add LFO control styles near the existing `.lfo-block` / `.lfo-curve` rules (match the cyan-glow aesthetic and the existing `.power-btn` / `.stepper` look). Required classes: `.lfo-toggles` (flex row, small gap, centered), `.lfo-tg` (small pill button: dim border/text by default; lit accent border + glow + brighter text when `.on`), `.lfo-knobs` (the knob row; ensure it lays out 2–3 children without overflow — `min-width: 0` on children if needed), `.lfo-div` (wraps the compact division stepper to occupy the first knob slot height). Keep it consistent with existing panel styling; no fixed pixel widths that could overflow the half-panel.

## WEB build gate

`npm run build` (runs `tsc && vite build`) must pass with no type errors.

---

# Track JUCE

## J1 — `juce/source/dsp/Params.h`

Add the field enum next to the other field enums (after `MatField`):

```cpp
enum LfoField { LFO_SHAPE, LFO_RATE, LFO_SYNC, LFO_SYNCRATE, LFO_RISE, LFO_PHASE, LFO_RETRIG, LFO_NFIELDS };
```

Add the division list + factor helper near `LFO_SHAPES`:

```cpp
inline const std::vector<std::string> LFO_DIVS = {"1/1", "1/2", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16T", "1/32"};

// Cycles per beat (beat = quarter note) for each LFO_DIVS entry. Clamped.
inline double lfoDivFactor(int i) {
    static const double f[] = {0.25, 0.5, 1.0, 2.0 / 3.0, 1.5, 2.0, 4.0 / 3.0, 3.0, 4.0, 6.0, 8.0};
    constexpr int n = (int)(sizeof(f) / sizeof(f[0]));
    return f[i < 0 ? 0 : (i >= n ? n - 1 : i)];
}
```

In the `Pid` enum, change the LFO/MAT base computation so each LFO block is `LFO_NFIELDS` wide:

```cpp
    LFO1_BASE   = ENV2_BASE + 4,              // shape,rate,sync,syncrate,rise,phase,retrig
    LFO2_BASE   = LFO1_BASE + LFO_NFIELDS,
    MAT1_BASE   = LFO2_BASE + LFO_NFIELDS,     // 4 slots x 3
```

(All downstream bases are computed relative to `MAT1_BASE`, so they shift automatically. `NUM_PARAMS` grows by 10.)

## J2 — `juce/source/dsp/Params.cpp`

Replace the four `lfo1`/`lfo2` descriptor `push_back`s with a helper + two calls:

```cpp
    auto addLfo = [&](const std::string& pre, int base, float defRate) {
        v.push_back({base + LFO_SHAPE,    pre + ".shape",    "SHAPE", 0, (float)LFO_SHAPES.size() - 1, 0, Curve::Int, Kind::Enum, &LFO_SHAPES});
        v.push_back({base + LFO_RATE,     pre + ".rate",     "RATE",  0.02f, 30, defRate, Curve::Log, Kind::Float, nullptr});
        v.push_back({base + LFO_SYNC,     pre + ".sync",     "SYNC",  0, 1, 0, Curve::Int, Kind::Bool, nullptr});
        v.push_back({base + LFO_SYNCRATE, pre + ".syncrate", "DIV",   0, (float)LFO_DIVS.size() - 1, 2, Curve::Int, Kind::Enum, &LFO_DIVS});
        v.push_back({base + LFO_RISE,     pre + ".rise",     "RISE",  0, 5, 0, Curve::Lin, Kind::Float, nullptr});
        v.push_back({base + LFO_PHASE,    pre + ".phase",    "PHASE", 0, 1, 0, Curve::Lin, Kind::Float, nullptr});
        v.push_back({base + LFO_RETRIG,   pre + ".retrig",   "RETRIG",0, 1, 1, Curve::Int, Kind::Bool, nullptr});
    };
    addLfo("lfo1", LFO1_BASE, 2);
    addLfo("lfo2", LFO2_BASE, 5);
```

## J3 — `juce/source/dsp/Engine.h`

Replace the `Lfo` class with:

```cpp
class Lfo {
public:
    double phase = 0, hold = 0;
    long   elapsed = 0;                 // samples since reset (for rise/fade-in)
    Rng*   rng = nullptr;
    void   reset() { phase = 0; hold = rng->next() * 2 - 1; elapsed = 0; }
    double value(int shape) const;                      // reads this.phase
    double valueOff(int shape, double off) const;       // reads frac(phase + off)
    double riseGain(double riseSec, double sr) const {
        return riseSec <= 0 ? 1.0 : std::min(1.0, (double)elapsed / (riseSec * sr));
    }
    void   advance(double rate, int n, double sr);
};
```

(`<algorithm>` is needed for `std::min`; Engine.cpp already includes it — Engine.h uses `std::min` only in the inlined `riseGain`, so add `#include <algorithm>` to Engine.h to be safe.)

In `class Engine`, add a public BPM setter and private global LFOs + helper:

- public: `void setBpm(double b) { bpm_ = (b > 1.0 ? b : 120.0); }`
- private members (near `Rng rng_;`): `Lfo gLfo1_, gLfo2_; double bpm_ = 120;`
- private method decl: `double lfoHz(int base) const;`

## J4 — `juce/source/dsp/Engine.cpp`

Add `valueOff` next to `value` and update `advance`:

```cpp
double Lfo::valueOff(int shape, double off) const {
    double p = phase + off;
    p -= std::floor(p);
    switch (shape) {
        case 0: return std::sin(2 * PI * p);
        case 1: return 1 - 4 * std::abs(p - 0.5);
        case 2: return 1 - 2 * p;
        case 3: return p < 0.5 ? 1 : -1;
        default: return hold;
    }
}
void Lfo::advance(double rate, int n, double sr) {
    elapsed += n;
    phase += (rate * n) / sr;
    if (phase >= 1) { phase -= std::floor(phase); hold = rng->next() * 2 - 1; }
}
```

In `Engine::prepare`, give the global LFOs an rng and reset them:

```cpp
void Engine::prepare(double sampleRate) {
    sr_ = sampleRate;
    for (auto& v : voices_) { v.lfo1.rng = &rng_; v.lfo2.rng = &rng_; }
    gLfo1_.rng = &rng_; gLfo2_.rng = &rng_;
    gLfo1_.reset(); gLfo2_.reset();
}
```

Add the helper (e.g. just above `renderVoice`):

```cpp
double Engine::lfoHz(int base) const {
    if (p_[base + LFO_SYNC] != 0)
        return (bpm_ / 60.0) * lfoDivFactor((int)p_[base + LFO_SYNCRATE]);
    return p_[base + LFO_RATE];
}
```

In `renderVoice`, replace:

```cpp
    double l1 = v.lfo1.value((int)p_[LFO1_BASE + 0]);
    double l2 = v.lfo2.value((int)p_[LFO2_BASE + 0]);
```

with:

```cpp
    bool   rt1 = p_[LFO1_BASE + LFO_RETRIG] != 0, rt2 = p_[LFO2_BASE + LFO_RETRIG] != 0;
    double l1 = (rt1 ? v.lfo1 : gLfo1_).valueOff((int)p_[LFO1_BASE + LFO_SHAPE], p_[LFO1_BASE + LFO_PHASE])
                * v.lfo1.riseGain(p_[LFO1_BASE + LFO_RISE], sr_);
    double l2 = (rt2 ? v.lfo2 : gLfo2_).valueOff((int)p_[LFO2_BASE + LFO_SHAPE], p_[LFO2_BASE + LFO_PHASE])
                * v.lfo2.riseGain(p_[LFO2_BASE + LFO_RISE], sr_);
```

In `renderVoice`, replace the advance lines:

```cpp
    v.lfo1.advance(p_[LFO1_BASE + 1], n, sr_);
    v.lfo2.advance(p_[LFO2_BASE + 1], n, sr_);
```

with:

```cpp
    v.lfo1.advance(lfoHz(LFO1_BASE), n, sr_);
    v.lfo2.advance(lfoHz(LFO2_BASE), n, sr_);
```

In `renderBlock`, after the `for (auto& v : voices_)` loop (right after `vizActive = act;` / the idle reset is fine too — place after the loop), advance the global LFOs once per block:

```cpp
    // Free-running LFOs advance once per block regardless of active voices.
    gLfo1_.advance(lfoHz(LFO1_BASE), n, sr_);
    gLfo2_.advance(lfoHz(LFO2_BASE), n, sr_);
```

## J5 — `juce/source/PluginProcessor.cpp`

In `processBlock`, after pulling params into the engine array (after the `fx.setParams(p);` line, before the MIDI loop), push host tempo:

```cpp
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    engine.setBpm(bpm);
```

(Uses the JUCE 7 `AudioPlayHead::getPosition()` API, which returns `Optional<PositionInfo>`; `getBpm()` returns `Optional<double>`. Verify against the JUCE version in use; if older, fall back to `CurrentPositionInfo`.)

## J6 — `juce/source/ui/Panels.h`

Replace the `LfoPanel` class with one that owns the new controls and runs a timer to swap rate/division visibility:

```cpp
class LfoPanel : public juce::Component, private juce::Timer {
public:
    explicit LfoPanel(APVTS&);
    ~LfoPanel() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    struct Block {
        Block(APVTS&, juce::String id, juce::String title, Accent);
        juce::String title;
        Stepper shape;
        LfoView view;
        juce::TextButton syncBtn{"SYNC"}, retrigBtn{"TRIG"};
        std::unique_ptr<APVTS::ButtonAttachment> syncAtt, retrigAtt;
        Stepper syncRate;
        Knob rate, rise, phase;
        bool lastSync = false;
        juce::Rectangle<int> titleArea, slot0;   // slot0 = rate/div swap area
        void layout(juce::Rectangle<int>);
        void paintTitle(juce::Graphics&);
        void applySync(bool sync);                // toggle rate/syncRate visibility
    };
    Block l1, l2;
};
```

Ensure `APVTS` is the alias already used in the header and `<juce_gui_basics...>`/TextButton are available (Panels.h already pulls in JUCE GUI).

## J7 — `juce/source/ui/Panels.cpp`

Replace the `LfoPanel` section. The `Block` ctor wires attachments; `applySync` toggles visibility; the panel timer polls each block's `sync` param and re-lays-out on change. Reference implementation:

```cpp
LfoPanel::Block::Block(APVTS& s, juce::String id, juce::String t, Accent ac)
    : title(t),
      shape(s, id + ".shape", Accent::N),
      view(s, id + ".shape", id + ".rate", accentColour(ac)),
      syncRate(s, id + ".syncrate", ac),
      rate(s, id + ".rate", Knob::Sm, ac),
      rise(s, id + ".rise", Knob::Sm, ac),
      phase(s, id + ".phase", Knob::Sm, ac) {
    syncBtn.setClickingTogglesState(true);
    retrigBtn.setClickingTogglesState(true);
    for (auto* b : { &syncBtn, &retrigBtn }) {
        b->setColour(juce::TextButton::buttonOnColourId, accentColour(ac));
        b->setColour(juce::TextButton::textColourOnId, col::bg);
    }
    syncAtt   = std::make_unique<APVTS::ButtonAttachment>(s, id + ".sync", syncBtn);
    retrigAtt = std::make_unique<APVTS::ButtonAttachment>(s, id + ".retrig", retrigBtn);
}

void LfoPanel::Block::applySync(bool sync) {
    lastSync = sync;
    rate.setVisible(!sync);
    syncRate.setVisible(sync);
}

void LfoPanel::Block::layout(juce::Rectangle<int> r) {
    auto head = r.removeFromTop(20);
    shape.setBounds(head.removeFromRight(90).withSizeKeepingCentre(90, 18));
    titleArea = head;
    r.removeFromTop(5);
    view.setBounds(r.removeFromTop(38));
    r.removeFromTop(5);
    auto tg = r.removeFromTop(18);
    syncBtn.setBounds(tg.removeFromLeft(tg.getWidth() / 2).reduced(2, 0));
    retrigBtn.setBounds(tg.reduced(2, 0));
    r.removeFromTop(5);
    auto row = r.removeFromTop(60);
    int w = row.getWidth() / 3;
    slot0 = row.removeFromLeft(w);
    rate.setBounds(slot0.withSizeKeepingCentre(40, 56));
    syncRate.setBounds(slot0.withSizeKeepingCentre(w - 4, 18));
    rise.setBounds(row.removeFromLeft(w).withSizeKeepingCentre(40, 56));
    phase.setBounds(row.withSizeKeepingCentre(40, 56));
    applySync(lastSync);
}

void LfoPanel::Block::paintTitle(juce::Graphics& g) { paintHeaderTitle(g, titleArea, title, col::text); }

LfoPanel::LfoPanel(APVTS& s) : l1(s, "lfo1", "LFO 1", Accent::A), l2(s, "lfo2", "LFO 2", Accent::B) {
    for (auto* b : { &l1, &l2 }) {
        addAndMakeVisible(b->shape);
        addAndMakeVisible(b->view);
        addAndMakeVisible(b->syncBtn);
        addAndMakeVisible(b->retrigBtn);
        addAndMakeVisible(b->syncRate);
        addAndMakeVisible(b->rate);
        addAndMakeVisible(b->rise);
        addAndMakeVisible(b->phase);
    }
    // Initialise swap state from current param values.
    l1.applySync(s.getRawParameterValue("lfo1.sync")->load() != 0.0f);
    l2.applySync(s.getRawParameterValue("lfo2.sync")->load() != 0.0f);
    startTimerHz(15);
}

void LfoPanel::timerCallback() {
    auto& s = l1.rate.apvtsRef(); // if Knob exposes its APVTS; otherwise store APVTS& in LfoPanel
    // Poll sync params and re-layout on change.
    bool s1 = s.getRawParameterValue("lfo1.sync")->load() != 0.0f;
    bool s2 = s.getRawParameterValue("lfo2.sync")->load() != 0.0f;
    if (s1 != l1.lastSync) { l1.applySync(s1); }
    if (s2 != l2.lastSync) { l2.applySync(s2); }
}

void LfoPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    l1.paintTitle(g); l2.paintTitle(g);
}

void LfoPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto left = r.removeFromLeft(r.getWidth() / 2);
    left.removeFromRight(7);
    l1.layout(left);
    r.removeFromLeft(7);
    l2.layout(r);
}
```

**Important:** `Knob` does not expose its APVTS. Store an `APVTS&` reference in `LfoPanel` (add `APVTS& apvts;` member, init in ctor) and use it in `timerCallback` instead of the `apvtsRef()` placeholder above. Verify `col::bg` / `col::text` exist in `Theme.h` (use the correct theme colour names); `accentColour(Accent)` and `paintHeaderTitle` / `paintPanelBg` are already used in this file.

## J8 — `juce/test/engine_test.cpp`

Add a new section (use the existing `check(...)` harness; place near the other engine sections):

```cpp
    {
        using namespace fable;
        check(std::abs(lfoDivFactor(2) - 1.0) < 1e-9, "lfoDivFactor 1/4 = 1.0");
        check(std::abs(lfoDivFactor(5) - 2.0) < 1e-9, "lfoDivFactor 1/8 = 2.0");
        check(std::abs(lfoDivFactor(0) - 0.25) < 1e-9, "lfoDivFactor 1/1 = 0.25");

        auto dp = defaultParams();
        check(dp[LFO1_BASE + LFO_RETRIG] == 1.0f, "lfo retrig defaults on (legacy behaviour)");
        check(dp[LFO1_BASE + LFO_SYNC] == 0.0f, "lfo sync defaults off");
        check((int)dp[LFO1_BASE + LFO_SYNCRATE] == 2, "lfo syncrate defaults 1/4");

        Rng rng; Lfo lf; lf.rng = &rng; lf.reset();
        check(lf.riseGain(1.0, 48000) == 0.0, "rise gain 0 at note-on");
        lf.advance(2.0, 24000, 48000);
        check(std::abs(lf.riseGain(1.0, 48000) - 0.5) < 1e-6, "rise gain ~0.5 mid-ramp");
        check(lf.riseGain(0.0, 48000) == 1.0, "rise gain 1 when rise=0");

        // Engine renders finite/bounded audio with synced + free-running LFO -> POS.
        Engine eng; eng.prepare(48000);
        auto tables = generateTables();
        eng.setTables(tables);
        auto& p = eng.params();
        p[LFO1_BASE + LFO_SYNC] = 1; p[LFO1_BASE + LFO_SYNCRATE] = 5; p[LFO1_BASE + LFO_RETRIG] = 0;
        p[LFO1_BASE + LFO_RISE] = 0.2f;
        p[MAT1_BASE + MAT_SRC] = 1; p[MAT1_BASE + MAT_DST] = 1; p[MAT1_BASE + MAT_AMT] = 1.0f; // LFO1 -> A POS
        eng.setBpm(128);
        eng.noteOn(60, 1.0);
        std::vector<float> bl(2048), br(2048);
        eng.render(bl.data(), br.data(), 2048);
        check(finite(bl) && peak(bl) < 4.0f, "engine finite/bounded with synced free-run LFO");
    }
```

Match the helper names actually present in `engine_test.cpp` (`finite`, `peak`, `generateTables`/table builder, `MAT_SRC`/`MAT_DST`/`MAT_AMT`). Adjust to the real API as needed.

## JUCE build gate

```
cmake --build juce/build --target engine_test && (cd juce/build && ctest --output-on-failure)
cmake --build juce/build --target FableSynth_VST3
```

Both must succeed; ctest must be all-green.

---

## Parity checklist (both tracks)

- Identical param ids, `LFO_DIVS`, factor table, defaults.
- Identical rate formula, rise formula, phase-offset read, retrig/free-run semantics.
- Global LFO advances once per 128-sample block on both platforms (web `process`, JUCE `renderBlock`).
- New defaults reproduce current behaviour exactly (sync off, retrig on, rise 0, phase 0).
