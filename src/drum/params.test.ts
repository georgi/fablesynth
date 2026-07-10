import { describe, it, expect } from 'vitest';
import {
  PAD_COUNT, MIDI_BASE, pad, DRUM_PARAM_DEFS, DRUM_PARAMS, defaultDrumParams,
  DRUM_TABLE_NAMES, DMOD_SOURCES, DMOD_DESTS, PAD_FIELDS,
} from './params';

describe('drum params', () => {
  it('defines 16 pads from MIDI 36', () => {
    expect(PAD_COUNT).toBe(16);
    expect(MIDI_BASE).toBe(36);
    expect(pad(2, 'oscA.tune')).toBe('pad2.oscA.tune');
  });

  it('every pad has the full field set with sane defs', () => {
    for (let i = 0; i < PAD_COUNT; i++) {
      for (const f of PAD_FIELDS) {
        const d = DRUM_PARAMS[pad(i, f)];
        expect(d, `${pad(i, f)} defined`).toBeDefined();
        expect(Number.isFinite(d.def)).toBe(true);
      }
    }
    // spot-check ranges off the spec
    expect(DRUM_PARAMS['pad0.oscA.tune']).toMatchObject({ min: -48, max: 48, curve: 'int' });
    expect(DRUM_PARAMS['pad0.aenv.dec']).toMatchObject({ min: 0.005, max: 4, curve: 'log' });
    expect(DRUM_PARAMS['pad0.flt.cut']).toMatchObject({ min: 20, max: 20000, curve: 'log', def: 1800 });
    expect(DRUM_PARAMS['pad15.modenv.dec'].def).toBeCloseTo(0.084);
    expect(DRUM_PARAMS['pad0.choke']).toMatchObject({ min: 0, max: 4, curve: 'int' });
  });

  it('globals: bpm/swing/volume/fx', () => {
    expect(DRUM_PARAMS['seq.bpm']).toMatchObject({ min: 60, max: 200, def: 126, curve: 'int' });
    expect(DRUM_PARAMS['master.swing'].def).toBeCloseTo(0.22);
    expect(DRUM_PARAMS['master.volume'].def).toBeCloseTo(0.78);
    expect(DRUM_PARAMS['fx.comp.thr']).toMatchObject({ min: -40, max: 0, def: -16 });
    expect(DRUM_PARAMS['fx.comp.on'].def).toBe(1);
    expect(DRUM_PARAMS['fx.reverb.on'].def).toBe(1);
    expect(DRUM_PARAMS['fx.drive.on'].def).toBe(0);
  });

  it('defaults map covers every def and enums line up', () => {
    const dp = defaultDrumParams();
    expect(Object.keys(dp).length).toBe(DRUM_PARAM_DEFS.length);
    expect(DRUM_TABLE_NAMES.slice(0, 4)).toEqual(['THUD', 'CRACK', 'TINE', 'GRIT']);
    expect(DMOD_SOURCES).toHaveLength(4);
    expect(DMOD_DESTS).toHaveLength(10);
  });
});
