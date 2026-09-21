import { describe, expect, it } from 'vitest';
import shared from '../engine/ott-worklet.js?raw';
import source from './master-worklet.js?raw';

interface MasterProcess {
  process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean;
  port: { onmessage: ((event: { data: any }) => void) | null };
}

function boot(sampleRate = 48000): MasterProcess {
  let Processor!: new () => MasterProcess;
  class WorkletBase {
    port = {
      onmessage: null as ((event: { data: any }) => void) | null,
      postMessage: (_message: any) => {},
    };
  }
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor',
    `${shared}\n${source}\nreturn MasterProcessor;`
  )(sampleRate, WorkletBase, (_name: string, ctor: new () => MasterProcess) => { Processor = ctor; });
  return new Processor();
}

function send(proc: MasterProcess, data: any): void {
  proc.port.onmessage?.({ data });
}

function render(proc: MasterProcess, input: Float32Array): Float32Array {
  const left = new Float32Array(input.length), right = new Float32Array(input.length);
  proc.process([[input, input]], [[left, right]]);
  return left;
}

const inactiveStages = {
  'master.fx.eq.on': 0,
  'master.fx.ott.on': 0,
  'master.fx.comp.on': 0,
};

describe('SQ-4 master output ceiling', () => {
  it('applies the selected limiter ceiling after the legacy output stage', () => {
    const proc = boot();
    send(proc, {
      t: 'params',
      params: { ...inactiveStages, 'master.fx.limiter.on': 1, 'master.fx.limiter.ceiling': -12 },
    });
    // Let both control smoothers reach the requested settings before the test
    // impulse enters the lookahead queue.
    render(proc, new Float32Array(2400));
    const impulse = new Float32Array(256);
    impulse[0] = 2;
    const out = render(proc, impulse);
    const peak = Math.max(...out.slice(0, 200).map(Math.abs));
    expect(peak).toBeLessThanOrEqual(Math.pow(10, -12 / 20) + 1e-5);
  });
});
