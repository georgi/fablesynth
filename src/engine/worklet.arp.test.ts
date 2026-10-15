import { describe, expect, it } from 'vitest';
import { arpCompile, arpNotes, newArp } from '../arp';
import { parseCompSteps } from '../comp';
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

describe('WT-1 chord comping', () => {
  it('strikes a voicing, holds it through ties and releases it at the end of the run', () => {
    const h = bootWt({ 'seq.bpm': 120 });
    const a = { ...newArp(), style: 'comp' as const, gate: .5, ...parseCompSteps('BC - . . . . . . . . . . . . . .') };
    h.send({ t: 'arp', config: { ...a, ...arpCompile(a, [60, 64, 67], 1) } }); h.send({ t: 'play' });
    const gated = () => state(h).voices.filter(v => v.gate).map(v => v.note).sort((x, y) => x - y);
    h.render(20); expect(gated()).toEqual([48, 60, 64, 67]);
    h.render(50); expect(gated()).toEqual([48, 60, 64, 67]); // 6400 samples: inside the tied step
    h.render(30); expect(gated()).toEqual([]); // past 1.5 steps
  });
  it('strums notes in stack order and clears pending notes on stop', () => {
    const h = bootWt({ 'seq.bpm': 120 });
    const a = { ...newArp(), style: 'comp' as const, strum: 20, strumDir: 'down' as const, ...parseCompSteps('C - - - . . . . . . . . . . . .') };
    h.send({ t: 'arp', config: { ...a, ...arpCompile(a, [60, 64, 67], 1) } }); h.send({ t: 'play' });
    const gated = () => state(h).voices.filter(v => v.gate).map(v => v.note);
    h.render(1); expect(gated()).toEqual([67]);
    h.render(8); expect(gated()).toEqual([67, 64]); // 20 ms = 960 samples per note
    h.render(8); expect(gated().sort()).toEqual([60, 64, 67]);
    h.send({ t: 'stop' }); h.send({ t: 'play' }); h.render(1);
    h.send({ t: 'stop' }); h.render(20);
    expect(gated()).toEqual([]);
  });
});

describe('WT-1 chord-track arp cycle', () => {
  it('plays a cycle longer than 16 steps with 16-step rhythm lanes', () => {
    const h = bootWt({ 'seq.bpm': 120 });
    const notes = Array.from({ length: 32 }, (_, i) => 48 + (i >> 4) * 5);
    h.send({ t: 'arp', config: { ...newArp(), notes } }); h.send({ t: 'play' });
    h.render(Math.ceil(48000 * 2.04 / 128));
    expect(state(h).voices.filter(v => v.gate).map(v => v.note)).toEqual([53]);
    h.render(Math.ceil(48000 * 2.2 / 128));
    expect(steps(h).slice(0, 34)).toEqual(Array.from({ length: 34 }, (_, i) => i % 32));
  });
});
