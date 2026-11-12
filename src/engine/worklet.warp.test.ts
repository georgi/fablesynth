import { describe, expect, it } from 'vitest';
import { MOD_DESTS, WARP_MODES, type ParamValues } from '../params';
import { bootWt } from './workletHarness';
import { generateTables } from './wavetables';

// Oscillator warp. The tests read the rendered signal: harmonic magnitudes of
// a steady A4 (440 Hz, an exact bin of the half-second analysis window), and
// the energy that lies between the harmonics, which is aliasing.

const SR = 48000;
// 0.6 s = 264 cycles of A4. A warped waveform has a DC offset; the skip lets
// the DC blockers settle, because their decay is not a harmonic.
const SKIP = 28800;
const WIN = 24000;   // 0.5 s = 220 cycles of A4
const BLOCKS = Math.ceil((SKIP + WIN) / 128);
const W = (name: string) => WARP_MODES.indexOf(name);

// A dry oscillator path: no filter, no leveling compressor, full sustain.
const DRY: Partial<ParamValues> = {
  'filter.on': 0, 'fx.comp.on': 0, 'env1.a': 0.001, 'env1.s': 1,
  'oscA.table': 0, 'oscA.pos': 0, 'oscA.level': 0.5,
};

function renderNote(params: Partial<ParamValues>, note = 69): Float32Array {
  const h = bootWt({ ...DRY, ...params });
  h.send({ t: 'on', n: note, v: 1 });
  return h.render(BLOCKS).L.subarray(SKIP, SKIP + WIN);
}
/** The first `blocks` blocks of a note, for the checks that need no spectrum. */
function renderStart(params: Partial<ParamValues>, note: number, blocks: number): Float32Array {
  const h = bootWt({ ...DRY, ...params });
  h.send({ t: 'on', n: note, v: 1 });
  return h.render(blocks).L;
}

/** Magnitude of the component at `hz` (a whole number of cycles per window). */
function magAt(x: Float32Array, hz: number): number {
  const w = (2 * Math.PI * hz) / SR;
  let re = 0, im = 0;
  for (let i = 0; i < x.length; i++) { re += x[i]! * Math.cos(w * i); im -= x[i]! * Math.sin(w * i); }
  return (2 * Math.hypot(re, im)) / x.length;
}

function harmonics(x: Float32Array, f0: number, count: number): number[] {
  return Array.from({ length: count }, (_, k) => magAt(x, f0 * (k + 1)));
}

/** Share of the signal power that is not on a harmonic of f0, in dB. */
function inharmonicDb(x: Float32Array, f0: number): number {
  let total = 0, mean = 0;
  for (const v of x) mean += v;
  mean /= x.length;
  for (const v of x) total += (v - mean) * (v - mean);
  total /= x.length;
  let harm = 0;
  for (let k = 1; k * f0 < SR / 2; k++) { const m = magAt(x, k * f0); harm += (m * m) / 2; }
  return 10 * Math.log10(Math.max(1e-12, (total - harm) / total));
}

function centroid(x: Float32Array, f0: number): number {
  let num = 0, den = 0;
  for (let k = 1; k * f0 < SR / 2; k++) { const m = magAt(x, k * f0); num += k * m * m; den += m * m; }
  return num / den;
}

const maxDiff = (a: Float32Array, b: Float32Array) => a.reduce((m, v, i) => Math.max(m, Math.abs(v - b[i]!)), 0);
const peak = (x: Float32Array) => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

describe('warp: neutral settings', () => {
  const off = renderNote({ 'oscA.pos': 0.6 });

  it('the amount has no effect while the mode is OFF', () => {
    expect(maxDiff(renderNote({ 'oscA.pos': 0.6, 'oscA.warpamt': 0.8 }), off)).toBe(0);
  });

  it.each([['BEND', 0.5], ['ASYM', 0.5], ['PWM', 0], ['SYNC', 0]] as const)('%s at %f keeps the waveform', (mode, amt) => {
    const x = renderNote({ 'oscA.pos': 0.6, 'oscA.warp': W(mode), 'oscA.warpamt': amt });
    expect(maxDiff(x, off)).toBeLessThan(2e-3);
  });

  it.each(['FM', 'AM', 'RM'])('%s at zero keeps the spectrum', (mode) => {
    const x = renderNote({ 'oscA.pos': 0.6, 'oscA.warp': W(mode), 'oscA.warpamt': 0, 'oscB.on': 1, 'oscB.level': 0 });
    const a = harmonics(x, 440, 8), b = harmonics(off, 440, 8);
    a.forEach((m, k) => expect(m).toBeCloseTo(b[k]!, 4));
  });
});

describe('warp: phase maps', () => {
  it('SYNC adds upper harmonics that rise with the amount', () => {
    const c = [0, 0.3, 0.6, 0.9].map((amt) => centroid(renderNote({ 'oscA.warp': W('SYNC'), 'oscA.warpamt': amt }), 440));
    expect(c[0]).toBeCloseTo(1, 1); // a sine
    for (let i = 1; i < c.length; i++) expect(c[i]!).toBeGreaterThan(c[i - 1]! + 0.5);
  });

  it('SYNC WIN reaches zero at the cycle edge and stays band-limited', () => {
    const x = renderNote({ 'oscA.pos': 0.66, 'oscA.warp': W('SYNC WIN'), 'oscA.warpamt': 0.55 });
    expect(inharmonicDb(x, 440)).toBeLessThan(-60);
    expect(centroid(x, 440)).toBeGreaterThan(3);
  });

  it('hard SYNC of a saw keeps aliasing low', () => {
    // Non-integer slave ratios reset mid-cycle; a plain reset step measures
    // -23 to -33 dB here.
    for (const amt of [0.2, 0.35, 0.62, 0.9]) {
      const x = renderNote({ 'oscA.pos': 0.66, 'oscA.warp': W('SYNC'), 'oscA.warpamt': amt });
      expect(inharmonicDb(x, 440), `amount ${amt}`).toBeLessThan(-50);
    }
  });

  it('hard SYNC holds the slave below a quarter of the sample rate', () => {
    // A6 at the top amount: the slave stops at 12 kHz in place of 28 kHz.
    const x = renderNote({ 'oscA.pos': 0.66, 'oscA.warp': W('SYNC'), 'oscA.warpamt': 1 }, 93);
    expect(inharmonicDb(x, 440 * 4)).toBeLessThan(-40);
  });

  it.each(['BEND', 'ASYM', 'PWM', 'MIRROR'])('%s reshapes a sine without aliasing', (mode) => {
    for (const amt of [0.1, 0.9]) {
      const x = renderNote({ 'oscA.warp': W(mode), 'oscA.warpamt': amt });
      const h = harmonics(x, 440, 6);
      expect(Math.max(...h.slice(1)), `${mode} ${amt} adds harmonics`).toBeGreaterThan(0.02 * h[0]!);
      // The map's slope changes at the cycle wrap: a kink, not a step.
      expect(inharmonicDb(x, 440), `${mode} ${amt}`).toBeLessThan(-38);
    }
  });

  it('QUANTIZE steps the waveform', () => {
    const x = renderNote({ 'oscA.warp': W('QUANTIZE'), 'oscA.warpamt': 0.75 }, 45); // A2, long cycle
    // Held samples differ only by the DC blockers' droop.
    let flat = 0;
    const tol = 2e-3 * peak(x);
    for (let i = 1; i < x.length; i++) if (Math.abs(x[i]! - x[i - 1]!) < tol) flat++;
    expect(flat / x.length).toBeGreaterThan(0.9);
  });
});

describe('warp: FM, AM and RM from the other oscillator', () => {
  const B_SINE: Partial<ParamValues> = { 'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0, 'oscB.level': 0 };

  it('FM builds sidebands with the amount, with osc B at level 0', () => {
    const lo = harmonics(renderNote({ ...B_SINE, 'oscA.warp': W('FM'), 'oscA.warpamt': 0.3 }), 440, 8);
    const hi = harmonics(renderNote({ ...B_SINE, 'oscA.warp': W('FM'), 'oscA.warpamt': 0.8 }), 440, 8);
    const upper = (h: number[]) => h.slice(2).reduce((s, m) => s + m * m, 0);
    expect(upper(lo)).toBeGreaterThan(1e-6);
    expect(upper(hi)).toBeGreaterThan(10 * upper(lo));
  });

  it('FM matches the phase-modulation series of two sines', () => {
    // Index = amount^2 * 2 cycles * the modulator peak; the 1:1 series of
    // sin(x + I sin x) has harmonic n = |J(n-1)(I) + (-1)^n J(n+1)(I)|.
    const t = generateTables()[0]!;
    const sinePeak = t.data.subarray(0, t.size).reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    const index = 0.5 * 0.5 * 2 * 2 * Math.PI * sinePeak;
    const bessel = (n: number, x: number) => {
      let sum = 0, term = Math.pow(x / 2, n);
      for (let k = 1; k <= n; k++) term /= k;
      for (let k = 0; k < 30; k++) { sum += term; term *= -(x * x) / 4 / ((k + 1) * (k + 1 + n)); }
      return sum;
    };
    const h = harmonics(renderNote({ ...B_SINE, 'oscA.warp': W('FM'), 'oscA.warpamt': 0.5 }), 440, 5);
    const carrier = harmonics(renderNote({}), 440, 1)[0]!;
    [1, 2, 3, 4, 5].forEach((n) => {
      const want = Math.abs(bessel(n - 1, index) + (n % 2 ? -1 : 1) * bessel(n + 1, index));
      expect(h[n - 1]! / carrier, `harmonic ${n}`).toBeCloseTo(want, 2);
    });
  });

  it('FM gives every note the same timbre', () => {
    const h = bootWt({ ...DRY, ...B_SINE, 'oscA.warp': W('FM'), 'oscA.warpamt': 0.6, 'env1.r': 0.01 });
    const take = () => {
      h.send({ t: 'on', n: 69, v: 1 });
      const x = h.render(BLOCKS).L.subarray(SKIP, SKIP + WIN);
      h.send({ t: 'off', n: 69 });
      h.render(100);
      return harmonics(x, 440, 6);
    };
    const a = take(), b = take();
    a.forEach((m, k) => expect(b[k]!).toBeCloseTo(m, 4));
  });

  it('the modulator follows osc B tuning', () => {
    // An octave-up modulator moves the 1:1 sidebands to odd harmonics only.
    const h = harmonics(renderNote({ ...B_SINE, 'oscB.oct': 1, 'oscA.warp': W('FM'), 'oscA.warpamt': 0.5 }), 440, 5);
    expect(h[1]!).toBeLessThan(0.02 * h[0]!);
    expect(h[2]!).toBeGreaterThan(0.2 * h[0]!);
  });

  it('RM of two sines an octave apart leaves equal first and third harmonics', () => {
    const h = harmonics(renderNote({ ...B_SINE, 'oscB.oct': 1, 'oscA.warp': W('RM'), 'oscA.warpamt': 1 }), 440, 4);
    expect(h[2]! / h[0]!).toBeCloseTo(1, 1);
    expect(h[1]!).toBeLessThan(0.02 * h[0]!);
  });

  it('AM keeps the carrier and adds the sidebands', () => {
    const h = harmonics(renderNote({ ...B_SINE, 'oscB.oct': 1, 'oscA.warp': W('AM'), 'oscA.warpamt': 1 }), 440, 4);
    expect(h[2]! / h[0]!).toBeGreaterThan(0.2);
    expect(h[2]! / h[0]!).toBeLessThan(0.8);
  });

  it('is off when osc B is off', () => {
    const x = harmonics(renderNote({ 'oscA.warp': W('FM'), 'oscA.warpamt': 0.8 }), 440, 4);
    expect(x[1]!).toBeLessThan(1e-3 * x[0]!);
  });

  it('both oscillators can modulate each other', () => {
    const x = renderNote({ 'oscB.on': 1, 'oscB.table': 0, 'oscB.level': 0.5, 'oscB.semi': 7,
      'oscA.warp': W('FM'), 'oscA.warpamt': 0.6, 'oscB.warp': W('FM'), 'oscB.warpamt': 0.6 });
    expect(x.every(Number.isFinite)).toBe(true);
    expect(peak(x)).toBeGreaterThan(0.05);
  });
});

describe('warp: modulation and safety', () => {
  it('A WARP is a modulation destination', () => {
    const direct = renderNote({ 'oscA.pos': 0.66, 'oscA.warp': W('SYNC'), 'oscA.warpamt': 0.5 });
    const routed = renderNote({ 'oscA.pos': 0.66, 'oscA.warp': W('SYNC'), 'oscA.warpamt': 0,
      'macro1': 0.5, 'mat1.src': 9, 'mat1.dst': MOD_DESTS.indexOf('A WARP'), 'mat1.amt': 1 });
    expect(maxDiff(direct, routed)).toBeLessThan(1e-6);
  });

  it('a swept amount has no step at the chunk rate', () => {
    // LFO -> WARP on a sine: the map parameter ramps per sample. The steepest
    // slope of the warped sine is 0.11 of the peak per sample here; a
    // parameter that stepped once per chunk would jump by half the peak.
    const x = renderNote({ 'oscA.warp': W('ASYM'), 'oscA.warpamt': 0.5,
      'lfo1.rate': 8, 'mat1.src': 1, 'mat1.dst': MOD_DESTS.indexOf('A WARP'), 'mat1.amt': 0.5 }, 45);
    let step = 0;
    for (let i = 1; i < x.length; i++) step = Math.max(step, Math.abs(x[i]! - x[i - 1]!));
    expect(step).toBeLessThan(0.2 * peak(x));
  });

  it('every mode stays finite and bounded at the extremes', () => {
    for (let mode = 1; mode < WARP_MODES.length; mode++) {
      for (const amt of [0, 1]) {
        for (const note of [24, 108]) {
          const x = renderStart({ 'oscA.pos': 0.66, 'oscA.unison': 16, 'oscA.detune': 1,
            'oscB.on': 1, 'oscB.table': 5, 'oscB.pos': 1, 'oscB.oct': 3,
            'oscA.warp': mode, 'oscA.warpamt': amt, 'oscB.warp': mode, 'oscB.warpamt': amt }, note, 40);
          expect(x.every(Number.isFinite), `${WARP_MODES[mode]} ${amt} note ${note}`).toBe(true);
          expect(peak(x), `${WARP_MODES[mode]} ${amt} note ${note}`).toBeLessThan(1.5);
        }
      }
    }
  });
});
