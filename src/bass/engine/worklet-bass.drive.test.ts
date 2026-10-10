import { describe, expect, it } from 'vitest';
import { makeBassProcessor } from './bassHarness';
import { generateTables } from '../../engine/wavetables';
import { gainRippleDb, inharmonicDb } from '../../engine/driveCheck';
import { defaultBassParams } from '../params';

// The in-voice drive saturator runs at 2x (upsample -> ADAA tanh -> downsample)
// and the stage always sits in the filter path, so its dry and driven paths
// share one delay. Native twin: the "BL-1 drive" block in juce/test/bass_engine_test.cpp.
const tables = generateTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};
const FX_OFF = {
  'fx.drive.on': 0, 'fx.comp.on': 0, 'fx.ott.on': 0,
  'fx.chorus.on': 0, 'fx.delay.on': 0, 'fx.reverb.on': 0,
};

function tone(sr: number, semi: number, ftype: number, cut: number, drive: number, level: number, uni = 1): Float32Array {
  const p = defaultBassParams();
  Object.assign(p, {
    'osc.table': 0, 'osc.pos': 0.8, 'osc.level': level, 'sub.level': 0,
    'osc.unison': uni, 'osc.spread': 0, 'osc.detune': 0,
    'flt.type': ftype, 'flt.cut': cut, 'flt.res': 0.3, 'flt.drive': drive, 'flt.env': 0, 'flt.track': 0,
    'lfo.depth': 0, 'aenv.att': 0.001, 'aenv.sus': 1,
  });
  const h = makeBassProcessor(sr);
  h.send({ t: 'init', params: { ...p, ...FX_OFF } });
  h.send(tableMsg);
  h.send({ t: 'noteon', semi, vel: 1 });
  const n = 32768, skip = 4096;
  return h.render(Math.ceil((n + skip) / 128)).L.slice(skip, skip + n);
}

describe('BL-1 drive oversampling', () => {
  const f0 = 440 * 2 ** ((72 - 69) / 12);
  it.each([
    [48000, 0, 20000, 1, -80, 'LP12 open'], [48000, 3, 300, 1, -80, 'HP12'],
    [44100, 0, 20000, 1, -76, 'LP12 open'], [44100, 3, 300, 1, -76, 'HP12'],
    [48000, 0, 20000, 5, -68, 'LP12 open, 5 coherent unison voices'],
  ])('keeps drive-1 alias below the limit @ %i Hz (%i %i %i %i %s)', (sr, ftype, cut, uni, limit) => {
    expect(inharmonicDb(tone(sr, 36, ftype, cut, 1, 1, uni), sr, f0)).toBeLessThan(limit);
  });

  it.each([48000, 44100])('adds no comb filtering at half drive mix @ %i Hz', (sr) => {
    const off = tone(sr, 12, 0, 20000, 0, 0.02);
    const on = tone(sr, 12, 0, 20000, 0.05, 0.02);
    expect(gainRippleDb(off, on, sr, 440 * 2 ** ((48 - 69) / 12))).toBeLessThan(0.1);
  });
});
