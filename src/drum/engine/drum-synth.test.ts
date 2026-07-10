import { describe, it, expect } from 'vitest';
import { isFxParam } from './drum-synth';
import { generateTables } from '../../engine/wavetables';
import { generateDrumTables } from './drumtables';
import { generateSampledDrumTables } from './sampledtables.gen';
import { DRUM_TABLE_NAMES } from '../params';

describe('drum engine param routing', () => {
  it('fx + master.volume stay on the main thread, the rest goes to the worklet', () => {
    expect(isFxParam('fx.drive.amt')).toBe(true);
    expect(isFxParam('fx.comp.thr')).toBe(true);
    expect(isFxParam('master.volume')).toBe(true);
    expect(isFxParam('master.swing')).toBe(false); // worklet needs it for timing
    expect(isFxParam('seq.bpm')).toBe(false);
    expect(isFxParam('pad0.oscA.tune')).toBe(false);
  });
});

describe('drum engine built-in table assembly', () => {
  it('appends the 5 sample-derived tables after drum + synth tables, matching DRUM_TABLE_NAMES', () => {
    const builtInTables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()];
    expect(builtInTables.length).toBe(DRUM_TABLE_NAMES.length);
    expect(builtInTables.length).toBe(15);
    const sampledNames = builtInTables.slice(10, 15).map((t) => t.name);
    expect(sampledNames).toEqual(['808SD', '808CP', '808CH', '808OH', '808CY']);
    expect(sampledNames).toEqual(DRUM_TABLE_NAMES.slice(10));
  });
});
