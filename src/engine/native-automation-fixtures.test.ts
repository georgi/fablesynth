// Web-derived compiler goldens and actual worklet traces for the native tests.
// Refresh intentionally: UPDATE_AUTO_FIXTURES=1 npm test -- src/engine/native-automation-fixtures.test.ts
// @ts-expect-error Test-only Node IO; the application has no Node typings.
import { readFileSync, writeFileSync } from 'node:fs';
declare const process: { env: Record<string, string | undefined> };
import { expect, it } from 'vitest';
import { compileAutomation, paintCell, drawLine, addPoint, movePoint, bendSegment, removePoint, tidyPoints, type AutoPoint, type AutoLane } from '../seq/clipAutomation';
import type { MachineId } from '../seq/protocol';
import type { DrumRhythm } from '../drum/rhythm';
import { makeWtProcessor } from './workletHarness';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { makeDrumProcessor } from '../drum/engine/workletHarness';
import { defaultParams } from '../params';
import { defaultBassParams } from '../bass/params';
import { defaultDrumParams } from '../drum/params';

const rhythm: DrumRhythm = { v: 1, lanes: Array.from({ length: 16 }, (_, i) => i === 5
  ? { enabled: true, sourceBar: 0, steps: 3, rotation: 1, timing: { mode: 'fit', cycleBeats: 8 } } : null) };
const cases: { machine: MachineId; bars: number; automation: AutoLane[]; drumRhythm?: DrumRhythm }[] = [
  { machine: 'WT1', bars: 2, automation: [
    { target: 'filter.cutoff', enabled: true, time: { mode: 'grid', steps: 7 }, points: [{ t: 0, v: .1, c: .6 }, { t: 3, v: .6, c: -.5 }, { t: 6, v: .3 }] },
    { target: 'fx.delay.mix', enabled: true, time: { mode: 'clip' }, points: [{ t: 0, v: 0, hold: true }, { t: 4, v: .75, hold: true }, { t: 8, v: 0, hold: true }] },
    { target: 'filter.cutoff', enabled: true, time: { mode: 'clip' }, points: [{ t: 0, v: 1 }] }, // first target wins
  ] },
  { machine: 'BL1', bars: 2, automation: [
    { target: 'flt.cut', enabled: true, time: { mode: 'fit', steps: 3, cycleBeats: 8 }, points: [{ t: 0, v: .2, hold: true }, { t: 1, v: .6, hold: true }, { t: 2, v: .4, hold: true }] },
    { target: 'osc.pos', enabled: false, time: { mode: 'grid', steps: 3 }, points: [{ t: 0, v: 1 }] },
  ] },
  { machine: 'DR1', bars: 2, drumRhythm: rhythm, automation: [
    { target: 'pad5.aenv.dec', enabled: true, time: { mode: 'pad' }, points: [{ t: .25, v: .1 }, { t: 1.5, v: .6 }, { t: 2.75, v: .2, c: -.8 }] },
    { target: 'fx.reverb.mix', enabled: true, time: { mode: 'grid', steps: 5 }, points: [{ t: 0, v: .1, c: .8 }, { t: 2, v: .5 }] },
    { target: 'pad0.lvl', enabled: true, time: { mode: 'pad' }, points: [{ t: 0, v: .3 }, { t: 16, v: .7 }] }, // PAD falls back to CLIP
  ] },
];

const editBase: AutoPoint[] = [{ t: 0, v: .15, c: .5 }, { t: 2, v: .7, hold: true }, { t: 4, v: .3 }, { t: 7, v: .5 }];
const editCases = [
  { op: 'paint', points: editBase, len: 8, args: [1.4, .8, 1], result: paintCell(editBase, 8, 1.4, .8, 1) },
  { op: 'paint', points: [], len: 8, args: [7.95, .4, .25], result: paintCell([], 8, 7.95, .4, .25) },
  { op: 'paint', points: [{ t: 16, v: .9 }], len: 8, args: [2, .5, 1], result: paintCell([{ t: 16, v: .9 }], 8, 2, .5, 1) },
  { op: 'line', points: editBase, len: 8, args: [5.2, .1, 1.1, .9], result: drawLine(editBase, 8, 5.2, .1, 1.1, .9) },
  { op: 'line', points: editBase, len: 8, args: [2, .4, 2, .4], result: drawLine(editBase, 8, 2, .4, 2, .4) },
  { op: 'add', points: editBase, len: 8, args: [2, .1], result: addPoint(editBase, 2, .1).points },
  { op: 'move', points: editBase, len: 8, args: [1, 6, 1.2], result: movePoint(editBase, 8, 1, 6, 1.2) },
  { op: 'bend', points: editBase, len: 8, args: [1, -1.5], result: bendSegment(editBase, 1, -1.5) },
  { op: 'remove', points: editBase, len: 8, args: [0], result: removePoint(editBase, 0) },
  { op: 'tidy', points: [{ t: 1, v: .2 }, { t: 1, v: .5 }, { t: 1, v: .8 }, { t: 0, v: .1, hold: true }], len: 8, args: [],
    result: tidyPoints([{ t: 1, v: .2 }, { t: 1, v: .5 }, { t: 1, v: .8 }, { t: 0, v: .1, hold: true }]) },
];

it('keeps native compiler tables and 128-sample traces in sync with the web', () => {
  const fixtures = cases.map((c, i) => {
    const lanes = compileAutomation(c.automation, c.bars, c.drumRhythm, c.machine);
    const h = c.machine === 'WT1' ? makeWtProcessor() : c.machine === 'BL1' ? makeBassProcessor() : makeDrumProcessor();
    h.send({ t: 'init', params: c.machine === 'WT1' ? defaultParams() : c.machine === 'BL1' ? defaultBassParams() : defaultDrumParams() });
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: .35, anchor: 256 });
    h.send({ t: 'clip', data: new Uint8Array(4096), bars: c.bars, rhythm: c.drumRhythm, atFrame: 256 });
    h.send({ t: 'auto', lanes });
    // AudioWorklets run on 128-sample quanta; native tests use 512-sample blocks.
    const proc = h.proc as unknown as { autoTick(frame: number): void; autoHeld: Map<string | number, number>; p: Record<string | number, number>; pv?: Record<string | number, number> };
    const original = proc.autoTick.bind(proc);
    const trace: { frame: number; values: number[] }[] = [];
    proc.autoTick = frame => {
      original(frame);
      if (proc.autoHeld.size && frame % 128 === 0 && trace[trace.length - 1]?.frame !== frame) trace.push({ frame, values: [...proc.autoHeld.keys()].map(id => (proc.pv ?? proc.p)[id]) });
    };
    const outputs = [[new Float32Array(128), new Float32Array(128)]];
    for (let frame = 0; frame < 384512; frame += 128) {
      Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: frame });
      h.proc.process([], outputs);
    }
    expect(trace.every(t => t.values.every(Number.isFinite))).toBe(true);
    return { ...c, ...(i === 0 ? { edits: editCases } : {}), tables: lanes.map(l => ({ ...l, table: Array.from(l.table) })), trace };
  });
  const path = 'juce/test/fixtures/web-automation-tables.json';
  const json = JSON.stringify(fixtures) + '\n';
  if (process.env.UPDATE_AUTO_FIXTURES === '1') writeFileSync(path, json);
  expect(readFileSync(path, 'utf8')).toBe(json);
});
