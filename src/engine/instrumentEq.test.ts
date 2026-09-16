import { describe, expect, it } from 'vitest';
import shared from './ott-worklet.js?raw';
import bass from '../bass/engine/worklet-bass.js?raw';
import drum from '../drum/engine/worklet-drum.js?raw';
import { defaultBassParams } from '../bass/params';
import { defaultDrumParams, DRUM_PARAM_DEFS } from '../drum/params';
import { EQ_BANDS, eqCoefficients, eqResponseDb, readEqBands } from './eqResponse';

interface Fx { setParams(p: unknown, fields?: number[]): void; process(l: Float32Array, r: Float32Array, n: number, live?: boolean): void }
function makeFx(machine: 'bass' | 'drum', sr: number) {
  return new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor',
    `${shared}\n${machine === 'bass' ? bass : drum}\nreturn new ${machine === 'bass' ? 'BassFx' : 'PadFx'}(sampleRate);`
  )(sr, class {}, () => {}) as Fx;
}

describe.each(['bass', 'drum'] as const)('%s four-band EQ', machine => {
  it.each([44100, 48000, 96000])('matches the displayed response at %i Hz, with exact bypass and independent channels', sr => {
    const measure = (on: boolean, gain: number, shape = 1, selected = 1, frequency = 1000) => {
      const fx = makeFx(machine, sr);
      const p = machine === 'bass' ? defaultBassParams() : defaultDrumParams();
      const prefix = machine === 'bass' ? '' : 'pad7.';
      for (const id of Object.keys(p)) if (id.includes('fx.') && id.endsWith('.on')) p[id] = 0;
      const keys = EQ_BANDS[selected];
      p[prefix + 'fx.eq.on'] = +on;
      p[prefix + keys.gain] = gain;
      p[prefix + keys.freq] = frequency;
      p[prefix + keys.type] = shape;
      p[prefix + keys.q] = 1.7;
      if (machine === 'bass') fx.setParams(p);
      else {
        const values = DRUM_PARAM_DEFS.map(d => p[d.id]);
        const fields = DRUM_PARAM_DEFS.flatMap((d, i) => d.id.startsWith(prefix) ? [i] : []);
        fx.setParams(values, fields);
      }
      let energy = 0, finite = true, rightPeak = 0;
      for (let offset = 0; offset < sr; offset += 127) {
        const n = Math.min(127, sr - offset), l = new Float32Array(n), r = new Float32Array(n);
        for (let i = 0; i < n; i++) l[i] = .01 * Math.sin(2 * Math.PI * frequency * (offset + i) / sr);
        fx.process(l, r, n, true);
        for (let i = 0; i < n; i++) {
          finite &&= Number.isFinite(l[i]) && Number.isFinite(r[i]);
          rightPeak = Math.max(rightPeak, Math.abs(r[i]));
          if (offset + i >= sr / 2) energy += l[i] * l[i];
        }
      }
      expect(finite).toBe(true);
      expect(rightPeak).toBeLessThan(1e-8);
      const normalized = Object.fromEntries(Object.entries(p).map(([k, v]) => [k.startsWith(prefix) ? k.slice(prefix.length) : k, v]));
      const response = readEqBands(normalized).reduce((db, b) => db + eqResponseDb(eqCoefficients(b, sr), frequency, sr), 0);
      return { energy, response };
    };
    const bypass = measure(false, 0).energy;
    expect(measure(false, 6).energy).toBe(bypass);
    expect(10 * Math.log10(measure(true, 0).energy / bypass)).toBeCloseTo(0, 2);
    for (let band = 0; band < 4; band++) for (const shape of [0, 1, 2]) for (const gain of [-6, 6]) {
      const result = measure(true, gain, shape, band);
      expect(10 * Math.log10(result.energy / bypass)).toBeCloseTo(result.response, 1);
    }
    const highBypass = measure(false, 0, 1, 3, 20000).energy;
    const high = measure(true, 6, 1, 3, 20000);
    expect(10 * Math.log10(high.energy / highBypass)).toBeCloseTo(high.response, 1);
  }, 30000);
});
