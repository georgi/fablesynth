import { describe, expect, it } from 'vitest';
import { WARP_MODES, warpNeutral } from '../params';
import { warpSample } from './warp';

const saw = (q: number) => 1 - 2 * (q - Math.floor(q));
const sine = (q: number) => Math.sin(2 * Math.PI * q);
const W = (name: string) => WARP_MODES.indexOf(name);
const PHASES = Array.from({ length: 64 }, (_, i) => (i + 0.5) / 64);

describe('display warp', () => {
  it('leaves the waveform alone at each neutral amount', () => {
    for (const name of ['OFF', 'SYNC', 'BEND', 'ASYM', 'PWM', 'FM', 'AM', 'RM']) {
      const mode = W(name);
      for (const p of PHASES) expect(warpSample(mode, warpNeutral(mode), p, saw, 0.7), `${name} ${p}`).toBeCloseTo(saw(p), 9);
    }
  });

  it('SYNC plays the slave ratio and SYNC WIN closes the cycle edges', () => {
    expect(warpSample(W('SYNC'), 0.25, 0.25, saw)).toBeCloseTo(saw(0.5), 9); // ratio 2
    expect(warpSample(W('SYNC WIN'), 0.5, 0, saw)).toBe(0);
    expect(Math.abs(warpSample(W('SYNC WIN'), 0.5, 0.999, saw))).toBeLessThan(1e-4);
  });

  it('phase maps keep both ends of the cycle', () => {
    for (const name of ['BEND', 'ASYM']) for (const amt of [0, 0.3, 1]) {
      expect(warpSample(W(name), amt, 0, saw)).toBeCloseTo(saw(0), 9);
      expect(warpSample(W(name), amt, 0.999999, saw)).toBeCloseTo(saw(0.999999), 3);
    }
    // BEND keeps the cycle centre; ASYM moves it.
    expect(warpSample(W('BEND'), 0.9, 0.5, saw)).toBeCloseTo(saw(0.5), 9);
    expect(warpSample(W('ASYM'), 0.9, 0.5, saw)).toBeGreaterThan(saw(0.5) + 0.3);
  });

  it('PWM squeezes the cycle and MIRROR plays it back', () => {
    expect(warpSample(W('PWM'), 0.5, 0.275, saw)).toBeCloseTo(saw(0.5), 9); // width 0.55
    expect(warpSample(W('PWM'), 0.5, 0.8, sine)).toBeCloseTo(0, 9);
    expect(warpSample(W('MIRROR'), 0, 0.25, saw)).toBeCloseTo(warpSample(W('MIRROR'), 0, 0.75, saw), 9);
  });

  it('QUANTIZE holds steps and the cross modes follow the modulator', () => {
    expect(warpSample(W('QUANTIZE'), 1, 0.1, saw)).toBe(warpSample(W('QUANTIZE'), 1, 0.4, saw)); // 2 steps
    expect(warpSample(W('FM'), 1, 0.1, saw, 0.05)).toBeCloseTo(saw(0.2), 9); // 2 cycles * 0.05
    expect(warpSample(W('RM'), 1, 0.3, sine, -0.5)).toBeCloseTo(-0.5 * sine(0.3), 9);
    expect(warpSample(W('AM'), 1, 0.3, sine, -1)).toBeCloseTo(0, 9);
  });
});
