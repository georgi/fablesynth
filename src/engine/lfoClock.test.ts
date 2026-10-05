import { describe, it, expect } from 'vitest';
import { LfoClock } from './lfoClock';
import { bootWt } from './workletHarness';
import { PARAM_DEFS } from '../params';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { defaultBassParams } from '../bass/params';

describe('continuous LFO rate edits', () => {
  it('keeps phase through repeated edits and advances at the new rate', () => {
    const clock = new LfoClock();
    expect(clock.phase(100.125, 2)).toBeCloseTo(.25);
    for (const rate of [3, 9, .1, 7]) expect(clock.phase(100.125, rate)).toBeCloseTo(.25);
    expect(clock.phase(100.175, 7)).toBeCloseTo(.6);
    expect(clock.phase(0, 7)).toBe(0);
    clock.reset(); expect(clock.phase(4.25, 2)).toBeCloseTo(.5);
  });
  it('preserves WT synced phase and sample-and-hold during division edits', () => {
    const h = bootWt({ 'lfo1.sync': 1, 'lfo1.syncrate': 2 });
    const p = h.proc as unknown as { p: number[]; gLfo1: { phase: number; hold: number }; updateGlobalLfo(g: unknown, base: number, ppq: number, n: number): void };
    const base = PARAM_DEFS.findIndex(d => d.id === 'lfo1.shape');
    const div = PARAM_DEFS.findIndex(d => d.id === 'lfo1.syncrate');
    p.updateGlobalLfo(p.gLfo1, base, 17.125, 128);
    const phase = p.gLfo1.phase, hold = p.gLfo1.hold;
    for (const rate of [0, 4, 7, 1]) {
      p.p[div] = rate; p.updateGlobalLfo(p.gLfo1, base, 17.125, 128);
      expect(p.gLfo1.phase).toBeCloseTo(phase); expect(p.gLfo1.hold).toBe(hold);
    }
    p.updateGlobalLfo(p.gLfo1, base, 0, 128); expect(p.gLfo1.phase).toBe(0);
  });
  it('preserves BL phase and sample-and-hold during division edits', () => {
    const h = makeBassProcessor(); h.send({ t: 'init', params: defaultBassParams() });
    const p = h.proc as unknown as { p: Record<string, number>; songPos: number; lfoValue(): number };
    p.songPos = 123456; p.p['lfo.shape'] = 0;
    const before = p.lfoValue();
    for (const rate of [0, 4, 7, 1]) { p.p['lfo.rate'] = rate; expect(p.lfoValue()).toBeCloseTo(before); }
    p.p['lfo.shape'] = 4; const hold = p.lfoValue();
    p.p['lfo.rate'] = 6; expect(p.lfoValue()).toBe(hold);
    p.songPos = 0; p.p['lfo.shape'] = 0; expect(p.lfoValue()).toBe(0);
  });
});
