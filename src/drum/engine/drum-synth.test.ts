import { describe, it, expect } from 'vitest';
import { isFxParam } from './drum-synth';

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
