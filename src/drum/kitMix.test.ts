import { describe, expect, it } from 'vitest';
import { BAND_HZ, referenceById } from '../engine/autoEq';
import { KitCapture, fitKitMix } from './kitMix';

const drums = referenceById('drums');
// Band-power shapes for a kick, a snare and a hat, in dB.
const shape = {
  kick: (f: number) => (f < 90 ? 0 : -20 * Math.log2(f / 90)),
  snare: (f: number) => -8 * Math.abs(Math.log2(f / 900)) - 6,
  hat: (f: number) => (f > 6000 ? -10 : -10 - 24 * Math.log2(6000 / f)),
  perc: (f: number) => -10 * Math.abs(Math.log2(f / 2500)) - 10,
};
const padsOf = (offsets: number[]) => Object.values(shape).map((s, i) => Float64Array.from(BAND_HZ, f => 10 ** ((s(f) + offsets[i]) / 10)));
const LVL = [0.7, 0.7, 0.7, 0.7];
const apply = (lvl: number[], levels: (number | null)[]) => lvl.map((v, i) => levels[i] ?? v);
const offsetsAfter = (offsets: number[], from: number[], to: number[]) => offsets.map((o, i) => o + 40 * Math.log10(to[i] / from[i]));

describe('DR-1 kit auto-mix', () => {
  it('lowers an overpowering kick and lowers the kit error', () => {
    const base = fitKitMix(padsOf([0, 0, 0, 0]), LVL, drums, 1)!;
    const loud = fitKitMix(padsOf([8, 0, 0, 0]), LVL, drums, 1)!;
    // The kit level is kept, so compare the kick's move with the other pads'.
    const rel = (m: number[]) => m[0] - (m[1] + m[2] + m[3]) / 3;
    expect(rel(loud.moves) - rel(base.moves)).toBeLessThan(-3);
    expect(loud.errorAfter).toBeLessThan(loud.errorBefore);
  });

  it('converges: a second fit after applying the first moves less', () => {
    const offsets = [6, -4, 3, -6];
    const first = fitKitMix(padsOf(offsets), LVL, drums, 1)!;
    const lvl2 = apply(LVL, first.levels);
    const second = fitKitMix(padsOf(offsetsAfter(offsets, LVL, lvl2)), lvl2, drums, 1)!;
    const spread = (m: number[]) => Math.max(...m) - Math.min(...m);
    expect(spread(second.moves)).toBeLessThan(spread(first.moves) * 0.5);
    expect(second.errorBefore).toBeLessThan(first.errorBefore);
  });

  it('keeps the kit level and stays inside the level range', () => {
    const fit = fitKitMix(padsOf([0, -10, 0, 0]), [0.7, 0.98, 0.7, 0.7], drums, 1)!;
    expect(fit.levels.every(v => v === null || (v >= 0 && v <= 1))).toBe(true);
    // The quiet snare cannot rise past 1, so the other pads come down instead.
    expect(Math.max(...fit.moves)).toBeLessThanOrEqual(40 * Math.log10(1 / 0.98) + 0.01);
  });

  it('scales with amount and ignores silent pads', () => {
    const pads = padsOf([8, 0, 0, 0]);
    pads.push(new Float64Array(BAND_HZ.length));
    const full = fitKitMix(pads, [...LVL, 0.7], drums, 1)!, half = fitKitMix(pads, [...LVL, 0.7], drums, 0.5)!;
    const spread = (m: number[]) => Math.max(...m) - Math.min(...m);
    expect(spread(half.moves)).toBeLessThan(spread(full.moves));
    expect(full.heard[4]).toBe(false);
    expect(full.levels[4]).toBeNull();
  });

  it('measures band power from captured blocks', () => {
    const sr = 48000, cap = new KitCapture(sr, 2);
    const tone = (f: number) => Float32Array.from({ length: 4096 }, (_, i) => Math.sin(2 * Math.PI * f * i / sr));
    cap.add(0, tone(100), 4096); cap.add(1, tone(4000), 4096);
    const [low, high] = cap.meanBands();
    const peak = (b: Float64Array) => BAND_HZ[b.indexOf(Math.max(...b))];
    expect(peak(low)).toBe(100);
    expect(peak(high)).toBe(4000);
    // A unit sine has mean power 0.5.
    expect(low.reduce((s, v) => s + v, 0)).toBeCloseTo(0.5, 1);
  });
});
