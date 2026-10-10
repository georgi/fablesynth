import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import { gainRippleDb, inharmonicDb } from './driveCheck';
import type { ParamValues } from '../params';

// The in-voice drive saturator runs at 2x (upsample -> ADAA tanh -> downsample)
// and the stage always sits in the filter path, so its dry and driven paths
// share one delay. Native twin: the "WT-1 drive" block in juce/test/engine_test.cpp.
const FX_OFF: Partial<ParamValues> = {
  'fx.eq.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0,
  'fx.delay.on': 0, 'fx.reverb.on': 0, 'fx.comp.on': 0,
  'master.volume': 0.4,
};

function tone(sr: number, note: number, ftype: number, cut: number, drive: number, level: number): Float32Array {
  const h = bootWt({
    ...FX_OFF,
    'oscA.on': 1, 'oscA.table': 0, 'oscA.pos': 0.8, 'oscA.unison': 1, 'oscA.level': level,
    'oscA.detune': 0, 'oscA.spread': 0, 'oscA.pan': 0,
    'oscB.on': 0, 'sub.on': 0, 'noise.on': 0,
    'filter.on': 1, 'filter.type': ftype, 'filter.cutoff': cut, 'filter.res': 0.3, 'filter.drive': drive,
    'filter.env': 0, 'filter.key': 0, 'filter2.on': 0,
    'env1.a': 0.001, 'env1.d': 0.005, 'env1.s': 1, 'env1.r': 0.1,
  }, sr);
  h.send({ t: 'on', n: note, v: 1 });
  const n = 32768, skip = 4096;
  const { L } = h.render(Math.ceil((n + skip) / 128));
  return L.slice(skip, skip + n);
}

describe('WT-1 drive oversampling', () => {
  const f0 = 440 * 2 ** ((72 - 69) / 12);
  it.each([
    [48000, 0, 20000, -80, 'LP12 open'], [48000, 3, 300, -80, 'HP12'],
    [44100, 0, 20000, -76, 'LP12 open'], [44100, 3, 300, -76, 'HP12'],
  ])('keeps drive-1 alias below the limit @ %i Hz (%i %i %i %s)', (sr, ftype, cut, limit) => {
    expect(inharmonicDb(tone(sr, 72, ftype, cut, 1, 1), sr, f0)).toBeLessThan(limit);
  });

  it.each([48000, 44100])('adds no comb filtering at half drive mix @ %i Hz', (sr) => {
    const off = tone(sr, 48, 0, 20000, 0, 0.02);
    const on = tone(sr, 48, 0, 20000, 0.05, 0.02);
    expect(gainRippleDb(off, on, sr, 440 * 2 ** ((48 - 69) / 12))).toBeLessThan(0.1);
  });
});
