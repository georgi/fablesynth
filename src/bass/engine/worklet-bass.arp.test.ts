import { describe, expect, it } from 'vitest';
import { arpNotes, newArp } from '../../arp';
import { generateTables } from '../../engine/wavetables';
import { defaultBassParams } from '../params';
import { makeBassProcessor, type BassHarness } from './bassHarness';

const tables = generateTables();
const config = () => {
  const a = { ...newArp(), notes: [36, 39, 43, 46], slides: Array(16).fill(false) };
  return { ...a, notes: arpNotes(a) };
};
function boot() {
  const h = makeBassProcessor();
  h.send({ t: 'init', params: { ...defaultBassParams(), 'seq.bpm': 120, 'master.swing': 0 } });
  h.send({ t: 'tables', list: tables.map(t => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })) });
  return h;
}
const state = (h: BassHarness) => h.proc as unknown as { gate: boolean; semiTarget: number; fenvT: number; samplesToGateOff: number; songPos: number; noteOn: (semi: number, acc: boolean) => void };

describe('BL-1 arpeggiator', () => {
  it('renders finite, audible bass and releases at the gate boundary and on stop', () => {
    const h = boot(); h.send({ t: 'arp', config: config() }); h.send({ t: 'play' });
    const { L, R } = h.render(20);
    expect([...L, ...R].every(Number.isFinite)).toBe(true);
    const peak = Math.max(...L.map(Math.abs), ...R.map(Math.abs));
    expect(peak).toBeGreaterThan(.001); expect(peak).toBeLessThanOrEqual(1);
    expect(state(h).semiTarget).toBe(0);
    h.render(14); expect(state(h).gate).toBe(false);
    h.render(14); expect(state(h).semiTarget).toBe(3); expect(state(h).gate).toBe(true);
    h.send({ t: 'stop' }); expect(state(h).gate).toBe(false);
  });
  it('slides into the destination without restarting envelopes, including across the loop', () => {
    const h = boot(); const a = config(); a.slides.fill(true);
    h.send({ t: 'arp', config: a }); h.send({ t: 'play' });
    h.render(48);
    expect(state(h).semiTarget).toBe(3);
    expect(state(h).fenvT).toBeGreaterThan(6000);
    expect(state(h).samplesToGateOff).toBe(-1);
    h.render(703); // 96128 samples, into the second loop
    expect(h.sent.filter(m => m.t === 'step').slice(-1)[0]).toMatchObject({ s: 0, slide: true });
    expect(state(h).fenvT).toBeGreaterThan(96000);
  });
  it('rests break slides and the next note retriggers', () => {
    const h = boot(); const a = config(); a.slides.fill(true); a.hits[1] = false;
    h.send({ t: 'arp', config: a }); h.send({ t: 'play' });
    h.render(48); expect(state(h).gate).toBe(false);
    h.render(46); // 12032 samples
    expect(state(h).gate).toBe(true);
    expect(state(h).fenvT).toBeLessThan(128);
    expect(h.sent.filter(m => m.t === 'step').slice(-1)[0]).toMatchObject({ s: 2, slide: false });
  });
  it('clearing a sustained pool and switching modes release the voice', () => {
    const h = boot(); const a = config(); a.slides.fill(true);
    h.send({ t: 'arp', config: a }); h.send({ t: 'play' }); h.render(1);
    h.send({ t: 'arp', config: { ...a, notes: Array(16).fill(-1) } });
    expect(state(h).gate).toBe(false);
    h.send({ t: 'arp', config: a }); h.render(48);
    expect(state(h).gate).toBe(true);
    h.send({ t: 'arp', config: null }); expect(state(h).gate).toBe(false);
  });
  it('keeps fractional timing independent of render block size', () => {
    function run(size: number) {
      const h = boot(); const p = state(h); const events: number[] = [];
      const noteOn = p.noteOn.bind(p);
      p.noteOn = (n, acc) => { events.push(p.songPos); noteOn(n, acc); };
      h.send({ t: 'p', k: 'seq.bpm', v: 127 });
      h.send({ t: 'p', k: 'master.swing', v: .19 });
      h.send({ t: 'arp', config: { ...config(), rate: 1/6 } }); h.send({ t: 'play' });
      for (let i = 0; i < 49152 / size; i++) h.proc.process([], [[new Float32Array(size), new Float32Array(size)]]);
      return events;
    }
    expect(run(64)).toEqual(run(128));
    const events = run(96);
    const dur = 60 / 127 / 6 * 48000;
    events.forEach((frame, i) => expect(Math.abs(frame - (i * dur + (i % 2 ? .19 * .667 * dur : 0)))).toBeLessThan(1.01));
  });
  it('does not start a second clock in hosted SQ-4 mode', () => {
    const old = Object.getOwnPropertyDescriptor(globalThis, 'currentFrame');
    Object.defineProperty(globalThis, 'currentFrame', { value: 0, configurable: true });
    try {
      const h = boot(); h.send({ t: 'host', on: true });
      h.send({ t: 'arp', config: config() }); h.send({ t: 'play' }); h.render(1);
      expect(state(h).gate).toBe(false);
      expect(h.sent.filter(m => m.t === 'step')).toHaveLength(0);
    } finally {
      if (old) Object.defineProperty(globalThis, 'currentFrame', old);
      else Reflect.deleteProperty(globalThis, 'currentFrame');
    }
  });
});
