#!/usr/bin/env node
// Renders every DR-1 factory patch through the worklet for repeatable loudness calibration.
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { createServer } from 'vite';

const sr = 48000, frames = sr * 2, block = 128;
const vite = await createServer({ server: { middlewareMode: true, hmr: false }, appType: 'custom' });
const { FACTORY_PATCHES, applyPatchToParams } = await vite.ssrLoadModule('/src/drum/patches.ts');
const { defaultDrumParams } = await vite.ssrLoadModule('/src/drum/params.ts');
const { generateDrumTables } = await vite.ssrLoadModule('/src/drum/engine/drumtables.ts');
const { generateTables } = await vite.ssrLoadModule('/src/engine/wavetables.ts');
const { generateSampledDrumTables } = await vite.ssrLoadModule('/src/drum/engine/sampledtables.gen.ts');
function wav(buf) { const b = new Uint8Array(buf), d = new DataView(b.buffer, b.byteOffset, b.byteLength); let rate = 0, at = -1, size = 0; const tag = (i) => String.fromCharCode(b[i], b[i + 1], b[i + 2], b[i + 3]); for (let i = 12; i + 8 <= b.length;) { const n = d.getUint32(i + 4, true), body = i + 8; if (tag(i) === 'fmt ') rate = d.getUint32(body + 4, true); if (tag(i) === 'data') { at = body; size = n; } i = body + n + (n & 1); } const out = new Float32Array(size / 2); for (let i = 0; i < out.length; i++) out[i] = d.getInt16(at + i * 2, true) / 32768; return { sampleRate: rate, data: out }; }
const source = await readFile(new URL('../src/drum/engine/oneshots.gen.ts', import.meta.url), 'utf8');
const paths = [...source.matchAll(/from '(\.\.\/\.\.\/\.\.\/assets\/drum-samples\/[^']+)'/g)].map((m) => m[1].replace('../../../', ''));
const samples = await Promise.all(paths.map(async (p) => wav(await readFile(new URL(`../${p}`, import.meta.url)))));
if (samples.length !== 32 || samples.some((sample) => sample.data.length < 2)) {
  throw new Error(`expected 32 decoded one-shots; received ${samples.length}`);
}
const context = vm.createContext({ Float32Array, Float64Array, Uint8Array, Math, sampleRate: sr, currentFrame: 0, AudioWorkletProcessor: class { constructor() { this.port = { onmessage: null, postMessage() {} }; } }, registerProcessor: (_name, Ctor) => { context.Ctor = Ctor; } });
new vm.Script(await readFile(new URL('../src/drum/engine/worklet-drum.js', import.meta.url), 'utf8')).runInContext(context);
const tables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()].map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, mask: t.size - 1, data: t.data }));
function loudnessDb(audio) { let sum = 0; for (const sample of audio) sum += sample * sample; return -0.691 + 10 * Math.log10(sum / audio.length); }
const readings = [];
for (const patch of FACTORY_PATCHES) { const proc = new context.Ctor(); proc.p = defaultDrumParams(); Object.assign(proc.p, applyPatchToParams(proc.p, 0, patch)); proc.tables = tables; proc.samples = samples; proc.trigger(0, 1); const audio = new Float32Array(frames); for (let at = 0; at < frames; at += block) { const out = Array.from({ length: 16 }, () => [new Float32Array(block), new Float32Array(block)]); context.currentFrame = at; proc.process([], out); audio.set(out[0][0], at); } readings.push({ name: patch.name, lufs: loudnessDb(audio) }); }
if (readings.some((reading) => !Number.isFinite(reading.lufs))) {
  throw new Error('one or more patches rendered silence; calibration aborted');
}
const target = readings.reduce((sum, x) => sum + x.lufs, 0) / readings.length;
console.table(readings.map((x) => ({ ...x, trimDb: +(target - x.lufs).toFixed(2) })));
console.log(`Target: ${target.toFixed(2)} LUFS-like RMS (2 s, 48 kHz, peak velocity)`);
await vite.close();
