#!/usr/bin/env node
// Renders each BL-1 factory phrase through the actual worklet and reports a
// repeatable 8-bar RMS loudness reading for calibration.
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { createServer } from 'vite';

const sr = 48000, block = 128, frames = sr * 16;
const vite = await createServer({ server: { middlewareMode: true, hmr: false }, appType: 'custom' });
const { FACTORY_PATCHES } = await vite.ssrLoadModule('/src/bass/patches.ts');
const { defaultBassParams } = await vite.ssrLoadModule('/src/bass/params.ts');
const { generateTables } = await vite.ssrLoadModule('/src/engine/wavetables.ts');
const context = vm.createContext({ Float32Array, Float64Array, Uint8Array, Math, sampleRate: sr, currentFrame: 0, AudioWorkletProcessor: class { constructor() { this.port = { onmessage: null, postMessage() {} }; } }, registerProcessor: (_name, Ctor) => { context.Ctor = Ctor; } });
new vm.Script(await readFile(new URL('../src/bass/engine/worklet-bass.js', import.meta.url), 'utf8')).runInContext(context);
const tables = generateTables().map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, mask: t.size - 1, data: t.data }));
const db = (audio) => { let sum = 0; for (const sample of audio) sum += sample * sample; return -0.691 + 10 * Math.log10(sum / audio.length); };
const readings = [];
for (const patch of FACTORY_PATCHES) {
  const proc = new context.Ctor(); proc.p = { ...defaultBassParams(), ...patch.params }; proc.tables = tables; proc.pats = new Uint8Array(patch.patterns); proc.chain = patch.chain; proc.playing = true;
  const audio = new Float32Array(frames);
  for (let at = 0; at < frames; at += block) { const out = [[new Float32Array(block), new Float32Array(block)]]; context.currentFrame = at; proc.process([], out); audio.set(out[0][0], at); }
  readings.push({ name: patch.name, lufs: db(audio) });
}
const target = readings.reduce((sum, item) => sum + item.lufs, 0) / readings.length;
console.table(readings.map((item) => ({ ...item, trimDb: +(target - item.lufs).toFixed(2) })));
console.log(`Target: ${target.toFixed(2)} LUFS-like RMS (8 bars, 48 kHz, factory phrase)`);
await vite.close();
