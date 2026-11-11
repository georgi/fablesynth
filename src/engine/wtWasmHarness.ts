// Node harness for worklet-wasm.js — the same contract as workletHarness.ts,
// with the compiled WT-1 module (juce/source/dsp/wasm) read from disk.
import WASM_SRC from './worklet-wasm.js?raw';
import { generateTables } from './wavetables';
import { defaultParams, type ParamValues } from '../params';
import type { WtHarness } from './workletHarness';

// Read the module from disk. The specifier is built at runtime so tsc needs
// no Node types and Vite leaves the .wasm file alone.
const fsName = 'node:' + 'fs';
const { readFileSync } = (await import(/* @vite-ignore */ fsName)) as { readFileSync(path: URL): Uint8Array };
const wt1Bytes = (): ArrayBuffer => {
  const bytes = readFileSync(new URL('./wasm/wt1.wasm', import.meta.url));
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
};

export type WtWasmHarness = WtHarness & { sendInit(params: unknown): void };

export function makeWtWasmProcessor(sampleRate = 48000): WtWasmHarness {
  const sent: WtHarness['sent'] = [];
  let Proc: new () => WtHarness['proc'];
  class AWP {
    port = {
      onmessage: null as WtHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as WtHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => WtHarness['proc']) => { Proc = cls; };
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', WASM_SRC)(sampleRate, AWP, register);
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const sendInit = (params: unknown) => send({ t: 'init', params, wasm: wt1Bytes() });
  let l = new Float32Array(128), r = new Float32Array(128);
  const render = (blocks: number, size = 128) => {
    if (l.length !== size) { l = new Float32Array(size); r = new Float32Array(size); }
    const L = new Float32Array(blocks * size), R = new Float32Array(blocks * size);
    for (let b = 0; b < blocks; b++) {
      proc.process([], [[l, r]]);
      L.set(l, b * size); R.set(r, b * size);
    }
    return { L, R };
  };
  // The wasm worklet reads no currentFrame except for the 'anchor' reply.
  return { proc, sent, send, sendInit, render, setFrame: () => {} };
}

const TABLES = generateTables();
export const wtTableMsg = () => ({
  t: 'tables',
  list: TABLES.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});

/** bootWt() for the wasm engine. */
export function bootWtWasm(params: Partial<ParamValues> = {}, sampleRate = 48000): WtWasmHarness {
  const h = makeWtWasmProcessor(sampleRate);
  h.sendInit({ ...defaultParams(), ...params });
  h.send(wtTableMsg());
  return h;
}
