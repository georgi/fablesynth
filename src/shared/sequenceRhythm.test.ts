import { describe, expect, it } from 'vitest';
import { copySequenceRhythm, defaultSequenceRhythm, updateSequenceRhythm, validSequenceRhythm } from './sequenceRhythm';
import { bootWt } from '../engine/workletHarness';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { defaultBassParams } from '../bass/params';
import { makeEmptyPatterns as wtEmpty, setStep as wtStep } from '../noteseq';
import { makeEmptyPatterns as blEmpty, setStep as blStep } from '../bass/seq';
import { generateTables } from '../engine/wavetables';
import { wtNoteIdx } from '../seq/protocol';

const rhythm = (patch = {}) => ({ ...defaultSequenceRhythm(), enabled: true, steps: 3, ...patch });
function bass() {
  const h = makeBassProcessor();
  h.send({ t: 'init', params: { ...defaultBassParams(), 'seq.bpm': 120, 'master.swing': 0 } });
  h.send({ t: 'tables', list: generateTables().map(t => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })) });
  return h;
}
function notes(isBass: boolean) {
  let p = isBass ? blEmpty() : wtEmpty();
  const set = isBass ? blStep : wtStep;
  for (let i = 0; i < 16; i++) p = set(p, 0, i, { on: true, note: i % 12, duration: 1 });
  return p;
}
const steps = (h: ReturnType<typeof bass>) => h.sent.filter(m => m.t === 'step').map(m => m.s);
type PolyProc = { polyK: number; polyFrame: (r: ReturnType<typeof rhythm>, k: number, dur: number, swing: number) => number;
  polyFire: (bpm: number) => void; samplesToNext: number; seqToNext: number; samplesToGateOff: number;
  gate: boolean; fenvT: number; voices: { gate: boolean; note: number }[]; seqOffQueue: { remaining: number }[] };

describe('melodic POLY', () => {
  it('validates imports and normalizes rotation when shortening without mutating its source', () => {
    const original = rhythm({ steps: 15, rotation: 14 });
    expect(updateSequenceRhythm(original, { steps: 5 }).rotation).toBe(4);
    expect(original.rotation).toBe(14);
    expect(copySequenceRhythm(original)).not.toBe(original);
    for (const patch of [{ steps: 0 }, { steps: 17 }, { rotation: -1 }, { cycleBeats: 3 }, { mode: 'other' }]) {
      expect(validSequenceRhythm({ ...original, ...patch })).toBe(false);
    }
  });
  for (const isBass of [false, true]) {
    const name = isBass ? 'BL-1' : 'WT-1';
    const boot = () => isBass ? bass() : bootWt({ 'seq.bpm': 120, 'seq.swing': 0 });
    it(`${name} repeats only the first N source cells, rotates right and restores the normal sequence`, () => {
      const h = boot(); h.send({ t: 'pats', data: notes(isBass) });
      h.send({ t: 'sequenceRhythm', rhythm: rhythm({ rotation: 1 }) }); h.send({ t: 'play' });
      const output = h.render(190);
      expect(steps(h).slice(0, 5)).toEqual([2, 0, 1, 2, 0]);
      expect([...output.L, ...output.R].every(Number.isFinite)).toBe(true);
      expect(output.L.some(v => Math.abs(v) > .001)).toBe(true);
      const count = steps(h).length;
      h.send({ t: 'sequenceRhythm', rhythm: { ...rhythm(), enabled: false } });
      h.render(190);
      expect(steps(h).slice(count)).toEqual([5, 6, 7, 8]);
      h.send({ t: 'stop' });
      const p = h.proc as unknown as PolyProc;
      expect(isBass ? p.gate : p.voices.some(v => v.gate)).toBe(false);
    });
    it(`${name} follows global swing across odd loops and keeps FIT slots straight`, () => {
      const h = boot(); const p = h.proc as unknown as PolyProc;
      const r = rhythm();
      const times = Array.from({ length: 7 }, (_, k) => p.polyFrame(r, k, 6000, .5));
      expect(times.slice(1).map((v, i) => v - times[i])).toEqual([8001, 3999, 8001, 3999, 8001, 3999]);
      expect(p.polyFrame(rhythm({ mode: 'fit' }), 3, 32000, 1)).toBe(96000);
      h.send({ t: 'pats', data: notes(isBass) });
      h.send({ t: 'sequenceRhythm', rhythm: rhythm({ mode: 'fit' }) }); h.send({ t: 'play' });
      h.render(251); // crosses the second 32,000-sample slot at 120 BPM
      expect(steps(h)).toEqual([0, 1]);
      expect(isBass ? p.samplesToGateOff : p.seqOffQueue[0].remaining).toBeGreaterThan(31000);
      // A rotation edit changes lookup without resetting the phrase clock.
      const k = p.polyK;
      h.send({ t: 'sequenceRhythm', rhythm: rhythm({ mode: 'fit', rotation: 1 }) });
      expect(p.polyK).toBe(k);
      h.render(250);
      expect(steps(h)).toEqual([0, 1, 1]);
    });
    it(`${name} releases old notes on timing changes and ignores malformed worklet messages`, () => {
      const h = boot(); h.send({ t: 'pats', data: notes(isBass) });
      h.send({ t: 'sequenceRhythm', rhythm: rhythm() }); h.send({ t: 'play' }); h.render(1);
      const p = h.proc as unknown as PolyProc;
      h.send({ t: 'sequenceRhythm', rhythm: rhythm({ mode: 'fit' }) });
      expect(isBass ? p.gate : p.voices.some(v => v.gate)).toBe(false);
      h.send({ t: 'sequenceRhythm', rhythm: rhythm({ steps: 0 }) });
      h.render(751);
      expect(steps(h).slice(-3)).toEqual([1, 2, 0]);
    });
    it(`${name} uses global hosted phase, preserves chords/slides and clears rhythm on clip replacement`, () => {
      const h = boot(); let data: Uint8Array;
      if (isBass) data = notes(true).slice(0, 48);
      else {
        data = new Uint8Array(16 * 8 * 3);
        for (let s = 0; s < 3; s++) for (let lane = 0; lane < 2; lane++) {
          const i = wtNoteIdx(0, s, lane);
          data[i] = 5; data[i + 1] = s + lane * 4; data[i + 2] = 1;
        }
      }
      h.send({ t: 'host', on: true }); h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
      h.send({ t: 'clip', data, bars: 1, atFrame: 0, sequenceRhythm: rhythm({ mode: 'fit' }) });
      // The bass harness leaves currentFrame to the caller, as in hostedArp.test.
      let frame = 0;
      const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'currentFrame');
      if (isBass) Object.defineProperty(globalThis, 'currentFrame', { configurable: true, get: () => frame });
      const render = (blocks: number) => {
        if (!isBass) { h.render(blocks); return; }
        for (let i = 0; i < blocks; i++) { h.render(1); frame += 128; }
      };
      render(501);
      expect(h.sent.filter(m => m.t === 'pos').map(m => m.step)).toEqual([0, 1, 2]);
      if (!isBass) expect((h.proc as unknown as PolyProc).voices.filter(v => v.gate)).toHaveLength(2);
      h.send({ t: 'clipupdate', data, bars: 1 }); render(1);
      expect(h.sent.filter(m => m.t === 'pos').slice(-1)[0]).toMatchObject({ step: 11 });
      h.send({ t: 'clipstop', atFrame: 0 }); render(1);
      const p = h.proc as unknown as PolyProc;
      expect(isBass ? p.gate : p.voices.some(v => v.gate)).toBe(false);
      if (isBass && descriptor) Object.defineProperty(globalThis, 'currentFrame', descriptor);
    });
  }
  it('BL-1 slides through the shortened wrap and a rest breaks the held chain', () => {
    const h = bass(); let data = blEmpty();
    for (let i = 0; i < 3; i++) data = blStep(data, 0, i, { on: true, note: i, slide: true });
    h.send({ t: 'pats', data }); h.send({ t: 'sequenceRhythm', rhythm: rhythm() }); h.send({ t: 'play' });
    h.render(142);
    const p = h.proc as unknown as PolyProc;
    expect(steps(h)).toEqual([0, 1, 2, 0]); expect(p.fenvT).toBeGreaterThan(18000);
    data = blStep(data, 0, 1, { on: false }); h.send({ t: 'pats', data });
    h.render(47); expect(p.gate).toBe(false);
    h.render(46); expect(p.gate).toBe(true); expect(p.fenvT).toBeLessThan(256);
  });
});
