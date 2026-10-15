import { describe, expect, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { generateDrumTables } from './drumtables';
import { generateSampledDrumTables } from './sampledtables.gen';
import { generateTables } from '../../engine/wavetables';
import { gainRippleDb, inharmonicDb } from '../../engine/driveCheck';
import { defaultDrumParams, pad } from '../params';

// The per-pad drive saturator runs at 2x (upsample -> ADAA tanh -> downsample)
// and the stage always sits in the filter path, so its dry and driven paths
// share one delay. Native twin: checkDriveOversampling in juce/test/drum_engine_test.cpp.
const tables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()];
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

function tone(sr: number, table: number, ftype: number, cut: number, drive: number, level: number, f0: number): Float32Array {
  const p = defaultDrumParams();
  const S = (k: string, v: number) => { p[pad(0, k)] = v; };
  S('oscA.table', table); S('oscA.pos', table === 4 ? 0.8 : 0.5); S('oscA.fine', 0); S('oscA.phase', 0);
  S('oscA.unison', 1); S('oscA.detune', 0); S('oscA.level', level);
  S('oscB.level', 0); S('noise.level', 0); S('ring.mix', 0); S('penv.amt', 0);
  S('aenv.att', 0.0005); S('aenv.hold', 0.25); S('aenv.dec', 4); S('aenv.curve', 0); S('lvl', 1); S('pan', 0);
  S('flt.on', 1); S('flt.type', ftype); S('flt.res', 0.3); S('flt.drive', drive); S('flt.cut', cut);
  S('oscA.tune', Math.round(69 + 12 * Math.log2(f0 / 440) - 60));
  const h = makeDrumProcessor(sr);
  h.send({ t: 'init', params: p });
  h.send(tableMsg);
  h.send({ t: 'trig', pad: 0, v: 1 });
  const n = 32768, skip = 16384; // past the 0.25 s hold, so the window sees only the smooth decay
  return h.render(Math.ceil((n + skip) / 128)).L.slice(skip, skip + n);
}

// Steady saw (harmonics below 4 kHz, like a PRIME table) through the real per-pad filter section, fed in the
// 32-sample chunks the voice uses. A drum hit decays, and that envelope puts
// skirts around every harmonic that a stationary alias metric cannot tell from
// fold-back, so the alias checks drive the filter directly.
interface FilterProc {
  voices: { f: unknown }[];
  padIds: Int32Array[];
  setupFilter(fs: unknown, f: Int32Array, mCut: number, mRes: number, count: number): void;
  runFilter(fs: unknown, inL: Float32Array, inR: Float32Array, outL: Float32Array, outR: Float32Array,
    drive: number, off: number, n: number): void;
}

function filteredSaw(sr: number, ftype: number, cut: number, drive: number, f0: number): Float32Array {
  const p = defaultDrumParams();
  p[pad(0, 'flt.on')] = 1; p[pad(0, 'flt.type')] = ftype; p[pad(0, 'flt.res')] = 0.3;
  p[pad(0, 'flt.cut')] = cut; p[pad(0, 'flt.drive')] = drive;
  const h = makeDrumProcessor(sr);
  h.send({ t: 'init', params: p });
  const proc = h.proc as unknown as FilterProc;
  const n = 32768 + 4096;
  const x = new Float32Array(n);
  for (let k = 1; k * f0 < 4000; k++) {
    const w = (2 * Math.PI * k * f0) / sr;
    for (let i = 0; i < n; i++) x[i] += (0.55 / k) * Math.sin(w * i);
  }
  const out = new Float32Array(n), outR = new Float32Array(n);
  for (let at = 0; at < n; at += 32) {
    proc.setupFilter(proc.voices[0].f, proc.padIds[0], 0, 0, 32);
    proc.runFilter(proc.voices[0].f, x, x, out, outR, drive, at, 32);
  }
  return out.slice(4096);
}

describe('DR-1 drive oversampling', () => {
  it.each([
    [48000, 0, 20000, -80, 'LP12 open'], [48000, 3, 300, -80, 'HP12'],
    [44100, 0, 20000, -76, 'LP12 open'], [44100, 3, 300, -76, 'HP12'],
    [48000, 1, 20000, -76, 'LP24 open'],
  ])('keeps drive-1 alias below the limit @ %i Hz (%i %i %i %s)', (sr, ftype, cut, limit) => {
    expect(inharmonicDb(filteredSaw(sr, ftype, cut, 1, 523.25), sr, 523.25)).toBeLessThan(limit);
  });

  it.each([48000, 44100])('adds no comb filtering at half drive mix @ %i Hz', (sr) => {
    const off = tone(sr, 4, 0, 20000, 0, 0.02, 130.81);
    const on = tone(sr, 4, 0, 20000, 0.05, 0.02, 130.81);
    expect(gainRippleDb(off, on, sr, 130.81)).toBeLessThan(0.1);
  });
});
