# FableSynth DR-1 Drum Machine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship DR-1 — a fully working 16-pad wavetable drum machine with a sample-accurate step sequencer, mod matrix, choke groups, FX rack, and kit save/load — as a third surface (`drum/index.html`) of the FableSynth web build.

**Architecture:** A dedicated self-contained AudioWorklet (`src/drum/engine/worklet-drum.js`) runs 16 one-shot pad voices and the sequencer clock in the audio thread; a thin main-thread engine (`drum-synth.ts`) owns the AudioContext and a Web Audio FX graph mirroring WT-1's `synth.ts`; a Zustand store keys everything off one canonical param table (`src/drum/params.ts`), exactly like WT-1. UI components are drum-local but reuse WT-1's CSS classes, display canvases, and wavetable pipeline. WT-1 code is **never modified** except `vite.config.ts` (new entry) and the landing page (a link).

**Tech Stack:** TypeScript, React 18, Zustand, Vite (third entry), AudioWorklet, vitest.

**Spec:** `docs/superpowers/specs/2026-07-10-dr1-drum-machine-design.md`

## Global Constraints

- WT-1 files (`src/params.ts`, `src/store.ts`, `src/engine/*`, `src/components/*`, `src/App.tsx`, `app/index.html`) are read-only for this plan. Reuse by import only.
- One deviation from the spec's "reuse Knob/Stepper directly": WT-1's `Knob`/`Stepper`/`VSlider`/`PowerOverlay` are hard-wired to the WT-1 store, so DR-1 gets thin drum-local equivalents that bind to the drum store but reuse WT-1's **CSS classes and math** (`normToValue`/`valueToNorm` from `../params`). Display canvases (`WavetableView`, `FilterView`, `ScopeView`) are pure-props and ARE imported directly.
- `worklet-drum.js` must stay a self-contained ES module (no imports) — it is loaded with `?url` into the audio render thread and tested by evaluating its raw source.
- All tests live in `src/**/*.test.ts` (vitest `include` only matches `.ts`, not `.tsx`).
- Design tokens: colors/typography come from `src/index.css` `:root` vars (`--ac-a` cyan #4de8ff, `--ac-b` amber #ffa14d, `--ac-f` violet #b18cff, etc.). Match the mockup `FableSynth DR-1.dc.html` visually.
- Run `npm test` and `npx tsc --noEmit` before every commit. Commit messages: `feat(drum): …`, ending with the Claude Code trailer.
- Work on a branch: `git checkout -b feat/dr1-drum-machine` before Task 1.

---

### Task 1: Drum parameter model (`src/drum/params.ts`)

**Files:**
- Create: `src/drum/params.ts`
- Test: `src/drum/params.test.ts`

**Interfaces:**
- Consumes: `fmtHz, fmtSec, fmtPct, fmtPan, fmtSigned, fmtBi, normToValue, valueToNorm, type ParamDef, type ParamValues` from `../params`
- Produces (all later tasks key off these):
  - `PAD_COUNT = 16`, `MIDI_BASE = 36`
  - `DRUM_TABLE_NAMES: string[]` = `['THUD','CRACK','TINE','GRIT','PRIME','BLOOM','PULSE','VOX','CHIME','GLITCH']`
  - `DRUM_FILTER_TYPES = ['LP 12','LP 24','BP 12','HP 12','NOTCH']`
  - `DMOD_SOURCES = ['—','MOD ENV','VELO','RAND']`, `DMOD_DESTS = ['—','A POS','B POS','LEVEL','CUTOFF','PITCH','A FINE','B FINE','NOISE LVL','RES']`
  - `NOISE_COLORS = ['WHITE']` (readout only; COLOR knob tilts it)
  - `pad(i: number, field: string): string` → `` `pad${i}.${field}` ``
  - `DRUM_PARAM_DEFS: ParamDef[]`, `DRUM_PARAMS: Record<string, ParamDef>`, `defaultDrumParams(): ParamValues`
  - `PAD_FIELDS: string[]` — every per-pad field suffix (used by kits to iterate)

- [ ] **Step 1: Write the failing test**

```ts
// src/drum/params.test.ts
import { describe, it, expect } from 'vitest';
import {
  PAD_COUNT, MIDI_BASE, pad, DRUM_PARAM_DEFS, DRUM_PARAMS, defaultDrumParams,
  DRUM_TABLE_NAMES, DMOD_SOURCES, DMOD_DESTS, PAD_FIELDS,
} from './params';

describe('drum params', () => {
  it('defines 16 pads from MIDI 36', () => {
    expect(PAD_COUNT).toBe(16);
    expect(MIDI_BASE).toBe(36);
    expect(pad(2, 'oscA.tune')).toBe('pad2.oscA.tune');
  });

  it('every pad has the full field set with sane defs', () => {
    for (let i = 0; i < PAD_COUNT; i++) {
      for (const f of PAD_FIELDS) {
        const d = DRUM_PARAMS[pad(i, f)];
        expect(d, `${pad(i, f)} defined`).toBeDefined();
        expect(Number.isFinite(d.def)).toBe(true);
      }
    }
    // spot-check ranges off the spec
    expect(DRUM_PARAMS['pad0.oscA.tune']).toMatchObject({ min: -48, max: 48, curve: 'int' });
    expect(DRUM_PARAMS['pad0.aenv.dec']).toMatchObject({ min: 0.005, max: 4, curve: 'log' });
    expect(DRUM_PARAMS['pad0.flt.cut']).toMatchObject({ min: 20, max: 20000, curve: 'log', def: 1800 });
    expect(DRUM_PARAMS['pad15.modenv.dec'].def).toBeCloseTo(0.084);
    expect(DRUM_PARAMS['pad0.choke']).toMatchObject({ min: 0, max: 4, curve: 'int' });
  });

  it('globals: bpm/swing/volume/fx', () => {
    expect(DRUM_PARAMS['seq.bpm']).toMatchObject({ min: 60, max: 200, def: 126, curve: 'int' });
    expect(DRUM_PARAMS['master.swing'].def).toBeCloseTo(0.22);
    expect(DRUM_PARAMS['master.volume'].def).toBeCloseTo(0.78);
    expect(DRUM_PARAMS['fx.comp.thr']).toMatchObject({ min: -40, max: 0, def: -16 });
    expect(DRUM_PARAMS['fx.comp.on'].def).toBe(1);
    expect(DRUM_PARAMS['fx.reverb.on'].def).toBe(1);
    expect(DRUM_PARAMS['fx.drive.on'].def).toBe(0);
  });

  it('defaults map covers every def and enums line up', () => {
    const dp = defaultDrumParams();
    expect(Object.keys(dp).length).toBe(DRUM_PARAM_DEFS.length);
    expect(DRUM_TABLE_NAMES.slice(0, 4)).toEqual(['THUD', 'CRACK', 'TINE', 'GRIT']);
    expect(DMOD_SOURCES).toHaveLength(4);
    expect(DMOD_DESTS).toHaveLength(10);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/drum/params.test.ts`
Expected: FAIL — `Cannot find module './params'`

- [ ] **Step 3: Implement `src/drum/params.ts`**

```ts
// Canonical DR-1 parameter definitions. Per-pad params are namespaced
// `pad<i>.<field>`; every knob, kit, and worklet message keys off this table
// (same params-as-truth discipline as WT-1's src/params.ts).

import {
  fmtHz, fmtSec, fmtPct, fmtPan, fmtSigned, fmtBi,
  type ParamDef, type ParamValues,
} from '../params';

export const PAD_COUNT = 16;
export const MIDI_BASE = 36; // pad 0 = MIDI C1, pads 0..15 = 36..51

// 4 drum tables (Task 2) followed by WT-1's six procedural tables.
export const DRUM_TABLE_NAMES = ['THUD', 'CRACK', 'TINE', 'GRIT', 'PRIME', 'BLOOM', 'PULSE', 'VOX', 'CHIME', 'GLITCH'];
export const DRUM_FILTER_TYPES = ['LP 12', 'LP 24', 'BP 12', 'HP 12', 'NOTCH'];
export const NOISE_COLORS = ['WHITE'];
// Mod sources are per-pad: MOD ENV (per-pad decay env), VELO (hit velocity ×
// V→MOD), RAND (uniform ±1, drawn once per hit).
export const DMOD_SOURCES = ['—', 'MOD ENV', 'VELO', 'RAND'];
export const DMOD_DESTS = ['—', 'A POS', 'B POS', 'LEVEL', 'CUTOFF', 'PITCH', 'A FINE', 'B FINE', 'NOISE LVL', 'RES'];
export const CHOKE_NAMES = ['—', 'CHK 1', 'CHK 2', 'CHK 3', 'CHK 4'];
export const OUT_NAMES = ['MAIN', 'AUX 1', 'AUX 2', 'AUX 3', 'AUX 4'];

export const pad = (i: number, field: string): string => `pad${i}.${field}`;

const fmtSt = (v: number) => (v > 0 ? '+' : '') + Math.round(v) + ' ST';
const fmtCt = (v: number) => (v > 0 ? '+' : '') + Math.round(v) + ' CT';

function oscFields(pre: 'oscA' | 'oscB'): ParamDef[] {
  return [
    { id: `${pre}.table`, type: 'enum', options: DRUM_TABLE_NAMES, def: pre === 'oscA' ? 0 : 1 },
    { id: `${pre}.pos`, label: 'POS', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${pre}.tune`, label: 'TUNE', min: -48, max: 48, def: 0, curve: 'int', fmt: fmtSt },
    { id: `${pre}.fine`, label: 'FINE', min: -100, max: 100, def: 0, curve: 'int', fmt: fmtCt },
    { id: `${pre}.phase`, label: 'PHASE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${pre}.unison`, label: 'UNI', min: 1, max: 7, def: 1, curve: 'int', fmt: (v) => String(Math.round(v)) },
    { id: `${pre}.detune`, label: 'DET', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
    { id: `${pre}.level`, label: 'LVL', min: 0, max: 1, def: pre === 'oscA' ? 0.75 : 0, curve: 'lin', fmt: fmtPct },
  ];
}

// One pad's field suffixes + defs (id fields are RELATIVE here; padded below).
const PAD_DEFS: ParamDef[] = [
  ...oscFields('oscA'),
  ...oscFields('oscB'),
  { id: 'noise.color', label: 'COLOR', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
  { id: 'noise.level', label: 'LVL', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  { id: 'penv.amt', label: 'AMT', min: -48, max: 48, def: 0, curve: 'int', fmt: fmtSt },
  { id: 'penv.dec', label: 'DEC', min: 0.005, max: 2, def: 0.06, curve: 'log', fmt: fmtSec },
  { id: 'aenv.att', label: 'ATT', min: 0.0005, max: 0.5, def: 0.001, curve: 'log', fmt: fmtSec },
  { id: 'aenv.hold', label: 'HOLD', min: 0, max: 0.25, def: 0.01, curve: 'lin', fmt: fmtSec },
  { id: 'aenv.dec', label: 'DEC', min: 0.005, max: 4, def: 0.24, curve: 'log', fmt: fmtSec },
  { id: 'aenv.curve', label: 'CURVE', min: 0, max: 1, def: 0.35, curve: 'lin', fmt: fmtPct },
  { id: 'flt.on', type: 'bool', def: 0 },
  { id: 'flt.type', type: 'enum', options: DRUM_FILTER_TYPES, def: 0 },
  { id: 'flt.cut', label: 'CUT', min: 20, max: 20000, def: 1800, curve: 'log', fmt: fmtHz },
  { id: 'flt.res', label: 'RES', min: 0, max: 1, def: 0.18, curve: 'lin', fmt: fmtPct },
  { id: 'flt.drive', label: 'DRIVE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  ...[1, 2, 3, 4].flatMap((n): ParamDef[] => [
    { id: `mod${n}.src`, type: 'enum', options: DMOD_SOURCES, def: 0 },
    { id: `mod${n}.dst`, type: 'enum', options: DMOD_DESTS, def: 0 },
    { id: `mod${n}.amt`, label: '', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
  ]),
  { id: 'modenv.dec', label: 'DEC', min: 0.005, max: 2, def: 0.084, curve: 'log', fmt: fmtSec },
  { id: 'lvl', label: 'LVL', min: 0, max: 1, def: 0.8, curve: 'lin', fmt: fmtPct },
  { id: 'pan', label: 'PAN', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtPan },
  { id: 'v2l', label: 'V→LVL', min: 0, max: 1, def: 0.6, curve: 'lin', fmt: fmtPct },
  { id: 'v2m', label: 'V→MOD', min: 0, max: 1, def: 0.4, curve: 'lin', fmt: fmtPct },
  { id: 'choke', type: 'enum', options: CHOKE_NAMES, def: 0, min: 0, max: 4, curve: 'int' },
  { id: 'out', type: 'enum', options: OUT_NAMES, def: 0, min: 0, max: 4, curve: 'int' },
];

export const PAD_FIELDS: string[] = PAD_DEFS.map((d) => d.id);

const GLOBAL_DEFS: ParamDef[] = [
  { id: 'seq.bpm', label: 'BPM', min: 60, max: 200, def: 126, curve: 'int', fmt: (v) => String(Math.round(v)) },
  { id: 'master.swing', label: 'SWING', min: 0, max: 1, def: 0.22, curve: 'lin', fmt: fmtPct },
  { id: 'master.volume', label: 'VOL', min: 0, max: 1, def: 0.78, curve: 'lin', fmt: fmtPct },
  { id: 'fx.drive.on', type: 'bool', def: 0 },
  { id: 'fx.drive.amt', label: 'AMT', min: 0, max: 1, def: 0.3, curve: 'lin', fmt: fmtPct },
  { id: 'fx.drive.mix', label: 'MIX', min: 0, max: 1, def: 1, curve: 'lin', fmt: fmtPct },
  { id: 'fx.comp.on', type: 'bool', def: 1 },
  { id: 'fx.comp.thr', label: 'THRESH', min: -40, max: 0, def: -16, curve: 'lin', fmt: (v) => Math.round(v) + ' dB' },
  { id: 'fx.comp.gain', label: 'MAKEUP', min: 0, max: 12, def: 4, curve: 'lin', fmt: (v) => '+' + v.toFixed(1) + ' dB' },
  { id: 'fx.chorus.on', type: 'bool', def: 0 },
  { id: 'fx.chorus.rate', label: 'RATE', min: 0.05, max: 8, def: 0.6, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
  { id: 'fx.chorus.depth', label: 'DEPTH', min: 0, max: 1, def: 0.4, curve: 'lin', fmt: fmtPct },
  { id: 'fx.chorus.mix', label: 'MIX', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
  { id: 'fx.delay.on', type: 'bool', def: 0 },
  { id: 'fx.delay.time', label: 'TIME', min: 0.02, max: 1.5, def: 0.36, curve: 'log', fmt: fmtSec },
  { id: 'fx.delay.fb', label: 'FDBK', min: 0, max: 0.92, def: 0.35, curve: 'lin', fmt: fmtPct },
  { id: 'fx.delay.mix', label: 'MIX', min: 0, max: 1, def: 0.15, curve: 'lin', fmt: fmtPct },
  { id: 'fx.reverb.on', type: 'bool', def: 1 },
  { id: 'fx.reverb.size', label: 'SIZE', min: 0, max: 1, def: 0.4, curve: 'lin', fmt: fmtPct },
  { id: 'fx.reverb.mix', label: 'MIX', min: 0, max: 1, def: 0.16, curve: 'lin', fmt: fmtPct },
];

export const DRUM_PARAM_DEFS: ParamDef[] = [
  ...Array.from({ length: PAD_COUNT }, (_, i) =>
    PAD_DEFS.map((d) => ({ ...d, id: pad(i, d.id) })),
  ).flat(),
  ...GLOBAL_DEFS,
];

export const DRUM_PARAMS: Record<string, ParamDef> = Object.fromEntries(DRUM_PARAM_DEFS.map((d) => [d.id, d]));

export function defaultDrumParams(): ParamValues {
  const o: ParamValues = {};
  for (const d of DRUM_PARAM_DEFS) o[d.id] = d.def;
  return o;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx vitest run src/drum/params.test.ts` — Expected: PASS
Run: `npx tsc --noEmit` — Expected: clean

- [ ] **Step 5: Commit**

```bash
git add src/drum/params.ts src/drum/params.test.ts
git commit -m "feat(drum): DR-1 canonical parameter model"
```

---

### Task 2: Drum wavetables (`src/drum/engine/drumtables.ts`)

**Files:**
- Create: `src/drum/engine/drumtables.ts`
- Test: `src/drum/engine/drumtables.test.ts`

**Interfaces:**
- Consumes: `buildUserTable, SIZE, FRAMES, MIPS, type GeneratedTable` from `../../engine/wavetables` (time-domain frames go through the exact same FFT band-limit + mip pipeline as everything else — no changes to `wavetables.ts`).
- Produces: `generateDrumTables(): GeneratedTable[]` — 4 tables named THUD, CRACK, TINE, GRIT, each `FRAMES × MIPS × SIZE`.

Frame recipes (t = frame position 0..1, x = phase 0..1 over one cycle):
- **THUD**: `tanh((sin(2πx) + 0.6t·sin(4πx+0.6) + 0.45t²·sin(6πx+1.2))·(1+1.8t))` — sine into folded low harmonics (kick body). Same formula as the mockup's `genTerrain('thud')`.
- **CRACK**: odd-harmonic comb, `Σ_{k odd ≤31} (0.5+0.5cos(0.9k+5.2t))·exp(−((k−5−10t)/7)²)·sin(2πkx + 1.4sin(7.31k))` (snare/clap bite). Same as mockup `genTerrain('crack')`.
- **TINE**: sparse inharmonic-ish partial stack `Σ_j a_j(t)·sin(2π k_j x + φ_j)` with `k_j = [1,4,7,11,16,22]`, `a_j = t^{0.3j}/ (j+1)` (metallic hats/rides).
- **GRIT**: sample-held pseudo-random steps, `hold = 2 + round(30t)` samples, deterministic hash `sin(i·127.1)·43758.5453` fractional part (glitch/perc).

- [ ] **Step 1: Write the failing test**

```ts
// src/drum/engine/drumtables.test.ts
import { describe, it, expect } from 'vitest';
import { generateDrumTables } from './drumtables';
import { FRAMES, MIPS, SIZE } from '../../engine/wavetables';

describe('drum tables', () => {
  const tables = generateDrumTables();

  it('produces THUD/CRACK/TINE/GRIT with full mip data', () => {
    expect(tables.map((t) => t.name)).toEqual(['THUD', 'CRACK', 'TINE', 'GRIT']);
    for (const t of tables) {
      expect(t.frames).toBe(FRAMES);
      expect(t.mips).toBe(MIPS);
      expect(t.size).toBe(SIZE);
      expect(t.data.length).toBe(FRAMES * MIPS * SIZE);
      expect(t.viz.length).toBeGreaterThan(0);
    }
  });

  it('every sample is finite and within normalization headroom', () => {
    for (const t of tables) {
      let peak = 0;
      for (let i = 0; i < t.data.length; i++) {
        expect(Number.isFinite(t.data[i])).toBe(true);
        peak = Math.max(peak, Math.abs(t.data[i]));
      }
      expect(peak).toBeGreaterThan(0.5); // audible
      expect(peak).toBeLessThanOrEqual(1.0); // mip-0 normalized to 0.92, coarser mips can't exceed ~1
    }
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/drum/engine/drumtables.test.ts` — Expected: FAIL, module not found.

- [ ] **Step 3: Implement `src/drum/engine/drumtables.ts`**

```ts
// DR-1 procedural drum tables. Each spec renders FRAMES time-domain single
// cycles which buildUserTable runs through the shared FFT band-limit + mip
// pipeline — so drum tables anti-alias exactly like every other table.

import { buildUserTable, FRAMES, SIZE, type GeneratedTable } from '../../engine/wavetables';

type FrameFn = (t: number, out: Float32Array) => void;

const thud: FrameFn = (t, out) => {
  for (let i = 0; i < SIZE; i++) {
    const x = i / SIZE;
    let s = Math.sin(2 * Math.PI * x);
    s += t * 0.6 * Math.sin(2 * Math.PI * 2 * x + 0.6);
    s += t * t * 0.45 * Math.sin(2 * Math.PI * 3 * x + 1.2);
    out[i] = Math.tanh(s * (1 + t * 1.8));
  }
};

const crack: FrameFn = (t, out) => {
  for (let i = 0; i < SIZE; i++) {
    const x = i / SIZE;
    let s = 0;
    for (let k = 1; k <= 31; k += 2) {
      const comb = 0.5 + 0.5 * Math.cos(k * 0.9 + t * 5.2);
      const roll = Math.exp(-Math.pow((k - 5 - t * 10) / 7, 2));
      s += comb * roll * Math.sin(2 * Math.PI * k * x + Math.sin(k * 7.31) * 1.4);
    }
    out[i] = s;
  }
};

const TINE_K = [1, 4, 7, 11, 16, 22];
const tine: FrameFn = (t, out) => {
  for (let i = 0; i < SIZE; i++) {
    const x = i / SIZE;
    let s = 0;
    for (let j = 0; j < TINE_K.length; j++) {
      const a = Math.pow(Math.max(t, 0.001), 0.3 * j) / (j + 1);
      s += a * Math.sin(2 * Math.PI * TINE_K[j] * x + j * j * 1.7);
    }
    out[i] = s;
  }
};

const grit: FrameFn = (t, out) => {
  const hold = 2 + Math.round(30 * t);
  let held = 0;
  for (let i = 0; i < SIZE; i++) {
    if (i % hold === 0) {
      const r = Math.sin((i + 1) * 127.1) * 43758.5453;
      held = (r - Math.floor(r)) * 2 - 1;
    }
    // blend toward a sine at t=0 so frame 0 is tonal, not static
    const x = i / SIZE;
    out[i] = held * t + Math.sin(2 * Math.PI * x) * (1 - t);
  }
};

const SPECS: { name: string; fn: FrameFn }[] = [
  { name: 'THUD', fn: thud },
  { name: 'CRACK', fn: crack },
  { name: 'TINE', fn: tine },
  { name: 'GRIT', fn: grit },
];

export function generateDrumTables(): GeneratedTable[] {
  return SPECS.map((spec) => {
    const frames: Float32Array[] = [];
    for (let f = 0; f < FRAMES; f++) {
      const buf = new Float32Array(SIZE);
      spec.fn(f / (FRAMES - 1), buf);
      frames.push(buf);
    }
    return buildUserTable(spec.name, frames);
  });
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx vitest run src/drum/engine/drumtables.test.ts` — Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/drum/engine/drumtables.ts src/drum/engine/drumtables.test.ts
git commit -m "feat(drum): THUD/CRACK/TINE/GRIT procedural drum tables"
```

---

### Task 3: Sequencer model (`src/drum/seq.ts`)

Pure data + math shared by the store (editing) and tests; the worklet re-implements the same math internally (kept in lockstep by the parity assertions in Task 4).

**Files:**
- Create: `src/drum/seq.ts`
- Test: `src/drum/seq.test.ts`

**Interfaces:**
- Produces:
  - `STEPS = 16`, `NPATTERNS = 4`, `PATTERN_NAMES = ['A','B','C','D']`
  - `type StepVal = 0 | 1 | 2` (off / on / accent), `ACCENT_VEL = 1.0`, `PLAIN_VEL = 0.72`
  - `type Patterns = Uint8Array` — length `NPATTERNS*PAD_COUNT*STEPS`, index `patIdx(pat, padI, step)`
  - `makeEmptyPatterns(): Patterns`, `patIdx(pat: number, padI: number, step: number): number`
  - `cycleStep(v: number): StepVal` — 0→1→2→0
  - `stepDurSamples(bpm: number, sampleRate: number): number` — one 16th
  - `swingDelaySamples(step: number, swing: number, stepDur: number): number` — 0 on even steps, `swing * 0.667 * stepDur` on odd steps
  - `nextChainPos(chainLen: number, pos: number): number`

- [ ] **Step 1: Write the failing test**

```ts
// src/drum/seq.test.ts
import { describe, it, expect } from 'vitest';
import {
  STEPS, NPATTERNS, makeEmptyPatterns, patIdx, cycleStep,
  stepDurSamples, swingDelaySamples, nextChainPos, ACCENT_VEL, PLAIN_VEL,
} from './seq';

describe('sequencer model', () => {
  it('pattern buffer layout', () => {
    const p = makeEmptyPatterns();
    expect(p.length).toBe(NPATTERNS * 16 * STEPS);
    p[patIdx(1, 2, 3)] = 2;
    expect(p[1 * 256 + 2 * 16 + 3]).toBe(2);
  });

  it('step cycle off→on→accent→off', () => {
    expect(cycleStep(0)).toBe(1);
    expect(cycleStep(1)).toBe(2);
    expect(cycleStep(2)).toBe(0);
    expect(ACCENT_VEL).toBe(1);
    expect(PLAIN_VEL).toBeCloseTo(0.72);
  });

  it('step duration: 126 bpm, 48k → 60/126/4 s of samples', () => {
    expect(stepDurSamples(126, 48000)).toBeCloseTo((60 / 126 / 4) * 48000, 3);
  });

  it('swing delays only off-16ths, up to 2/3 of a step', () => {
    const dur = 1000;
    expect(swingDelaySamples(0, 1, dur)).toBe(0);
    expect(swingDelaySamples(2, 0.5, dur)).toBe(0);
    expect(swingDelaySamples(1, 1, dur)).toBeCloseTo(667, 0);
    expect(swingDelaySamples(3, 0.5, dur)).toBeCloseTo(333.5, 0);
  });

  it('chain advances and wraps', () => {
    expect(nextChainPos(3, 0)).toBe(1);
    expect(nextChainPos(3, 2)).toBe(0);
    expect(nextChainPos(1, 0)).toBe(0);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/drum/seq.test.ts` — Expected: FAIL, module not found.

- [ ] **Step 3: Implement `src/drum/seq.ts`**

```ts
// Pure sequencer data model + timing math. The worklet re-implements the same
// math internally (self-contained); Task 4's parity test asserts the constants.

import { PAD_COUNT } from './params';

export const STEPS = 16;
export const NPATTERNS = 4;
export const PATTERN_NAMES = ['A', 'B', 'C', 'D'];
export type StepVal = 0 | 1 | 2; // off / on / accent
export const ACCENT_VEL = 1.0;
export const PLAIN_VEL = 0.72;
// Swing: odd 16ths are delayed by swing * SWING_MAX of a step (1.0 → triplet feel).
export const SWING_MAX = 0.667;

export type Patterns = Uint8Array;

export const patIdx = (pat: number, padI: number, step: number): number =>
  pat * PAD_COUNT * STEPS + padI * STEPS + step;

export const makeEmptyPatterns = (): Patterns => new Uint8Array(NPATTERNS * PAD_COUNT * STEPS);

export const cycleStep = (v: number): StepVal => ((v + 1) % 3) as StepVal;

export const stepDurSamples = (bpm: number, sampleRate: number): number =>
  (60 / bpm / 4) * sampleRate;

export const swingDelaySamples = (step: number, swing: number, stepDur: number): number =>
  step % 2 === 1 ? swing * SWING_MAX * stepDur : 0;

export const nextChainPos = (chainLen: number, pos: number): number =>
  chainLen > 0 ? (pos + 1) % chainLen : 0;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx vitest run src/drum/seq.test.ts` — Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/drum/seq.ts src/drum/seq.test.ts
git commit -m "feat(drum): sequencer data model and timing math"
```

---

### Task 4: Drum DSP core (`src/drum/engine/worklet-drum.js`) + offline test harness

The heart of DR-1. Self-contained AudioWorklet module: 16 one-shot pad voices (2 wavetable oscs + noise, AHD amp env, pitch env, SVF filter + ADAA drive, 4-slot mod), choke groups, and the sample-accurate sequencer. DSP primitives (mip playback, SVF, lcosh/ADAA) are copied from `worklet.js` — that file is the reference; keep the math identical where the spec matches.

**Files:**
- Create: `src/drum/engine/worklet-drum.js`
- Create: `src/drum/engine/workletHarness.ts` (test-only helper)
- Test: `src/drum/engine/worklet-drum.test.ts`

**Interfaces:**
- Message protocol IN: `{t:'init',params}`, `{t:'tables',list:[{frames,mips,size,buf}]}`, `{t:'p',k,v}`, `{t:'trig',pad,v}` (live hit), `{t:'pats',data:ArrayBuffer}` (full 4×16×16 Uint8 dump), `{t:'chain',list:number[]}`, `{t:'play'}`, `{t:'stop'}`, `{t:'sel',pad}`, `{t:'panic'}`
- Message protocol OUT: `{t:'step', s, pat, hits:number[]}` fired at each step boundary while playing; `{t:'viz', a, b, env}` every 2048 samples (selected pad's osc positions 0..1 + amp env level, −1 when idle)
- `registerProcessor('fable-dr', DrumProcessor)`
- Harness: `makeDrumProcessor(sampleRate?): { proc, sent: any[], render(blocks: number): {L,R} }` — evaluates the raw worklet source with stubbed `AudioWorkletProcessor`/`registerProcessor`/`sampleRate` globals, so vitest exercises the real DSP offline.

Voice behavior contract (what the tests assert):
- `trig` on a default-param pad (oscA THUD, level .75, aenv defaults) produces non-zero, finite, bounded output.
- Retrigger restarts the voice (one-shot, mono per pad). Amp env: linear attack `att`, hold, decay morphing linear→exp by `curve` (blend of `1−t/dec` and `exp(−4.5t/dec)`).
- Pitch env adds `amt·exp(−t·4.5/dec)` semitones to both oscs; freq/incs recomputed every 16 samples inside the block.
- Velocity: gain factor `1 − v2l·(1 − vel)`; mod source VELO = `vel · v2m`; RAND drawn once per hit ∈ [−1,1].
- Mod dests fold like WT-1: lin dests `base + x·(hi−lo)` (pos 0..1, level 0..1, fine ±100 → x·200 ct, noise lvl, res), CUTOFF `base·2^(x·5)`, PITCH additive `x·24` semitones.
- Choke: triggering pad with `choke=g>0` puts every other active pad with the same `g` into a ~5 ms fade.
- Sequencer: while playing, at each step boundary (with swing offset on odd steps) every pad whose current-pattern step is 1|2 is triggered at PLAIN_VEL/ACCENT_VEL; step events are sample-accurate (triggers split the 128-sample block); chain advances at bar wrap.
- All output NaN-guarded at the param choke point exactly like `worklet.js` (`Number.isFinite` on init/p).

- [ ] **Step 1: Write the harness**

```ts
// src/drum/engine/workletHarness.ts — evaluate the self-contained worklet
// source in-process so vitest can drive the real DSP offline. Mirrors the
// AudioWorklet contract: constructor gets a port, process(inputs, outputs).
import DRUM_SRC from './worklet-drum.js?raw';

export interface DrumHarness {
  proc: {
    port: { onmessage: ((e: { data: unknown }) => void) | null; postMessage(m: unknown): void };
    process(inputs: unknown[], outputs: Float32Array[][][]): boolean;
  };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
  render(blocks: number): { L: Float32Array; R: Float32Array };
}

export function makeDrumProcessor(sampleRate = 48000): DrumHarness {
  const sent: DrumHarness['sent'] = [];
  let Proc: new () => DrumHarness['proc'];
  class AWP {
    port = {
      onmessage: null as DrumHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as DrumHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => DrumHarness['proc']) => { Proc = cls; };
  // The worklet is an ES module only because of Vite's loader; it has no
  // imports/exports, so Function-evaluating its text is safe and exact.
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', DRUM_SRC)(
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
```

- [ ] **Step 2: Write the failing tests**

```ts
// src/drum/engine/worklet-drum.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { makeDrumProcessor, type DrumHarness } from './workletHarness';
import { generateDrumTables } from './drumtables';
import { defaultDrumParams, pad } from '../params';
import { makeEmptyPatterns, patIdx, stepDurSamples } from '../seq';

const tables = generateDrumTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

function boot(params = defaultDrumParams()): DrumHarness {
  const h = makeDrumProcessor();
  h.send({ t: 'init', params });
  h.send(tableMsg);
  return h;
}

const peak = (x: Float32Array) => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
const finite = (x: Float32Array) => x.every(Number.isFinite);

describe('drum voice', () => {
  let h: DrumHarness;
  beforeEach(() => { h = boot(); });

  it('is silent until triggered, then audible, finite, bounded', () => {
    expect(peak(h.render(4).L)).toBe(0);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const { L, R } = h.render(40);
    expect(peak(L)).toBeGreaterThan(0.01);
    expect(peak(L)).toBeLessThan(2);
    expect(finite(L) && finite(R)).toBe(true);
  });

  it('one-shot: decays to silence without a note-off', () => {
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(40);
    // default aenv: att 1ms + hold 10ms + dec 240ms ≈ 12k samples; render 1.5s
    const tail = h.render(560).L;
    expect(peak(tail.slice(-2560))).toBeLessThan(1e-3);
  });

  it('accent (v=1) is louder than plain (v=0.72) with default v2l', () => {
    h.send({ t: 'trig', pad: 0, v: 0.72 });
    const plain = peak(h.render(20).L);
    h.send({ t: 'panic' });
    h.render(8);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const accent = peak(h.render(20).L);
    expect(accent).toBeGreaterThan(plain * 1.05);
  });

  it('pitch env sweeps: with +48st penv the early zero-crossing rate is higher', () => {
    const p = defaultDrumParams();
    p[pad(0, 'penv.amt')] = 48; p[pad(0, 'penv.dec')] = 0.2;
    const hz = (x: Float32Array) => { let c = 0; for (let i = 1; i < x.length; i++) if (x[i - 1] <= 0 && x[i] > 0) c++; return c; };
    const hp = boot(p); hp.send({ t: 'trig', pad: 0, v: 1 });
    const early = hz(hp.render(8).L);
    const late = hz(hp.render(80).L.slice(-1024));
    expect(early / 8).toBeGreaterThan(late / 8); // per-block crossing rate falls as env decays
  });

  it('choke: pad in group 1 silences the other group-1 pad fast', () => {
    const p = defaultDrumParams();
    p[pad(0, 'choke')] = 1; p[pad(1, 'choke')] = 1;
    p[pad(0, 'aenv.dec')] = 4; // long tail so the choke is what kills it
    p[pad(1, 'lvl')] = 0;      // choking pad itself silent, so only pad 0's tail is measured
    const hc = boot(p);
    hc.send({ t: 'trig', pad: 0, v: 1 });
    hc.render(20);
    hc.send({ t: 'trig', pad: 1, v: 1 });
    hc.render(4); // > 5ms fade at 48k = 240 samples < 512
    const after = hc.render(20).L;
    expect(peak(after)).toBeLessThan(0.02);
  });

  it('mod: VELO→CUTOFF route changes output when filter is on', () => {
    const p = defaultDrumParams();
    p[pad(0, 'flt.on')] = 1; p[pad(0, 'flt.cut')] = 200;
    const base = boot(p); base.send({ t: 'trig', pad: 0, v: 1 });
    const dull = peak(base.render(20).L);
    p[pad(0, 'mod1.src')] = 2; p[pad(0, 'mod1.dst')] = 4; p[pad(0, 'mod1.amt')] = 1; // VELO→CUTOFF
    p[pad(0, 'v2m')] = 1;
    const mod = boot(p); mod.send({ t: 'trig', pad: 0, v: 1 });
    const bright = peak(mod.render(20).L);
    expect(Math.abs(bright - dull)).toBeGreaterThan(0.001);
  });
});

describe('drum sequencer', () => {
  it('plays a programmed step sample-accurately and posts step events', () => {
    const h = boot();
    const pats = makeEmptyPatterns();
    pats[patIdx(0, 0, 0)] = 2; // pad 0, step 0, accent
    pats[patIdx(0, 0, 8)] = 1;
    h.send({ t: 'pats', data: pats.buffer });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const dur = stepDurSamples(126, 48000);
    const blocks = Math.ceil((dur * 16) / 128) + 2; // one bar
    const { L } = h.render(blocks);
    expect(peak(L)).toBeGreaterThan(0.01);
    const steps = h.sent.filter((m) => m.t === 'step');
    expect(steps.length).toBeGreaterThanOrEqual(16);
    expect((steps[0] as { hits: number[] }).hits).toContain(0);
    // audio actually starts inside the very first block (sample-accurate trigger)
    expect(peak(L.slice(0, 256))).toBeGreaterThan(0);
  });

  it('stop silences new triggers and chain advances at bar wrap', () => {
    const h = boot();
    const pats = makeEmptyPatterns();
    pats[patIdx(1, 0, 0)] = 1; // only pattern B has a hit
    h.send({ t: 'pats', data: pats.buffer });
    h.send({ t: 'chain', list: [0, 1] }); // A then B
    h.send({ t: 'play' });
    const dur = stepDurSamples(126, 48000);
    const barBlocks = Math.ceil((dur * 16) / 128);
    const bar1 = h.render(barBlocks);
    const bar2 = h.render(barBlocks);
    expect(peak(bar1.L)).toBe(0); // pattern A is empty
    expect(peak(bar2.L)).toBeGreaterThan(0.01); // chain moved to B
    h.send({ t: 'stop' });
    h.render(barBlocks * 2); // drain tails
    const after = h.render(barBlocks);
    expect(peak(after.L.slice(-2560))).toBeLessThan(1e-3);
  });
});
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `npx vitest run src/drum/engine/worklet-drum.test.ts` — Expected: FAIL (worklet-drum.js missing).

- [ ] **Step 4: Implement `src/drum/engine/worklet-drum.js`**

Structure (all in one self-contained file, ~450 lines; copy the referenced functions from `src/engine/worklet.js` verbatim where noted):

```js
// FableSynth DR-1 DSP core — AudioWorklet thread. Self-contained (no imports).
// 16 one-shot pad voices + sample-accurate step sequencer. See worklet.js for
// the reference implementations of the shared primitives (mip playback, SVF,
// ADAA drive) — copied here because worklets can't import.
//
// In:  {t:'init',params} {t:'tables',list} {t:'p',k,v} {t:'trig',pad,v}
//      {t:'pats',data} {t:'chain',list} {t:'play'} {t:'stop'} {t:'sel',pad} {t:'panic'}
// Out: {t:'step',s,pat,hits} per step while playing
//      {t:'viz',a,b,env} every 2048 samples for the selected pad

const NPADS = 16;
const MAXUNI = 7;
const STEPS = 16;
const NPATTERNS = 4;
const ACCENT_VEL = 1.0;
const PLAIN_VEL = 0.72;
const SWING_MAX = 0.667;
const MOD_LOG_D = 5;
const CHOKE_FADE = 0.12; // per-sample decay factor ≈ silence in ~2ms at 48k (worklet.js state-5 fade)
const BASE_NOTE = 60;    // pad pitch reference: C3 + tune + fine

// dst index -> handler tag (see DMOD_DESTS in drum/params.ts — keep in sync)
// 1 A POS, 2 B POS, 3 LEVEL, 4 CUTOFF, 5 PITCH, 6 A FINE, 7 B FINE, 8 NOISE LVL, 9 RES

function lcosh(z) { /* copy verbatim from worklet.js lines 197-200 */ }
function makeOscState() { /* as worklet.js makeOscState but MAXUNI=7, no blend fields needed */ }
function makeFilterState() {
  // SVF-only subset of worklet.js makeFilterState (no comb/formant):
  return { svf: new Float64Array(8), cutSm: 0, satXL: 0, satXR: 0, ftype: 0, twoPole: false, a1: 0, a2: 0, a3: 0, k1: 0 };
}

class PadVoice {
  constructor() {
    this.active = false;
    this.vel = 1; this.rand = 0;
    this.t = 0;            // samples since trigger
    this.ampLevel = 0; this.choking = false;
    this.oA = makeOscState(); this.oB = makeOscState();
    this.f = makeFilterState();
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
  }
  trigger(v) {
    this.active = true; this.choking = false;
    this.vel = v; this.rand = Math.random() * 2 - 1;
    this.t = 0; this.ampLevel = 0;
    // Start phase: pad<i>.oscA/B.phase sets the phase in cycles (×2048 samples,
    // all tables are 2048 wide) — deterministic transients, unlike WT-1's
    // random phase. Applied in the processor's trigger() where `p` is visible.
    this.oA.posSm = -1; this.oB.posSm = -1;
    this.f.svf.fill(0); this.f.cutSm = 0; this.f.satXL = 0; this.f.satXR = 0;
    this.dcxL = this.dcxR = this.dcyL = this.dcyR = 0;
  }
  choke() { if (this.active) this.choking = true; }
  kill() { this.active = false; this.choking = false; this.ampLevel = 0; }
}

class DrumProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = Object.create(null);
    this.tables = [];
    this.voices = []; for (let i = 0; i < NPADS; i++) this.voices.push(new PadVoice());
    this.pats = new Uint8Array(NPATTERNS * NPADS * STEPS);
    this.chain = [0]; this.chainPos = 0;
    this.playing = false;
    this.step = -1;                 // last fired step
    this.samplesToNext = 0;         // samples until the next step boundary fires
    this.sel = 0;
    this.vizCount = 0;
    this.tmpL = new Float32Array(128); this.tmpR = new Float32Array(128);
    this.fL = new Float32Array(128); this.fR = new Float32Array(128);
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(d) {
    switch (d.t) {
      case 'init': for (const k in d.params) { const v = d.params[k]; if (Number.isFinite(v)) this.p[k] = v; } break;
      case 'p': if (Number.isFinite(d.v)) this.p[d.k] = d.v; break;
      case 'tables': /* same mapping as worklet.js case 'tables' */ break;
      case 'trig': this.trigger(d.pad | 0, d.v); break;
      case 'pats': this.pats = new Uint8Array(d.data.slice(0)); break;
      case 'chain': if (Array.isArray(d.list) && d.list.length) { this.chain = d.list.map((x) => x | 0); this.chainPos = Math.min(this.chainPos, this.chain.length - 1); } break;
      case 'play': this.playing = true; this.step = -1; this.chainPos = 0; this.samplesToNext = 0; break;
      case 'stop': this.playing = false; this.step = -1; break;
      case 'sel': this.sel = Math.max(0, Math.min(NPADS - 1, d.pad | 0)); break;
      case 'panic': for (const v of this.voices) v.kill(); break;
    }
  }

  trigger(padI, vel) {
    if (padI < 0 || padI >= NPADS) return;
    const g = this.p['pad' + padI + '.choke'] | 0;
    if (g > 0) for (let j = 0; j < NPADS; j++) {
      if (j !== padI && (this.p['pad' + j + '.choke'] | 0) === g) this.voices[j].choke();
    }
    this.voices[padI].trigger(Math.max(0, Math.min(1, vel)));
  }

  // Per-block per-pad mod: returns {posA,posB,level,cutMul,pitch,fineCt,noiseLvl,res}
  padMod(padI, v) {
    const p = this.p, pre = 'pad' + padI + '.';
    const dec = Math.max(0.002, p[pre + 'modenv.dec'] / 4.5);
    const env = Math.exp(-v.t / (dec * sampleRate));
    const srcs = [0, env, v.vel * p[pre + 'v2m'], v.rand];
    const m = { posA: 0, posB: 0, level: 0, cut: 0, pitch: 0, fineA: 0, fineB: 0, noise: 0, res: 0 };
    for (let n = 1; n <= 4; n++) {
      const src = p[pre + 'mod' + n + '.src'] | 0, dst = p[pre + 'mod' + n + '.dst'] | 0;
      if (!src || !dst) continue;
      const x = srcs[src] * (p[pre + 'mod' + n + '.amt'] || 0);
      switch (dst) {
        case 1: m.posA += x; break;           // lin, width 1
        case 2: m.posB += x; break;
        case 3: m.level += x; break;          // lin, width 1
        case 4: m.cut += x; break;            // log: 2^(x·5) applied at cutoff
        case 5: m.pitch += x * 24; break;     // ±24 st per full route
        case 6: m.fineA += x * 200; break;    // lin over the ±100ct range
        case 7: m.fineB += x * 200; break;
        case 8: m.noise += x; break;
        case 9: m.res += x; break;
      }
    }
    return m;
  }

  // setupOsc / renderOsc: adapted from worklet.js setupOsc/renderOsc —
  //  * pitch = BASE_NOTE + tune + (fine + mFine)/100 + mPitch  (no keyboard, no bend)
  //  * pos = clamp01(p[pos] + mPos), same posSm smoothing + mip crossfade
  //  * unison detune/gains: same math, spread fixed at 0.6, blend absent (all weights 1,
  //    normalize by sqrt(uni))
  //  * gain = level²·0.32/sqrt(uni)
  // setupFilter / runFilter: SVF subset of worklet.js (types 0..4) + the same ADAA
  //  drive; cutoff = clamp(p[cut]·2^(mCut·MOD_LOG_D), 20, 0.45·sr), res = clamp(p[res]+mRes, 0, 0.999).

  // AHD amp envelope, per sample. Returns gain; advances v.t externally.
  // att: linear 0→1 over att·sr samples; hold at 1; decay: morph lin↔exp by curve.
  ampEnv(v, pre, i) {
    const p = this.p;
    const att = Math.max(1, p[pre + 'aenv.att'] * sampleRate);
    const hold = p[pre + 'aenv.hold'] * sampleRate;
    const dec = Math.max(1, p[pre + 'aenv.dec'] * sampleRate);
    const t = v.t + i;
    if (t < att) return t / att;
    const td = t - att - hold;
    if (td < 0) return 1;
    if (td >= dec) return 0;
    const lin = 1 - td / dec;
    const exp = Math.exp(-4.5 * td / dec);
    const c = p[pre + 'aenv.curve'];
    return lin + (exp - lin) * c;
  }

  // renderPad(v, padI, L, R, off, n): renders one pad voice into L/R at offset.
  //  1. m = this.padMod(padI, v)
  //  2. setupOsc A/B with mod; render into tmp buffers (zeroed 0..n)
  //  3. noise: white × tilt one-pole: y += (w−y)·a where a maps color −1..1 →
  //     0.02..1 (dark→bright); level (p+m.noise)² · 0.35
  //  4. pitch env: pe = amt·exp(−4.5·t/(dec·sr)) semitones — recompute osc incs
  //     every 16 samples (sub-chunk loop) so kick sweeps are smooth
  //  5. filter if flt.on (SVF + drive)
  //  6. per sample: amp = ampEnv × velGain × lvl² ; velGain = 1 − v2l·(1−vel)
  //     choke: while v.choking, ampLevel ×= (1−CHOKE_FADE) toward 0, kill at 1e-4
  //     pan: equal-power from pad.pan; DC blocker same as worklet.js (DC_R 0.9998)
  //  7. v.t += n; deactivate when past att+hold+dec and level < 1e-4

  process(_inputs, outputs) {
    const out = outputs[0];
    const L = out[0], R = out.length > 1 ? out[1] : out[0];
    L.fill(0); if (R !== L) R.fill(0);
    const n = L.length;

    // Sequencer: split the block at step boundaries so triggers are sample-accurate.
    let pos = 0;
    while (pos < n) {
      let run = n - pos;
      if (this.playing) {
        if (this.samplesToNext <= 0) this.fireStep();
        run = Math.min(run, Math.ceil(this.samplesToNext));
      }
      for (let i = 0; i < NPADS; i++) {
        const v = this.voices[i];
        if (v.active) this.renderPad(v, i, L, R, pos, run);
      }
      if (this.playing) this.samplesToNext -= run;
      pos += run;
    }

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      const v = this.voices[this.sel];
      this.port.postMessage({
        t: 'viz',
        a: v.active ? v.oA.posSm : -1,
        b: v.active ? v.oB.posSm : -1,
        env: v.active ? v.ampLevel : 0,
      });
    }
    return true;
  }

  fireStep() {
    const bpm = Math.max(60, Math.min(200, this.p['seq.bpm'] || 126));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = this.p['master.swing'] || 0;
    const next = this.step + 1;
    if (next >= STEPS) { this.step = -1; this.chainPos = (this.chainPos + 1) % this.chain.length; }
    const s = (this.step + 1) % STEPS;
    const pat = this.chain[this.chainPos] | 0;
    const hits = [];
    for (let i = 0; i < NPADS; i++) {
      const val = this.pats[pat * NPADS * STEPS + i * STEPS + s];
      if (val) { this.trigger(i, val === 2 ? ACCENT_VEL : PLAIN_VEL); hits.push(i); }
    }
    this.step = s;
    // time to the NEXT boundary, including the swing offsets of this and the next step
    const offNow = s % 2 === 1 ? swing * SWING_MAX * dur : 0;
    const sNext = (s + 1) % STEPS;
    const offNext = sNext % 2 === 1 ? swing * SWING_MAX * dur : 0;
    this.samplesToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'step', s, pat, hits });
  }
}

registerProcessor('fable-dr', DrumProcessor);
```

The elided bodies (`setupOsc`, `renderOsc`, `setupFilter`, `runFilter`, `tables` mapping, `PadVoice.trigger` phase/filter reset) are direct adaptations of the identically named code in `src/engine/worklet.js` — copy them and strip what drums don't have (no glide/bend/keytrack, no blend, no comb/vowel, no LFOs, no filter2/routing). Every numeric constant that survives (0.32 osc gain, 0.9998 DC_R, ADAA thresholds, SVF coefficient math, detune cents = sprd·det·50, spread pan law) must remain identical to `worklet.js`.

- [ ] **Step 5: Run tests until green**

Run: `npx vitest run src/drum/engine/worklet-drum.test.ts` — Expected: PASS (iterate on the DSP until all 8 tests pass; the harness makes failures reproducible).

- [ ] **Step 6: Full suite + typecheck**

Run: `npm test && npx tsc --noEmit` — Expected: all green (WT-1 tests untouched).

- [ ] **Step 7: Commit**

```bash
git add src/drum/engine/worklet-drum.js src/drum/engine/workletHarness.ts src/drum/engine/worklet-drum.test.ts
git commit -m "feat(drum): DR-1 DSP core — 16 pad voices + sample-accurate sequencer"
```

---

### Task 5: Kits (`src/drum/kits.ts`)

**Files:**
- Create: `src/drum/kits.ts`
- Test: `src/drum/kits.test.ts`

**Interfaces:**
- Consumes: `defaultDrumParams, pad, PAD_COUNT` from `./params`; `makeEmptyPatterns, patIdx, type Patterns` from `./seq`; `type SerializedUserTable` from `../engine/usertables`
- Produces:
  - `interface Kit { name: string; params: Partial<ParamValues>; padNames: string[]; patterns: number[]; chain: number[]; tables?: SerializedUserTable[] }` (`patterns` = plain array copy of the Uint8Array for JSON)
  - `FACTORY_KITS: Kit[]` — `TR-VOID` (default; pad names + pattern from the mockup: KICK/KICK 2/SNARE/CLAP/RIM/CH HAT/OH HAT/RIDE/TOM LO/TOM MD/TOM HI/CRASH/PERC 1/PERC 2/VOX/GLITCH; kick on 0/4/8/12 with accent 0, snare 4/12, clap 4/12, rim 7, ch-hat 0/2/4/6/8/10/12 accents 4/12, oh-hat 14 accent, perc1 3/11, vox 10), `ROOM ONE`, `BITCRUSH`
  - `loadUserKits(): Kit[]`, `saveUserKit(name, kit): Kit[]` (localStorage key `fable-dr-kits`)
  - `kitToState(kit)` / `stateToKit(name, params, padNames, patterns, chain, tables)` — plain converters between Kit JSON and live store state (patterns array ↔ Uint8Array)

Factory kit sound design (params each kit overrides — keep musical, exact values are the implementer's ear but structure is fixed):
- **TR-VOID**: pad 0 KICK = THUD osc A, tune −14, penv +22st/60ms, dec 300ms, choke —; pad 2 SNARE = CRACK + noise 0.5/color +0.3, dec 180ms; pads 5/6 hats = TINE, choke group 1, dec 40/300ms, filter HP on; pad 15 GLITCH = GRIT. BPM 126, swing 0.22, comp + reverb on (defaults).
- **ROOM ONE**: softer — lower penv amounts, longer holds, reverb mix 0.3, chorus on.
- **BITCRUSH**: GRIT-heavy, drive on amt 0.6, delay on, BPM 140.

- [ ] **Step 1: Write the failing test**

```ts
// src/drum/kits.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { FACTORY_KITS, loadUserKits, saveUserKit, kitToState, stateToKit, type Kit } from './kits';
import { DRUM_PARAMS, defaultDrumParams, PAD_COUNT } from './params';
import { NPATTERNS, STEPS, patIdx } from './seq';

describe('kits', () => {
  beforeEach(() => localStorage.clear());

  it('ships TR-VOID, ROOM ONE, BITCRUSH; TR-VOID matches the mockup names', () => {
    expect(FACTORY_KITS.map((k) => k.name)).toEqual(['TR-VOID', 'ROOM ONE', 'BITCRUSH']);
    const tv = FACTORY_KITS[0];
    expect(tv.padNames).toHaveLength(PAD_COUNT);
    expect(tv.padNames[0]).toBe('KICK');
    expect(tv.padNames[2]).toBe('SNARE');
    expect(tv.patterns).toHaveLength(NPATTERNS * PAD_COUNT * STEPS);
    expect(tv.patterns[patIdx(0, 0, 0)]).toBe(2); // kick accent on the one
  });

  it('every factory kit param id exists in DRUM_PARAMS and is in range', () => {
    for (const k of FACTORY_KITS) {
      for (const [id, v] of Object.entries(k.params)) {
        const d = DRUM_PARAMS[id];
        expect(d, `${k.name}: ${id}`).toBeDefined();
        if (d.min !== undefined) { expect(v).toBeGreaterThanOrEqual(d.min!); expect(v).toBeLessThanOrEqual(d.max!); }
      }
    }
  });

  it('user kit save/load round-trip preserves everything', () => {
    const params = defaultDrumParams();
    params['pad3.oscA.tune'] = -7;
    const patterns = new Uint8Array(NPATTERNS * PAD_COUNT * STEPS);
    patterns[patIdx(2, 3, 5)] = 2;
    const kit = stateToKit('MY KIT', params, FACTORY_KITS[0].padNames, patterns, [0, 2], []);
    saveUserKit('MY KIT', kit);
    const loaded = loadUserKits();
    expect(loaded).toHaveLength(1);
    const st = kitToState(loaded[0]);
    expect(st.params['pad3.oscA.tune']).toBe(-7);
    expect(st.patterns[patIdx(2, 3, 5)]).toBe(2);
    expect(st.chain).toEqual([0, 2]);
  });

  it('saving under an existing user name overwrites, not duplicates', () => {
    const kit: Kit = stateToKit('X', defaultDrumParams(), FACTORY_KITS[0].padNames, new Uint8Array(NPATTERNS * PAD_COUNT * STEPS), [0], []);
    saveUserKit('X', kit);
    saveUserKit('X', kit);
    expect(loadUserKits()).toHaveLength(1);
  });
});
```

Note: vitest needs a localStorage. `vitest.config.ts` has no jsdom; add to the **test file only** if `localStorage` is undefined — implement `kits.ts` to fall back to an in-memory Map when `localStorage` is missing (same guard WT-1's `usertables.ts` uses — check and mirror its pattern; if it has none, wrap access in try/catch with a module-level Map fallback).

- [ ] **Step 2: Run test to verify it fails** — `npx vitest run src/drum/kits.test.ts`

- [ ] **Step 3: Implement `src/drum/kits.ts`** — Kit interface + converters (patterns `Array.from(uint8)` ↔ `Uint8Array.from(arr)`), `FACTORY_KITS` with the three kits (TR-VOID pattern data transcribed from the mockup's constructor: `set(0,[0,4,8,12],[0]); set(2,[4,12],[12]); set(3,[4,12]); set(4,[7]); set(5,[0,2,4,6,8,10,12],[4,12]); set(6,[14],[14]); set(12,[3,11]); set(14,[10])` — all in pattern A), localStorage persistence under `fable-dr-kits` with the try/catch Map fallback.

- [ ] **Step 4: Run test to verify it passes** — `npx vitest run src/drum/kits.test.ts`

- [ ] **Step 5: Commit**

```bash
git add src/drum/kits.ts src/drum/kits.test.ts
git commit -m "feat(drum): kit model + TR-VOID / ROOM ONE / BITCRUSH factory kits"
```

---

### Task 6: Main-thread engine (`src/drum/engine/drum-synth.ts`)

**Files:**
- Create: `src/drum/engine/drum-synth.ts`
- Test: `src/drum/engine/drum-synth.test.ts` (pure routing logic only)

**Interfaces:**
- Consumes: `generateDrumTables` (Task 2), `generateTables, type GeneratedTable` from `../../engine/wavetables`, `defaultDrumParams` (Task 1), `import workletUrl from './worklet-drum.js?url'`
- Produces: `class DrumEngine` —
  - `params: ParamValues`, `tables: VizTable[] | null`, `ready: boolean`, `onstep: ((d:{s:number;pat:number;hits:number[]})=>void)|null`, `onviz: ((d:{a:number;b:number;env:number})=>void)|null`, `scopeAnalyser!: AnalyserNode`
  - `async init()`: AudioContext → addModule → tables = drum + procedural → node `'fable-dr'` → `{t:'init'}` + `pushTables()` → `buildFx()` → connect
  - `setParam(id, v)`: `fx.*`/`master.volume` → `applyAllFx()`; everything else → `{t:'p'}` (exported pure helper `isFxParam(id: string): boolean` so it's testable)
  - `applyAllParams()`, `pushTables()`, `setUserTables(tables: GeneratedTable[])` (per-kit imported tables, appended after the 10 built-ins)
  - `trigger(pad: number, vel: number)`, `play()`, `stop()`, `setPatterns(p: Uint8Array)`, `setChain(c: number[])`, `selectPad(i: number)`, `panic()`
  - FX graph: `worklet → drive (WaveShaper, as synth.ts) → comp (DynamicsCompressorNode: threshold=fx.comp.thr, ratio 4, knee 9, attack 0.003, release 0.25, makeup = post-gain 10^(fx.comp.gain/20), bypass via WetDry with mix 1) → chorus → delay → reverb → masterGain → dcBlock → limiter → destination`, `scopeAnalyser` on masterGain. Copy `mkWetDry`, `setMix`, `renderImpulse`, and the drive/chorus/delay/reverb node code from `synth.ts` `buildFx`/`applyAllFx` verbatim, adding the comp stage.

- [ ] **Step 1: Write the failing test**

```ts
// src/drum/engine/drum-synth.test.ts
import { describe, it, expect } from 'vitest';
import { isFxParam } from './drum-synth';

describe('drum engine param routing', () => {
  it('fx + master.volume stay on the main thread, the rest goes to the worklet', () => {
    expect(isFxParam('fx.drive.amt')).toBe(true);
    expect(isFxParam('fx.comp.thr')).toBe(true);
    expect(isFxParam('master.volume')).toBe(true);
    expect(isFxParam('master.swing')).toBe(false); // worklet needs it for timing
    expect(isFxParam('seq.bpm')).toBe(false);
    expect(isFxParam('pad0.oscA.tune')).toBe(false);
  });
});
```

- [ ] **Step 2: Run test to verify it fails** — `npx vitest run src/drum/engine/drum-synth.test.ts`

- [ ] **Step 3: Implement `drum-synth.ts`** per the interface block above. `export const isFxParam = (id: string): boolean => id.startsWith('fx.') || id === 'master.volume';` All node-touching methods guard on `this.ready` exactly like `SynthEngine` so the class is constructible (and the store testable) without audio.

- [ ] **Step 4: Run test + typecheck** — `npx vitest run src/drum/engine/drum-synth.test.ts && npx tsc --noEmit` — PASS

- [ ] **Step 5: Commit**

```bash
git add src/drum/engine/drum-synth.ts src/drum/engine/drum-synth.test.ts
git commit -m "feat(drum): main-thread engine — worklet host + FX graph with comp stage"
```

---

### Task 7: Drum store (`src/drum/store.ts`)

**Files:**
- Create: `src/drum/store.ts`
- Test: `src/drum/store.test.ts`

**Interfaces:**
- Consumes: `DrumEngine` (Task 6), params (Task 1), seq (Task 3), kits (Task 5), `makeUserTable, loadUserTablePool…` NOT needed (kit tables handled in kits), `buildUserTable` for drop-WAV later.
- Produces: `export const drumEngine = new DrumEngine()` singleton + `useDrumStore` (zustand) with state
  `{ params, sel, patterns (Uint8Array), chain: number[], chaining: boolean, editPattern: number, playing, curStep, curPat, powered, midiActive, kitValue: string, userKits: Kit[], padNames: string[], hitTick: Record<number, number> (pad → performance.now of last hit, for LED flashes), mode: 'step'|'pads', userTables: UserTable-like[] }`
  and actions
  `{ setParam, selectPad(i) (also engine.selectPad + audition trigger at 0.8), triggerPad(i, vel), toggleStep(step) (cycles editPattern×sel×step, pushes full patterns to engine), setEditPattern(i), setChaining(on), chainClick(i) (append while chaining), setMode, powerOn, play, stop, saveKit(name), loadKitByValue(v), stepKit(d), setPadName(i, name), setParamsFromKit, importPadTable(padI, table) }`
- Worklet callbacks wired in `powerOn`: `engine.onstep = (d) => set({ curStep: d.s, curPat: d.pat, hitTick: merge d.hits })`, `engine.onviz` → `{ modPosA, modPosB, envLevel }`.

- [ ] **Step 1: Write the failing test**

```ts
// src/drum/store.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { useDrumStore } from './store';
import { patIdx } from './seq';
import { defaultDrumParams } from './params';

describe('drum store', () => {
  beforeEach(() => {
    localStorage.clear();
    useDrumStore.setState({
      params: defaultDrumParams(), sel: 0, editPattern: 0, chain: [0], chaining: false,
      patterns: new Uint8Array(4 * 16 * 16),
    });
  });

  it('toggleStep cycles off→on→accent→off on the selected pad + edit pattern', () => {
    const s = useDrumStore.getState();
    s.selectPad(2);
    s.toggleStep(5);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(1);
    useDrumStore.getState().toggleStep(5);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(2);
    useDrumStore.getState().toggleStep(5);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(0);
  });

  it('setParam updates the store map', () => {
    useDrumStore.getState().setParam('pad0.oscA.tune', -14);
    expect(useDrumStore.getState().params['pad0.oscA.tune']).toBe(-14);
  });

  it('chain building: chaining mode appends, leaving keeps ≥1 entry', () => {
    const s = useDrumStore.getState();
    s.setChaining(true);
    s.chainClick(0); s.chainClick(1); s.chainClick(0);
    expect(useDrumStore.getState().chain).toEqual([0, 1, 0]);
    useDrumStore.getState().setChaining(false);
    // outside chaining, clicking a pattern selects it for editing and resets the chain to just it
    useDrumStore.getState().chainClick(2);
    expect(useDrumStore.getState().editPattern).toBe(2);
    expect(useDrumStore.getState().chain).toEqual([2]);
  });

  it('kit save + load round-trips patterns and params', () => {
    const s = useDrumStore.getState();
    s.setParam('pad1.penv.amt', 30);
    s.selectPad(1);
    s.toggleStep(0);
    s.saveKit('TEST KIT');
    s.setParam('pad1.penv.amt', 0);
    const val = useDrumStore.getState().kitValue;
    expect(val.startsWith('u')).toBe(true);
    useDrumStore.getState().loadKitByValue(val);
    expect(useDrumStore.getState().params['pad1.penv.amt']).toBe(30);
    expect(useDrumStore.getState().patterns[patIdx(0, 1, 0)]).toBe(1);
  });
});
```

(Engine calls inside actions are safe unpowered: every `DrumEngine` method guards on `ready`.)

- [ ] **Step 2: Run test to verify it fails** — `npx vitest run src/drum/store.test.ts`

- [ ] **Step 3: Implement `src/drum/store.ts`** following `src/store.ts` structurally: singleton `drumEngine`, zustand `create<DrumStore>()`, kit value scheme `'f<i>' | 'u<i>'` with `kitOptions()` like `presetOptions`. `toggleStep` copies the Uint8Array (`patterns.slice()`), cycles via `cycleStep`, `set`s and calls `drumEngine.setPatterns`. `selectPad` sets `sel`, calls `drumEngine.selectPad(i)` and `drumEngine.trigger(i, 0.8)` for audition, stamps `hitTick`. `loadKitByValue` applies `kitToState`, calls `engine.panic() + applyAllParams() + setPatterns + setChain`, restores per-kit user tables via `engine.setUserTables`. `powerOn` mirrors WT-1's.

- [ ] **Step 4: Run all tests + typecheck** — `npm test && npx tsc --noEmit` — PASS

- [ ] **Step 5: Commit**

```bash
git add src/drum/store.ts src/drum/store.test.ts
git commit -m "feat(drum): zustand store — params, patterns, chain, kits, transport"
```

---

### Task 8: App shell — Vite entry, controls, header (`drum/index.html`, `DrumApp`, knob/stepper, power overlay, CSS)

**Files:**
- Create: `drum/index.html` (copy `app/index.html`, title `FABLESYNTH DR-1 — Play`, script `/src/drum/main.tsx`)
- Create: `src/drum/main.tsx` (StrictMode mount of `DrumApp`, imports `../index.css` then `./drum.css`)
- Create: `src/drum/drum.css`
- Create: `src/drum/components/DrumKnob.tsx`, `src/drum/components/DrumStepper.tsx`, `src/drum/components/DrumPowerOverlay.tsx`, `src/drum/components/Header.tsx`
- Create: `src/drum/DrumApp.tsx`
- Modify: `vite.config.ts` (add `drum: resolve(__dirname, 'drum/index.html')` to `rollupOptions.input`)

**Interfaces:**
- `DrumKnob({ paramId, size?, accent?, label? })` — WT-1 `Knob.tsx` minus ModRing/drop logic, reading `DRUM_PARAMS` + `useDrumStore`; identical CSS classes (`knob knob-{size}`, `k-body/k-track/k-arc/k-ptr/k-label/k-value`), same drag/wheel/dblclick/keyboard handlers, same `arcPath` (copy the 3 tiny geometry helpers or re-export them — they're 15 lines; copy into `DrumKnob.tsx`).
- `DrumStepper({ paramId, label?, accent? })` — WT-1 `Stepper.tsx` numeric+enum modes against the drum store (no user-table extension here; the osc table stepper appends store `userTables` names).
- `DrumPowerOverlay` — WT-1 `PowerOverlay.tsx` with model line `DR-1 · DRUM SYNTHESIS ENGINE` and `useDrumStore((s) => s.powerOn)`.
- `Header` — brand `FABLE SYNTH … DR-1`, kit stepper (`◂ name ▸` + SAVE prompting via `window.prompt` like WT-1's TopBar — check TopBar's save flow and mirror it), STEP/PADS toggle buttons (store `mode`), `ScopeView` (imported from `../../components/displays/ScopeView`, `analyser={drumEngine.scopeAnalyser}` — render only when `powered`), MIDI LED, BPM readout (a `DrumKnob`-less draggable number: reuse DrumStepper numeric mode for `seq.bpm` plus `SYNC` label), `DrumKnob paramId="master.swing"` + `"master.volume"` size `md`.
- `DrumApp` — `<DrumPowerOverlay/><main id="drum-rack"><Header/>` + placeholder empty grid divs for the sections built in Tasks 9–12 (each subsequent task replaces one placeholder).
- `drum.css` — layout: `#drum-rack{max-width:1460px;min-width:1180px;margin:0 auto;padding:14px 18px 22px}`, `.dr-main{display:grid;grid-template-columns:352px 1fr;gap:9px}`, panel chrome reuses `.panel/.panel-head` from index.css; add `.pad-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}`, `.pad`, `.pad.sel`, `.pad-led`, `.step-row{display:flex;gap:6px}`, `.step`, `.step .acc`, `.step .fill`, `.step.cur`, `.fx-rack{display:grid;grid-template-columns:repeat(5,1fr) 190px;gap:10px}` etc. — colors/borders/shadows transcribed from the mockup's inline styles (panel bg `linear-gradient(180deg,#181c26,#0d1017)`, border `rgba(255,255,255,0.07)`, radius 12px, well `#07090e`).

- [ ] **Step 1: Add the Vite entry + files above** (no test framework for tsx; the gate is build + visual)
- [ ] **Step 2: Verify build**

Run: `npx tsc --noEmit && npm run build`
Expected: clean; `dist/drum/index.html` exists.

- [ ] **Step 3: Verify visually**

Run: `npm run dev` (background), open `http://localhost:5173/drum/` with the chrome-debug skill, screenshot. Expected: power overlay renders; after power-on the header row matches the mockup (brand, kit bar, STEP/PADS, scope box, BPM/SYNC, 2 knobs).

- [ ] **Step 4: Commit**

```bash
git add drum/index.html src/drum/main.tsx src/drum/drum.css src/drum/DrumApp.tsx src/drum/components/ vite.config.ts
git commit -m "feat(drum): DR-1 app shell — third vite entry, header, drum-store controls"
```

---

### Task 9: Pad grid + pad strip + input (keys, MIDI, drop-WAV)

**Files:**
- Create: `src/drum/components/PadGrid.tsx`, `src/drum/components/PadStrip.tsx`
- Create: `src/drum/hooks/useDrumKeys.ts`, `src/drum/hooks/useDrumMidi.ts`
- Modify: `src/drum/DrumApp.tsx` (mount left column + hooks)

**Interfaces:**
- `PadGrid` — 4×4 buttons in drum-machine order (`row 3→0` outer, `col 0→3` inner, i = row*4+col — pad 01 bottom-left, exactly the mockup's `renderVals` loop). Tile: `{num 2-digit}`, LED (lit ~180 ms after `hitTick[i]`, driven by a 100 ms interval or rAF check), name (input-on-double-click → `setPadName`), choke/out tag (`CHOKE_NAMES/OUT_NAMES` of the pad's params). Click → `selectPad(i)`. Selected ring class `.sel`. Panel header note `DROP WAV → WAVETABLE`; drag-over highlights tile; drop decodes the file with `AudioContext.decodeAudioData` → `mixToMono` + `detectCycleLength` + `sliceToFrames` from `../../engine/usertables` → `buildUserTable` → `importPadTable(i, table)` (store appends to `userTables`, calls `engine.setUserTables`, points `pad<i>.oscA.table` at index `10 + poolIdx`).
- `useDrumKeys` — keymap `Digit1..4 / KeyQWER / KeyASDF / KeyZXCV` → pads 12..15 / 8..11 / 4..7 / 0..3 (top row of keys = top row of grid = pads 13–16), `triggerPad(i, 0.85)` on keydown (no repeat), Escape → panic-stop. Skip when focus is in an input.
- `useDrumMidi` — WT-1 `useMidi` adapted: note-on `n` in `[MIDI_BASE, MIDI_BASE+15]` → `triggerPad(n − MIDI_BASE, vel)`; sets `midiActive`.
- `PadStrip` — panel `PAD`: CHOKE stepper (`pad<sel>.choke`), OUT stepper (`pad<sel>.out`), knobs LVL/PAN/V→LVL/V→MOD (`pad<sel>.lvl` etc., size `sm`). All paramIds derived from `sel` so the strip re-binds on selection.

- [ ] **Step 1: Implement all files** per interfaces (components read `sel` from the store; paramIds are computed strings, e.g. `pad(sel,'lvl')`).
- [ ] **Step 2: Verify** — `npx tsc --noEmit && npm test` clean; dev-server screenshot: pad grid matches mockup ordering (01 bottom-left, 13 top-left), clicking pads moves the ring and (with audio powered) auditions; keys trigger.
- [ ] **Step 3: Commit** — `git add src/drum && git commit -m "feat(drum): pad grid, pad strip, key/MIDI/drop-WAV input"`

---

### Task 10: Oscillator, noise + editor-row panels

**Files:**
- Create: `src/drum/components/OscSection.tsx`, `src/drum/components/NoiseSection.tsx`, `src/drum/components/PosSlider.tsx`, `src/drum/components/NoiseView.tsx`, `src/drum/components/SelBar.tsx`
- Modify: `src/drum/DrumApp.tsx` (right column top: SelBar + osc grid `1fr 1fr 196px`)

**Interfaces:**
- `SelBar` — the thin bar above the editor: LED dot, `PAD {selNum}` + name (cyan), right-side hint text `MOD ENV ▸ POS · THE MORPH AXIS` (static).
- `OscSection({ osc: 'oscA'|'oscB' })` — panel accent cyan/amber (`data-accent="a"|"b"` reusing index.css accent plumbing). Header: LED dot + `OSC A|B` + table stepper (`pad<sel>.<osc>.table`, options extended with store userTables names). Body grid `1fr 36px / 104px auto`: `WavetableView` (imported from WT-1 displays; `table` = viz entry for the pad's table index from `drumEngine.tables`, `pos` = param, `modPos` = store `modPosA/B` when `sel` is the viz pad, else −1, `accent` = '#4de8ff'|'#ffa14d'), `PosSlider` (vertical slider bound to `pad<sel>.<osc>.pos` — WT-1 `VSlider` drag logic against the drum store, fill + handle styled like the mockup, no mod band), knob row TUNE/FINE/PHASE/UNI/DET/LVL (LVL size `md`, rest `sm`).
- `NoiseSection` — header `NOISE` + `WHITE` readout; `NoiseView` canvas (rAF smoothed random walk line in amber, tilt responds to `noise.color` — port the mockup's `drawNoise` with the color feeding the smoothing factor); knobs COLOR (`sm`) + LVL (`md`), accent `b`.

- [ ] **Step 1: Implement** per interfaces.
- [ ] **Step 2: Verify** — typecheck + build; screenshot: three panels render, terrain animates with POS, table steppers switch tables (viz changes), knobs move audio when powered.
- [ ] **Step 3: Commit** — `git commit -am "feat(drum): osc A/B + noise editor panels with live terrain views"`

---

### Task 11: Pitch env, amp env, filter, mod panels

**Files:**
- Create: `src/drum/components/PitchEnvPanel.tsx`, `src/drum/components/AmpEnvPanel.tsx`, `src/drum/components/FilterSection.tsx`, `src/drum/components/ModPanel.tsx`, `src/drum/components/DrumEnvView.tsx`
- Modify: `src/drum/DrumApp.tsx` (right column bottom grid `1fr 1.15fr 1.15fr 1.3fr`)

**Interfaces:**
- `DrumEnvView({ mode: 'pitch'|'ahd', amt?, att?, hold?, dec, curve?, accent })` — pure canvas: pitch mode plots `amt·e^(−7p)` above/below a zero line with the `+NN ST` caption (mockup `drawPitchEnv`); ahd mode plots attack ramp → hold → curve-morphed decay (mockup `drawAmpEnv`, but honoring the real att/hold/dec/curve params, time axis normalized). Uses `setupCanvas` from WT-1 displays.
- `PitchEnvPanel` — title `PITCH ENV` (cyan), DrumEnvView(pitch), knobs AMT + DEC (`md`, accent `a`).
- `AmpEnvPanel` — title `AMP ENV`, right hint `AHD · ONE-SHOT`, DrumEnvView(ahd, accent `#e8edf7`), knobs ATT/HOLD/DEC/CURVE (`sm`).
- `FilterSection` — title `FILTER` (violet, accent `f`), type stepper (`pad<sel>.flt.type`), power dot toggling `pad<sel>.flt.on` (reuse `.power-btn` CSS), `FilterView` from WT-1 displays with `filters={[{type, cutoff, res, on}, {on:false,…}]} route={0}` accent `#b18cff`, knobs CUT (`md`) / RES / DRIVE (`sm`).
- `ModPanel` — title `MOD`, right hint `MOD ENV DEC {fmtSec(modenv.dec)}` (live) — plus a small DEC knob (`xs`) for `pad<sel>.modenv.dec` in the header. 4 rows: src select ▸ dst select (native `<select>` styled `.mod-select`, options `DMOD_SOURCES`/`DMOD_DESTS`, bound to `pad<sel>.mod<n>.src/.dst`) + amount knob (`xs`, `pad<sel>.mod<n>.amt`).

- [ ] **Step 1: Implement** per interfaces.
- [ ] **Step 2: Verify** — typecheck + tests + screenshot: four panels match mockup layout; filter curve tracks knobs; env views track their knobs; mod routes audibly change the selected pad (VELO→CUTOFF etc.).
- [ ] **Step 3: Commit** — `git commit -am "feat(drum): pitch/amp env, filter, and mod matrix panels"`

---

### Task 12: Step sequencer + FX rack + out panel

**Files:**
- Create: `src/drum/components/StepSeq.tsx`, `src/drum/components/FxRack.tsx`, `src/drum/components/OutPanel.tsx`
- Modify: `src/drum/DrumApp.tsx` (mount below the main grid)

**Interfaces:**
- `StepSeq` — header: play/stop button (`playing ? '■' : '▶'`, store `play`/`stop`), title `STEP SEQ`, A–D pattern buttons (lit = `editPattern`; while `chaining` clicks append via `chainClick`), `CHAIN` toggle button + readout `CHAIN A→B→…` from store `chain`, `EDITING {padName}` chip, hint `TAP STEP · ON → ACCENT → OFF`. Steps: 16 buttons flex row, extra 8px gap after every 4th (`margin-right` on 3,7,11), each showing accent bar (top, amber when `2`), fill (cyan gradient when ≥1, glow), amber current-step ring when `playing && curStep===j && curPat===chain[…]` (compare against `curPat === editPattern` for display), step number below. Click → `toggleStep(j)`. While playing the playhead advances from store `curStep` (updated by worklet `step` messages).
- `FxRack` — five groups DRIVE/COMP/CHORUS/DELAY/REVERB in the fx grid: each a bordered sub-panel with LED dot (click toggles `fx.<x>.on`; lit style = the mockup's radial glow dot) + title + its knobs (`sm`): drive AMT/MIX, comp THRESH/MAKEUP, chorus RATE/DEPTH/MIX, delay TIME/FDBK/MIX, reverb SIZE/MIX.
- `OutPanel` — informational routing list exactly like the mockup (MAIN — `N PADS` counted from params `pad*.out===0`; AUX 1..4 rows listing assigned pad names or `—`; header note `FX → MAIN ONLY`).

- [ ] **Step 1: Implement** per interfaces.
- [ ] **Step 2: Verify end-to-end** — power on, press play: TR-VOID groove plays with swing, playhead sweeps, pads flash on hits, tapping steps edits live, pattern B/C/D switch + chain works, FX knobs audibly change the master bus.
- [ ] **Step 3: Commit** — `git commit -am "feat(drum): step sequencer, FX rack, out panel — DR-1 feature complete"`

---

### Task 13: Wiring polish, landing link, docs, final verification

**Files:**
- Modify: `index.html` (landing) — add a `DR-1` link next to the existing app link (find the `app/` CTA and mirror it; keep styling consistent with the landing's markup).
- Modify: `README.md` — short DR-1 paragraph under a `## DR-1 drum machine (web)` heading: what it is, `drum/index.html` entry, synthesis-only, kits in localStorage; note the JUCE port is planned.
- Modify: `vite.config.ts` comment block (mention the third entry).

- [ ] **Step 1: Make the edits above**
- [ ] **Step 2: Full verification**

```bash
npm test                 # all suites green (params, tables, seq, worklet, kits, store, engine + existing WT-1 tests)
npx tsc --noEmit
npm run build            # three entries build
```

Then a manual chrome-debug pass of `dist/` (or dev server): power on → TR-VOID plays → program a beat on pad 05 → tweak OSC A tune + pitch env (kick changes) → choke test (open hat choked by closed hat) → save kit `MINE` → reload page → load `MINE` → everything restored. Screenshot the full UI next to the mockup for a visual diff.

- [ ] **Step 3: Commit + finish**

```bash
git add -A && git commit -m "feat(drum): landing link, README, final polish"
```

Then use superpowers:finishing-a-development-branch (merge/PR decision with Matti).

---

## Self-Review Notes

- **Spec coverage:** params model (T1), drum tables (T2), sequencer math + accent cycle + chain (T3, T4, T12), 16 one-shot voices with osc/noise/pitch-env/AHD/filter/mod/choke (T4), FX chain incl. new comp (T6), kits ×3 + persistence (T5, T7), pixel-faithful UI panels (T8–T12), pads-order grid + keys `1234/qwer/asdf/zxcv` + MIDI 36–51 + drop-WAV (T9), power overlay (T8), landing/README (T13), out-routing UI-only (T12 OutPanel). JUCE port explicitly out of scope.
- **Deviation from spec, on purpose:** drum-local `DrumKnob`/`DrumStepper`/`PosSlider`/`DrumPowerOverlay` instead of importing WT-1's store-bound versions (Global Constraints, bullet 2). Display canvases and all engine pipelines are imported, not copied.
- **Type consistency:** message names (`trig/pats/chain/play/stop/sel/step/viz`), store action names, and param ids are each defined once above and referenced identically across tasks.
