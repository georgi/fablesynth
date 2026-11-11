// Drives one device worklet (JavaScript or WebAssembly) through a scripted
// SQ-4 hosted session: messages are sent before given render blocks, with
// `currentFrame` set like the AudioWorklet global. Used by the
// worklet-*-wasm-hosted tests (docs/wasm-sq4.md).

export interface HostedProc {
  proc: { process(inputs: unknown[], outputs: Float32Array[][]): boolean };
  sent: { t: string; [k: string]: unknown }[];
  send(msg: unknown): void;
}

export interface HostedEvent { t: string; block: number; frame?: number; step?: number; bar?: number }

export interface HostedRun { L: Float32Array; R: Float32Array; events: HostedEvent[] }

/** script[block] = messages sent just before that block renders. The first
 *  `sum` of the `outputs` stereo outputs are mixed into L/R (DR-1 buses). */
export function runHosted(h: HostedProc, script: Record<number, unknown[]>, blocks: number, outputs = 1, sum = 1): HostedRun {
  const L = new Float32Array(blocks * 128), R = new Float32Array(blocks * 128);
  const outs = Array.from({ length: outputs }, () => [new Float32Array(128), new Float32Array(128)]);
  const events: HostedEvent[] = [];
  for (let b = 0; b < blocks; b++) {
    Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: b * 128 });
    for (const m of script[b] ?? []) h.send(m);
    const before = h.sent.length;
    for (const o of outs) for (const ch of o) ch.fill(0);
    h.proc.process([], outs);
    for (const m of h.sent.slice(before)) {
      if (m.t === 'clipstart' || m.t === 'clipstop') events.push({ t: m.t, block: b, frame: m.frame as number });
      else if (m.t === 'pos') events.push({ t: m.t, block: b, step: m.step as number, bar: m.bar as number });
    }
    for (let o = 0; o < sum; o++) for (let i = 0; i < 128; i++) { L[b * 128 + i] += outs[o][0][i]; R[b * 128 + i] += outs[o][1][i]; }
  }
  return { L, R, events };
}

const db = (x: number) => 20 * Math.log10(Math.max(1e-12, x));

/** Level of the JS render and of the JS - wasm difference, in dBFS. */
export function parity(a: HostedRun, b: HostedRun) {
  let peakA = 0, peakD = 0, sumA = 0, sumB = 0, sumD = 0;
  for (const [x, y] of [[a.L, b.L], [a.R, b.R]] as const) {
    for (let i = 0; i < x.length; i++) {
      const d = x[i] - y[i];
      peakA = Math.max(peakA, Math.abs(x[i])); peakD = Math.max(peakD, Math.abs(d));
      sumA += x[i] * x[i]; sumB += y[i] * y[i]; sumD += d * d;
    }
  }
  const n = a.L.length * 2;
  return {
    peakJs: db(peakA), rmsJs: db(Math.sqrt(sumA / n)), rmsWasm: db(Math.sqrt(sumB / n)),
    peakDiff: db(peakD), rmsDiff: db(Math.sqrt(sumD / n)),
    finite: b.L.every(Number.isFinite) && b.R.every(Number.isFinite),
  };
}

/** A FIT/grid automation lane table: `len` sixteenths, 16 values each, ramping lo -> hi. */
export function rampLane(k: string, len: number, lo: number, hi: number, fit = 0) {
  const table = new Float32Array(len * 16);
  for (let i = 0; i < table.length; i++) table[i] = lo + (hi - lo) * (i / table.length);
  return { k, table, len, fit, rot: 0 };
}

/** Event timing check: clipstart/clipstop exactly equal; pos in the same
 *  order, each within `slack` render blocks (the native engines split a
 *  block at gate-offs and fire a due step inside it; the JS worklets fire at
 *  block starts). Returns the largest pos block offset. */
export function compareEvents(js: HostedEvent[], wasm: HostedEvent[]) {
  const edges = (e: HostedEvent[]) => e.filter((x) => x.t !== 'pos');
  const pos = (e: HostedEvent[]) => e.filter((x) => x.t === 'pos');
  const pj = pos(js), pw = pos(wasm);
  let maxSkew = 0, moved = 0;
  for (let i = 0; i < Math.min(pj.length, pw.length); i++) {
    const d = Math.abs(pj[i].block - pw[i].block);
    if (d) moved++;
    maxSkew = Math.max(maxSkew, d);
  }
  return {
    edgesJs: edges(js), edgesWasm: edges(wasm),
    posJs: pj.map((x) => [x.step, x.bar]), posWasm: pw.map((x) => [x.step, x.bar]),
    maxSkew, moved, posCount: pj.length,
  };
}

/** Writes a result line to stderr (vitest hides console output here). */
export function report(line: string): void {
  (globalThis as { process?: { stderr?: { write(s: string): void } } }).process?.stderr?.write(line + '\n');
}
