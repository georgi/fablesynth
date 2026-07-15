import { describe, it, expect } from 'vitest';
import { FACTORY_PRESETS, resolvePresetMods } from './presets';
import { defaultParams, type ModConnection, type ParamValues } from './params';
import { MOD_MATRIX_SIZE } from './store/slotHelpers';

describe('resolvePresetMods', () => {
  it('keeps every driven factory WT-1 patch at 20% wet', () => {
    for (const preset of FACTORY_PRESETS) {
      if (preset.params['fx.drive.on']) expect(preset.params['fx.drive.mix'], preset.name).toBe(0.2);
    }
  });

  it('factory-style params (only mat1..4 authored) leave mat5..16 zeroed', () => {
    const presetParams: Partial<ParamValues> = {
      'mat1.src': 1, 'mat1.dst': 1, 'mat1.amt': 0.3,
      'mat2.src': 2, 'mat2.dst': 6, 'mat2.amt': 0.25,
    };
    const merged = resolvePresetMods(presetParams);
    expect(merged['mat1.src']).toBe(1);
    expect(merged['mat2.dst']).toBe(6);
    for (let s = 5; s <= MOD_MATRIX_SIZE; s++) {
      expect(merged[`mat${s}.src`]).toBe(0);
      expect(merged[`mat${s}.dst`]).toBe(0);
      expect(merged[`mat${s}.amt`]).toBe(0);
    }
  });

  it('round-trips routes saved in high slots (mat5, mat12) — params-as-truth, no mods[]', () => {
    // Mirrors saveUserPreset: a new-build save persists every routed slot in
    // `params` and carries NO `mods` array. The load path must keep slots 5..16.
    const saved = defaultParams();
    saved['mat5.src'] = 3;
    saved['mat5.dst'] = 4; // PITCH
    saved['mat5.amt'] = 0.6;
    saved['mat12.src'] = 1;
    saved['mat12.dst'] = 9; // F2 CUT
    saved['mat12.amt'] = -0.5;

    const merged = resolvePresetMods(saved, undefined);

    expect(merged['mat5.src']).toBe(3);
    expect(merged['mat5.dst']).toBe(4);
    expect(merged['mat5.amt']).toBe(0.6);
    expect(merged['mat12.src']).toBe(1);
    expect(merged['mat12.dst']).toBe(9);
    expect(merged['mat12.amt']).toBe(-0.5);
  });

  it('expands a legacy explicit mods[] into slots in order, truncating beyond 16', () => {
    const explicit: ModConnection[] = Array.from({ length: 20 }, (_, i) => ({
      src: 1,
      dst: ((i % 10) + 1),
      amt: 0.1,
    }));
    const merged = resolvePresetMods({}, explicit);
    // Slots 1..16 filled in order...
    for (let s = 1; s <= MOD_MATRIX_SIZE; s++) {
      expect(merged[`mat${s}.src`]).toBe(1);
      expect(merged[`mat${s}.dst`]).toBe(((s - 1) % 10) + 1);
      expect(merged[`mat${s}.amt`]).toBe(0.1);
    }
    // ...entries 17..20 dropped (no mat17 param exists at all).
    expect(merged['mat17.src']).toBeUndefined();
  });

  it('zeros trailing slots when explicit mods[] is shorter than 16', () => {
    const explicit: ModConnection[] = [
      { src: 1, dst: 3, amt: 0.4 },
      { src: 2, dst: 5, amt: -0.2 },
    ];
    const merged = resolvePresetMods({}, explicit);
    expect(merged['mat1.src']).toBe(1);
    expect(merged['mat2.dst']).toBe(5);
    for (let s = 3; s <= MOD_MATRIX_SIZE; s++) {
      expect(merged[`mat${s}.src`]).toBe(0);
      expect(merged[`mat${s}.dst`]).toBe(0);
      expect(merged[`mat${s}.amt`]).toBe(0);
    }
  });
});
