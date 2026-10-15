import { describe, expect, it } from 'vitest';
import { BAND_HZ, MixListener, analyse, buildPlan, type PlanInput } from './mixAssistant';
import { referenceById, referenceDb } from '../engine/autoEq';

const targetDb = (_: string, f: number) => referenceDb(referenceById('mix'), f);
import { masterFxParams } from './masterFx';

const SR = 48000, FFT = 8192;

const shapeSpec = (shapeDb: (f: number) => number) => new Float32Array(FFT / 2).map((_, k) => shapeDb(Math.max(1, k * SR / FFT)) - 10 * Math.log10(Math.max(1, k * SR / FFT)));
const sig = (a: number) => new Float32Array(FFT).map((_, i) => a * Math.SQRT2 * Math.sin(i * 0.37));

// A test track: its RMS level, spectrum shape, and the share of frames it plays in.
interface FakeTrack { rms: number; shape?: (f: number) => number; duty?: number; wobble?: number }

// Feed frames whose spectrum follows `shapeDb(f)` and whose time-domain level is `rms`.
function listen(shapeDb: (f: number) => number, rms: number, frames = 40, tracks: (number | FakeTrack)[] = []) {
  const l = new MixListener(SR, FFT, tracks.length);
  const spec = shapeSpec(shapeDb);
  const ts = tracks.map(t => typeof t === 'number' ? { rms: t } : t);
  const specs = ts.map(t => t.shape && shapeSpec(t.shape));
  for (let i = 0; i < frames; i++) l.add(spec, sig(rms), sig(rms), ts.map((t, k) => {
    const on = (i % 10) < (t.duty ?? 1) * 10;
    const level = t.rms * (1 + (t.wobble ?? 0) * (i % 2 ? 1 : -1));
    return { time: sig(on ? level : 0), spectrum: specs[k] };
  }));
  return analyse(l);
}

const DRUM = (f: number) => (f < 120 ? 0 : -6) - 3 * Math.log2(Math.max(f, 120) / 1000);
const BASS = (f: number) => (f < 150 ? 0 : -24 * Math.log2(f / 150));
const MID = (f: number) => -12 * Math.abs(Math.log2(f / 800));

const input = (over: Partial<PlanInput>): PlanInput => ({
  analysis: listen(f => targetDb('BALANCED', f), 0.1), reference: 'mix', loudness: -11, amount: 1, bpm: 120, sampleRate: SR,
  masterFx: masterFxParams(), masterVol: 0.8, trackVol: [], tracks: [], ...over,
});

describe('mix assistant', () => {
  it('leaves an on-target mix flat', () => {
    const plan = buildPlan(input({}));
    expect(plan.masterFx['master.fx.eq.on']).toBe(0);
    expect(Math.max(...plan.eq.residual.filter((_, i) => BAND_HZ[i] >= 63).map(Math.abs))).toBeLessThan(1.5);
  });

  it('cuts excess low end and finds a mid resonance', () => {
    const shape = (f: number) => targetDb('BALANCED', f) + (f < 110 ? 6 : 0) + 5 * Math.exp(-(Math.log2(f / 400) ** 2) * 8);
    const plan = buildPlan(input({ analysis: listen(shape, 0.1) }));
    expect(plan.masterFx['master.fx.eq.low']).toBeLessThan(-3);
    expect(plan.masterFx['master.fx.eq.mid']).toBeLessThan(-2);
    expect(plan.masterFx['master.fx.eq.mfreq']).toBeGreaterThanOrEqual(315);
    expect(plan.masterFx['master.fx.eq.mfreq']).toBeLessThanOrEqual(500);
    const i400 = BAND_HZ.indexOf(400);
    expect(Math.abs(plan.eq.residual[i400])).toBeLessThan(5 - 1.5);
  });

  it('moves the master fader toward the loudness target', () => {
    const quiet = buildPlan(input({ analysis: listen(f => targetDb('BALANCED', f), 0.02), masterVol: 0.6 }));
    expect(quiet.masterVol).toBeGreaterThan(0.6);
    const loud = buildPlan(input({ analysis: listen(f => targetDb('BALANCED', f), 0.9), masterVol: 0.6 }));
    expect(loud.masterVol).toBeLessThan(0.6);
  });

  // Authored-style names carry no role, so detection works from the audio.
  const TRACKS = [{ machine: 'DR1', name: 'ROOM' }, { machine: 'BL1', name: 'UNDERTOW' }, { machine: 'WT1', name: 'SIGNAL' }, { machine: 'WT1', name: 'HAZE' }] as const;
  const parts = (rms: number[]) => [
    { rms: rms[0], shape: DRUM, wobble: 0.6 }, { rms: rms[1], shape: BASS, duty: 0.8, wobble: 0.5 },
    { rms: rms[2], shape: MID, duty: 0.6, wobble: 0.5 }, { rms: rms[3], shape: MID },
  ];
  const lu = (a: number, b: number) => 20 * Math.log10(a / b);
  const volDbOf = (from: number, to: number) => 40 * Math.log10(to / from);

  it('detects each track role from its register and sustain', () => {
    const plan = buildPlan(input({ analysis: listen(DRUM, 0.1, 40, parts([0.1, 0.1, 0.1, 0.1])), trackVol: [0.8, 0.8, 0.8, 0.8], tracks: [...TRACKS] }));
    expect(plan.levels.map(l => l.detected)).toEqual(['drums', 'bass', 'lead', 'pad']);
    // A generic name states the role and wins over the audio.
    const named = buildPlan(input({ analysis: listen(DRUM, 0.1, 40, parts([0.1, 0.1, 0.1, 0.1])), trackVol: [0.8, 0.8, 0.8, 0.8], tracks: [...TRACKS.slice(0, 3), { machine: 'WT1', name: 'LEAD 2' }] }));
    expect(named.levels[3].detected).toBe('lead');
  });

  it('levels each track to its role and leaves a balanced mix alone', () => {
    // Equal levels: the lead and pad are too loud for their roles.
    const flat = buildPlan(input({ analysis: listen(DRUM, 0.1, 40, parts([0.1, 0.1, 0.1, 0.1])), trackVol: [0.8, 0.8, 0.8, 0.8], tracks: [...TRACKS] }));
    // Every track lands on its target up to one common loudness offset.
    const off = flat.levels.map(l => l.current + l.move - l.target);
    expect(Math.max(...off) - Math.min(...off)).toBeLessThan(0.6);
    expect(flat.trackVol[3]!).toBeLessThan(flat.trackVol[2] ?? 0.8);
    // A mix whose measured levels already follow the role targets keeps its
    // balance; any loudness make-up moves every fader by the same amount.
    const err = flat.levels.map(l => l.target - l.current);
    const rms = err.map(e => 0.1 * 10 ** ((e - err[0]) / 20));
    const held = buildPlan(input({ analysis: listen(DRUM, 0.1, 40, parts(rms)), trackVol: [0.8, 0.8, 0.8, 0.8], tracks: [...TRACKS] }));
    const heldMoves = held.levels.map(l => l.move);
    expect(Math.max(...heldMoves) - Math.min(...heldMoves)).toBeLessThan(0.2);
    // Loudness goes to the track faders together, since the session saves
    // them; the master fader takes only what they cannot.
    const lifted = buildPlan(input({ analysis: listen(DRUM, 0.1, 40, parts(rms)), trackVol: [0.8, 0.8, 0.8, 0.8], tracks: [...TRACKS], loudness: -9, masterVol: 0.6 }));
    for (const l of lifted.levels) expect(l.move).toBeCloseTo(volDbOf(0.8, 1), 0);
    expect(lifted.masterVol!).toBeGreaterThan(0.6);
    expect(lu(rms[0], rms[3])).toBeGreaterThan(4);
  });

  it('honours role overrides and shifts all faders when one runs out of travel', () => {
    const analysis = listen(DRUM, 0.1, 40, parts([0.1, 0.02, 0.1, 0.1]));
    const plan = buildPlan(input({ analysis, trackVol: [0.8, 1, 0.8, 0.8], tracks: [...TRACKS], roles: [null, null, null, 'lead'] }));
    expect(plan.levels[3].role).toBe('lead');
    expect(plan.levels[3].target).toBeCloseTo(plan.levels[2].target);
    // The quiet bass is at the top already: the others come down instead.
    expect(plan.trackVol[1]).toBeNull();
    expect(plan.trackVol[0]!).toBeLessThan(0.8);
    expect(plan.notes.some(n => n.includes('fader is at the top'))).toBe(true);
  });
});
