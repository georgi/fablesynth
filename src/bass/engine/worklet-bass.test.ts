import { describe, it, expect, beforeEach } from 'vitest';
import { makeBassProcessor, type BassHarness } from './bassHarness';
import { generateTables } from '../../engine/wavetables';
import { defaultBassParams } from '../params';
import { makeEmptyPatterns, setStep, stepDurSamples } from '../seq';
import type { ParamValues } from '../../params';

const tables = generateTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

function boot(params: ParamValues = defaultBassParams()): BassHarness {
  const h = makeBassProcessor();
  h.send({ t: 'init', params });
  h.send(tableMsg);
  return h;
}

const peak = (x: Float32Array) => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
const finite = (x: Float32Array) => x.every(Number.isFinite);
// rising zero-crossings — a cheap monophonic pitch proxy
const crossings = (x: Float32Array) => {
  let c = 0;
  for (let i = 1; i < x.length; i++) if (x[i - 1] <= 0 && x[i] > 0) c++;
  return c;
};

describe('bass voice', () => {
  let h: BassHarness;
  beforeEach(() => { h = boot(); });

  it('is silent until noteon, then audible, finite, bounded', () => {
    expect(peak(h.render(4).L)).toBe(0);
    h.send({ t: 'noteon', semi: 12, vel: 1 });
    const { L, R } = h.render(40);
    expect(peak(L)).toBeGreaterThan(0.01);
    expect(peak(L)).toBeLessThan(2);
    expect(finite(L) && finite(R)).toBe(true);
  });

  it('noteoff releases: tail decays to silence', () => {
    h.send({ t: 'noteon', semi: 12, vel: 1 });
    h.render(40);
    h.send({ t: 'noteoff', semi: 12 });
    // default release 80ms ≈ 3.8k samples; render 1.5s
    const tail = h.render(560).L;
    expect(peak(tail.slice(-2560))).toBeLessThan(1e-3);
  });

  it('legato is a slide: pitch approaches the new note over slide.time', () => {
    const p = defaultBassParams();
    p['slide.time'] = 0.4; // slow, measurable glide
    p['osc.unison'] = 1;
    p['osc.tune'] = 0; // C2 (65 Hz) so zero-crossings resolve pitch
    p['sub.level'] = 0;
    p['flt.cut'] = 20000; // open filter so crossings track the osc
    p['flt.env'] = 0;
    p['aenv.sus'] = 1;
    const hs = boot(p);
    hs.send({ t: 'noteon', semi: 0, vel: 1 });
    hs.render(40);
    hs.send({ t: 'noteon', semi: 12, vel: 1 }); // second key held → legato
    const early = crossings(hs.render(32).L); // ~85ms of a 400ms glide
    hs.render(400); // ride out the rest of the glide
    const late = crossings(hs.render(32).L);
    expect(late).toBeGreaterThan(early * 1.4); // ~2x at the octave
  });

  it('sub oscillator transposes -1/-2 octaves from the root', () => {
    const subFreq = (oct: number): number => {
      const p = defaultBassParams();
      p['osc.level'] = 0; // sub only
      p['sub.level'] = 1;
      p['sub.shape'] = 0;
      p['sub.oct'] = oct;
      p['flt.cut'] = 20000;
      p['flt.env'] = 0;
      p['flt.drive'] = 0;
      p['aenv.sus'] = 1;
      const hs = boot(p);
      hs.send({ t: 'noteon', semi: 24, vel: 1 }); // root C4 (MIDI 60)
      hs.render(8); // skip the attack
      const n = 64; // 8192 samples ≈ 171ms
      return (crossings(hs.render(n).L) / (n * 128)) * 48000;
    };
    const root = 440 * Math.pow(2, (60 - 69) / 12); // ≈ 261.6 Hz
    expect(subFreq(-1)).toBeGreaterThan(root / 2 * 0.9);
    expect(subFreq(-1)).toBeLessThan(root / 2 * 1.1);
    expect(subFreq(-2)).toBeGreaterThan(root / 4 * 0.9);
    expect(subFreq(-2)).toBeLessThan(root / 4 * 1.1);
  });

  it('retrigger (non-legato) jumps pitch immediately', () => {
    const p = defaultBassParams();
    p['slide.time'] = 0.4;
    p['osc.unison'] = 1;
    p['osc.tune'] = 0;
    p['sub.level'] = 0;
    p['flt.cut'] = 20000;
    p['flt.env'] = 0;
    p['aenv.sus'] = 1;
    const hs = boot(p);
    hs.send({ t: 'noteon', semi: 0, vel: 1 });
    hs.render(8);
    const low = crossings(hs.render(32).L);
    hs.send({ t: 'noteoff', semi: 0 });
    hs.send({ t: 'noteon', semi: 12, vel: 1 }); // stack emptied → fresh trigger
    hs.render(2);
    const high = crossings(hs.render(32).L);
    expect(high).toBeGreaterThan(low * 1.6);
  });
});

describe('bass sequencer', () => {
  function patsWith(mut: (p: Uint8Array) => Uint8Array): ArrayBuffer {
    return mut(makeEmptyPatterns()).buffer as ArrayBuffer;
  }

  it('plays a programmed step sample-accurately and posts step events', () => {
    const h = boot();
    const data = patsWith((p) => {
      p = setStep(p, 0, 0, { on: true, note: 0, acc: true });
      p = setStep(p, 0, 8, { on: true, note: 7 });
      return p;
    });
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const dur = stepDurSamples(138, 48000);
    const blocks = Math.ceil((dur * 16) / 128) + 2; // one bar
    const { L } = h.render(blocks);
    expect(peak(L)).toBeGreaterThan(0.01);
    const steps = h.sent.filter((m) => m.t === 'step');
    expect(steps.length).toBeGreaterThanOrEqual(16);
    expect(steps[0]).toMatchObject({ s: 0, acc: true });
    // audio actually starts inside the very first block (sample-accurate trigger)
    expect(peak(L.slice(0, 256))).toBeGreaterThan(0);
  });

  it('accented steps are louder than plain ones', () => {
    const play = (acc: boolean): number => {
      const h = boot();
      const data = patsWith((p) => setStep(p, 0, 0, { on: true, note: 0, acc }));
      h.send({ t: 'pats', data });
      h.send({ t: 'chain', list: [0] });
      h.send({ t: 'play' });
      return peak(h.render(30).L);
    };
    expect(play(true)).toBeGreaterThan(play(false) * 1.1);
  });

  it('slide ties steps: no amp retrigger, pitch glides into the slid step', () => {
    const p = defaultBassParams();
    p['aenv.sus'] = 1;
    p['aenv.rel'] = 0.01;
    p['osc.unison'] = 1;
    p['osc.tune'] = 0; // C2 so zero-crossings resolve pitch
    p['sub.level'] = 0;
    p['slide.time'] = 0.03; // glide mostly done ~2τ into the slid step
    p['flt.cut'] = 20000;
    p['flt.env'] = 0;
    p['lfo.depth'] = 0;
    const h = boot(p);
    const data = patsWith((pp) => {
      pp = setStep(pp, 0, 0, { on: true, note: 0 });
      pp = setStep(pp, 0, 1, { on: true, note: 0, oct: 1, slide: true });
      return pp;
    });
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const dur = stepDurSamples(138, 48000);
    const twoSteps = Math.ceil((dur * 2) / 128);
    const { L } = h.render(twoSteps);
    // the region straddling the step boundary stays loud (tied gate, no gap)
    const boundary = Math.floor(dur);
    const straddle = L.slice(boundary - 512, boundary + 512);
    expect(peak(straddle)).toBeGreaterThan(0.02);
    // pitch rises into the slid step: crossings well after the boundary
    // (glide mostly settled, gate still open at <55%) beat mid-step-0's
    const before = crossings(L.slice(512, 2560));
    const after = crossings(L.slice(boundary + 2048, boundary + 4096));
    expect(after).toBeGreaterThan(before * 1.3);
  });

  it('non-slide gates close inside the step (staccato)', () => {
    const p = defaultBassParams();
    p['aenv.sus'] = 1;
    p['aenv.rel'] = 0.005;
    p['master.swing'] = 0;
    const h = boot(p);
    const data = patsWith((pp) => setStep(pp, 0, 0, { on: true, note: 0 }));
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const dur = stepDurSamples(138, 48000);
    const oneStep = Math.ceil(dur / 128);
    const { L } = h.render(oneStep);
    // gate at 55% + 5ms release → the last 10% of the step is silent
    expect(peak(L.slice(Math.floor(dur * 0.9), Math.floor(dur)))).toBeLessThan(1e-3);
    expect(peak(L.slice(0, Math.floor(dur * 0.5)))).toBeGreaterThan(0.02);
  });

  it('stop releases the voice and keyboard is ignored while playing', () => {
    const h = boot();
    const data = patsWith((pp) => setStep(pp, 0, 0, { on: true, note: 0 }));
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    h.send({ t: 'noteon', semi: 24, vel: 1 }); // must be ignored
    h.render(10);
    h.send({ t: 'stop' });
    h.render(400); // drain release + reverb-free tail
    const after = h.render(80);
    expect(peak(after.L.slice(-2560))).toBeLessThan(1e-3);
  });
});
