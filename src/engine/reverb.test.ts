import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import type { ReverbMessage } from './reverb';

const BASE = {
  'fx.eq.on': 0, 'fx.ott.on': 0, 'fx.comp.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0, 'fx.delay.on': 0,
  'fx.reverb.on': 1, 'fx.reverb.size': 0.6, 'fx.reverb.mix': 0.7,
  'oscA.unison': 1, 'filter.on': 0, 'filter2.on': 0, 'env1.a': 0.001, 'env1.s': 1, 'env1.r': 0.005,
};
const messages = (h: ReturnType<typeof bootWt>) => h.sent.filter(m => m.t === 'reverb') as unknown as ReverbMessage[];
const last = (h: ReturnType<typeof bootWt>) => messages(h).slice(-1)[0];

describe('WT-1 live reverb return', () => {
  it('is opt-in and sample-identical with metering enabled', () => {
    const plain = bootWt(BASE); plain.send({ t: 'on', n: 60, v: 0.8 });
    const reference = plain.render(250);
    expect(messages(plain)).toHaveLength(0);
    const h = bootWt(BASE); h.send({ t: 'reverb', on: true }); h.send({ t: 'on', n: 60, v: 0.8 });
    const actual = h.render(250);
    expect(actual.L).toEqual(reference.L); expect(actual.R).toEqual(reference.R);
    expect(messages(h).length).toBeGreaterThan(10);
    expect(messages(h).some(m => m.left > -60 && m.right > -60)).toBe(true);
    expect(messages(h).every(m => Number.isFinite(m.correlation) && Math.abs(m.correlation) <= 1)).toBe(true);
    const count = messages(h).length;
    h.send({ t: 'reverb', on: false }); h.render(30);
    expect(messages(h)).toHaveLength(count);
  });

  it('measures the lingering stereo tail after note release, then its decay', () => {
    const h = bootWt(BASE); h.send({ t: 'reverb', on: true }); h.send({ t: 'on', n: 60, v: 0.8 }); h.render(250);
    h.send({ t: 'off', n: 60 }); h.render(80);
    const early = last(h);
    expect(early.left).toBeGreaterThan(-65); expect(early.right).toBeGreaterThan(-65);
    h.render(1800);
    expect(last(h).left).toBeLessThan(early.left - 15);
    expect(last(h).right).toBeLessThan(early.right - 15);
  });

  it.each(['fx.reverb.mix', 'fx.reverb.on'])('reports a silent return when %s is zero, including with dry audio playing', key => {
    const h = bootWt(BASE); h.send({ t: 'reverb', on: true }); h.send({ t: 'on', n: 60, v: 0.8 }); h.render(200);
    expect(last(h).left).toBeGreaterThan(-60);
    h.send({ t: 'p', k: key, v: 0 }); const dry = h.render(300);
    expect(dry.L.some(v => Math.abs(v) > 0.01)).toBe(true);
    expect(last(h)).toEqual({ t: 'reverb', left: -90, right: -90, correlation: 0 });
  });

  it('clears the meter window and tail on panic', () => {
    const h = bootWt(BASE); h.send({ t: 'reverb', on: true }); h.send({ t: 'on', n: 60, v: 0.8 }); h.render(200);
    h.send({ t: 'panic' }); const silent = h.render(40);
    expect(silent.L.every(v => v === 0) && silent.R.every(v => v === 0)).toBe(true);
    expect(last(h)).toEqual({ t: 'reverb', left: -90, right: -90, correlation: 0 });
  });
});
