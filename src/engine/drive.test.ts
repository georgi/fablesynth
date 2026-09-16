import { describe, it, expect } from 'vitest';
import source from './ott-worklet.js?raw';
import { driveTransfer, driveToneDb } from './drive';

interface Color {
  setParams(type: number, tone: number): void;
  reset(): void;
  shape(x: number, k: number, norm: number): number;
  processTone(x: number): number;
}
const Color = new Function(`${source}\nreturn DriveColor;`)() as new (sr: number) => Color;

describe('drive color and response display', () => {
  it('retains the legacy curve and exact neutral tone by default', () => {
    const color = new Color(48000);
    for (let i = -100; i <= 100; i++) {
      const x = i / 100;
      expect(color.shape(x, 7, 0.5)).toBe(Math.tanh(x * 7) * 0.5);
      expect(color.processTone(x)).toBe(x);
    }
  });
  it.each([0, 1, 2])('displays the actual bounded, odd-symmetric saturation type %i', type => {
    const color = new Color(48000); color.setParams(type, 0); color.reset();
    for (const amount of [0, 0.3, 1]) {
      const pre = 1 + amount * 2, k = 1 + amount * 12, norm = 1 / (pre * Math.tanh(k));
      for (let i = -100; i <= 100; i++) {
        const x = i / 100, actual = color.shape(x * pre, k, norm);
        expect(actual).toBeCloseTo(driveTransfer(x, amount, type), 12);
        expect(actual).toBeCloseTo(-color.shape(-x * pre, k, norm), 12);
        expect(Math.abs(actual)).toBeLessThanOrEqual(norm + 1e-12);
      }
    }
  });
  it('crossfades live type changes instead of stepping', () => {
    const color = new Color(48000), initial = color.shape(0.3, 1, 1);
    color.setParams(2, 0);
    const first = color.shape(0.3, 1, 1);
    expect(Math.abs(first - initial)).toBeLessThan(0.00001);
    let final = first;
    for (let i = 0; i < 48000; i++) final = color.shape(0.3, 1, 1);
    expect(final).toBeCloseTo(0.3, 8);
  });
  it.each([44100, 48000, 96000])('matches measured tone response at %i Hz and preserves silence', sr => {
    for (const tone of [-1, 0, 1]) {
      for (const hz of [100, 1000, 10000]) {
        const color = new Color(sr); color.setParams(0, tone); color.reset();
        expect(color.processTone(0)).toBe(0);
        let input = 0, output = 0;
        for (let i = 0; i < sr; i++) {
          const x = Math.sin(2 * Math.PI * hz * i / sr), y = color.processTone(x);
          if (i >= sr / 2) { input += x * x; output += y * y; }
        }
        expect(10 * Math.log10(output / input)).toBeCloseTo(driveToneDb(hz, tone, sr), 6);
      }
    }
  });
});
