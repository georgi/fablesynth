// Offline verification of what is left of the DR-1 main-thread graph. The FX
// rack moved into the worklet (review W6), so each worklet output is a finished
// bus and this file covers only the wiring around it: the meter tap, the route
// to the device, the parameter plumbing and the reported chain latency. The FX
// themselves are covered by drum-fx.test.ts, which renders real audio.
import { describe, it, expect } from 'vitest';
import { DrumEngine } from './drum-synth';
import { OUT_NAMES, pad } from '../params';

interface MockNode {
  kind: string;
  id: number;
  outs: { target: MockNode; out: number }[];
  [k: string]: unknown;
}

function makeCtx() {
  let next = 0;
  const nodes: MockNode[] = [];
  const mk = (kind: string, extra: Record<string, unknown> = {}): MockNode => {
    const n: MockNode = {
      kind,
      id: next++,
      outs: [],
      connect(t: MockNode, out = 0) {
        n.outs.push({ target: t, out });
        return t;
      },
      disconnect() {
        n.outs.length = 0;
      },
      ...extra,
    };
    nodes.push(n);
    return n;
  };
  const ctx = {
    sampleRate: 48000,
    currentTime: 0,
    destination: mk('destination'),
    nodes,
    createAnalyser: () => mk('analyser', { fftSize: 0 }),
  };
  return ctx;
}

// Boot the output graph without init() — no worklet module, no audio device.
function bootGraph() {
  const eng = new DrumEngine();
  const ctx = makeCtx();
  const sent: { t: string; [k: string]: unknown }[] = [];
  const node = { ...({} as MockNode), outs: [] as { target: MockNode; out: number }[] };
  const mockNode = {
    outs: node.outs,
    connect(t: MockNode, out = 0) { node.outs.push({ target: t, out }); },
    port: { postMessage: (m: unknown) => { sent.push(m as { t: string }); }, onmessage: null },
  };
  // @ts-expect-error — injecting mocks in place of the real WebAudio objects
  eng.ctx = ctx;
  // @ts-expect-error — see above
  eng.node = mockNode;
  eng.ready = true;
  eng.buildOut();
  return { eng, ctx, sent, outs: node.outs };
}

describe('DR-1 output graph', () => {
  it('routes every bus output to the device and to the scope', () => {
    const { eng, ctx, outs } = bootGraph();
    expect(outs.length).toBe(OUT_NAMES.length * 2);
    for (let b = 0; b < OUT_NAMES.length; b++) {
      expect(outs.some((c) => c.out === b && c.target === ctx.destination)).toBe(true);
      const scope = eng.scopeAnalyser as unknown as MockNode;
      expect(outs.some((c) => c.out === b && c.target === scope)).toBe(true);
    }
    // one analyser, nothing else: no gains, no filters, no convolvers
    expect(ctx.nodes.filter((n) => n.kind !== 'destination').map((n) => n.kind)).toEqual(['analyser']);
  });
});

describe('DR-1 parameter plumbing', () => {
  it('sends pad-scoped FX parameters straight to the worklet', () => {
    const { eng, sent } = bootGraph();
    eng.setParam(pad(3, 'fx.reverb.size'), 0.77);
    expect(sent).toEqual([{ t: 'p', k: 'pad3.fx.reverb.size', v: 0.77 }]);
    expect(eng.params[pad(3, 'fx.reverb.size')]).toBe(0.77);
  });

  it('sends the group-strip value without changing any independent pad chain', () => {
    const { eng, sent } = bootGraph();
    const padId = pad(4, 'fx.drive.amt');
    eng.setParam(padId, 0.21);
    sent.length = 0;
    eng.setParam('fx.drive.amt', 0.5);
    expect(sent).toEqual([{ t: 'p', k: 'fx.drive.amt', v: 0.5 }]);
    expect(eng.params['fx.drive.amt']).toBe(0.5);
    expect(eng.params[padId]).toBe(0.21);
  });

  it('sends master.volume and pad.out like any other parameter', () => {
    const { eng, sent } = bootGraph();
    eng.setParam('master.volume', 0.5);
    eng.setParam(pad(2, 'out'), 3);
    expect(sent).toEqual([
      { t: 'p', k: 'master.volume', v: 0.5 },
      { t: 'p', k: 'pad2.out', v: 3 },
    ]);
  });
});
