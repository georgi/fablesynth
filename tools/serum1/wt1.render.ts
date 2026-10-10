// Renders WT-1 presets (Preset JSON with embedded tables) through the real
// worklet with the same phrase as `s1host render`. Env: PRESETS=list file,
// OUT=dir. Writes 24-bit stereo WAV at 48 kHz.
import { it } from 'vitest';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { basename } from 'node:path';
import { bootWt } from '/Users/mg/dev/fablesynth/src/engine/workletHarness';
import { deserializeUserTable } from '/Users/mg/dev/fablesynth/src/engine/usertables';

const SR = 48000;
// Single notes, a chord, then an overlapping run (exposes mono/legato).
const EV: [number, number, number][] = [[0, 1, 48], [1.25, 2.25, 55], [2.5, 4.5, 48], [2.5, 4.5, 52], [2.5, 4.5, 55], [2.5, 4.5, 59],
  [6.0, 6.7, 48], [6.5, 7.2, 51], [7.0, 7.9, 55]];
const TOTAL = 9.5;
// Pre-roll: let parameter smoothing settle after the preset loads, as in the app.
const PREROLL = 0.5;

function wav(L: Float32Array, R: Float32Array): Buffer {
  const n = L.length, b = Buffer.alloc(44 + n * 6);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 6, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(SR, 24);
  b.writeUInt32LE(SR * 6, 28); b.writeUInt16LE(6, 32); b.writeUInt16LE(24, 34); b.write('data', 36); b.writeUInt32LE(n * 6, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < 2; c++) {
    const v = Math.max(-1, Math.min(1, (c ? R : L)[i]!)) * 8388607;
    b.writeIntLE(Math.round(v), 44 + i * 6 + c * 3, 3);
  }
  return b;
}

it('renders WT-1 presets', () => {
  const list = readFileSync(process.env.PRESETS!, 'utf8').split('\n').filter(Boolean);
  mkdirSync(process.env.OUT!, { recursive: true });
  for (const path of list) {
    const preset = JSON.parse(readFileSync(path, 'utf8'));
    const h = bootWt(preset.params, SR);
    (preset.tables ?? []).forEach((s: never, k: number) => {
      const t = deserializeUserTable(s).table;
      h.send({ t: 'table', i: 6 + k, frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer });
    });
    h.render(Math.ceil(PREROLL * SR / 128));
    const blocks = Math.ceil(TOTAL * SR / 128);
    const events = EV.flatMap(([a, b, n]) => [{ s: Math.round(a * SR) + 256, m: { t: 'on', n, v: 100 / 127 } }, { s: Math.round(b * SR) + 256, m: { t: 'off', n } }])
      .sort((x, y) => x.s - y.s);
    const L = new Float32Array(blocks * 128), R = new Float32Array(blocks * 128);
    let e = 0;
    for (let b = 0; b < blocks; b++) {
      while (e < events.length && events[e]!.s < (b + 1) * 128) h.send(events[e++]!.m);
      const out = h.render(1);
      L.set(out.L, b * 128); R.set(out.R, b * 128);
    }
    writeFileSync(`${process.env.OUT}/${basename(path, '.json')}.wav`, wav(L, R));
  }
});
