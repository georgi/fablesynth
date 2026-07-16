#!/usr/bin/env node
// Authoritative DR-1 factory clip patterns. Stable ids are retained while
// names, metadata, and packed bytes are rebuilt around the 808, UZU, and
// oscillator/sample hybrid kits.

import { readFile, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const source = join(root, 'shared/seq-clips.json');
const P = { BD: 0, BD2: 1, SD: 2, CP: 3, RIM: 4, HH: 5, OH: 6, RD: 7,
  LT: 8, MT: 9, HT: 10, CR: 11, P1: 12, P2: 13, VOX: 14, MOD: 15 };
const q = [0, 4, 8, 12], off = [2, 6, 10, 14], six = [0, 2, 4, 6, 8, 10, 12, 14];
const all = Array.from({ length: 16 }, (_, i) => i);
const h = (pad, steps, accents = []) => [pad, steps, accents];

function pattern(bars) {
  const bytes = new Uint8Array(bars.length * 256);
  bars.forEach((hits, bar) => hits.forEach(([pad, steps, accents]) => steps.forEach((step) => {
    bytes[(bar * 16 + pad) * 16 + step] = accents.includes(step) ? 2 : 1;
  })));
  return Buffer.from(bytes).toString('base64');
}

const c = (id, name, family, role, energy, tags, bars) => ({
  id, name, machine: 'DR1', bars: bars.length, pattern: pattern(bars),
  family, role, energy, tags, transpose: false,
});

const clips = [
  c('dr1-neon-drive', 'HYBRID DRIVE', 'techno', 'four-on-floor', 4, ['driving', 'peak-time'], [
    [h(P.BD, q, [0]), h(P.SD, [4, 12]), h(P.HH, off), h(P.OH, [14]), h(P.P1, [3, 11])],
    [h(P.BD, [0, 4, 7, 8, 12], [0]), h(P.SD, [4, 12], [12]), h(P.HH, off), h(P.RD, [10]), h(P.P2, [15])],
  ]),
  c('dr1-house-pocket', '808 FLOOR', 'house', 'four-on-floor', 3, ['groovy', 'straight'], [
    [h(P.BD, q, [0]), h(P.CP, [4, 12]), h(P.HH, off), h(P.OH, [6, 14]), h(P.P1, [3, 11])],
  ]),
  c('dr1-lofi-break', 'UZU BREAK', 'lo-fi', 'breakbeat', 3, ['warm', 'syncopated'], [
    [h(P.BD, [0, 7, 10]), h(P.SD, [4, 12]), h(P.HH, [2, 5, 8, 11, 14]), h(P.RIM, [15])],
    [h(P.BD, [0, 3, 10, 14]), h(P.SD, [4, 11]), h(P.CP, [12]), h(P.HH, [1, 6, 9, 13]), h(P.MOD, [15])],
  ]),
  c('dr1-cinema-half', 'HALF LAYERS', 'cinematic', 'half-time', 2, ['dark', 'sparse'], [
    [h(P.BD, [0, 10], [0]), h(P.SD, [8]), h(P.RD, [4, 12]), h(P.P2, [14])],
    [h(P.BD2, [0, 6]), h(P.CP, [8]), h(P.OH, [10]), h(P.VOX, [15])],
  ]),
  c('dr1-electro-grid', 'CROSSWIRE GRID', 'electro', 'electro', 4, ['cold', 'syncopated'], [
    [h(P.BD, [0, 3, 8, 11]), h(P.SD, [4, 12]), h(P.HH, six), h(P.RIM, [2, 10]), h(P.MOD, [7, 15])],
  ]),
  c('dr1-perc-orbit', 'MODULAR ORBIT', 'experimental', 'percussion', 3, ['hypnotic', 'atonal'], [
    [h(P.P1, [0, 5, 11]), h(P.P2, [3, 8, 14]), h(P.VOX, [6, 12]), h(P.MOD, [2, 10]), h(P.RIM, [15])],
    [h(P.P1, [1, 7, 13]), h(P.P2, [4, 10]), h(P.VOX, [0, 9]), h(P.MOD, [6, 14]), h(P.RD, [15])],
  ]),
  c('dr1-hat-rise', 'DUAL HAT RISE', 'house', 'build-up', 4, ['build-up', 'dense'], [
    [h(P.BD, q), h(P.HH, [0, 4, 8, 12]), h(P.P1, [14])],
    [h(P.BD, q, [0]), h(P.HH, six, [4, 12]), h(P.OH, [14]), h(P.P2, [15])],
    [h(P.BD, q, [0]), h(P.HH, all, [4, 12]), h(P.OH, [6, 14]), h(P.CR, [15])],
  ]),
  c('dr1-tom-fall', 'TOM CASCADE', 'cinematic', 'fill', 5, ['dark', 'peak-time'], [
    [h(P.BD, [0]), h(P.SD, [4]), h(P.HT, [8, 9]), h(P.MT, [10, 11]), h(P.LT, [12, 13, 14]), h(P.CR, [15], [15])],
  ]),
  c('dr1-dusty-amen', 'UZU AMEN', 'breaks', 'breakbeat', 4, ['warm', 'syncopated'], [
    [h(P.BD, [0, 3, 10]), h(P.SD, [4, 7, 12]), h(P.HH, [2, 6, 9, 14]), h(P.RIM, [15])],
    [h(P.BD2, [0, 6, 11]), h(P.SD, [4, 9, 12]), h(P.HH, [1, 5, 8, 13]), h(P.MOD, [14, 15])],
  ]),
  c('dr1-ghost-shuffle', 'GHOST RIM', 'breaks', 'sparse', 2, ['sparse', 'triplet-feel'], [
    [h(P.BD, [0, 9]), h(P.RIM, [3, 6, 11, 14]), h(P.HH, [5, 13]), h(P.P2, [15])],
  ]),
  c('dr1-iron-quarters', '808 PRESSURE', 'techno', 'four-on-floor', 5, ['driving', 'peak-time'], [
    [h(P.BD, q, [0, 8]), h(P.SD, [4, 12]), h(P.HH, all, [2, 6, 10, 14]), h(P.OH, [14]), h(P.CR, [15])],
  ]),
  c('dr1-dub-chambers', 'DUB SPACE', 'techno', 'half-time', 3, ['dark', 'hypnotic'], [
    [h(P.BD, [0, 7, 10]), h(P.SD, [8]), h(P.OH, [3, 14]), h(P.P1, [6, 12])],
    [h(P.BD2, [0, 11]), h(P.CP, [8]), h(P.RD, [4, 12]), h(P.VOX, [15])],
  ]),
  c('dr1-velvet-jack', 'UZU JACK', 'house', 'four-on-floor', 3, ['warm', 'groovy'], [
    [h(P.BD, q, [0]), h(P.CP, [4, 12]), h(P.HH, [2, 6, 10, 14]), h(P.P2, [3, 11])],
  ]),
  c('dr1-sunrise-hats', 'AIR HATS', 'house', 'hats', 2, ['bright', 'straight'], [
    [h(P.HH, six, [4, 12]), h(P.OH, [6, 14]), h(P.RD, [10]), h(P.P2, [15])],
  ]),
  c('dr1-chrome-sync', 'ELECTRO METAL', 'electro', 'electro', 5, ['cold', 'syncopated'], [
    [h(P.BD, [0, 3, 8, 10, 14]), h(P.SD, [4, 12]), h(P.HH, six), h(P.RD, [2, 6]), h(P.MOD, [7, 15])],
  ]),
  c('dr1-machine-funk', 'MACHINE SHUFFLE', 'electro', 'breakbeat', 3, ['groovy', 'glitchy'], [
    [h(P.BD, [0, 6, 10]), h(P.SD, [4, 12]), h(P.HH, [2, 5, 9, 13]), h(P.P1, [3, 11]), h(P.MOD, [15])],
  ]),
  c('dr1-squelch-frame', 'ACID HYBRID', 'acid', 'four-on-floor', 4, ['driving', 'hypnotic'], [
    [h(P.BD, q, [0]), h(P.SD, [4, 12]), h(P.HH, six), h(P.RIM, [3, 11]), h(P.VOX, [7, 15])],
  ]),
  c('dr1-distant-ticks', 'SAMPLE TICKS', 'ambient', 'sparse', 1, ['cold', 'sparse'], [
    [h(P.RIM, [1, 6, 13]), h(P.HH, [4, 12]), h(P.P2, [9]), h(P.MOD, [15])],
  ]),
  c('dr1-tape-swing', 'TAPE CROSS', 'lo-fi', 'breakbeat', 3, ['warm', 'triplet-feel'], [
    [h(P.BD, [0, 7, 11]), h(P.SD, [4, 12]), h(P.HH, [2, 5, 10, 14]), h(P.P1, [3, 9])],
  ]),
  c('dr1-titan-march', 'TITAN HYBRID', 'cinematic', 'half-time', 5, ['dark', 'peak-time'], [
    [h(P.BD, [0, 4, 10], [0]), h(P.SD, [8]), h(P.LT, [12]), h(P.CR, [15], [15])],
  ]),
  c('dr1-broken-clock', 'BROKEN MOD', 'experimental', 'experimental', 3, ['atonal', 'glitchy'], [
    [h(P.BD2, [1, 9]), h(P.RIM, [3, 12]), h(P.P1, [5, 14]), h(P.VOX, [7]), h(P.MOD, [0, 10, 15])],
  ]),
  c('dr1-jungle-sparks', 'JUNGLE UZU', 'breaks', 'breakbeat', 5, ['dense', 'peak-time'], [
    [h(P.BD, [0, 3, 6, 10, 14]), h(P.SD, [4, 7, 12, 15], [15]), h(P.HH, all), h(P.RIM, [2, 11])],
    [h(P.BD2, [0, 5, 9, 13]), h(P.SD, [4, 8, 12, 14]), h(P.HH, all, [3, 7, 11, 15]), h(P.MOD, [15])],
  ]),
  c('dr1-garage-skip', 'GARAGE HYBRID', 'house', 'breakbeat', 3, ['groovy', 'syncopated'], [
    [h(P.BD, [0, 6, 10]), h(P.CP, [4, 12]), h(P.HH, [2, 5, 9, 14]), h(P.OH, [11]), h(P.P2, [15])],
  ]),
  c('dr1-warehouse-fill', 'WAREHOUSE ROLL', 'techno', 'fill', 5, ['build-up', 'dense'], [
    [h(P.BD, q), h(P.SD, [4, 12]), h(P.HT, [8, 10]), h(P.MT, [11, 12]), h(P.LT, [13, 14]), h(P.CR, [15], [15])],
  ]),
  c('dr1-pulse-dust', 'COWBELL DUST', 'ambient', 'percussion', 1, ['sparse', 'hypnotic'], [
    [h(P.P1, [0, 7, 12]), h(P.P2, [3, 10]), h(P.RD, [14]), h(P.MOD, [15])],
  ]),
  c('dr1-ritual-toms', 'TOM RITUAL', 'cinematic', 'percussion', 3, ['dark', 'triplet-feel'], [
    [h(P.LT, [0, 7, 14]), h(P.MT, [3, 10]), h(P.HT, [5, 12]), h(P.P1, [8]), h(P.CR, [15])],
  ]),
  c('dr1-binary-rain', 'BINARY UZU', 'experimental', 'experimental', 4, ['cold', 'glitchy'], [
    [h(P.HH, [0, 1, 4, 6, 9, 12, 14]), h(P.RIM, [3, 11]), h(P.P2, [2, 8]), h(P.MOD, [5, 10, 15])],
  ]),
  c('dr1-bedroom-knock', 'BEDROOM 808', 'lo-fi', 'half-time', 2, ['warm', 'sparse'], [
    [h(P.BD, [0, 10]), h(P.SD, [8]), h(P.HH, [3, 6, 11, 14]), h(P.P1, [12])],
  ]),
  c('dr1-corrosive-build', 'CORROSIVE MIX', 'acid', 'build-up', 5, ['build-up', 'driving'], [
    [h(P.BD, q), h(P.HH, [0, 4, 8, 12])],
    [h(P.BD, q, [0]), h(P.SD, [4, 12]), h(P.HH, six), h(P.P1, [14])],
    [h(P.BD, q, [0, 8]), h(P.SD, [4, 12]), h(P.HH, all), h(P.OH, [14]), h(P.CR, [15])],
  ]),
  c('dr1-robot-fill', 'ROBOT CROSS', 'electro', 'fill', 4, ['glitchy', 'build-up'], [
    [h(P.BD, [0, 8]), h(P.RIM, [4, 10]), h(P.P1, [11]), h(P.P2, [12]), h(P.VOX, [13]), h(P.MOD, [14, 15], [15])],
  ]),
  c('dr1-halftime-crush', 'HALFTIME UZU', 'breaks', 'half-time', 4, ['dark', 'driving'], [
    [h(P.BD, [0, 3, 10]), h(P.SD, [8]), h(P.HH, six), h(P.LT, [14]), h(P.CR, [15])],
  ]),
  c('dr1-disco-lift', 'DISCO HYBRID', 'house', 'build-up', 5, ['bright', 'peak-time'], [
    [h(P.BD, q, [0]), h(P.CP, [4, 12]), h(P.HH, all, [2, 6, 10, 14]), h(P.OH, [6, 14]), h(P.P1, [3, 11]), h(P.CR, [15])],
  ]),
];

const doc = JSON.parse(await readFile(source, 'utf8'));
const byId = new Map(clips.map((clip) => [clip.id, clip]));
let replaced = 0;
doc.clips = doc.clips.map((clip) => {
  if (clip.machine !== 'DR1') return clip;
  const replacement = byId.get(clip.id);
  if (!replacement) throw new Error(`Missing DR-1 redesign for ${clip.id}`);
  replaced++;
  return replacement;
});
if (replaced !== clips.length) throw new Error(`Replaced ${replaced} DR-1 clips, expected ${clips.length}`);
await writeFile(source, JSON.stringify(doc, null, 2) + '\n');
console.log(`Rebuilt ${replaced} DR-1 clips around 808, UZU, and hybrid kits`);
