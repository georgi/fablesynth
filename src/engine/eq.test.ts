import { describe, expect, it } from 'vitest';
import { defaultParams, type ParamValues } from '../params';
import { EQ_BANDS, eqCoefficients, eqResponseDb, readEqBands, type EqCoefficients } from './eqResponse';
import { bootWt } from './workletHarness';

interface Filter extends EqCoefficients { process(x: number): number; reset(): void }
function filters(params: ParamValues, sampleRate = 48000) {
  const h = bootWt(params, sampleRate);
  h.render(1);
  return (h.proc as unknown as { fx: { eqBands: Filter[][] } }).fx.eqBands;
}

describe('WT-1 four-band EQ', () => {
  it('preserves the original three-band response when loading an old patch', () => {
    const p = { ...defaultParams(), 'fx.eq.on': 1, 'fx.eq.low': -4, 'fx.eq.mid': 3,
      'fx.eq.mfreq': 650, 'fx.eq.high': -6 };
    const actual = filters(p);
    const original = [
      { freq: 120, gain: -4, q: Math.SQRT1_2, type: 0, on: true },
      { freq: 650, gain: 3, q: 0.9, type: 1, on: true },
      { freq: 6000, gain: -6, q: Math.SQRT1_2, type: 2, on: true },
    ].map(b => eqCoefficients(b, 48000));
    for (const freq of [30, 120, 650, 2500, 6000, 16000]) {
      const db = actual.reduce((sum, pair) => sum + eqResponseDb(pair[0], freq, 48000), 0);
      const old = original.reduce((sum, c) => sum + eqResponseDb(c, freq, 48000), 0);
      expect(db).toBeCloseTo(old, 7);
    }
  });

  it.each([44100, 48000, 96000])('matches the displayed response to both audio channels at %i Hz', sampleRate => {
    for (const type of [0, 1, 2]) {
      const p = { ...defaultParams(), 'fx.eq.on': 1 };
      EQ_BANDS.forEach((k, i) => Object.assign(p, {
        [k.freq]: [40, 330, 3200, 19500][i], [k.gain]: [7, -9, 12, -5][i],
        [k.q]: [0.2, 0.9, 4, 12][i], [k.type]: type,
      }));
      const actual = filters(p, sampleRate);
      readEqBands(p).forEach((b, i) => {
        const expected = eqCoefficients(b, sampleRate);
        for (const channel of actual[i]) {
          for (const key of ['b0', 'b1', 'b2', 'a1', 'a2'] as const) expect(channel[key]).toBeCloseTo(expected[key], 12);
          channel.reset();
          for (let n = 0; n < 2048; n++) expect(Number.isFinite(channel.process(n === 0 ? 0.1 : 0))).toBe(true);
        }
      });
    }
  });

  it('the added bell processes audio, with independent bypass and global bypass', () => {
    const p = { ...defaultParams(), 'fx.eq.on': 1, 'fx.eq.mid2': 9, 'fx.eq.m2freq': 2500 };
    const measure = (overrides: ParamValues) => {
      const stage = filters({ ...p, ...overrides })[2][0];
      stage.reset();
      let input = 0, output = 0;
      for (let n = 0; n < 8192; n++) {
        const x = 0.01 * Math.sin(2 * Math.PI * 2500 * n / 48000), y = stage.process(x);
        if (n > 4096) { input += x * x; output += y * y; }
      }
      return 10 * Math.log10(output / input);
    };
    expect(measure({})).toBeCloseTo(9, 3);
    expect(measure({ 'fx.eq.m2on': 0 })).toBeCloseTo(0, 6);
    expect(measure({ 'fx.eq.on': 0 })).toBeCloseTo(0, 6);
  });
});
