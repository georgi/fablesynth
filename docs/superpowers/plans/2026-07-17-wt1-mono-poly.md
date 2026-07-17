# WT-1 Mono/Poly Switch Implementation Plan (Web)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `master.mono` voice mode to the web WT-1: classic last-note-priority mono with legato glide, no envelope retrigger, glide-back on release; lead factory presets default to mono.

**Architecture:** Mono is implemented entirely inside the audio worklet (`src/engine/worklet.js`) as a dispatch in `noteOn`/`noteOff` plus a press-order held-note stack. Legato retunes the sounding voice's `note` field — the existing per-block glide in `renderVoice` (which slews `v.pitch` toward `v.note`) does the pitch movement for free. The hosted-clip chord path collapses to its first active lane in mono. A new in-process worklet harness (mirroring `src/bass/engine/bassHarness.ts`) lets vitest drive the real DSP.

**Tech Stack:** TypeScript + vitest (`npm test` runs `vitest run`), plain-JS AudioWorklet, Zustand store, React UI.

Spec: `docs/superpowers/specs/2026-07-17-wt1-mono-poly-design.md`

## Global Constraints

- Web app only — do NOT touch anything under `juce/`.
- `master.mono` is a bool param, default 0 (poly).
- Legato note-ons must not retrigger envelopes or reset voice state (`Voice.noteOn` must not be called on legato).
- No allocation in the audio-thread hot path (compact arrays in place, like `seqScheduleOff` does).
- Mono presets (exactly these 11): ACID LINE, SCREECH LEAD, 8-BIT LEAD, GLIDE LEAD, MINI LEAD, FUNKY WORM, TAURUS PEDAL, FOG LIGHT, GLASS RIBBON, NORTH WIRE, TEMPLE BREATH.

---

### Task 1: WT worklet test harness

`src/engine/worklet.js` currently has only text-parity tests. Create an in-process harness that `Function`-evaluates the worklet source so vitest can drive the real DSP, exactly like `src/bass/engine/bassHarness.ts` does for the bass worklet.

**Files:**
- Create: `src/engine/workletHarness.ts`
- Test: `src/engine/worklet.mono.test.ts` (smoke test only in this task; mono tests come in Task 2)

**Interfaces:**
- Produces: `makeWtProcessor(sampleRate = 48000): WtHarness` with `WtHarness = { proc, sent, send(msg), render(blocks) }` — identical shape to `BassHarness`. Also `bootWt(params?): WtHarness` which sends `init` (with `defaultParams()` merged with overrides) and the `tables` message.
- Note: worklet.js references the bare global `currentFrame` (AudioWorklet global) inside `hostTick`. Tests must set `(globalThis as any).currentFrame = 0` before rendering and advance it +128 per block when exercising the hosted-clip path (same convention as `src/bass/engine/worklet-bass.test.ts:204-211`).

- [ ] **Step 1: Write the harness**

```ts
// src/engine/workletHarness.ts — evaluate the self-contained worklet
// source in-process so vitest can drive the real DSP offline. Mirrors the
// AudioWorklet contract: constructor gets a port, process(inputs, outputs).
import WT_SRC from './worklet.js?raw';
import { generateTables } from './wavetables';
import { defaultParams, type ParamValues } from '../params';

export interface WtHarness {
  proc: {
    port: { onmessage: ((e: { data: unknown }) => void) | null; postMessage(m: unknown): void };
    process(inputs: unknown[], outputs: Float32Array[][]): boolean;
  };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
  render(blocks: number): { L: Float32Array; R: Float32Array };
}

export function makeWtProcessor(sampleRate = 48000): WtHarness {
  const sent: WtHarness['sent'] = [];
  let Proc: new () => WtHarness['proc'];
  class AWP {
    port = {
      onmessage: null as WtHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as WtHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => WtHarness['proc']) => { Proc = cls; };
  // The worklet is an ES module only because of Vite's loader; it has no
  // imports/exports, so Function-evaluating its text is safe and exact.
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', WT_SRC)(
    sampleRate, AWP, register,
  );
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const render = (blocks: number) => {
    const L = new Float32Array(blocks * 128);
    const R = new Float32Array(blocks * 128);
    for (let b = 0; b < blocks; b++) {
      const l = new Float32Array(128), r = new Float32Array(128);
      proc.process([], [[l, r]]);
      L.set(l, b * 128); R.set(r, b * 128);
    }
    return { L, R };
  };
  return { proc, sent, send, render };
}

const TABLES = generateTables();
const tableMsg = {
  t: 'tables',
  list: TABLES.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

export function bootWt(params: Partial<ParamValues> = {}): WtHarness {
  const h = makeWtProcessor();
  h.send({ t: 'init', params: { ...defaultParams(), ...params } });
  h.send(tableMsg);
  return h;
}
```

Before writing, check `defaultParams` is the actual export name in `src/params.ts` (`rg -n "defaultParams" src/params.ts`); if it's e.g. a const not a function, adapt the spread accordingly.

- [ ] **Step 2: Write the smoke test**

```ts
// src/engine/worklet.mono.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { bootWt, type WtHarness } from './workletHarness';

const g = globalThis as unknown as { currentFrame: number };
const peak = (x: Float32Array) => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

interface VoiceState { gate: boolean; note: number; pitch: number; age: number; ampEnv: { state: number; level: number } }
const voices = (h: WtHarness) => (h.proc as unknown as { voices: VoiceState[] }).voices;
const gated = (h: WtHarness) => voices(h).filter((v) => v.gate);

describe('harness smoke', () => {
  beforeEach(() => { g.currentFrame = 0; });

  it('renders audio for a live note-on and releases on note-off', () => {
    const h = bootWt();
    expect(peak(h.render(4).L)).toBe(0);
    h.send({ t: 'on', n: 60, v: 1 });
    expect(peak(h.render(20).L)).toBeGreaterThan(0.001);
    h.send({ t: 'off', n: 60 });
    h.render(400); // default env1.r is short; let the tail die
    expect(gated(h)).toHaveLength(0);
  });
});
```

- [ ] **Step 3: Run the test**

Run: `npx vitest run src/engine/worklet.mono.test.ts`
Expected: PASS (2 assertions in 1 test). If `peak` stays 0, check the `tables` message shape against `src/engine/synth.ts:157-160` — it must match exactly.

- [ ] **Step 4: Commit**

```bash
git add src/engine/workletHarness.ts src/engine/worklet.mono.test.ts
git commit -m "test(wt1): in-process worklet harness for DSP-level tests"
```

---

### Task 2: `master.mono` param + mono engine behavior

**Files:**
- Modify: `src/params.ts` (PARAM_DEFS, next to `master.glide` around line 262)
- Modify: `src/engine/worklet.js` (constructor state, `noteOn`/`noteOff` dispatch, `panic`)
- Test: `src/engine/worklet.mono.test.ts`

**Interfaces:**
- Consumes: `bootWt`, `voices`/`gated` helpers from Task 1.
- Produces: param id `'master.mono'` (bool, def 0) — used by Task 3 (clip collapse), Task 4 (presets), Task 5 (UI). Worklet-internal: `this.held` stack, `monoNoteOn(n, vel)`, `monoNoteOff(n)`, and the existing poly body renamed to `polyNoteOn(n, vel)`.
- Key invariant for tests: `Voice.noteOn` stamps `v.age = this.clock++`; legato must NOT call it, so an unchanged `v.age` proves envelopes did not retrigger.

- [ ] **Step 1: Write the failing tests** (append to `worklet.mono.test.ts`)

```ts
describe('mono mode', () => {
  beforeEach(() => { g.currentFrame = 0; });

  it('poly default: two held notes gate two voices', () => {
    const h = bootWt();
    h.send({ t: 'on', n: 60, v: 1 });
    h.send({ t: 'on', n: 64, v: 1 });
    h.render(4);
    expect(gated(h)).toHaveLength(2);
  });

  it('legato note-on retunes the sounding voice without retrigger', () => {
    const h = bootWt({ 'master.mono': 1 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(8);
    const v0 = gated(h)[0];
    const age = v0.age;
    h.send({ t: 'on', n: 67, v: 1 });
    h.render(4);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0]).toBe(v0);
    expect(gs[0].note).toBe(67);
    expect(gs[0].age).toBe(age); // Voice.noteOn not called => envelopes untouched
  });

  it('legato glides: pitch approaches the new note over blocks', () => {
    const h = bootWt({ 'master.mono': 1, 'master.glide': 0.3 });
    h.send({ t: 'on', n: 48, v: 1 });
    h.render(8);
    h.send({ t: 'on', n: 72, v: 1 });
    h.render(2);
    const v = gated(h)[0];
    expect(v.pitch).toBeGreaterThan(48);
    expect(v.pitch).toBeLessThan(72);
    h.render(2000);
    expect(gated(h)[0].pitch).toBeCloseTo(72, 1);
  });

  it('releasing the top note glides back to the held note without retrigger', () => {
    const h = bootWt({ 'master.mono': 1 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(4);
    const age = gated(h)[0].age;
    h.send({ t: 'on', n: 67, v: 1 });
    h.render(4);
    h.send({ t: 'off', n: 67 });
    h.render(4);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0].note).toBe(60);
    expect(gs[0].age).toBe(age);
  });

  it('releasing the last held note releases the voice', () => {
    const h = bootWt({ 'master.mono': 1 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.send({ t: 'on', n: 67, v: 1 });
    h.send({ t: 'off', n: 67 });
    h.send({ t: 'off', n: 60 });
    h.render(4);
    expect(gated(h)).toHaveLength(0);
  });

  it('first mono note-on after a poly->mono flip collapses extra gated voices', () => {
    const h = bootWt();
    h.send({ t: 'on', n: 60, v: 1 });
    h.send({ t: 'on', n: 64, v: 1 });
    h.send({ t: 'on', n: 67, v: 1 });
    h.render(4);
    expect(gated(h)).toHaveLength(3);
    h.send({ t: 'p', k: 'master.mono', v: 1 });
    h.send({ t: 'on', n: 72, v: 1 });
    h.render(4);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0].note).toBe(72);
  });
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `npx vitest run src/engine/worklet.mono.test.ts`
Expected: the poly test passes; every mono test FAILS (e.g. 2 gated voices where 1 expected) because `master.mono` doesn't exist yet.

- [ ] **Step 3: Add the param def**

In `src/params.ts`, directly after the `master.glide` line (~262):

```ts
  { id: 'master.mono', type: 'bool', def: 0 },
```

- [ ] **Step 4: Implement mono in the worklet**

In `src/engine/worklet.js`:

(a) Constructor — next to `this.lastPitch = 60;` (~line 304):

```js
    this.held = []; // mono press-order stack of {n, vel}, newest last
```

(b) Rename the existing `noteOn(n, vel)` method (line ~584) to `polyNoteOn(n, vel)` (body unchanged), and add the dispatch + mono handlers:

```js
  noteOn(n, vel) {
    if (this.p['master.mono']) this.monoNoteOn(n, vel);
    else this.polyNoteOn(n, vel);
  }

  monoNoteOn(n, vel) {
    // Re-press of a held pitch moves it to the top of the stack. Compact in
    // place: the audio thread must not allocate per note event.
    let w = 0;
    for (let i = 0; i < this.held.length; i++) if (this.held[i].n !== n) this.held[w++] = this.held[i];
    this.held.length = w;
    this.held.push({ n, vel });

    // The newest gated voice carries the mono line; release any others (they
    // can only exist right after a poly -> mono flip).
    let voice = null;
    for (const v of this.voices) {
      if (!v.gate) continue;
      if (!voice) { voice = v; continue; }
      if (v.age > voice.age) { voice.noteOff(); voice = v; } else v.noteOff();
    }
    if (voice) {
      // Legato: retune only — renderVoice glides v.pitch toward v.note, and
      // not calling Voice.noteOn is what keeps the envelopes running.
      voice.note = n;
      this.lastPitch = n;
      return;
    }
    this.polyNoteOn(n, vel); // from silence: normal trigger (glide-from-lastPitch, steal fade)
  }

  monoNoteOff(n) {
    let w = 0;
    for (let i = 0; i < this.held.length; i++) if (this.held[i].n !== n) this.held[w++] = this.held[i];
    this.held.length = w;
    for (const v of this.voices) {
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) {
        const back = this.held[this.held.length - 1];
        if (back) { v.note = back.n; this.lastPitch = back.n; } // fall back to the held note, no retrigger
        else v.noteOff();
      }
    }
  }
```

(c) The existing `noteOff(n)` (line ~612) becomes the dispatch; keep its body as the poly branch:

```js
  noteOff(n) {
    if (this.p['master.mono']) { this.monoNoteOff(n); return; }
    for (const v of this.voices) {
      // A note released before its steal fade finished must not start at all.
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) v.noteOff();
    }
  }
```

(d) `case 'panic':` in `onMsg` — add `this.held.length = 0;` next to `this.seqOffQueue.length = 0;`. (Seq/clip stops go through `seqGateOff` → `noteOff` per note, which already drains the stack; only panic bypasses that.)

- [ ] **Step 5: Run tests to verify they pass**

Run: `npx vitest run src/engine/worklet.mono.test.ts`
Expected: all PASS.

- [ ] **Step 6: Run the full suite** (params.test.ts and worklet.parity.test.ts must not care about the new param)

Run: `npm test`
Expected: PASS. If a test asserts a param count, update that count — the new param is intentional.

- [ ] **Step 7: Commit**

```bash
git add src/params.ts src/engine/worklet.js src/engine/worklet.mono.test.ts
git commit -m "feat(wt1): master.mono param + last-note-priority legato mono engine"
```

---

### Task 3: Mono collapses hosted-clip chords to the first active lane

**Files:**
- Modify: `src/engine/worklet.js` (`clipFire`, ~line 533)
- Test: `src/engine/worklet.mono.test.ts`

**Interfaces:**
- Consumes: `bootWt`, `gated`, the `g.currentFrame` global from Task 1; `'master.mono'` from Task 2.
- Clip wire format (from `clipRead`, worklet.js:474-483): `bars * 16` steps × 8 lanes × 3 bytes, step-major then lane. Byte 0 flags: bit0 on, bit1 accent, bits2-7 duration in steps. Byte 1: semi 0-11. Byte 2: octave byte where 1 = oct 0. Note = `(p['seq.root'] | 0) || 48` + semi + 12*(oct-1).

- [ ] **Step 1: Write the failing test** (append to `worklet.mono.test.ts`)

```ts
describe('mono + hosted clip chords', () => {
  function playChordStep(h: WtHarness) {
    g.currentFrame = 0;
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    const data = new Uint8Array(16 * 8 * 3);
    const set = (step: number, lane: number, semi: number) => {
      const o = (step * 8 + lane) * 3;
      data[o] = 1 | (4 << 2); // on, duration 4 steps
      data[o + 1] = semi;
      data[o + 2] = 1; // oct 0
    };
    set(0, 0, 0); // root (48)
    set(0, 1, 7); // fifth (55)
    h.send({ t: 'clip', data: data.buffer, bars: 1, atFrame: 0 });
    for (let b = 0; b < 4; b++) { h.render(1); g.currentFrame += 128; }
  }

  it('poly: a two-lane chord step gates two voices', () => {
    const h = bootWt();
    playChordStep(h);
    expect(gated(h).map((v) => v.note).sort()).toEqual([48, 55]);
  });

  it('mono: the chord collapses to the first active lane', () => {
    const h = bootWt({ 'master.mono': 1 });
    playChordStep(h);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0].note).toBe(48);
  });
});
```

- [ ] **Step 2: Run to verify the mono case fails**

Run: `npx vitest run src/engine/worklet.mono.test.ts`
Expected: poly chord test PASSES (proves the clip fixture is right); mono chord test FAILS. If the poly test also fails, fix the fixture against `clipRead`/`hostTick` before touching the engine — do not proceed on a broken fixture.

- [ ] **Step 3: Implement the collapse**

In `clipFire` (worklet.js ~line 533), after the chord array is built:

```js
    const chord = Array.from({ length: WT_POLY_LANES }, (_, lane) => this.clipRead(abs, lane)).filter((st) => st.on);
    // Mono: the melody lives in the first active lane; the rest of a chord
    // step would just steal the line note for note.
    if (this.p['master.mono'] && chord.length > 1) chord.length = 1;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/engine/worklet.mono.test.ts`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/engine/worklet.js src/engine/worklet.mono.test.ts
git commit -m "feat(wt1): mono collapses clip chord steps to the first active lane"
```

---

### Task 4: Lead factory presets default to mono

**Files:**
- Modify: `src/presets.ts`
- Test: `src/presets.test.ts`

**Interfaces:**
- Consumes: `'master.mono'` param id from Task 2; `FACTORY_PRESETS` export of `src/presets.ts`.

- [ ] **Step 1: Write the failing test** (append to `src/presets.test.ts`, reusing its existing imports of `FACTORY_PRESETS` — add the import if the file doesn't already have it)

```ts
describe('mono leads', () => {
  const MONO_LEADS = [
    'ACID LINE', 'SCREECH LEAD', '8-BIT LEAD', 'GLIDE LEAD', 'MINI LEAD',
    'FUNKY WORM', 'TAURUS PEDAL', 'FOG LIGHT', 'GLASS RIBBON', 'NORTH WIRE',
    'TEMPLE BREATH',
  ];

  it('exactly the lead presets set master.mono', () => {
    for (const p of FACTORY_PRESETS) {
      expect(!!p.params['master.mono'], p.name).toBe(MONO_LEADS.includes(p.name));
    }
    // guard against a typo silently shrinking the list
    const names = new Set(FACTORY_PRESETS.map((p) => p.name));
    for (const n of MONO_LEADS) expect(names.has(n), n).toBe(true);
  });
});
```

- [ ] **Step 2: Run to verify it fails**

Run: `npx vitest run src/presets.test.ts`
Expected: FAIL — every MONO_LEADS preset currently reports `false`.

- [ ] **Step 3: Add `'master.mono': 1`** to the `params` object of each of the 11 presets in `src/presets.ts` (place it next to `'master.glide'` where present).

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/presets.test.ts`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/presets.ts src/presets.test.ts
git commit -m "feat(wt1): lead factory presets default to mono"
```

---

### Task 5: MONO toggle in the keyboard bar

**Files:**
- Modify: `src/components/panels/KeyboardBar.tsx`
- Modify: `src/index.css`

**Interfaces:**
- Consumes: `'master.mono'` param (Task 2); `useStore` `params`/`setParam` (same pattern as `src/components/PowerButton.tsx`).

- [ ] **Step 1: Add the toggle**

In `src/components/panels/KeyboardBar.tsx`, inside `.kb-side` right after the `#glide-knob` div (mono and glide are conceptually paired):

```tsx
        <MonoToggle />
```

and at the bottom of the file:

```tsx
function MonoToggle() {
  const on = useStore((s) => !!s.params['master.mono']);
  const setParam = useStore((s) => s.setParam);
  return (
    <button
      id="mono-toggle"
      className={on ? 'on' : ''}
      aria-label="mono mode"
      aria-pressed={on}
      onClick={() => setParam('master.mono', on ? 0 : 1)}
    >
      MONO
    </button>
  );
}
```

- [ ] **Step 2: Style it**

In `src/index.css`, next to the existing kb-bar / oct-ctl rules (find them with `rg -n "oct-ctl" src/index.css`), following the visual language of the octave buttons:

```css
#mono-toggle {
  font: inherit;
  font-size: 9px;
  letter-spacing: 0.08em;
  padding: 3px 8px;
  border: 1px solid var(--line, #333);
  border-radius: 4px;
  background: transparent;
  color: var(--dim, #888);
  cursor: pointer;
}
#mono-toggle.on {
  color: var(--accent-n, #4de8ff);
  border-color: currentColor;
}
```

Before committing, check which CSS custom properties the surrounding kb-bar rules actually use (`rg -n "kb-bar" -A 6 src/index.css`) and use those same variables instead of the fallbacks above if they differ.

- [ ] **Step 3: Verify in the running app**

Use the project's `/verify` skill recipe (build + headless drive) or `npm run dev` manually: load GLIDE LEAD, confirm the MONO button lights, play two overlapping keys — pitch glides with no retrigger attack; toggle MONO off — chords work again. Also confirm INIT loads with MONO off (preset params are partial overrides, so a preset without the key must reset it — `applyPreset` resets to defaults; verify by switching GLIDE LEAD → VELVET PAD and checking the button goes dark).

- [ ] **Step 4: Run the full suite + typecheck/build**

Run: `npm test && npm run build`
Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
git add src/components/panels/KeyboardBar.tsx src/index.css
git commit -m "feat(wt1): MONO toggle in the keyboard bar"
```
