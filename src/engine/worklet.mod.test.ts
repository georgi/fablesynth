import { describe, expect, it } from 'vitest';
import { MOD_DESTS, MOD_SOURCES } from '../params';
import { bootWt, type WtHarness } from './workletHarness';

// Live-modulation telemetry contract: while at least one per-param mod route
// is active AND a voice is sounding, the worklet streams {t:'mod', d} at the
// viz cadence (every 2048 samples), where d[MOD_DESTS index] is that
// destination's live route sum x (the same value the engine folds into the
// modulated param). When the last voice dies, a single {t:'mod', d:null}
// closes the stream so the UI can settle back to base values.

const DST_F1CUT = MOD_DESTS.indexOf('F1 CUT');
const DST_APOS = MOD_DESTS.indexOf('A POS');

function modMsgs(h: WtHarness): { t: string; d: Float32Array | null }[] {
  return h.sent.filter((m) => m.t === 'mod') as { t: string; d: Float32Array | null }[];
}

describe('live modulation telemetry', () => {
  it('streams per-destination route sums while a routed note plays', () => {
    const h = bootWt({
      'mat1.src': 1, 'mat1.dst': DST_F1CUT, 'mat1.amt': 0.8, // LFO1 -> F1 CUT
      'mat2.src': 1, 'mat2.dst': DST_APOS, 'mat2.amt': 0.5,  // LFO1 -> A POS
      'lfo1.rate': 5, 'lfo1.retrig': 1, 'lfo1.rise': 0,
    });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(64); // 8192 samples -> 4 telemetry windows

    const msgs = modMsgs(h);
    expect(msgs.length).toBeGreaterThanOrEqual(3);
    for (const m of msgs) expect(m.d).toBeInstanceOf(Float32Array);

    const cut = msgs.map((m) => m.d![DST_F1CUT]!);
    const pos = msgs.map((m) => m.d![DST_APOS]!);
    // Plausible: bounded by the route amount...
    for (const x of cut) expect(Math.abs(x)).toBeLessThanOrEqual(0.8 + 1e-6);
    for (const x of pos) expect(Math.abs(x)).toBeLessThanOrEqual(0.5 + 1e-6);
    // ...nonzero at some point, and MOVING (a 5 Hz LFO sweeps ~0.21 cycles
    // per 2048-sample window, so consecutive sums must differ).
    expect(Math.max(...cut.map(Math.abs))).toBeGreaterThan(0.05);
    expect(Math.max(...pos.map(Math.abs))).toBeGreaterThan(0.03);
    expect(new Set(cut.map((x) => x.toFixed(4))).size).toBeGreaterThan(1);
    // Unrouted destinations stay at zero.
    expect(msgs[0]!.d![MOD_DESTS.indexOf('B POS')]).toBe(0);
  });

  it('sends a single null terminator once idle, and nothing after', () => {
    const h = bootWt({
      'mat1.src': 1, 'mat1.dst': DST_F1CUT, 'mat1.amt': 0.8,
      'lfo1.rate': 5,
      'env1.r': 0.02, // short release so the voice actually dies in-test
    });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(48);
    h.send({ t: 'off', n: 60 });
    h.render(200); // well past release
    const msgs = modMsgs(h);
    expect(msgs.length).toBeGreaterThan(1);
    expect(msgs[msgs.length - 1]!.d).toBeNull();
    expect(msgs.filter((m) => m.d === null)).toHaveLength(1);
    const count = msgs.length;
    h.render(100);
    expect(modMsgs(h)).toHaveLength(count); // idle -> silent, no re-sends
  });

  it('stays silent with no active mod routes', () => {
    const h = bootWt();
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(64);
    expect(modMsgs(h)).toHaveLength(0);
  });

  it('stays silent for global-only routes (no owning knob to animate)', () => {
    const h = bootWt({
      'mat1.src': 1, 'mat1.dst': MOD_DESTS.indexOf('PITCH'), 'mat1.amt': 0.5,
      'lfo1.rate': 5,
    });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(64);
    expect(modMsgs(h)).toHaveLength(0);
  });
});

describe('Serum-parity mod sources', () => {
  const src = (name: string) => MOD_SOURCES.indexOf(name);
  const posSums = (params: Record<string, number>, windows = 8) => {
    const h = bootWt({ 'mat1.dst': DST_APOS, ...params });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(windows * 16);
    return modMsgs(h).filter((m) => m.d).map((m) => m.d![DST_APOS]!);
  };

  it('a macro drives a route by its knob value', () => {
    const x = posSums({ 'mat1.src': src('MACRO 1'), 'mat1.amt': 0.5, macro1: 0.6 });
    for (const v of x) expect(v).toBeCloseTo(0.3, 6);
  });

  it('the aux source scales a route by its unipolar value', () => {
    const route = { 'mat1.src': src('MACRO 2'), 'mat1.amt': 0.5, macro2: 1, 'mat1.aux': src('MOD WHL') };
    for (const v of posSums({ ...route, 'perf.modwheel': 0.5 })) expect(v).toBeCloseTo(0.25, 6);
    for (const v of posSums({ ...route, 'perf.modwheel': 0 })) expect(v).toBe(0);
    // A bipolar aux (LFO 3) contributes (x + 1) / 2, so the scale stays in 0..1.
    const lfo = posSums({ ...route, 'mat1.aux': src('LFO 3'), 'lfo3.rate': 3, 'lfo3.shape': 1 }, 16);
    for (const v of lfo) {
      expect(v).toBeGreaterThanOrEqual(-1e-9);
      expect(v).toBeLessThanOrEqual(0.5 + 1e-9);
    }
    expect(Math.max(...lfo) - Math.min(...lfo)).toBeGreaterThan(0.1);
  });

  it('MOD ENV 2 follows env3, and LFO 4 moves', () => {
    const env = posSums({ 'mat1.src': src('MOD ENV 2'), 'mat1.amt': 0.5, 'env3.a': 0.001, 'env3.s': 1 });
    expect(env[env.length - 1]).toBeCloseTo(0.5, 3);
    const lfo = posSums({ 'mat1.src': src('LFO 4'), 'mat1.amt': 0.5, 'lfo4.rate': 5 });
    expect(new Set(lfo.map((v) => v.toFixed(4))).size).toBeGreaterThan(1);
    for (const v of lfo) expect(Math.abs(v)).toBeLessThanOrEqual(0.5 + 1e-9);
  });

  it('RANDOM holds one unipolar value per note', () => {
    const x = posSums({ 'mat1.src': src('RANDOM'), 'mat1.amt': 1 });
    expect(new Set(x.map((v) => v.toFixed(9))).size).toBe(1);
    // 5th modRng draw (gLfo3/4 reset, voice lfo3/4 reset, RANDOM); engine_test pins the same value.
    expect(x[0]).toBeCloseTo(0.8250203728675842, 7);
  });

  it('LFO 3/4 and RANDOM leave the legacy random stream untouched', () => {
    // Unison start phases come from the legacy RNG; an unrouted LFO 3/4 must
    // not change a patch's render.
    const render = (extra: Record<string, number>) => {
      const h = bootWt({ 'oscA.unison': 4, 'lfo1.shape': 4, 'mat1.src': 1, 'mat1.dst': DST_APOS, 'mat1.amt': 0.4, ...extra });
      h.send({ t: 'on', n: 60, v: 1 });
      return Array.from(h.render(16).L);
    };
    expect(render({ 'mat2.src': src('LFO 3'), 'mat2.dst': DST_F1CUT, 'mat2.amt': 0, 'lfo3.shape': 4 })).toEqual(render({}));
  });
});

describe('CURVE envelope model', () => {
  // Telemetry windows are 2048 samples at 48 kHz (42.7 ms); sample the MOD
  // ENV route at known times.
  const envAt = (params: Record<string, number>, windows: number) => {
    const h = bootWt({ 'mat1.src': 3, 'mat1.dst': DST_APOS, 'mat1.amt': 1, ...params });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(windows * 16);
    return modMsgs(h).filter((m) => m.d).map((m) => m.d![DST_APOS]!);
  };
  const T = 2048 / 48000;

  it('a 50% attack curve is linear and ends at the attack time', () => {
    const x = envAt({ 'env2.model': 1, 'env2.a': 1, 'env2.s': 1, 'env2.ac': 0.5 }, 30);
    for (let k = 0; k < 20; k++) expect(x[k]).toBeCloseTo(Math.min(1, ((k + 1) * T)), 2);
  });

  it('decay reaches the sustain level exactly at the decay time, bent by the curve', () => {
    const d = 0.5, c = 0.67, p = Math.pow(2, 17 / 12);
    const x = envAt({ 'env2.model': 1, 'env2.a': 0.001, 'env2.d': d, 'env2.s': 0, 'env2.dc': c }, 16);
    for (let k = 1; k < 16; k++) {
      // Telemetry reads the envelope at the start of each window's last 128-sample
      // chunk; decay starts after the 1 ms attack (48 samples).
      const t = (k + 1) * T - (128 + 48) / 48000;
      expect(x[k]).toBeCloseTo(t >= d ? 0 : Math.pow(1 - t / d, p), 2);
    }
  });

  it('CLASSIC stays the default shape', () => {
    expect(envAt({ 'env2.a': 0.2, 'env2.s': 1 }, 8)).toEqual(envAt({ 'env2.a': 0.2, 'env2.s': 1, 'env2.model': 0 }, 8));
  });
});
