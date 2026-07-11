import { describe, it, expect } from 'vitest';
import { BASS_PARAM_DEFS, BASS_PARAMS, defaultBassParams, BASS_FILTER_TYPES, SUB_SHAPES } from './params';
import { LFO_DIVS, LFO_SHAPES } from '../params';

describe('bass params', () => {
  it('ids are unique', () => {
    const ids = BASS_PARAM_DEFS.map((d) => d.id);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it('defaults are complete and within range', () => {
    const defs = defaultBassParams();
    for (const d of BASS_PARAM_DEFS) {
      expect(defs[d.id]).toBe(d.def);
      if (d.options) {
        expect(d.def).toBeGreaterThanOrEqual(0);
        expect(d.def).toBeLessThan(d.options.length);
      } else if (d.type !== 'bool') {
        expect(d.def).toBeGreaterThanOrEqual(d.min as number);
        expect(d.def).toBeLessThanOrEqual(d.max as number);
      }
    }
  });

  it('log-curve params have positive min', () => {
    for (const d of BASS_PARAM_DEFS) {
      if (d.curve === 'log') expect(d.min as number).toBeGreaterThan(0);
    }
  });

  it('design defaults: LP24 filter at 340 Hz, dotted-eighth LFO, 138 BPM', () => {
    expect(BASS_FILTER_TYPES[BASS_PARAMS['flt.type'].def]).toBe('LP 24');
    expect(BASS_PARAMS['flt.cut'].def).toBe(340);
    expect(LFO_DIVS[BASS_PARAMS['lfo.rate'].def]).toBe('1/8.');
    expect(LFO_SHAPES[BASS_PARAMS['lfo.shape'].def]).toBe('SINE');
    expect(BASS_PARAMS['seq.bpm'].def).toBe(138);
    expect(SUB_SHAPES[BASS_PARAMS['sub.shape'].def]).toBe('SINE');
    expect(BASS_PARAMS['sub.oct'].def).toBe(-1);
  });
});
