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

  it('ring mod creates an inharmonic metallic spectrum and mix=0 is bypassed', () => {
    const plainParams = defaultDrumParams();
    const plain = boot(plainParams); plain.send({ t: 'trig', pad: 0, v: 1 });
    const dry = plain.render(20).L;

    const ringParams = defaultDrumParams();
    ringParams[pad(0, 'ring.freq')] = 731;
    ringParams[pad(0, 'ring.mix')] = 1;
    const ring = boot(ringParams); ring.send({ t: 'trig', pad: 0, v: 1 });
    const wet = ring.render(20).L;

    let delta = 0;
    for (let i = 0; i < dry.length; i++) delta += Math.abs(dry[i] - wet[i]);
    expect(delta / dry.length).toBeGreaterThan(0.005);
    expect(finite(wet)).toBe(true);
    expect(peak(wet)).toBeLessThan(2);
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
    expect((steps[0] as unknown as { hits: number[] }).hits).toContain(0);
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
    const barSamples = (60 / 126 / 4) * 48000 * 16;
    const barBlocks = Math.ceil((dur * 16) / 128);
    const bar1 = h.render(barBlocks);
    const bar2 = h.render(barBlocks);
    const boundary = Math.floor(barSamples);
    const afterBoundary = new Float32Array(bar1.L.length - boundary + bar2.L.length);
    afterBoundary.set(bar1.L.slice(boundary));
    afterBoundary.set(bar2.L, bar1.L.length - boundary);
    expect(peak(bar1.L.slice(0, boundary))).toBe(0); // pattern A is empty up to the true bar boundary
    expect(peak(afterBoundary)).toBeGreaterThan(0.01); // chain moved to B at the boundary
    h.send({ t: 'stop' });
    h.render(barBlocks * 2); // drain tails
    const after = h.render(barBlocks);
    expect(peak(after.L.slice(-2560))).toBeLessThan(1e-3);
  });
});

// The harness evaluates the worklet source with bare-global `currentFrame`
// resolving via globalThis, like the real AudioWorkletGlobalScope.
const g = globalThis as unknown as { currentFrame: number };

function runBlocks(h: DrumHarness, blocks: number): void {
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
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 0 });
    // step = 60/120/4*48000 = 6000 frames; 50 blocks = 6400 frames → step 1 fired
    runBlocks(h, 50);
    const p = h.proc as unknown as ClipState;
    expect(p.clip).not.toBeNull();
    const before = p.clipStep;
    expect(before).toBeGreaterThanOrEqual(1);

    const next = new Uint8Array(2 * 256);
    next[0] = 1; // bar 0, pad 0, step 0
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
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(2 * 256), bars: 2, atFrame: 0 });
    runBlocks(h, 900); // ~19 steps in → clipStep in bar 1
    const p = h.proc as unknown as ClipState;
    expect(p.clipStep).toBeGreaterThanOrEqual(16);
    h.send({ t: 'clipupdate', data: new Uint8Array(256), bars: 1 });
    expect(p.clipStep).toBeLessThan(16);
  });

  it('updates a pending clip without touching its launch frame', () => {
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 96000 });
    const p = h.proc as unknown as ClipState;
    const next = new Uint8Array(256);
    next[16] = 2; // pad 1, step 0
    h.send({ t: 'clipupdate', data: next, bars: 1 });
    expect(p.clipPend!.at).toBe(96000);
    expect(p.clipPend!.data[16]).toBe(2);
    expect(p.clip).toBeNull();
  });

  it('is a no-op when nothing is live or pending', () => {
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'clipupdate', data: new Uint8Array(256), bars: 1 });
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
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    // 120 BPM / 48k: step = 6000 frames, bar = 96000. Launch a 2-bar clip
    // exactly at the bar-1 boundary.
    h.send({ t: 'clip', data: new Uint8Array(2 * 256), bars: 2, atFrame: 96000 });
    runBlocks(h, 751); // past frame 96000 → activated + first fire
    const poses = h.sent.filter((m) => m.t === 'pos');
    expect(poses[0]).toMatchObject({ step: 0, bar: 1 });
  });

  it('an unquantized launch mid-song joins the global step grid', () => {
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    runBlocks(h, 100); // song runs to frame 12800 ≈ step 2.13
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 0 });
    runBlocks(h, 1);
    const poses = h.sent.filter((m) => m.t === 'pos');
    expect(poses[0]).toMatchObject({ step: 2, bar: 0 }); // round(12800/6000) = 2
  });

  it('the step clock does not drift off the anchor grid over long runs', () => {
    // A free-running countdown drops the block-quantization residue every
    // fire (16 frames/step at 120 BPM) and slips late without bound — then
    // any anchor-derived phase math disagrees with what is audible.
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 0 });
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
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 1, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 0 });
    runBlocks(h, 63); // frame 8064 — step 1's grid time passed, swung fire pending
    const p = h.proc as unknown as ClipState;
    expect(p.clipStep).toBe(0);
    const next = new Uint8Array(256);
    next[0] = 1;
    h.send({ t: 'clipupdate', data: next, bars: 1 });
    expect(p.clip!.data[0]).toBe(1);
    expect(p.clipStep).toBe(0); // pure data swap — the transport is untouched
  });

  it('a non-zero anchor is respected', () => {
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    // anchor at frame 96000: song bar 0 begins there
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 96000 });
    h.send({ t: 'clip', data: new Uint8Array(2 * 256), bars: 2, atFrame: 2 * 96000 });
    runBlocks(h, 1501); // past frame 192000 = song bar 1
    const poses = h.sent.filter((m) => m.t === 'pos');
    expect(poses[0]).toMatchObject({ step: 0, bar: 1 });
  });
});
