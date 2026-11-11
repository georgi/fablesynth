// The compiled C++ WT-1 (worklet-wasm.js) against the JavaScript worklet:
// factory presets play the same note script through both engines.
// docs/wasm-wt1.md records the measured numbers. Run with WASM_REPORT=1 to
// write one JSON line per preset to stderr.
import { describe, it, expect } from 'vitest';
import { bootWt, type WtHarness } from './workletHarness';
import { bootWtWasm, makeWtWasmProcessor, wtTableMsg } from './wtWasmHarness';
import { defaultParams } from '../params';
import { FACTORY_PRESETS } from '../presets';
import { makeEmptyPatterns, randomPattern, writePattern } from '../noteseq';
import type { ParamValues } from '../params';

const SR = 48000;
const db = (x: number) => 20 * Math.log10(Math.max(1e-12, x));
const sec = (s: number) => Math.round((s * SR) / 128);

// Poly: a held three-note chord, a fourth note, then release.
// Mono: a legato line with overlaps, so glide and last-note priority run.
type Ev = [block: number, t: 'on' | 'off', note: number, vel?: number];
const POLY: Ev[] = [[0, 'on', 48, 0.9], [0, 'on', 55, 0.8], [0, 'on', 60, 0.7], [sec(0.5), 'on', 64, 1],
  [sec(1.2), 'off', 48], [sec(1.2), 'off', 55], [sec(1.2), 'off', 60], [sec(1.2), 'off', 64]];
const MONO: Ev[] = [[0, 'on', 45, 0.9], [sec(0.25), 'on', 52, 0.7], [sec(0.3), 'off', 45], [sec(0.5), 'on', 57, 1],
  [sec(0.6), 'off', 52], [sec(0.8), 'off', 57], [sec(0.9), 'on', 40, 0.8], [sec(1.3), 'off', 40]];
const TOTAL = sec(2.5);

function play(h: WtHarness, script: Ev[]) {
  const L = new Float32Array(TOTAL * 128), R = new Float32Array(TOTAL * 128);
  for (let b = 0; b < TOTAL; b++) {
    for (const [at, t, n, v] of script) if (at === b) h.send(t === 'on' ? { t, n, v } : { t, n });
    const o = h.render(1);
    L.set(o.L, b * 128); R.set(o.R, b * 128);
  }
  return { L, R };
}

function diff(a: { L: Float32Array; R: Float32Array }, b: { L: Float32Array; R: Float32Array }) {
  let peakA = 0, peakD = 0, sumA = 0, sumB = 0, sumD = 0, finite = true;
  for (const [x, y] of [[a.L, b.L], [a.R, b.R]]) {
    for (let i = 0; i < x.length; i++) {
      const d = x[i] - y[i];
      if (!Number.isFinite(y[i])) finite = false;
      peakA = Math.max(peakA, Math.abs(x[i])); peakD = Math.max(peakD, Math.abs(d));
      sumA += x[i] * x[i]; sumB += y[i] * y[i]; sumD += d * d;
    }
  }
  const n = a.L.length * 2;
  return { peakJs: db(peakA), rmsJs: db(Math.sqrt(sumA / n)), rmsWasm: db(Math.sqrt(sumB / n)),
    peakDiff: db(peakD), rmsDiff: db(Math.sqrt(sumD / n)), finite };
}

export function compare(name: string) {
  const preset = FACTORY_PRESETS.find((p) => p.name === name)!;
  const params = preset.params as Partial<ParamValues>;
  const mono = (params['master.mono'] ?? 0) > 0.5;
  const script = mono ? MONO : POLY;
  return { name, mono, ...diff(play(bootWt(params), script), play(bootWtWasm(params), script)) };
}

const report = (r: object) => {
  const env = (globalThis as { process?: { env?: Record<string, string | undefined>; stderr?: { write(s: string): void } } }).process;
  if (env?.env?.WASM_REPORT) env.stderr?.write(JSON.stringify(r) + '\n');
};

describe('WT-1 wasm engine', () => {
  it('reports the FX latency and reads the Params id table', () => {
    const js = bootWt();
    const h = bootWtWasm();
    expect(h.sent.find((m) => m.t === 'latency')).toEqual(js.sent.find((m) => m.t === 'latency'));
  });

  for (const name of ['INIT', 'VELVET PAD', 'CRYSTAL PLUCK', 'HYPER SAW', 'VOWEL TALK', 'CATHEDRAL BELL',
    'NEURO WOBBLE', 'REESE BASS', 'FUTURE CHORD', 'HARPSI COMB', 'PUMP PAD', 'GLASS MORPH',
    'ACID LINE', 'SCREECH LEAD', 'GLIDE LEAD', 'MINI LEAD', 'FUNKY WORM']) {
    it(`renders ${name} close to the JavaScript worklet`, () => {
      const r = compare(name);
      report(r);
      expect(r.finite).toBe(true);
      expect(r.rmsWasm).toBeGreaterThan(-50);
      expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(1);
      expect(r.rmsDiff).toBeLessThan(-60);
    });
  }

  it('accepts the slot-by-slot table messages that synth.ts sends, and sends viz and mod', () => {
    const params = { ...defaultParams(), ...(FACTORY_PRESETS.find((p) => p.name === 'VELVET PAD')!.params as Partial<ParamValues>) };
    const h = makeWtWasmProcessor(SR);
    h.sendInit(params);
    const list = wtTableMsg().list;
    h.send({ t: 'tablecount', n: list.length });
    list.forEach((t, i) => h.send({ t: 'table', i, ...t }));
    const r = diff(play(bootWt(params), POLY), play(h, POLY));
    expect(r.rmsDiff).toBeLessThan(-60);
    expect(h.sent.filter((m) => m.t === 'viz').length).toBeGreaterThan(10);
    const mod = h.sent.find((m) => m.t === 'mod' && m.d);
    expect(mod && (mod.d as Float32Array).length).toBe(28);
  });

  it('plays the note sequencer with the same step messages', () => {
    let seed = 7;
    const rng = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
    const pats = writePattern(makeEmptyPatterns(), 0, randomPattern(rng));
    const boot = (h: WtHarness) => {
      h.send({ t: 'pats', data: pats.slice().buffer });
      h.send({ t: 'chain', list: [0] });
      h.send({ t: 'play' });
      return h.render(sec(2));
    };
    const js = bootWt(), wasm = bootWtWasm();
    const r = { name: 'SEQ', mono: false, ...diff(boot(js), boot(wasm)) };
    report(r);
    const steps = (h: WtHarness) => h.sent.filter((m) => m.t === 'step').map((m) => `${m.s}:${m.pat}`);
    expect(steps(wasm)).toEqual(steps(js));
    expect(r.finite).toBe(true);
    expect(r.rmsDiff).toBeLessThan(-50);
  });
});
