import { describe, expect, it } from 'vitest';
import { MOD_DESTS } from '../params';
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
