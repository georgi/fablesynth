// Node harness for worklet-drum-wasm.js — the same contract as
// workletHarness.ts, with the compiled DR-1 module (juce/source/drum/wasm)
// read from disk.
import WASM_SRC from './worklet-drum-wasm.js?raw';
import { BUS_COUNT, type DrumHarness } from './workletHarness';

// Read the module from disk. The specifier is built at runtime so tsc needs
// no Node types and Vite leaves the .wasm file alone.
const fsName = 'node:' + 'fs';
const { readFileSync } = (await import(/* @vite-ignore */ fsName)) as {
  readFileSync(path: URL): Uint8Array;
  readFileSync(path: URL, enc: 'utf8'): string;
};

// The one-shot bank for the JS worklet's 'samples' message, decoded from the
// WAV files the way kitLevels.generate.test.ts does. The wasm module ignores
// the message: it embeds the same bank (OneShotSamples.gen.cpp).
function decodeWav(buf: Uint8Array): { sampleRate: number; buf: ArrayBuffer } {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  let at = 12, sampleRate = 0, data = new Float32Array(0);
  while (at + 8 <= dv.byteLength) {
    const id = String.fromCharCode(...buf.subarray(at, at + 4)), size = dv.getUint32(at + 4, true), body = at + 8;
    if (id === 'fmt ') sampleRate = dv.getUint32(body + 4, true);
    if (id === 'data') { data = new Float32Array(size / 2); for (let i = 0; i < data.length; i++) data[i] = dv.getInt16(body + i * 2, true) / 32768; }
    at = body + size + (size & 1);
  }
  return { sampleRate, buf: data.buffer };
}
let bank: { sampleRate: number; buf: ArrayBuffer }[] | null = null;
export function samplesMsg() {
  if (!bank) {
    const gen = new URL('./oneshots.gen.ts', import.meta.url);
    bank = [...readFileSync(gen, 'utf8').matchAll(/import sample(\d+)Url from '([^']+)\?url'/g)]
      .sort((a, b) => +a[1] - +b[1]).map((m) => decodeWav(readFileSync(new URL(m[2], gen))));
  }
  return { t: 'samples', list: bank.map((s) => ({ sampleRate: s.sampleRate, buf: s.buf.slice(0) })) };
}
let cached: Uint8Array | null = null;
export const dr1Bytes = (): ArrayBuffer => {
  cached ??= readFileSync(new URL('./wasm/dr1.wasm', import.meta.url));
  return cached.buffer.slice(cached.byteOffset, cached.byteOffset + cached.byteLength) as ArrayBuffer;
};

export type DrumWasmHarness = Omit<DrumHarness, 'latency'> & { sendInit(params: unknown): void };

export function makeDrumWasmProcessor(sampleRate = 48000): DrumWasmHarness {
  const clock = globalThis as unknown as { currentFrame?: number };
  if (clock.currentFrame === undefined) clock.currentFrame = 0;
  const sent: DrumHarness['sent'] = [];
  let Proc: new () => DrumHarness['proc'];
  class AWP {
    port = {
      onmessage: null as DrumHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as DrumHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => DrumHarness['proc']) => { Proc = cls; };
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', WASM_SRC)(sampleRate, AWP, register);
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const sendInit = (params: unknown) => send({ t: 'init', params, wasm: dr1Bytes() });
  const renderBus = (blocks: number, bus: number, blockSize = 128) => {
    const L = new Float32Array(blocks * blockSize), R = new Float32Array(blocks * blockSize);
    const outputs = Array.from({ length: BUS_COUNT }, () => [new Float32Array(blockSize), new Float32Array(blockSize)]);
    for (let b = 0; b < blocks; b++) {
      proc.process([], outputs);
      for (let o = 0; o < BUS_COUNT; o++) {
        if (bus >= 0 && o !== bus) continue;
        const [l, r] = outputs[o];
        for (let i = 0; i < blockSize; i++) { L[b * blockSize + i] += l[i]; R[b * blockSize + i] += r[i]; }
      }
      clock.currentFrame! += blockSize;
    }
    return { L, R };
  };
  const render = (blocks: number, blockSize = 128) => renderBus(blocks, -1, blockSize);
  return { proc, sent, send, sendInit, render, renderBus };
}
