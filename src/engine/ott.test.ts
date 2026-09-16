import { describe, expect, it } from 'vitest';
import source from './ott-worklet.js?raw';
import { DRUM_PARAMS } from '../drum/params';

interface Ott {
  setParams(on: boolean, depth: number, time: number, up: number, down: number): void;
  process(l: Float32Array, r: Float32Array, n: number): void;
  reset(): void;
}
const Ott = new Function(`${source}\nreturn OttCompressor;`)() as new (sr: number) => Ott;
const rms = (x: Float32Array) => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / x.length);
function tone(ott: Ott, amp: number, hz = 600, sr = 48000) {
  const l = Float32Array.from({ length: sr }, (_, i) => amp * Math.sin(2 * Math.PI * hz * i / sr));
  const r = l.slice();
  ott.process(l, r, l.length);
  return { l, r, rms: rms(l.slice(sr / 2)) };
}

describe('OTT dynamics component', () => {
  it('bypasses bit-exactly, including zero depth', () => {
    const dry = tone(new Ott(48000), 0.1).l;
    const ott = new Ott(48000);
    ott.setParams(true, 0, 1, 1, 1);
    expect(tone(ott, 0.1).l).toEqual(dry);
  });
  it.each([70, 600, 8000])('compensates both quiet boosts and loud cuts at %i Hz', (hz) => {
    const up = new Ott(48000); up.setParams(true, 1, 1, 1, 0);
    const down = new Ott(48000); down.setParams(true, 1, 1, 0, 1);
    tone(up, 0.002, hz); tone(down, 0.8, hz);
    const quiet = tone(up, 0.002, hz), loud = tone(down, 0.8, hz);
    expect(Math.abs(20 * Math.log10(quiet.rms / (0.002 / Math.SQRT2)))).toBeLessThan(0.5);
    expect(Math.abs(20 * Math.log10(loud.rms / (0.8 / Math.SQRT2)))).toBeLessThan(0.5);
    expect(quiet.l).toEqual(quiet.r);
  });
  it.each([44100, 48000, 96000])('preserves silence and stays finite at %i Hz', (sr) => {
    const ott = new Ott(sr); ott.setParams(true, 1, 0.1, 1, 1);
    expect(tone(ott, 0, 600, sr).rms).toBe(0);
    const signal = tone(ott, 0.8, 8000, sr);
    expect(signal.l.every(Number.isFinite)).toBe(true);
    ott.setParams(false, 1, 4, 1, 1);
    tone(ott, 0, 600, sr);
    expect(tone(ott, 0.1, 600, sr).l).toEqual(tone(new Ott(sr), 0.1, 600, sr).l);
  });
  it('reconstructs unity when both dynamics amounts are zero', () => {
    const ott = new Ott(48000); ott.setParams(true, 1, 1, 0, 0);
    const wet = tone(ott, 0.2).l, dry = tone(new Ott(48000), 0.2).l;
    expect(wet.reduce((m, v, i) => Math.max(m, Math.abs(v - dry[i])), 0)).toBeLessThan(1e-8);
  });
  it('retains stronger multiband tone shaping at 200% after matching level', () => {
    const sr = 48000;
    const input = Float32Array.from({ length: sr * 3 }, (_, i) =>
      0.1 * Math.sin(2 * Math.PI * 70 * i / sr) + 0.002 * Math.sin(2 * Math.PI * 8000 * i / sr));
    const render = (up: number) => {
      const ott = new Ott(sr); ott.setParams(true, 1, 1, up, 0);
      const l = input.slice(); ott.process(l, l.slice(), l.length);
      return l.slice(sr * 2);
    };
    const highAmplitude = (x: Float32Array) => Math.abs(x.reduce((sum, v, i) =>
      sum + v * Math.sin(2 * Math.PI * 8000 * i / sr), 0) * 2 / x.length);
    const normal = render(1), extreme = render(DRUM_PARAMS['pad0.fx.ott.up'].max!);
    expect(highAmplitude(extreme)).toBeGreaterThan(highAmplitude(normal) * 1.5);
    expect(Math.abs(20 * Math.log10(rms(extreme) / rms(input.slice(sr * 2))))).toBeLessThan(0.5);
  });
  it.each([44100, 48000, 96000])('handles extreme settings, automation, and silence at %i Hz', (sr) => {
    const ott = new Ott(sr);
    const time = DRUM_PARAMS['pad0.fx.ott.time'];
    for (const speed of [time.min!, time.max!]) {
      ott.setParams(true, 1, speed, 2, 2);
      expect(tone(ott, 0, 600, sr).rms).toBeLessThan(1e-8);
      const signal = tone(ott, 0.8, 8000, sr);
      expect(signal.l.every(Number.isFinite)).toBe(true);
      expect(signal.rms).toBeGreaterThan(0);
      ott.reset();
    }
    ott.setParams(false, 1, time.min!, 2, 2);
    tone(ott, 0, 600, sr);
    expect(tone(ott, 0.1, 600, sr).l).toEqual(tone(new Ott(sr), 0.1, 600, sr).l);
  });
});

describe('shared automatic gain', () => {
  const AutoGain = new Function(`${source}\nreturn AutoGain;`)() as new (sr: number) => {
    next(l: number, r: number, wl: number, wr: number): number;
    reset(): void;
  };
  it.each([0.1, 10])('matches stereo energy for a %sx wet level without canceling the initial transient', (level) => {
    const gain = new AutoGain(48000);
    expect(gain.next(0.1, 0.05, 0.1 * level, 0.05 * level)).toBeCloseTo(1, 2);
    let compensation = 1;
    for (let i = 0; i < 48000 * 3; i++) compensation = gain.next(0.1, 0.05, 0.1 * level, 0.05 * level);
    expect(compensation * level).toBeCloseTo(1, 3);
    for (let i = 0; i < 48000; i++) expect(Number.isFinite(gain.next(0, 0, 0, 0))).toBe(true);
    gain.reset();
    expect(gain.next(0, 0, 0, 0)).toBe(1);
  });
});

describe('shared compressor', () => {
  interface Comp {
    l: number; r: number; gain: number;
    setParams(on: boolean, threshold: number, attack?: number, release?: number, ratio?: number): void;
    process(l: Float32Array, r: Float32Array, n: number): void;
    processSample(l: number, r: number): void;
  }
  const Comp = new Function(`${source}\nreturn Compressor;`)() as new (sr: number) => Comp;
  it.each([44100, 48000, 96000])('controls transient attack, recovery and ratio at %i Hz', sr => {
    const run = (comp: Comp, seconds: number, level: number) => {
      for (let i = 0; i < sr * seconds; i++) comp.processSample(level, level);
      return comp.gain;
    };
    const fast = new Comp(sr), slow = new Comp(sr);
    fast.setParams(true, -24, 0.0001, 0.25, 8);
    slow.setParams(true, -24, 0.1, 0.25, 8);
    run(fast, 0.3, 0); run(slow, 0.3, 0);
    expect(run(fast, 0.01, 0.5)).toBeLessThan(run(slow, 0.01, 0.5) * 0.5);
    const quick = new Comp(sr), long = new Comp(sr);
    quick.setParams(true, -24, 0.003, 0.01, 4);
    long.setParams(true, -24, 0.003, 2, 4);
    run(quick, 0.4, 0.5); run(long, 0.4, 0.5);
    expect(run(quick, 0.1, 0)).toBeGreaterThan(run(long, 0.1, 0) * 2);
    const gains = [1, 2, 4, 20].map(ratio => {
      const comp = new Comp(sr); comp.setParams(true, -24, 0.003, 0.25, ratio);
      return run(comp, 1, 0.5);
    });
    expect(gains[0]).toBeCloseTo(1, 6);
    expect(gains[1]).toBeGreaterThan(gains[2]);
    expect(gains[2]).toBeGreaterThan(gains[3]);
  });
  it('uses the same DSP through block and sample entry points', () => {
    const block = new Comp(48000), sample = new Comp(48000);
    block.setParams(true, -28); sample.setParams(true, -28);
    const input = Float32Array.from({ length: 48000 }, (_, i) => Math.sin(i * 0.17) * (i < 24000 ? 0.02 : 0.4));
    const l = input.slice(), r = Float32Array.from(input, (v) => v * 0.3);
    block.process(l, r, l.length);
    for (let i = 0; i < input.length; i++) {
      sample.processSample(input[i], Math.fround(input[i] * 0.3));
      expect(l[i]).toBe(Math.fround(sample.l));
      expect(r[i]).toBe(Math.fround(sample.r));
    }
  });
  it.each([44100, 48000, 96000])('matches steady input energy and bypasses exactly at %i Hz', (sr) => {
    const comp = new Comp(sr);
    const input = Float32Array.from({ length: sr * 3 }, (_, i) => Math.sin(i * 2 * Math.PI * 600 / sr) * 0.2);
    let l = input.slice(), r = input.slice();
    comp.process(l, r, l.length);
    expect(l).toEqual(input);
    comp.setParams(true, -32);
    comp.process(l, r, l.length);
    expect(Math.abs(20 * Math.log10(rms(l.slice(sr * 2)) / rms(input.slice(sr * 2))))).toBeLessThan(0.5);
    comp.setParams(false, -32);
    comp.process(new Float32Array(sr), new Float32Array(sr), sr);
    l = input.slice(); r = input.slice(); comp.process(l, r, l.length);
    expect(l).toEqual(input);
  });
});
