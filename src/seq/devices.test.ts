import { describe, expect, it } from 'vitest';
import { factoryPatchNames, patchBaseIndex, patchName, stepFactoryPatchIndex } from './devices';

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

  it('keeps the factory origin name with a dirty marker after inline edits', () => {
    const name = factoryPatchNames('BL1')[2];
    // A clean factory patch shows its plain name.
    expect(patchName('BL1', { kind: 'factory', index: 2 })).toBe(name);
    // An inline edit that remembers its base keeps the name, marked dirty.
    expect(patchName('BL1', { kind: 'inline', data: {}, base: 2 })).toBe(`${name} *`);
    // An origin-less inline patch is genuinely CUSTOM.
    expect(patchName('BL1', { kind: 'inline', data: {} })).toBe('CUSTOM');
  });

  it('resolves the factory index used for selection and stepping', () => {
    expect(patchBaseIndex({ kind: 'factory', index: 4 })).toBe(4);
    expect(patchBaseIndex({ kind: 'inline', data: {}, base: 4 })).toBe(4);
    expect(patchBaseIndex({ kind: 'inline', data: {} })).toBe(-1);
  });
});
