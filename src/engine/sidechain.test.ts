import { describe, expect, it } from 'vitest';
import source from './ott-worklet.js?raw';
import { DRUM_SC_SOURCES, SC_SOURCES, isExternalSource, trackKeyLabels } from './sidechain';

new Function(source)();
type Comp = { setParams(...a: unknown[]): void; setKey(on: boolean, hp?: number): void; process(L: Float32Array, R: Float32Array, n: number, KL?: Float32Array | null, KR?: Float32Array | null): void };
const Compressor = (globalThis as unknown as { FableCompressor: new (sr: number) => Comp }).FableCompressor;
const SR = 48000;
const tone = (f: number, a: number, n = SR) => Float32Array.from({ length: n }, (_, i) => a * Math.sin(2 * Math.PI * f * i / SR));
const rms = (x: Float32Array, from = SR / 2) => Math.sqrt(x.subarray(from).reduce((s, v) => s + v * v, 0) / (x.length - from));

function run(key: Float32Array | null, keyed: boolean, hp = 20) {
  const c = new Compressor(SR);
  c.setParams(true, -30, 0.001, 0.1, 10);
  c.setKey(keyed, hp);
  const L = tone(220, 0.05), R = L.slice();
  for (let o = 0; o < SR; o += 128) c.process(L.subarray(o, o + 128), R.subarray(o, o + 128), 128, key ? key.subarray(o, o + 128) : null);
  return rms(L);
}

describe('compressor side-chain', () => {
  it('ducks a quiet signal on a loud key, and leaves it alone on a silent key', () => {
    const dry = rms(tone(220, 0.05));
    expect(run(tone(80, 0.8), true)).toBeLessThan(dry * 0.3);
    expect(run(new Float32Array(SR), true)).toBeCloseTo(dry, 3);
    // Unkeyed, the quiet signal sits below threshold and passes (auto gain on).
    expect(run(tone(80, 0.8), false)).toBeCloseTo(dry, 2);
  });

  it('high-passes the key: a 400 Hz key HP mostly ignores a 40 Hz key', () => {
    const flat = run(tone(40, 0.8), true, 20), hp = run(tone(40, 0.8), true, 400);
    expect(hp).toBeGreaterThan(flat * 2);
  });

  it('lists sources with their key routing', () => {
    expect(SC_SOURCES[1]).toBe('KICK');
    expect(DRUM_SC_SOURCES).toHaveLength(20);
    expect(isExternalSource(DRUM_SC_SOURCES, 1)).toBe(false);
    expect(isExternalSource(DRUM_SC_SOURCES, 17)).toBe(true);
    expect(isExternalSource(SC_SOURCES, 0)).toBe(false);
    expect(trackKeyLabels([{ name: 'DRUMS' }, { name: 'BASS' }])['TRACK 2']).toBe('2 BASS');
  });
});
