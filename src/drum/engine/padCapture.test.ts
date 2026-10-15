// Kit auto-mix capture: while on, the worklet posts each live pad's post-FX
// mono signal in fixed blocks; silent pads post nothing.
import { describe, expect, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { generateDrumTables } from './drumtables';
import { defaultDrumParams } from '../params';

const tables = generateDrumTables();

describe('DR-1 pad capture', () => {
  it('streams only the pads that sound, and only while on', () => {
    const h = makeDrumProcessor(48000);
    h.send({ t: 'init', params: defaultDrumParams() });
    h.send({ t: 'tables', list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })) });
    const posted = () => h.sent.filter((m) => (m as { t?: string }).t === 'padAudio') as unknown as { pad: number; frames: number; data: Float32Array }[];
    h.send({ t: 'trig', pad: 2, v: 1 }); h.render(2000);
    expect(posted()).toHaveLength(0);
    h.send({ t: 'padCapture', on: true });
    h.send({ t: 'trig', pad: 0, v: 1 }); h.render(80);
    const got = posted();
    expect(got.length).toBeGreaterThan(0);
    expect(new Set(got.map((m) => m.pad))).toEqual(new Set([0]));
    expect(got[0].data.length).toBe(4096);
    expect(got[0].data.some((v) => Math.abs(v) > 1e-3)).toBe(true);
    expect(got[got.length - 1].frames).toBeLessThanOrEqual(80 * 128);
    h.send({ t: 'padCapture', on: false });
    const n = posted().length;
    h.send({ t: 'trig', pad: 0, v: 1 }); h.render(80);
    expect(posted()).toHaveLength(n);
  });
});
