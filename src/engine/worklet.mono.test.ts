import { describe, it, expect, beforeEach } from 'vitest';
import { bootWt, type WtHarness } from './workletHarness';

const g = globalThis as unknown as { currentFrame: number };
const peak = (x: Float32Array) => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

interface VoiceState { gate: boolean; note: number; pitch: number; age: number; ampEnv: { state: number; level: number } }
const voices = (h: WtHarness) => (h.proc as unknown as { voices: VoiceState[] }).voices;
const gated = (h: WtHarness) => voices(h).filter((v) => v.gate);

describe('harness smoke', () => {
  beforeEach(() => { g.currentFrame = 0; });

  it('renders audio for a live note-on and releases on note-off', () => {
    const h = bootWt();
    expect(peak(h.render(4).L)).toBe(0);
    h.send({ t: 'on', n: 60, v: 1 });
    expect(peak(h.render(20).L)).toBeGreaterThan(0.001);
    h.send({ t: 'off', n: 60 });
    h.render(400); // default env1.r is short; let the tail die
    expect(gated(h)).toHaveLength(0);
  });
});

describe('mono mode', () => {
  beforeEach(() => { g.currentFrame = 0; });

  it('poly default: two held notes gate two voices', () => {
    const h = bootWt();
    h.send({ t: 'on', n: 60, v: 1 });
    h.send({ t: 'on', n: 64, v: 1 });
    h.render(4);
    expect(gated(h)).toHaveLength(2);
  });

  it('legato note-on retunes the sounding voice without retrigger', () => {
    const h = bootWt({ 'master.mono': 1 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(8);
    const v0 = gated(h)[0];
    const age = v0.age;
    h.send({ t: 'on', n: 67, v: 1 });
    h.render(4);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0]).toBe(v0);
    expect(gs[0].note).toBe(67);
    expect(gs[0].age).toBe(age); // Voice.noteOn not called => envelopes untouched
  });

  it('legato glides: pitch approaches the new note over blocks', () => {
    const h = bootWt({ 'master.mono': 1, 'master.glide': 0.3 });
    h.send({ t: 'on', n: 48, v: 1 });
    h.render(8);
    h.send({ t: 'on', n: 72, v: 1 });
    h.render(2);
    const v = gated(h)[0];
    expect(v.pitch).toBeGreaterThan(48);
    expect(v.pitch).toBeLessThan(72);
    h.render(2000);
    expect(gated(h)[0].pitch).toBeCloseTo(72, 1);
  });

  it('releasing the top note glides back to the held note without retrigger', () => {
    const h = bootWt({ 'master.mono': 1 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(4);
    const age = gated(h)[0].age;
    h.send({ t: 'on', n: 67, v: 1 });
    h.render(4);
    h.send({ t: 'off', n: 67 });
    h.render(4);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0].note).toBe(60);
    expect(gs[0].age).toBe(age);
  });

  it('releasing the last held note releases the voice', () => {
    const h = bootWt({ 'master.mono': 1 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.send({ t: 'on', n: 67, v: 1 });
    h.send({ t: 'off', n: 67 });
    h.send({ t: 'off', n: 60 });
    h.render(4);
    expect(gated(h)).toHaveLength(0);
  });

  it('first mono note-on after a poly->mono flip collapses extra gated voices', () => {
    const h = bootWt();
    h.send({ t: 'on', n: 60, v: 1 });
    h.send({ t: 'on', n: 64, v: 1 });
    h.send({ t: 'on', n: 67, v: 1 });
    h.render(4);
    expect(gated(h)).toHaveLength(3);
    h.send({ t: 'p', k: 'master.mono', v: 1 });
    h.send({ t: 'on', n: 72, v: 1 });
    h.render(4);
    const gs = gated(h);
    expect(gs).toHaveLength(1);
    expect(gs[0].note).toBe(72);
  });
});
