// src/engine/workletHarness.ts — evaluate the self-contained worklet
// source in-process so vitest can drive the real DSP offline. Mirrors the
// AudioWorklet contract: constructor gets a port, process(inputs, outputs).
import WT_SRC from './worklet.js?raw';
import OTT_SRC from './ott-worklet.js?raw';
import { generateTables } from './wavetables';
import { defaultParams, type ParamValues } from '../params';

export interface WtHarness {
  proc: {
    port: { onmessage: ((e: { data: unknown }) => void) | null; postMessage(m: unknown): void };
    process(inputs: unknown[], outputs: Float32Array[][]): boolean;
  };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
  /** Render `blocks` host blocks of `size` samples each (default 128). */
  render(blocks: number, size?: number): { L: Float32Array; R: Float32Array };
  /** Position the emulated AudioWorklet `currentFrame` clock (hosted transport tests). */
  setFrame(frame: number): void;
}

export function makeWtProcessor(sampleRate = 48000): WtHarness {
  const sent: WtHarness['sent'] = [];
  let Proc: new () => WtHarness['proc'];
  class AWP {
    port = {
      onmessage: null as WtHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as WtHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => WtHarness['proc']) => { Proc = cls; };
  // The worklet is an ES module only because of Vite's loader; it has no
  // imports/exports, so Function-evaluating its text is safe and exact.
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', OTT_SRC)(
    sampleRate, AWP, register,
  );
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', WT_SRC)(
    sampleRate, AWP, register,
  );
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  // AudioWorkletGlobalScope exposes a live `currentFrame` global; mirror it so
  // hosted-transport paths (clip scheduling, anchor-locked synced LFOs) run
  // under vitest. The getter reads this harness's counter — create/render one
  // harness at a time, as the global can only track the most recent harness.
  let frame = 0;
  Object.defineProperty(globalThis, 'currentFrame', {
    configurable: true,
    get: () => frame,
    set: (f: number) => { frame = f; }, // tests may also position the clock directly
  });
  // The two block buffers are hoisted (finding W7): allocating them per block
  // put GC pressure inside the measured region of the render benchmark.
  let l = new Float32Array(128), r = new Float32Array(128);
  const render = (blocks: number, size = 128) => {
    if (l.length !== size) { l = new Float32Array(size); r = new Float32Array(size); }
    const L = new Float32Array(blocks * size);
    const R = new Float32Array(blocks * size);
    const out = [[l, r]];
    for (let b = 0; b < blocks; b++) {
      proc.process([], out);
      frame += size;
      L.set(l, b * size); R.set(r, b * size);
    }
    return { L, R };
  };
  return { proc, sent, send, render, setFrame: (f: number) => { frame = f; } };
}

const TABLES = generateTables();
const tableMsg = {
  t: 'tables',
  list: TABLES.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

export function bootWt(params: Partial<ParamValues> = {}, sampleRate = 48000): WtHarness {
  const h = makeWtProcessor(sampleRate);
  h.send({ t: 'init', params: { ...defaultParams(), ...params } });
  h.send(tableMsg);
  return h;
}
