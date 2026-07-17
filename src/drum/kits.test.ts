import { describe, it, expect, beforeEach } from 'vitest';
import { FACTORY_KITS, loadUserKits, saveUserKit, kitToState, stateToKit, type Kit } from './kits';
import { DRUM_PARAMS, defaultDrumParams, pad, PAD_COUNT } from './params';
import { NPATTERNS, STEPS, patIdx } from './seq';

if (typeof localStorage === 'undefined') {
  const store = new Map<string, string>();
  Object.defineProperty(globalThis, 'localStorage', {
    value: {
      get length() { return store.size; },
      clear: () => store.clear(),
      getItem: (key: string) => store.get(key) ?? null,
      key: (index: number) => [...store.keys()][index] ?? null,
      removeItem: (key: string) => store.delete(key),
      setItem: (key: string, value: string) => store.set(key, value),
    } satisfies Storage,
  });
}

describe('kits', () => {
  beforeEach(() => localStorage.clear());

  it('ships 18 distinct factory kits; TR-VOID keeps the mockup names', () => {
    expect(FACTORY_KITS.map((k) => k.name)).toEqual([
      'TR-VOID', 'ROOM ONE', 'BITCRUSH', '808 CLASSIC', 'DEEP DUB', 'DUST HOUSE',
      'WAREHOUSE', 'METAL WORK', 'TAPE KIT', 'MINIMAL', 'BROKEN TOYS', 'LIVE ROOM', 'UZU',
      '808+UZU HYBRID', 'NEON GRID', 'ACID CAVE', 'BOOM BAP', 'PIRATE RADIO',
    ]);
    const tv = FACTORY_KITS[0];
    expect(tv.padNames).toHaveLength(PAD_COUNT);
    expect(tv.padNames[0]).toBe('KICK');
    expect(tv.padNames[2]).toBe('SNARE');
    expect(tv.patterns).toHaveLength(NPATTERNS * PAD_COUNT * STEPS);
    expect(tv.patterns[patIdx(0, 0, 0)]).toBe(2); // kick accent on the one
    const state = kitToState(tv);
    expect(state.params['pad5.ring.mix']).toBeCloseTo(0.18);
    expect(state.params['pad6.ring.mix']).toBeCloseTo(0.28);
    expect(state.params['pad7.ring.mix']).toBeCloseTo(0.46);
    expect(state.params['pad11.ring.mix']).toBeCloseTo(0.62);
    expect(state.params['pad12.ring.freq']).toBe(731);
    expect(state.params['pad12.ring.mix']).toBeCloseTo(0.78);
    expect(state.params['pad13.ring.freq']).toBe(3271);
    expect(state.params['pad13.ring.mix']).toBeCloseTo(0.88);
    expect(state.params['pad0.oscA.tune']).toBe(-26);
    expect(state.params['pad1.oscA.tune']).toBe(-19);
    expect(state.params['pad2.oscA.tune']).toBe(-12);
    expect(state.params['pad12.aenv.dec']).toBeCloseTo(0.16);
    expect(state.params['pad13.aenv.dec']).toBeCloseTo(0.20);
    expect([8, 9, 10].map((i) => state.params[pad(i, 'oscA.tune')])).toEqual([-19, -12, -5]);
    expect([8, 9, 10].map((i) => state.params[pad(i, 'aenv.dec')])).toEqual([0.28, 0.24, 0.20]);

    const classic = kitToState(FACTORY_KITS[3]);
    expect([0, 2, 4, 8, 9, 10, 12, 13, 14].map((i) => classic.params[pad(i, 'oscB.table')]))
      .toEqual([5, 0, 6, 13, 14, 15, 7, 9, 8]);
    expect(classic.params[pad(0, 'oscA.level')]).toBe(0);
    expect(classic.params[pad(0, 'oscB.level')]).toBeCloseTo(0.9);

    const uzu = kitToState(FACTORY_KITS[12]);
    expect(Array.from({ length: PAD_COUNT }, (_, i) => uzu.params[pad(i, 'oscB.table')]))
      .toEqual(Array.from({ length: PAD_COUNT }, (_, i) => 16 + i));
    expect(uzu.params[pad(0, 'oscA.level')]).toBe(0);
    expect(uzu.params[pad(0, 'oscB.level')]).toBeCloseTo(0.92);

    const hybrid = kitToState(FACTORY_KITS[13]);
    expect(hybrid.params[pad(0, 'oscB.table')]).toBe(16);
    expect(hybrid.params[pad(15, 'oscB.table')]).toBe(31);
    expect(hybrid.params[pad(0, 'oscA.level')]).toBeGreaterThan(0);
    expect(hybrid.params[pad(0, 'oscB.level')]).toBeGreaterThan(0);
  });

  it('authored kits mix osc and sample layers with their own grooves', () => {
    const byName = (name: string) => FACTORY_KITS.find((k) => k.name === name)!;

    // NEON GRID layers a THUD osc kick with a sampled UZU BD2 transient and
    // sweeps its noise pad's band-pass with the mod env.
    const neon = kitToState(byName('NEON GRID'));
    expect(neon.params[pad(0, 'oscA.tune')]).toBe(-22);
    expect(neon.params[pad(0, 'oscB.table')]).toBe(17);
    expect(neon.params[pad(0, 'oscB.level')]).toBeCloseTo(0.55);
    expect(neon.params[pad(15, 'noise.level')]).toBeCloseTo(0.85);
    expect(neon.params[pad(15, 'mod1.dst')]).toBe(4); // CUTOFF

    // ACID CAVE's blips squelch: resonant LP24 driven hard by the mod env.
    const acid = kitToState(byName('ACID CAVE'));
    for (const i of [8, 9, 10]) {
      expect(acid.params[pad(i, 'flt.type')]).toBe(1);
      expect(acid.params[pad(i, 'flt.res')]).toBeCloseTo(0.72);
      expect(acid.params[pad(i, 'mod1.src')]).toBe(1); // MOD ENV
      expect(acid.params[pad(i, 'mod1.dst')]).toBe(4);
    }
    expect(acid.params[pad(1, 'fx.reverb.mix')]).toBeCloseTo(0.55); // rumble pad

    // BOOM BAP is sample-forward, lo-fi capped, with a reversed UZU MOD pad.
    const bap = kitToState(byName('BOOM BAP'));
    expect(bap.params[pad(0, 'oscB.table')]).toBe(16);
    expect(bap.params[pad(0, 'oscA.level')]).toBeCloseTo(0.5); // THUD glue kept
    expect(bap.params[pad(15, 'oscB.phase')]).toBe(1); // reverse on
    expect(bap.params[pad(5, 'flt.cut')]).toBe(8500);

    // PIRATE RADIO swings hard and jitters its vox chop pitch per hit.
    const pirate = kitToState(byName('PIRATE RADIO'));
    expect(pirate.params['master.swing']).toBeCloseTo(0.58);
    expect(pirate.params[pad(14, 'mod1.src')]).toBe(3); // RAND
    expect(pirate.params[pad(14, 'mod1.dst')]).toBe(5); // PITCH

    // Each authored kit ships its own groove (not the TR-VOID pattern) as a
    // 4-bar A A A B loop: slots 1-3 repeat the groove, slot 4 is the fill.
    for (const name of ['NEON GRID', 'ACID CAVE', 'BOOM BAP', 'PIRATE RADIO']) {
      const kit = byName(name);
      expect(kit.patterns, name).not.toEqual(FACTORY_KITS[0].patterns);
      const slot = (p: number) => kit.patterns.slice(patIdx(p, 0, 0), patIdx(p + 1, 0, 0));
      expect(slot(1), name).toEqual(slot(0));
      expect(slot(2), name).toEqual(slot(0));
      expect(slot(3).some((v) => v > 0), name).toBe(true);
      expect(slot(3), name).not.toEqual(slot(0));
      expect(kit.chain, name).toEqual([0, 1, 2, 3]);
      expect(kit.padNames, name).toHaveLength(PAD_COUNT);
    }
  });

  it('every factory kit param id exists in DRUM_PARAMS and is in range', () => {
    for (const k of FACTORY_KITS) {
      for (const [id, v] of Object.entries(k.params)) {
        // Factory data deliberately retains v1 global FX IDs to exercise the
        // same migration path as saved user kits.
        const d = DRUM_PARAMS[id] ?? (id.startsWith('fx.') ? DRUM_PARAMS[pad(0, id)] : undefined);
        expect(d, `${k.name}: ${id}`).toBeDefined();
        if (d.min !== undefined) { expect(v).toBeGreaterThanOrEqual(d.min!); expect(v).toBeLessThanOrEqual(d.max!); }
      }
    }
  });

  it('keeps every driven factory kit at 20% wet', () => {
    for (const kit of FACTORY_KITS) {
      if (kit.params['fx.drive.on']) expect(kit.params['fx.drive.mix'], kit.name).toBe(0.2);
    }
  });

  it('broadcasts legacy global FX to every pad without overwriting new pad-scoped values', () => {
    const legacy: Kit = {
      ...FACTORY_KITS[0],
      params: {
        ...FACTORY_KITS[0].params,
        'fx.delay.mix': 0.73,
        'pad4.fx.delay.mix': 0.21,
      },
    };
    const state = kitToState(legacy);
    expect(state.params['fx.delay.mix']).toBeUndefined();
    expect(state.params['pad0.fx.delay.mix']).toBeCloseTo(0.73);
    expect(state.params['pad15.fx.delay.mix']).toBeCloseTo(0.73);
    expect(state.params['pad4.fx.delay.mix']).toBeCloseTo(0.21);
  });

  it('user kit save/load round-trip preserves everything', () => {
    const params = defaultDrumParams();
    params['pad3.oscA.tune'] = -7;
    const patterns = new Uint8Array(NPATTERNS * PAD_COUNT * STEPS);
    patterns[patIdx(2, 3, 5)] = 2;
    const kit = stateToKit('MY KIT', params, FACTORY_KITS[0].padNames, patterns, [0, 2], []);
    saveUserKit('MY KIT', kit);
    const loaded = loadUserKits();
    expect(loaded).toHaveLength(1);
    const st = kitToState(loaded[0]);
    expect(st.params['pad3.oscA.tune']).toBe(-7);
    expect(st.patterns[patIdx(2, 3, 5)]).toBe(2);
    expect(st.chain).toEqual([0, 2]);
  });

  it('saving under an existing user name overwrites, not duplicates', () => {
    const kit: Kit = stateToKit('X', defaultDrumParams(), FACTORY_KITS[0].padNames, new Uint8Array(NPATTERNS * PAD_COUNT * STEPS), [0], []);
    saveUserKit('X', kit);
    saveUserKit('X', kit);
    expect(loadUserKits()).toHaveLength(1);
  });
});
