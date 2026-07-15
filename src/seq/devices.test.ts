import { describe, expect, it } from 'vitest';
import { factoryPatchNames, stepFactoryPatchIndex } from './devices';

describe('SQ-4 device patch selection', () => {
  it('exposes a populated factory bank for every device', () => {
    for (const machine of ['DR1', 'BL1', 'WT1'] as const) {
      expect(factoryPatchNames(machine).length).toBeGreaterThan(1);
      expect(factoryPatchNames(machine).every(Boolean)).toBe(true);
    }
  });

  it('steps, wraps, and enters a factory bank from a custom patch', () => {
    const last = factoryPatchNames('BL1').length - 1;
    expect(stepFactoryPatchIndex('BL1', 0, -1)).toBe(last);
    expect(stepFactoryPatchIndex('BL1', last, 1)).toBe(0);
    expect(stepFactoryPatchIndex('BL1', -1, 1)).toBe(0);
    expect(stepFactoryPatchIndex('BL1', -1, -1)).toBe(last);
  });
});
