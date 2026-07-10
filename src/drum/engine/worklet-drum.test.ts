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
