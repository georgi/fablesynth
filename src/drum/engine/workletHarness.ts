// src/drum/engine/workletHarness.ts — evaluate the self-contained worklet
// source in-process so vitest can drive the real DSP offline. Mirrors the
// AudioWorklet contract: constructor gets a port, process(inputs, outputs).
import DRUM_SRC from './worklet-drum.js?raw';
import OTT_SRC from '../../engine/ott-worklet.js?raw';

export interface DrumHarness {
  proc: {
    port: { onmessage: ((e: { data: unknown }) => void) | null; postMessage(m: unknown): void };
    process(inputs: unknown[], outputs: Float32Array[][]): boolean;
  };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
  // Sum of every output bus, the mix a listener hears.
  render(blocks: number): { L: Float32Array; R: Float32Array };
  // One named bus, for routing and per-bus ceiling checks.
  renderBus(blocks: number, bus: number): { L: Float32Array; R: Float32Array };
  // Chain latency the worklet reports at construction (99 samples at 48 kHz).
  latency: number;
}

// One stereo output per OUT_NAMES entry, since the FX rack moved into the
// worklet (review W6): the pads sum onto these five buses inside it.
export const BUS_COUNT = 5;

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
  // Load the shared OTT component before the processor, just as init() does.
  // Both modules have no imports/exports and can be evaluated verbatim.
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', OTT_SRC + '\n' + DRUM_SRC)(
    sampleRate, AWP, register,
  );
  const proc = new Proc!();
  const send = (msg: unknown) => proc.port.onmessage!({ data: msg });
  const renderBus = (blocks: number, bus: number) => {
    const L = new Float32Array(blocks * 128);
    const R = new Float32Array(blocks * 128);
    for (let b = 0; b < blocks; b++) {
      const outputs = Array.from({ length: BUS_COUNT }, () => [new Float32Array(128), new Float32Array(128)]);
      proc.process([], outputs);
      for (let o = 0; o < BUS_COUNT; o++) {
        if (bus >= 0 && o !== bus) continue;
        const [l, r] = outputs[o];
        for (let i = 0; i < 128; i++) {
          L[b * 128 + i] += l[i];
          R[b * 128 + i] += r[i];
        }
      }
    }
    return { L, R };
  };
  const render = (blocks: number) => renderBus(blocks, -1);
  const latency = (sent.find((m) => m.t === 'latency')?.n as number) ?? 0;
  return { proc, sent, send, render, renderBus, latency };
}
