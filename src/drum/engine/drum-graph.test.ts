// Offline verification of the DR-1 WebAudio FX graph (five output buses + the
// shared reverb pool). vitest has no AudioContext, so the graph is built
// against a mock that records every connect/disconnect. This covers the one
// part of drum-synth.ts that renders no audio of its own and is therefore
// invisible to the worklet harness tests.
import { describe, it, expect } from 'vitest';
import { DrumEngine } from './drum-synth';
import { OUT_NAMES, PAD_COUNT, pad } from '../params';

interface MockNode {
  kind: string;
  id: number;
  outs: MockNode[];
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
      connect(t: MockNode) {
        n.outs.push(t);
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
  const param = () => ({ value: 0, setTargetAtTime() {}, setValueAtTime() {}, linearRampToValueAtTime() {} });
  const ctx = {
    sampleRate: 48000,
    currentTime: 0,
    destination: mk('destination'),
    nodes,
    createGain: () => mk('gain', { gain: param() }),
    createBiquadFilter: () => mk('biquad', { type: '', frequency: param(), Q: param() }),
    createDynamicsCompressor: () =>
      mk('comp', {
        threshold: param(), knee: param(), ratio: param(), attack: param(), release: param(),
      }),
    createAnalyser: () => mk('analyser', { fftSize: 0 }),
    createConvolver: () => mk('convolver', { buffer: null }),
    createDelay: () => mk('delay', { delayTime: param() }),
    createWaveShaper: () => mk('shaper', { curve: null, oversample: '' }),
    createChannelMerger: () => mk('merger'),
    createOscillator: () => mk('osc', { type: '', frequency: param(), start() {} }),
    createBuffer: (ch: number, len: number) => ({
      numberOfChannels: ch,
      length: len,
      getChannelData: () => new Float32Array(len),
    }),
  };
  return ctx;
}

// Build the FX graph without init() — no worklet module, no audio device.
function bootGraph() {
  const eng = new DrumEngine();
  const ctx = makeCtx();
  // @ts-expect-error — injecting the mock context in place of a real one
  eng.ctx = ctx;
  eng.ready = true;
  eng.buildFx();
  return { eng, ctx };
}

const convolvers = (ctx: ReturnType<typeof makeCtx>) => ctx.nodes.filter((n) => n.kind === 'convolver');

// The engine is typed against the real WebAudio interfaces; the mock adds an
// `outs` list. One cast keeps the assertions readable.
const m = (n: unknown) => n as MockNode;

// Convolvers a pad actually sends into. The pool never evicts, so a bucket that
// was used and then abandoned leaves a node behind with nothing connected to
// its input — silent, but not "in use". CPU tracks this count, not the pool.
const liveConvolvers = (eng: DrumEngine) => new Set(eng.fxChains.flatMap((c) => m(c.verbSend).outs));

describe('DR-1 output buses', () => {
  it('builds one gain/DC/limiter strip per OUT_NAMES entry', () => {
    const { eng } = bootGraph();
    expect(eng.buses.length).toBe(OUT_NAMES.length);
    for (const bus of eng.buses) {
      // gain -> dc -> limiter -> destination, and gain -> analyser
      expect(m(bus.gain).outs).toContain(bus.dc);
      expect(m(bus.dc).outs).toContain(bus.limiter);
      expect(m(bus.limiter).outs.length).toBe(1);
      expect(bus.dc.type).toBe('highpass');
      expect(bus.limiter.ratio.value).toBe(14);
    }
  });

  it('routes every pad to MAIN by default, and follows pad.out', () => {
    const { eng } = bootGraph();
    for (let i = 0; i < PAD_COUNT; i++) {
      expect(m(eng.fxChains[i].verbDry).outs).toEqual([eng.buses[0].gain]);
    }
    eng.params[pad(3, 'out')] = 2;
    eng.routePad(3);
    expect(m(eng.fxChains[3].verbDry).outs).toEqual([eng.buses[2].gain]);
    // the other pads are untouched
    expect(m(eng.fxChains[4].verbDry).outs).toEqual([eng.buses[0].gain]);
  });

  it('clamps an out-of-range pad.out instead of dropping the pad', () => {
    const { eng } = bootGraph();
    eng.params[pad(0, 'out')] = 99;
    eng.routePad(0);
    expect(m(eng.fxChains[0].verbDry).outs).toEqual([eng.buses[OUT_NAMES.length - 1].gain]);
    eng.params[pad(0, 'out')] = -5;
    eng.routePad(0);
    expect(m(eng.fxChains[0].verbDry).outs).toEqual([eng.buses[0].gain]);
  });
});

describe('DR-1 shared reverb pool', () => {
  it('runs one convolver for the default kit — 16 pads, one bus, one size', () => {
    const { eng, ctx } = bootGraph();
    // as built by buildFx(), with no parameter changes at all
    expect(liveConvolvers(eng).size).toBe(1);
    expect(convolvers(ctx).length).toBe(1);
    const conv = convolvers(ctx)[0];
    for (let i = 0; i < PAD_COUNT; i++) expect(m(eng.fxChains[i].verbSend).outs).toEqual([conv]);
  });

  it('adds a convolver per distinct (bus, size bucket) and reuses it', () => {
    const { eng } = bootGraph();
    for (let i = 0; i < PAD_COUNT; i++) {
      eng.params[pad(i, 'fx.reverb.size')] = 0.1;
      eng.routePad(i);
    }
    expect(liveConvolvers(eng).size).toBe(1);

    eng.params[pad(1, 'fx.reverb.size')] = 0.9; // different bucket, same bus
    eng.routePad(1);
    expect(liveConvolvers(eng).size).toBe(2);

    eng.params[pad(2, 'fx.reverb.size')] = 0.9; // same bucket as pad 1 — reused
    eng.routePad(2);
    expect(liveConvolvers(eng).size).toBe(2);

    eng.params[pad(3, 'fx.reverb.size')] = 0.9; // same bucket, different bus
    eng.params[pad(3, 'out')] = 1;
    eng.routePad(3);
    expect(liveConvolvers(eng).size).toBe(3);

    // 16 pads spread over every bucket and bus still never exceed the pool cap
    for (let i = 0; i < PAD_COUNT; i++) {
      eng.params[pad(i, 'fx.reverb.size')] = i / PAD_COUNT;
      eng.params[pad(i, 'out')] = i % OUT_NAMES.length;
      eng.routePad(i);
    }
    expect(liveConvolvers(eng).size).toBeLessThanOrEqual(PAD_COUNT);
  });

  it('sends a pad to exactly one convolver after repeated re-routes', () => {
    const { eng } = bootGraph();
    for (const size of [0.1, 0.9, 0.4, 0.9, 0.2]) {
      eng.params[pad(0, 'fx.reverb.size')] = size;
      eng.routePad(0);
    }
    expect(m(eng.fxChains[0].verbSend).outs.length).toBe(1);
  });

  it('keeps every shared convolver connected to its own bus', () => {
    const { eng, ctx } = bootGraph();
    eng.params[pad(0, 'out')] = 3;
    eng.routePad(0);
    for (const conv of convolvers(ctx)) {
      expect(conv.outs.length).toBe(1);
      expect(eng.buses.some((b) => m(b.gain) === conv.outs[0])).toBe(true);
    }
  });
});

describe('DR-1 routing is idempotent', () => {
  // routePad runs on every pad FX parameter change, so it must not churn the
  // graph when nothing about the routing actually moved.
  it('does not rebuild connections when the route is unchanged', () => {
    const { eng, ctx } = bootGraph();
    const before = ctx.nodes.length;
    const dryOuts = m(eng.fxChains[0].verbDry).outs.slice();
    const sendOuts = m(eng.fxChains[0].verbSend).outs.slice();
    for (let i = 0; i < 20; i++) eng.routePad(0);
    expect(ctx.nodes.length).toBe(before); // no new convolvers
    expect(m(eng.fxChains[0].verbDry).outs).toEqual(dryOuts);
    expect(m(eng.fxChains[0].verbSend).outs).toEqual(sendOuts);
  });
});
