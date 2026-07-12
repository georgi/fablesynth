import { describe, it, expect } from 'vitest';
// Read worklet.js as raw text via Vite's ?raw loader (works in both tsc and the
// vitest/vite transform — no node:fs / @types/node needed). worklet.js is itself
// import-free (loaded into the AudioWorklet via ?url), so we verify its hand-copied
// tables against params.ts by parsing this text.
import WORKLET_SRC from './worklet.js?raw';
import { MOD_DESTS, dstTarget, PARAMS } from '../params';
import {
  ACCENT_VEL, NPATTERNS, PLAIN_VEL, STEP_STRIDE, STEPS, SWING_MAX,
} from '../noteseq';

// worklet.js can't import params.ts (it's loaded into the AudioWorklet via ?url, a
// standalone module with no imports), so it hand-copies three tables from the
// canonical params.ts: MOD_PARAM_INFO (curve+range per modulatable param), the
// DST_TARGET dst->target map, and MOD_LOG_D (the Log curve depth D). These tests
// read worklet.js as TEXT and assert the copies still match the source of truth —
// if either side drifts, the engines would route/scale differently and this fails.

// --- (a) MOD_PARAM_INFO {curve, lo, hi} matches PARAMS[id] for every entry ---
describe('worklet MOD_PARAM_INFO parity', () => {
  // Parse the MOD_PARAM_INFO object literal: lines like
  //   'oscA.pos':       { curve: 'lin', lo: 0, hi: 1 },
  function parseModParamInfo(): Record<string, { curve: string; lo: number; hi: number }> {
    const block = WORKLET_SRC.match(/const MOD_PARAM_INFO = \{([\s\S]*?)\n\};/);
    expect(block, 'MOD_PARAM_INFO block found in worklet.js').not.toBeNull();
    const out: Record<string, { curve: string; lo: number; hi: number }> = {};
    const re = /'([^']+)':\s*\{\s*curve:\s*'(\w+)',\s*lo:\s*(-?[\d.]+),\s*hi:\s*(-?[\d.]+)\s*\}/g;
    let m: RegExpExecArray | null;
    while ((m = re.exec(block![1])) !== null) {
      out[m[1]] = { curve: m[2], lo: Number(m[3]), hi: Number(m[4]) };
    }
    return out;
  }

  const info = parseModParamInfo();

  it('parses all 24 modulatable param entries', () => {
    expect(Object.keys(info)).toHaveLength(24);
  });

  it('every entry matches PARAMS[id] {curve, min, max}', () => {
    for (const [id, w] of Object.entries(info)) {
      const def = PARAMS[id];
      expect(def, `params.ts defines ${id}`).toBeDefined();
      expect(w.curve, `${id}.curve`).toBe(def.curve);
      expect(w.lo, `${id}.lo`).toBe(def.min);
      expect(w.hi, `${id}.hi`).toBe(def.max);
    }
  });
});

// --- (b) DST_TARGET array matches dstTarget() index-for-index ---
describe('worklet DST_TARGET parity', () => {
  // Sentinels used in worklet.js for the three global dests + the unused slot 0.
  const SENTINEL: Record<string, string> = {
    'DST_PITCH': 'pitch',
    'DST_AMP': 'amp',
    'DST_PAN': 'pan',
    'null': 'none', // slot 0 is `null` in the worklet (guarded out)
  };

  function parseDstTarget(): string[] {
    const block = WORKLET_SRC.match(/const DST_TARGET = \[([\s\S]*?)\n\];/);
    expect(block, 'DST_TARGET array found in worklet.js').not.toBeNull();
    const out: string[] = [];
    // each entry is either 'paramId', DST_PITCH/AMP/PAN, or null (slot 0)
    const re = /^\s*(?:'([^']+)'|(DST_PITCH|DST_AMP|DST_PAN|null))\s*,/gm;
    let m: RegExpExecArray | null;
    while ((m = re.exec(block![1])) !== null) {
      // m[1] = a quoted paramId; m[2] = a sentinel/null token -> map to its dstTarget token.
      out.push(m[1] ?? SENTINEL[m[2]]);
    }
    return out;
  }

  const dst = parseDstTarget();

  it('has one entry per MOD_DESTS index', () => {
    expect(dst).toHaveLength(MOD_DESTS.length);
  });

  it('maps each index to the same target as params.ts dstTarget()', () => {
    for (let i = 0; i < MOD_DESTS.length; i++) {
      // worklet slot 0 maps to 'none' (the dst/src guard skips it), same as dstTarget(0).
      expect(dst[i], `DST_TARGET[${i}] (${MOD_DESTS[i]})`).toBe(dstTarget(i));
    }
  });
});

// --- (c) MOD_LOG_D equals the contract D=5 ---
describe('worklet MOD_LOG_D parity', () => {
  it('is D=5 (matches the CUTOFF Log scaling used in the curve-rule tests)', () => {
    const m = WORKLET_SRC.match(/const MOD_LOG_D = (\d+)\s*;/);
    expect(m, 'MOD_LOG_D declared in worklet.js').not.toBeNull();
    expect(Number(m![1])).toBe(5);
  });
});

// --- (d) note-sequencer constants match noteseq.ts ---
describe('worklet SEQ_* parity', () => {
  function seqConst(name: string): number {
    const m = WORKLET_SRC.match(new RegExp(`const ${name} = ([\\d.]+)\\s*;`));
    expect(m, `${name} declared in worklet.js`).not.toBeNull();
    return Number(m![1]);
  }

  it('pattern layout constants match', () => {
    expect(seqConst('SEQ_STEPS')).toBe(STEPS);
    expect(seqConst('SEQ_NPATTERNS')).toBe(NPATTERNS);
    expect(seqConst('SEQ_STRIDE')).toBe(STEP_STRIDE);
  });

  it('velocity + swing constants match', () => {
    expect(seqConst('SEQ_ACCENT_VEL')).toBe(ACCENT_VEL);
    expect(seqConst('SEQ_PLAIN_VEL')).toBe(PLAIN_VEL);
    expect(seqConst('SEQ_SWING_MAX')).toBe(SWING_MAX);
  });
});
