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
