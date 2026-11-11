// Node harness for worklet-bass-wasm.js — the same contract as bassHarness.ts,
// with the compiled BL-1 module (juce/source/bass/wasm) read from disk.
import WASM_SRC from './worklet-bass-wasm.js?raw';
import type { BassHarness } from './bassHarness';

// Read the module from disk. The specifier is built at runtime so tsc needs
// no Node types and Vite leaves the .wasm file alone.
const fsName = 'node:' + 'fs';
const { readFileSync } = (await import(/* @vite-ignore */ fsName)) as { readFileSync(path: URL): Uint8Array };
const bl1Bytes = (): ArrayBuffer => {
  const bytes = readFileSync(new URL('./wasm/bl1.wasm', import.meta.url));
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
};

export function makeBassWasmProcessor(sampleRate = 48000): BassHarness & { sendInit(params: unknown): void } {
  const sent: BassHarness['sent'] = [];
  let Proc: new () => BassHarness['proc'];
  class AWP {
    port = {
      onmessage: null as BassHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as BassHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => BassHarness['proc']) => { Proc = cls; };
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', WASM_SRC)(sampleRate, AWP, register);
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const sendInit = (params: unknown) => {
    send({ t: 'init', params, wasm: bl1Bytes() });
  };
  const render = (blocks: number) => {
    const L = new Float32Array(blocks * 128), R = new Float32Array(blocks * 128);
    const l = new Float32Array(128), r = new Float32Array(128);
    for (let b = 0; b < blocks; b++) {
      proc.process([], [[l, r]]);
      L.set(l, b * 128); R.set(r, b * 128);
    }
    return { L, R };
  };
  return { proc, sent, send, sendInit, render };
}
