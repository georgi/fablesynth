import { describe, expect, it } from 'vitest';
import LAB_SRC from './lab-worklet.js?raw';
import fixture from '../../juce/test/fixtures/lab-parity.json';
import { bootWt } from './workletHarness';
import type { ParamValues } from '../params';

// The web LAB (lab-worklet.js) is a port of juce/source/dsp/LabFx.h. The
// native engine_test measures the fidelity properties (tuning, sideband and
// alias rejection, click-free splices) and writes lab-parity.json from a
// deterministic input; this file renders the same input here and requires the
// same output, so every native measurement also holds for the web build.
// Regenerate the fixture with: juce/build/engine_test --write-lab-fixture <path>

interface LabFxLike {
  settings: Record<string, number | boolean>;
  out: number[];
  setParams(s: Record<string, number | boolean>, bpm: number): void;
  processSample(l: number, r: number): void;
}
const LabFx = new Function(`${LAB_SRC}\nreturn LabFx;`)() as new (sr: number) => LabFxLike;

const f32 = Math.fround;
// LabSettings defaults from LabFx.h, as the floats the plugin stores.
const DEFAULTS = {
  crushOn: false, crushBits: 6, crushRate: 6000, crushChaos: f32(0.2), crushMix: 1,
  resoOn: false, resoNote: 48, resoChord: 2, resoDecay: f32(0.7), resoMix: f32(0.5),
  shiftOn: false, shiftHz: 60, shiftFb: f32(0.5), shiftSpread: f32(0.5), shiftMix: f32(0.5),
  sprayOn: false, sprayPitch: 12, sprayDensity: 12, sprayScatter: f32(0.4), sprayMix: f32(0.45),
  glitchOn: false, glitchDiv: 2, glitchChance: f32(0.35), glitchDrift: 0, glitchMix: 1,
};
// labcheck::parityCases() in juce/test/LabFxChecks.h.
const CASES: Record<string, Partial<typeof DEFAULTS>> = {
  crush: { crushOn: true, crushMix: 1, crushBits: 5, crushRate: 4100, crushChaos: f32(0.3) },
  reso: { resoOn: true, resoMix: 1, resoNote: 45, resoDecay: f32(0.85) },
  shift: { shiftOn: true, shiftMix: 1, shiftHz: -70, shiftFb: f32(0.6), shiftSpread: f32(0.7) },
  spray: { sprayOn: true, sprayMix: 1, sprayPitch: 12, sprayDensity: 14, sprayScatter: f32(0.5) },
  glitch: { glitchOn: true, glitchMix: 1, glitchChance: 1, glitchDiv: 2, glitchDrift: f32(-0.5) },
};

// labcheck::parityInput: one xorshift draw per channel per sample, in order.
function makeInput(n: number): Float64Array {
  const x = new Float64Array(n * 2);
  let seed = 12345;
  const noise = () => {
    seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0;
    return (seed >>> 8) / 8388608 - 1;
  };
  for (let i = 0; i < n; i++) {
    for (let ch = 0; ch < 2; ch++) {
      const gate = (i % 12000) < 6000 ? 1 : 0.2;
      const f1 = ch === 0 ? 220 : 221;
      const v = noise();
      x[i * 2 + ch] = gate * (0.25 * Math.sin(2 * Math.PI * f1 * i / 48000) + 0.15 * Math.sin(2 * Math.PI * 1375 * i / 48000)) + 0.05 * v;
    }
  }
  return x;
}

describe('LAB web/native parity', () => {
  const { samples: n, block, bpm, cases } = fixture as unknown as {
    samples: number; block: number; bpm: number; cases: Record<string, { L: number[]; R: number[] }>;
  };
  const input = makeInput(n);

  for (const name of Object.keys(CASES)) {
    it(`${name} matches the plugin's block levels`, () => {
      const lab = new LabFx(48000);
      lab.setParams({ ...DEFAULTS, ...CASES[name] }, bpm);
      const L = new Float32Array(n), R = new Float32Array(n);
      for (let i = 0; i < n; i++) {
        lab.processSample(f32(input[i * 2]), f32(input[i * 2 + 1]));
        L[i] = lab.out[0]; R[i] = lab.out[1];
      }
      for (const [ch, x] of [['L', L], ['R', R]] as const) {
        const ref = cases[name][ch];
        let worst = 0;
        for (let b = 0; b < ref.length; b++) {
          let e = 0;
          const end = Math.min(n, (b + 1) * block);
          for (let i = b * block; i < end; i++) e += x[i] * x[i];
          const rms = Math.sqrt(e / (end - b * block));
          worst = Math.max(worst, Math.abs(rms - ref[b]) / Math.max(ref[b], 1e-4));
        }
        expect(worst, `${name} ${ch} worst relative block-RMS error`).toBeLessThan(1e-3);
      }
    });
  }
});

describe('LAB in the WT-1 worklet chain', () => {
  const TONE: Partial<ParamValues> = {
    'fx.eq.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0, 'fx.delay.on': 0, 'fx.reverb.on': 0,
    'fx.comp.on': 0, 'fx.ott.on': 0, 'filter.on': 0, 'filter2.on': 0, 'oscB.on': 0,
    'env1.a': 0.001, 'env1.s': 1, 'master.volume': 0.4,
  };
  const renderWith = (extra: Partial<ParamValues>) => {
    const h = bootWt({ ...TONE, ...extra });
    h.send({ t: 'on', n: 57, v: 1 });
    return h.render(820).L; // 2.2 s: GLITCH skips its first half-bar window
  };

  it('leaves the signal untouched while every stage is off', () => {
    const a = renderWith({}), b = renderWith({ 'fx.reso.decay': 0.1, 'fx.crush.bits': 2 });
    expect(Array.from(a)).toEqual(Array.from(b));
  });

  it('applies each stage, finite and below the limiter ceiling', () => {
    const dry = renderWith({});
    for (const stage of ['crush', 'reso', 'shift', 'spray', 'glitch']) {
      const wet = renderWith({ [`fx.${stage}.on`]: 1, [`fx.${stage}.mix`]: 1, 'fx.glitch.chance': 1 } as Partial<ParamValues>);
      let diff = 0, peak = 0;
      for (let i = 0; i < wet.length; i++) {
        expect(Number.isFinite(wet[i])).toBe(true);
        diff += (wet[i] - dry[i]) ** 2; peak = Math.max(peak, Math.abs(wet[i]));
      }
      expect(diff / wet.length, `${stage} changes the signal`).toBeGreaterThan(1e-6);
      expect(peak, `${stage} peak`).toBeLessThanOrEqual(0.8912509381337456 + 1e-6);
    }
  });
});
