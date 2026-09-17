import { describe, expect, it } from 'vitest';
import { arpNotes, newArp } from '../arp';
import { bootWt, type WtHarness } from './workletHarness';

const state = (h: WtHarness) => h.proc as unknown as { voices: { gate: boolean; note: number }[]; seqPlaying: boolean };
const config = () => { const a = newArp(); return { ...a, notes: arpNotes(a) }; };
const steps = (h: WtHarness) => h.sent.filter(m => m.t === 'step').map(m => m.s);

describe('WT-1 arpeggiator', () => {
  it('orders pitches, extends octaves and avoids repeated turnaround notes', () => {
    const a = { ...newArp(), notes: [60, 50, 57], order: 'updown' as const };
    expect(arpNotes(a).slice(0, 8)).toEqual([50, 57, 60, 57, 50, 57, 60, 57]);
    expect(arpNotes({ ...a, octaves: 2 }).slice(0, 7)).toEqual([50, 57, 60, 62, 69, 72, 69]);
    expect(arpNotes({ ...a, notes: [] })).toEqual(Array(16).fill(-1));
    expect(arpNotes({ ...a, order: 'random' })).toEqual(arpNotes({ ...a, order: 'random' }));
  });
  it('renders audible finite output, releases at gate time and stops without held notes', () => {
    const h = bootWt({ 'seq.bpm': 120 });
    h.send({ t: 'arp', config: config() }); h.send({ t: 'play' });
    const { L, R } = h.render(20);
    expect([...L, ...R].every(Number.isFinite)).toBe(true);
    expect(Math.max(...L.map(Math.abs))).toBeGreaterThan(.001);
    expect(Math.max(...L.map(Math.abs), ...R.map(Math.abs))).toBeLessThanOrEqual(1);
    expect(state(h).voices.filter(v => v.gate).map(v => v.note)).toEqual([50]);
    h.render(14); // 4352 samples: past 65% of a 6000 sample step
    expect(state(h).voices.some(v => v.gate)).toBe(false);
    h.render(14);
    expect(state(h).voices.filter(v => v.gate).map(v => v.note)).toEqual([53]);
    h.send({ t: 'stop' });
    expect(state(h).voices.some(v => v.gate)).toBe(false);
  });
  it('has identical musical timing across audio block sizes', () => {
    const run = (size: number) => {
      const h = bootWt({ 'seq.bpm': 120, 'seq.swing': .2 });
      h.send({ t: 'arp', config: { ...config(), rate: 1/6 } }); h.send({ t: 'play' });
      h.render(48000 / size, size);
      return steps(h);
    };
    expect(run(64)).toEqual(run(96));
    expect(run(64)).toHaveLength(12);
  });
  it('honors rests and releases immediately when the pool is cleared', () => {
    const h = bootWt(); const a = config(); a.hits[0] = false;
    h.send({ t: 'arp', config: a }); h.send({ t: 'play' }); h.render(10);
    expect(state(h).voices.some(v => v.gate)).toBe(false);
    h.render(40);
    expect(state(h).voices.some(v => v.gate)).toBe(true);
    h.send({ t: 'arp', config: { ...a, notes: Array(16).fill(-1) } });
    expect(state(h).voices.some(v => v.gate)).toBe(false);
  });
  it('does not take over hosted SQ-4 playback', () => {
    const h = bootWt(); h.send({ t: 'host', on: true });
    h.send({ t: 'arp', config: config() }); h.send({ t: 'play' }); h.render(10);
    expect(steps(h)).toHaveLength(0);
    expect(state(h).voices.some(v => v.gate)).toBe(false);
  });
});
