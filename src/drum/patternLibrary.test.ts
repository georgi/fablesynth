import { describe, expect, it } from 'vitest';
import { PATTERN_FAMILIES, PATTERN_GENRE_COUNT, PATTERN_LIBRARY, PATTERN_USES } from './patternLibrary';
import { NPATTERNS, STEPS, patIdx } from './seq';
import { PAD_COUNT } from './params';

const hits = (p: Uint8Array, bar: number, pads: number[]) => pads.reduce((n, pad) => n + Array.from({ length: STEPS }, (_, s): number => p[patIdx(bar, pad, s)] ? 1 : 0).reduce((a, b) => a + b, 0), 0);

describe('DR-1 pattern library', () => {
  it('holds hundreds of patterns, every genre in every song section', () => {
    expect(PATTERN_LIBRARY.length).toBeGreaterThanOrEqual(300);
    expect(PATTERN_LIBRARY.length).toBe(PATTERN_GENRE_COUNT * PATTERN_USES.length);
    const genres = new Set(PATTERN_LIBRARY.map((e) => e.genre));
    for (const g of genres) expect(new Set(PATTERN_LIBRARY.filter((e) => e.genre === g).map((e) => e.role))).toEqual(new Set(PATTERN_USES));
    expect(new Set(PATTERN_LIBRARY.map((e) => e.family))).toEqual(new Set(PATTERN_FAMILIES));
  });

  it('has unique names and values and valid DR-1 bytes', () => {
    expect(new Set(PATTERN_LIBRARY.map((e) => e.name)).size).toBe(PATTERN_LIBRARY.length);
    expect(new Set(PATTERN_LIBRARY.map((e) => e.value)).size).toBe(PATTERN_LIBRARY.length);
    for (const e of PATTERN_LIBRARY) {
      expect(e.patterns.length).toBe(NPATTERNS * PAD_COUNT * STEPS);
      expect(e.patterns.every((v) => v <= 2)).toBe(true);
      expect(e.chain.length).toBeGreaterThanOrEqual(1);
      expect(e.chain.length).toBeLessThanOrEqual(NPATTERNS);
      e.chain.forEach((bar) => expect(hits(e.patterns, bar, Array.from({ length: PAD_COUNT }, (_, i) => i)), `${e.name} bar ${bar}`).toBeGreaterThan(0));
      // Bars outside the chain stay empty, so a load leaves no stale slots.
      for (let bar = e.chain.length; bar < NPATTERNS; bar++) expect(hits(e.patterns, bar, Array.from({ length: PAD_COUNT }, (_, i) => i))).toBe(0);
    }
  });

  it('arranges sections by their rules', () => {
    for (const g of new Set(PATTERN_LIBRARY.map((e) => e.genre))) {
      const of = (role: string) => PATTERN_LIBRARY.find((e) => e.genre === g && e.role === role)!;
      const main = of('main');
      expect(hits(main.patterns, 0, [0, 1, 2, 3, 4]), `${g} main has kick or backbeat`).toBeGreaterThan(0);
      // Breakdowns drop the kick and the backbeat.
      for (const bar of of('breakdown').chain) expect(hits(of('breakdown').patterns, bar, [0, 1]), `${g} breakdown`).toBe(0);
      // Builds rise in backbeat density.
      const b = of('build-up');
      expect(hits(b.patterns, 3, [2, 3, 4])).toBeGreaterThan(hits(b.patterns, 0, [2, 3, 4]));
      // Fills put toms in the last bar.
      expect(hits(of('fill').patterns, 3, [8, 9, 10]), `${g} fill`).toBeGreaterThan(0);
      // Outros end on the kick alone.
      expect(hits(of('outro').patterns, 3, [0])).toBeGreaterThan(0);
      expect(hits(of('outro').patterns, 3, [2, 3, 4, 5, 6, 7])).toBe(0);
    }
  });
});
