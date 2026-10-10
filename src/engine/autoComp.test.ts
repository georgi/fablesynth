import { describe, expect, it } from 'vitest';
import source from './ott-worklet.js?raw';
import { BLOCK_RATE, COMP_STYLES, compGainDb, compStyleById, estimateBpm, fitComp } from './autoComp';

new Function(source)();

// A four-on-the-floor envelope at 30 blocks/s: a hit, then a decay to the bed.
function groove(bpm: number, seconds = 12) {
  const period = 30 * 60 / bpm;
  return Array.from({ length: seconds * 30 }, (_, i) => {
    const phase = (i % period) / period;
    return -24 + 14 * Math.exp(-phase * 6) + Math.sin(i * 1.7) * 0.5;
  });
}

describe('auto-comp', () => {
  it('matches the worklet soft-knee curve', () => {
    expect(compGainDb(-20, -16, 4)).toBe(0);
    expect(compGainDb(-16 + 20, -16, 4)).toBeCloseTo((1 / 4 - 1) * (20 - 4.5));
  });

  it('finds the tempo of a steady groove', () => {
    for (const bpm of [100, 122, 140]) expect(Math.abs(estimateBpm(groove(bpm))! - bpm)).toBeLessThanOrEqual(3);
  });

  it('keeps the beat period when an off-beat hit is louder than the bar pattern', () => {
    const period = 30 * 60 / 122;
    const levels = groove(122).map((v, i) => v + ((i + period / 2) % period < 1 ? 6 : 0) + (i % (period * 4) < 1 ? 4 : 0));
    expect(Math.abs(estimateBpm(levels)! - 122)).toBeLessThanOrEqual(3);
  });

  it('hits each style target on the loud section and follows the tempo', () => {
    for (const style of COMP_STYLES) {
      const fit = fitComp(groove(124), style, 1)!;
      expect(fit.loudGr, style.id).toBeCloseTo(style.targetGr, 0);
      expect(fit.release).toBeCloseTo(Math.min(2, Math.max(0.01, style.releaseBeats * 60 / fit.bpm!)), 2);
    }
  });

  it('places the peak catcher above the glue threshold and scales with depth', () => {
    const levels = groove(124);
    expect(fitComp(levels, compStyleById('peaks'), 1)!.threshold).toBeGreaterThan(fitComp(levels, compStyleById('level'), 1)!.threshold);
    expect(fitComp(levels, compStyleById('glue'), 0.5)!.loudGr).toBeLessThan(fitComp(levels, compStyleById('glue'), 1.5)!.loudGr);
  });

  it('needs at least a second of sound', () => {
    expect(fitComp(Array(20).fill(-12), COMP_STYLES[0], 1)).toBeNull();
  });

  // Test parts with different crest and register: the fit must predict what
  // the real worklet compressor does with the fitted settings.
  const SR = 48000;
  const kick = (t: number) => { const p = t % 0.5; return 0.6 * Math.exp(-p * 18) * Math.sin(2 * Math.PI * (50 + 120 * Math.exp(-p * 30)) * p); };
  const hats = (t: number) => { const p = t % 0.125; return 0.2 * Math.exp(-p * 60) * Math.sin(2 * Math.PI * 7000 * t + 3 * Math.sin(2 * Math.PI * 5300 * t)); };
  const pad = (t: number) => 0.15 * (1 + 0.5 * Math.sin(Math.PI * 0.5 * t)) * (Math.sin(2 * Math.PI * 220 * t) + 0.5 * Math.sin(2 * Math.PI * 331 * t));
  const PARTS = { kick, hats, mix: (t: number) => kick(t) + hats(t) + 0.5 * pad(t) };

  it.each(Object.entries(PARTS))('predicts the real compressor on %s', (_, f) => {
    const n = SR / BLOCK_RATE, x = Float32Array.from({ length: SR * 6 }, (_, i) => f(i / SR));
    const blocks: number[] = [], traces: Float32Array[] = [];
    for (let b = 0; b + n <= x.length; b += n) {
      const seg = x.subarray(b, b + n);
      blocks.push(10 * Math.log10(Math.max(1e-12, seg.reduce((a, v) => a + v * v, 0) / n)));
      traces.push(seg.map(Math.abs));
    }
    for (const style of COMP_STYLES) {
      const fit = fitComp(blocks, style, 1, { bpm: 120, traces, sampleRate: SR })!;
      expect(fit.exact).toBe(true);
      const Comp = (globalThis as unknown as { FableCompressor: new (sr: number) => { setParams(...a: unknown[]): void; processSample(l: number, r: number): void; gain: number; threshold: number; ratio: number; wet: number } }).FableCompressor;
      const c = new Comp(SR);
      c.setParams(true, fit.threshold, fit.attack, fit.release, fit.ratio);
      Object.assign(c, { threshold: fit.threshold, ratio: fit.ratio, wet: 1 });
      const gr = blocks.map((_, b) => {
        let s = 0;
        for (let i = b * n; i < (b + 1) * n; i++) { c.processSample(x[i], x[i]); s += -20 * Math.log10(c.gain); }
        return s / n;
      });
      const audible = blocks.map((v, i) => [v, i]).filter(([v]) => v > -60).sort((a, b) => a[0] - b[0]);
      const cut = audible[Math.round((audible.length - 1) * style.loudPct)][0];
      const loud = audible.filter(([v]) => v >= cut).map(([, i]) => gr[i]);
      const actual = loud.reduce((a, v) => a + v, 0) / loud.length;
      expect(Math.abs(actual - fit.loudGr), `${style.id}: predicted ${fit.loudGr.toFixed(2)}, actual ${actual.toFixed(2)}`).toBeLessThan(0.3);
      // The threshold rounds to 0.5 dB, so the result lands near the style target.
      expect(Math.abs(actual - style.targetGr), style.id).toBeLessThan(style.targetGr * 0.25);
    }
  });
});
