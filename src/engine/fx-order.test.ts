import { describe, expect, it, vi } from 'vitest';
import { makeWtProcessor } from './workletHarness';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { makeDrumProcessor } from '../drum/engine/workletHarness';

type Block = Float32Array | Float64Array;
interface Dynamics {
  process(l: Block, r: Block, n: number): void;
  processSample(l: number, r: number): void;
}
interface Guard {
  gainFor(l: number, r: number): number;
}
interface Rack {
  ott: Dynamics;
  comp: Dynamics;
  headroom: Record<string, Guard>;
  driveOff: boolean;
  driveWet: { snap(value: number): void };
  driveDry: { snap(value: number): void };
  driveBlock?: (...args: unknown[]) => void;
  driveSegment?: (...args: unknown[]) => void;
  process(l: Block, r: Block, n: number, live?: boolean): void;
}

const factories = [
  ['WT-1', () => (makeWtProcessor().proc as unknown as { fx: Rack }).fx],
  ['BL-1', () => (makeBassProcessor().proc as unknown as { fx: Rack }).fx],
  ['DR-1', () => (makeDrumProcessor().proc as unknown as { padFx: Rack[] }).padFx[0]],
] as const;

describe.each(factories)('%s FX routing', (_name, create) => {
  it('processes OTT, compressor, then oversampled drive with protection between stages', () => {
    const rack = create();
    const events: string[] = [];
    for (const stage of ['ott', 'comp'] as const) {
      const dynamics = rack[stage];
      for (const method of ['process', 'processSample'] as const) {
        const original = dynamics[method];
        vi.spyOn(dynamics, method).mockImplementation((...args: unknown[]) => {
          events.push(stage);
          return Reflect.apply(original, dynamics, args);
        });
      }
    }
    for (const stage of ['ott', 'comp', 'drive']) {
      const guard = rack.headroom[stage];
      const original = guard.gainFor;
      vi.spyOn(guard, 'gainFor').mockImplementation((l, r) => {
        events.push(`${stage}-guard`);
        return original.call(guard, l, r);
      });
    }
    const driveMethod = rack.driveSegment ? 'driveSegment' : 'driveBlock';
    const drive = rack[driveMethod]!;
    vi.spyOn(rack, driveMethod).mockImplementation((...args: unknown[]) => {
      events.push('drive');
      return Reflect.apply(drive, rack, args);
    });
    rack.driveOff = false;
    rack.driveWet.snap(1);
    rack.driveDry.snap(0);
    const l = Float32Array.from({ length: 128 }, (_, i) => 0.1 * Math.sin(i * 0.1));
    const r = l.map(v => v * 0.8); // Exercise stereo as well as the drive wet path.
    rack.process(l, r, l.length, true);
    expect([...new Set(events)]).toEqual([
      'ott', 'ott-guard', 'comp', 'comp-guard', 'drive', 'drive-guard',
    ]);
    expect(l.every(Number.isFinite) && r.every(Number.isFinite)).toBe(true);
  });
});
