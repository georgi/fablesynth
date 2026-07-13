// src/drum/engine/workletHarness.ts — evaluate the self-contained worklet
// source in-process so vitest can drive the real DSP offline. Mirrors the
// AudioWorklet contract: constructor gets a port, process(inputs, outputs).
import DRUM_SRC from './worklet-drum.js?raw';

export interface DrumHarness {
  proc: {
    port: { onmessage: ((e: { data: unknown }) => void) | null; postMessage(m: unknown): void };
    process(inputs: unknown[], outputs: Float32Array[][]): boolean;
  };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
  render(blocks: number): { L: Float32Array; R: Float32Array };
}

export function makeDrumProcessor(sampleRate = 48000): DrumHarness {
  const sent: DrumHarness['sent'] = [];
  let Proc: new () => DrumHarness['proc'];
  class AWP {
    port = {
      onmessage: null as DrumHarness['proc']['port']['onmessage'],
      postMessage: (m: unknown) => { sent.push(m as DrumHarness['sent'][number]); },
    };
  }
  const register = (_name: string, cls: new () => DrumHarness['proc']) => { Proc = cls; };
  // The worklet is an ES module only because of Vite's loader; it has no
  // imports/exports, so Function-evaluating its text is safe and exact.
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', DRUM_SRC)(
    sampleRate, AWP, register,
  );
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const render = (blocks: number) => {
    const L = new Float32Array(blocks * 128);
    const R = new Float32Array(blocks * 128);
    for (let b = 0; b < blocks; b++) {
      const outputs = Array.from({ length: 16 }, () => [new Float32Array(128), new Float32Array(128)]);
      proc.process([], outputs);
      for (const [l, r] of outputs) {
        for (let i = 0; i < 128; i++) {
          L[b * 128 + i] += l[i];
          R[b * 128 + i] += r[i];
        }
      }
    }
    return { L, R };
  };
  return { proc, sent, send, render };
}
