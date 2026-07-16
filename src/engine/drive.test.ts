import { describe, expect, it } from 'vitest';
import { driveShape, makeDriveCurve } from './drive';

describe('drive FX transfer', () => {
  it('uses the softened saturation coefficient', () => {
    expect(driveShape(0).k).toBe(1);
    expect(driveShape(0.5).k).toBe(7);
    expect(driveShape(1).k).toBe(13);
  });

  it('inversely compensates pre-gain after saturation', () => {
    const shape = driveShape(0.5);
    expect(shape.preGain).toBe(2);
    expect(shape.compensation).toBe(0.5);

    const { curve } = makeDriveCurve(0.5);
    expect(curve[0]).toBeCloseTo(-0.5, 6);
    expect(curve[256]).toBeCloseTo(0, 6);
    expect(curve[512]).toBeCloseTo(0.5, 6);
  });

  it('clamps out-of-range amounts before deriving gain', () => {
    expect(driveShape(-1)).toEqual(driveShape(0));
    expect(driveShape(2)).toEqual(driveShape(1));
  });
});
