// @ts-nocheck -- this focused Node-side generator test is run by Vitest; the app does not ship Node typings.
// Extracts shared/session-mixdowns.json from the sessions the browser batch
// exported after auto-EQ, auto-comp and ASSIST (build/auditions/mixdowns).
// Only device EQ/comp values that moved, the track gains, and the master
// section are kept. `MIXDOWNS=extract npx vitest run src/seq/sessionMixdowns.extract.test.ts`.
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { BASE_AUTHORED_SESSION_PRESETS, BASE_GENERATED_SESSION_PRESETS } from './sessionPresets';
import { resolvedPatchParams } from './sessionExport';

const ROOT = new URL('../../', import.meta.url).pathname;
const SRC = `${ROOT}build/auditions/mixdowns`;
const KEPT = /^fx\.(eq|comp)\./;
const r = (v: number) => Math.round(v * 1e5) / 1e5;

describe.skipIf(process.env.MIXDOWNS !== 'extract')('session mixdown extraction', () => {
  it('writes shared/session-mixdowns.json', () => {
    const bases = [...BASE_GENERATED_SESSION_PRESETS, ...BASE_AUTHORED_SESSION_PRESETS];
    const exported = new Map<string, any>();
    for (const f of readdirSync(SRC).filter((f) => f.endsWith('-automix.json'))) {
      const s = JSON.parse(readFileSync(`${SRC}/${f}`, 'utf8'));
      exported.set(s.name, s);
    }
    const sessions = [], report = [];
    for (const preset of bases) {
      const s = exported.get(preset.name);
      if (!s) { report.push(`${preset.name}: MISSING`); continue; }
      const tracks = preset.session.tracks.map((track, t) => {
        const base = resolvedPatchParams(track.machine, track.patch);
        const now = resolvedPatchParams(track.machine, s.tracks[t].patch);
        const params = {}, drift = [];
        for (const [k, v] of Object.entries(now)) {
          if (Math.abs((base[k] ?? NaN) - v) <= 1e-6) continue;
          if (KEPT.test(k)) params[k] = r(v); else drift.push(k);
        }
        if (drift.length) report.push(`${preset.name} track ${t}: ignored drift in ${drift.slice(0, 6).join(', ')}${drift.length > 6 ? ` +${drift.length - 6}` : ''}`);
        return { gain: r(s.tracks[t].gain), params };
      });
      sessions.push({
        name: preset.name,
        masterVol: r(s.masterVol ?? 0.75),
        masterFx: Object.fromEntries(Object.entries(s.masterFx ?? {}).map(([k, v]) => [k, r(v as number)])),
        tracks,
      });
    }
    writeFileSync(`${ROOT}shared/session-mixdowns.json`, `${JSON.stringify({ v: 1, sessions }, null, 1)}\n`);
    writeFileSync(`${ROOT}build/mixdown-extract-report.txt`, `${sessions.length} sessions\n${report.join('\n')}\n`);
    expect(sessions.length).toBeGreaterThan(0);
  });
});
