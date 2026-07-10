import { describe, it, expect } from 'vitest';
import { generateDrumTables } from './drumtables';
import { FRAMES, MIPS, SIZE } from '../../engine/wavetables';

describe('drum tables', () => {
  const tables = generateDrumTables();

  it('produces THUD/CRACK/TINE/GRIT with full mip data', () => {
    expect(tables.map((t) => t.name)).toEqual(['THUD', 'CRACK', 'TINE', 'GRIT']);
    for (const t of tables) {
      expect(t.frames).toBe(FRAMES);
      expect(t.mips).toBe(MIPS);
      expect(t.size).toBe(SIZE);
      expect(t.data.length).toBe(FRAMES * MIPS * SIZE);
      expect(t.viz.length).toBeGreaterThan(0);
    }
  });

  it('every sample is finite and within normalization headroom', () => {
    for (const t of tables) {
      let peak = 0;
      for (let i = 0; i < t.data.length; i++) {
        peak = Math.max(peak, Math.abs(t.data[i]));
      }
      expect(t.data.every((sample) => Number.isFinite(sample))).toBe(true);

      let mip0Peak = 0;
      for (let f = 0; f < FRAMES; f++) {
        const offset = (f * MIPS + 0) * SIZE;
        for (let i = offset; i < offset + SIZE; i++) {
          mip0Peak = Math.max(mip0Peak, Math.abs(t.data[i]));
        }
      }

      expect(peak).toBeGreaterThan(0.5); // audible
      expect(mip0Peak).toBeLessThanOrEqual(0.921); // mip-0 is normalized to 0.92, plus float rounding
      // Band-limiting to fewer harmonics can legitimately raise the time-domain
      // peak above the mip-0 normalization (Gibbs-style overshoot).
      expect(peak).toBeLessThan(1.5);
    }
  });
});
