// src/engine/workletHarness.ts — evaluate the self-contained worklet
// source in-process so vitest can drive the real DSP offline. Mirrors the
// AudioWorklet contract: constructor gets a port, process(inputs, outputs).
import WT_SRC from './worklet.js?raw';
import { generateTables } from './wavetables';
import { defaultParams, type ParamValues } from '../params';

export interface WtHarness {
  proc: {
    port: { onmessage: ((e: { data: unknown }) => void) | null; postMessage(m: unknown): void };
    process(inputs: unknown[], outputs: Float32Array[][]): boolean;
  };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
  render(blocks: number): { L: Float32Array; R: Float32Array };
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
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', WT_SRC)(
    sampleRate, AWP, register,
  );
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const render = (blocks: number) => {
    const L = new Float32Array(blocks * 128);
    const R = new Float32Array(blocks * 128);
    for (let b = 0; b < blocks; b++) {
      const l = new Float32Array(128), r = new Float32Array(128);
      proc.process([], [[l, r]]);
      L.set(l, b * 128); R.set(r, b * 128);
    }
    return { L, R };
  };
  return { proc, sent, send, render };
}

const TABLES = generateTables();
const tableMsg = {
  t: 'tables',
  list: TABLES.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

export function bootWt(params: Partial<ParamValues> = {}): WtHarness {
  const h = makeWtProcessor();
  h.send({ t: 'init', params: { ...defaultParams(), ...params } });
  h.send(tableMsg);
  return h;
}
