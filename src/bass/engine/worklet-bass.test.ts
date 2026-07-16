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

  it('one-step notes remain gated through their step', () => {
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
    expect(peak(L.slice(Math.floor(dur * 0.9), Math.floor(dur)))).toBeGreaterThan(0.02);
    expect(peak(L.slice(0, Math.floor(dur * 0.5)))).toBeGreaterThan(0.02);
  });

  it('slides into the next step while retaining adjustable duration', () => {
    const h = boot();
    const data = patsWith((p) => {
      p = setStep(p, 0, 0, { on: true, note: 0, duration: 1 });
      return setStep(p, 0, 1, { on: true, note: 7, slide: true, duration: 4 });
    });
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    h.render(Math.ceil(stepDurSamples(138, 48000) * 2 / 128) + 2);
    const steps = h.sent.filter((m) => m.t === 'step');
    expect(steps[1]).toMatchObject({ s: 1, slide: true, semi: 7 });
    const proc = h.proc as unknown as { gate: boolean; semiTarget: number; samplesToGateOff: number };
    expect(proc.gate).toBe(true);
    expect(proc.semiTarget).toBe(7);
    expect(proc.samplesToGateOff).toBeGreaterThan(stepDurSamples(138, 48000) * 2);
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

// The harness evaluates the worklet source with bare-global `currentFrame`
// resolving via globalThis, like the real AudioWorkletGlobalScope.
const g = globalThis as unknown as { currentFrame: number };

function runBlocks(h: BassHarness, blocks: number): void {
  for (let b = 0; b < blocks; b++) {
    h.proc.process([], [[new Float32Array(128), new Float32Array(128)]]);
    g.currentFrame += 128;
  }
}

type ClipState = {
  clip: { data: Uint8Array; bars: number } | null;
  clipPend: { data: Uint8Array; bars: number; at: number } | null;
  clipStep: number;
};

describe('clipupdate (hosted hot-swap)', () => {
  it('swaps a live clip in place and preserves the play position', () => {
    g.currentFrame = 0;
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(48), bars: 1, atFrame: 0 });
    // step = 60/120/4*48000 = 6000 frames; 50 blocks = 6400 frames → step 1 fired
    runBlocks(h, 50);
    const p = h.proc as unknown as ClipState;
    expect(p.clip).not.toBeNull();
    const before = p.clipStep;
    expect(before).toBeGreaterThanOrEqual(1);

    const next = new Uint8Array(2 * 48);
    next[0] = 1; // bar 0, step 0, flags byte
    h.send({ t: 'clipupdate', data: next, bars: 2 });
    expect(p.clip!.bars).toBe(2);
    expect(p.clip!.data[0]).toBe(1);
    expect(p.clipStep).toBe(before); // phase untouched

    runBlocks(h, 50); // keeps ticking: pos messages continue past the swap
    const poses = h.sent.filter((m) => m.t === 'pos');
    expect(poses.length).toBeGreaterThanOrEqual(3);
  });

  it('re-wraps clipStep when the clip shrinks below the current position', () => {
    g.currentFrame = 0;
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(2 * 48), bars: 2, atFrame: 0 });
    runBlocks(h, 900); // ~19 steps in → clipStep in bar 1
    const p = h.proc as unknown as ClipState;
    expect(p.clipStep).toBeGreaterThanOrEqual(16);
    h.send({ t: 'clipupdate', data: new Uint8Array(48), bars: 1 });
    expect(p.clipStep).toBeLessThan(16);
  });

  it('updates a pending clip without touching its launch frame', () => {
    g.currentFrame = 0;
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'clip', data: new Uint8Array(48), bars: 1, atFrame: 96000 });
    const p = h.proc as unknown as ClipState;
    const next = new Uint8Array(48);
    next[0] = 1; // step 0, flags byte
    h.send({ t: 'clipupdate', data: next, bars: 1 });
    expect(p.clipPend!.at).toBe(96000);
    expect(p.clipPend!.data[0]).toBe(1);
    expect(p.clip).toBeNull();
  });

  it('is a no-op when nothing is live or pending', () => {
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'clipupdate', data: new Uint8Array(48), bars: 1 });
    const p = h.proc as unknown as ClipState;
    expect(p.clip).toBeNull();
    expect(p.clipPend).toBeNull();
  });
});

// Clips are phase-locked to the shared timebase: activation derives the entry
// step from the tempo anchor, so a (re)launch can never desync devices.
describe('clip phase lock', () => {
  it('a clip launched at song bar 1 enters at its own bar 1, not step 0', () => {
    g.currentFrame = 0;
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    // 120 BPM / 48k: step = 6000 frames, bar = 96000. Launch a 2-bar clip
    // exactly at the bar-1 boundary.
    h.send({ t: 'clip', data: new Uint8Array(2 * 48), bars: 2, atFrame: 96000 });
    runBlocks(h, 751); // past frame 96000 → activated + first fire
    const poses = h.sent.filter((m) => m.t === 'pos');
    expect(poses[0]).toMatchObject({ step: 0, bar: 1 });
  });

  it('the step clock does not drift off the anchor grid over long runs', () => {
    // A free-running countdown drops the block-quantization residue every
    // fire (16 frames/step at 120 BPM) and slips late without bound — then
    // any anchor-derived phase math disagrees with what is audible.
    g.currentFrame = 0;
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(48), bars: 1, atFrame: 0 });
    // 2394 blocks → frame 306432, mid-interval of global step 51. A clock
    // slipping 16 frames/step would still be on step 50.
    runBlocks(h, 2394);
    const p = h.proc as unknown as ClipState;
    expect(p.clipStep).toBe(51 % 16);
  });

  it('a same-length pattern edit (sequencer click) never moves the phase', () => {
    // Swing delays odd steps past their unswung grid time; an edit inside
    // that window must not floor the anchor phase onto the not-yet-fired
    // step (that skips it and shifts the device against the others).
    g.currentFrame = 0;
    const h = makeBassProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 1, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(48), bars: 1, atFrame: 0 });
    runBlocks(h, 63); // frame 8064 — step 1's grid time passed, swung fire pending
    const p = h.proc as unknown as ClipState;
    expect(p.clipStep).toBe(0);
    const next = new Uint8Array(48);
    next[0] = 1;
    h.send({ t: 'clipupdate', data: next, bars: 1 });
    expect(p.clip!.data[0]).toBe(1);
    expect(p.clipStep).toBe(0); // pure data swap — the transport is untouched
  });
});
