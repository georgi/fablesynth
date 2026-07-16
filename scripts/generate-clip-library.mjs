#!/usr/bin/env node

import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sourcePath = path.join(root, 'shared/seq-clips.json');
const tsPath = path.join(root, 'src/seq/clipLibrary.gen.ts');
const cppPath = path.join(root, 'juce/source/seq/dsp/ClipLibrary.gen.h');

const roles = {
  DR1: new Set(['four-on-floor', 'breakbeat', 'half-time', 'electro', 'percussion', 'hats', 'fill', 'build-up', 'sparse', 'experimental']),
  BL1: new Set(['acid', 'sub', 'arpeggio', 'ostinato', 'syncopated', 'sustained', 'sliding', 'minimal', 'fill', 'transition']),
  WT1: new Set(['lead', 'chord', 'pad-pulse', 'arpeggio', 'hook', 'countermelody', 'bass', 'texture', 'riser', 'transition']),
};
const families = new Set(['techno', 'house', 'electro', 'breaks', 'acid', 'ambient', 'lo-fi', 'cinematic', 'experimental']);
const tags = new Set(['dark', 'bright', 'warm', 'cold', 'sparse', 'dense', 'syncopated', 'straight', 'triplet-feel', 'driving', 'hypnotic', 'melodic', 'atonal', 'peak-time', 'build-up', 'breakdown', 'groovy', 'glitchy']);
const bytesPerBar = { DR1: 256, BL1: 48, WT1: 384 };
const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

function fail(message) {
  throw new Error(`seq-clips.json: ${message}`);
}

function validate(doc) {
  if (doc?.v !== 1 || !Array.isArray(doc.clips)) fail('expected { "v": 1, "clips": [...] }');
  if (doc.clips.length === 0) fail('clips must not be empty');
  const ids = new Set();
  const names = new Set();
  for (const [index, clip] of doc.clips.entries()) {
    const at = `clips[${index}]`;
    for (const key of ['id', 'name', 'machine', 'pattern', 'family', 'role']) {
      if (typeof clip[key] !== 'string' || clip[key].length === 0) fail(`${at}.${key} must be a non-empty string`);
    }
    if (!idPattern.test(clip.id)) fail(`${at}.id must be a lowercase kebab-case identifier`);
    if (ids.has(clip.id)) fail(`${at}.id duplicates ${clip.id}`);
    ids.add(clip.id);
    const normalizedName = clip.name.toLocaleLowerCase('en-US');
    if (names.has(normalizedName)) fail(`${at}.name duplicates ${clip.name}`);
    names.add(normalizedName);
    if (!(clip.machine in roles)) fail(`${at}.machine must be DR1, BL1, or WT1`);
    if (!Number.isInteger(clip.bars) || clip.bars < 1 || clip.bars > 8) fail(`${at}.bars must be an integer from 1 to 8`);
    if (!families.has(clip.family)) fail(`${at}.family is not in the v1 taxonomy`);
    if (!roles[clip.machine].has(clip.role)) fail(`${at}.role is not valid for ${clip.machine}`);
    if (!Number.isInteger(clip.energy) || clip.energy < 1 || clip.energy > 5) fail(`${at}.energy must be an integer from 1 to 5`);
    if (!Array.isArray(clip.tags) || clip.tags.length === 0 || clip.tags.some((tag) => typeof tag !== 'string' || !idPattern.test(tag))) {
      fail(`${at}.tags must be a non-empty array of kebab-case strings`);
    }
    if (new Set(clip.tags).size !== clip.tags.length) fail(`${at}.tags contains duplicates`);
    if (clip.tags.some((tag) => !tags.has(tag))) fail(`${at}.tags contains a tag outside the v1 taxonomy`);
    if (typeof clip.transpose !== 'boolean') fail(`${at}.transpose must be boolean`);
    if (clip.machine === 'DR1' && ('root' in clip || 'scale' in clip || clip.transpose)) fail(`${at}: drum clips cannot be transposable or set root/scale`);
    if ('root' in clip && (!Number.isInteger(clip.root) || clip.root < 0 || clip.root > 11)) fail(`${at}.root must be a pitch class from 0 to 11`);
    if ('scale' in clip && (typeof clip.scale !== 'string' || !idPattern.test(clip.scale))) fail(`${at}.scale must be a kebab-case string`);
    if (clip.transpose && (!('root' in clip) || !('scale' in clip))) fail(`${at}: transposable clips require root and scale`);

    if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(clip.pattern)) fail(`${at}.pattern is not canonical base64`);
    const bytes = Buffer.from(clip.pattern, 'base64');
    if (bytes.toString('base64') !== clip.pattern) fail(`${at}.pattern is not canonical base64`);
    const expected = bytesPerBar[clip.machine] * clip.bars;
    if (bytes.length !== expected) fail(`${at}.pattern decodes to ${bytes.length} bytes; expected ${expected}`);
    if (clip.machine === 'DR1' && bytes.some((value) => value > 2)) fail(`${at}.pattern has an invalid DR1 step value`);
    if (clip.machine !== 'DR1') {
      for (let offset = 0; offset < bytes.length; offset += 3) {
        const flags = bytes[offset];
        const duration = (flags >> 2) & 0x3f;
        if (duration < 1 || duration > 63) fail(`${at}.pattern has invalid note flags at byte ${offset}`);
        if ((bytes[offset + 1] & 0x7f) > 11 || (clip.machine === 'WT1' && bytes[offset + 1] > 11)) {
          fail(`${at}.pattern has invalid note lane at byte ${offset + 1}`);
        }
        if (bytes[offset + 2] > 2) fail(`${at}.pattern has invalid octave at byte ${offset + 2}`);
      }
    }
  }
  return doc.clips;
}

function generateTs(clips) {
  const json = JSON.stringify(clips, null, 2).replace(/^/gm, '  ');
  const fingerprints = Object.fromEntries(clips.map((clip) => [
    clip.id, createHash('sha256').update(Buffer.from(clip.pattern, 'base64')).digest('hex'),
  ]));
  return `// Generated by scripts/generate-clip-library.mjs from shared/seq-clips.json.\n// Do not edit by hand.\n\nimport type { ClipLibraryEntry } from './clipLibrary';\n\nexport const FACTORY_CLIP_LIBRARY = ${json.trimStart()} satisfies readonly ClipLibraryEntry[];\n\nexport const FACTORY_CLIP_BYTE_FINGERPRINTS = ${JSON.stringify(fingerprints, null, 2)} as const;\n`;
}

function cppString(value) {
  return JSON.stringify(value).replace(/\\u([0-9a-fA-F]{4})/g, '\\u$1');
}

function generateCpp(clips) {
  const entries = clips.map((clip) => {
    const bytes = [...Buffer.from(clip.pattern, 'base64')];
    const fingerprint = createHash('sha256').update(Buffer.from(clip.pattern, 'base64')).digest('hex');
    const byteLines = [];
    for (let i = 0; i < bytes.length; i += 24) byteLines.push(`            ${bytes.slice(i, i + 24).join(', ')}`);
    const tags = clip.tags.map(cppString).join(', ');
    return `        // clip-begin ${clip.id}\n        // byte-fingerprint ${clip.id} ${fingerprint}\n        {\n            ${cppString(clip.id)}, ${cppString(clip.name)}, Machine::${clip.machine}, ${clip.bars},\n            {\n${byteLines.join(',\n')}\n            },\n            ${cppString(clip.family)}, ${cppString(clip.role)}, ${clip.energy}, { ${tags} },\n            ${clip.root ?? -1}, ${cppString(clip.scale ?? '')}, ${clip.transpose}\n        },\n        // clip-end ${clip.id}`;
  }).join('\n');
  return `// Generated by scripts/generate-clip-library.mjs from shared/seq-clips.json.\n// Do not edit by hand.\n#pragma once\n\n#include "ClipLibrary.h"\n\nnamespace fable {\n\ninline const std::vector<ClipLibraryEntry>& factoryClipLibrary() {\n    static const std::vector<ClipLibraryEntry> clips {\n${entries}\n    };\n    return clips;\n}\n\n} // namespace fable\n`;
}

export function extractTsPatterns(ts) {
  const prefix = 'export const FACTORY_CLIP_LIBRARY = ';
  const suffix = ' satisfies readonly ClipLibraryEntry[];';
  const start = ts.indexOf(prefix);
  const end = start < 0 ? -1 : ts.indexOf(suffix, start + prefix.length);
  if (start < 0 || end < 0) fail('generated web factory array is missing or malformed');

  let entries;
  try {
    entries = JSON.parse(ts.slice(start + prefix.length, end));
  } catch (error) {
    fail(`generated web factory array is not valid JSON: ${error instanceof Error ? error.message : String(error)}`);
  }
  if (!Array.isArray(entries)) fail('generated web factory value must be an array');
  return entries.map((entry, index) => {
    if (typeof entry?.id !== 'string' || typeof entry?.pattern !== 'string') {
      fail(`generated web entry ${index} is missing id or pattern`);
    }
    return { id: entry.id, bytes: Buffer.from(entry.pattern, 'base64') };
  });
}

export function extractCppPatterns(cpp) {
  const entries = [];
  const blockPattern = /\/\/ clip-begin ([a-z0-9]+(?:-[a-z0-9]+)*)\n([\s\S]*?)\n\s*\/\/ clip-end \1/g;
  const initializerPattern = /\{\s*("(?:\\.|[^"\\])*")\s*,\s*"(?:\\.|[^"\\])*"\s*,\s*Machine::(?:DR1|BL1|WT1)\s*,\s*\d+\s*,\s*\{\s*([\d,\s]+?)\s*\}\s*,/;
  for (const match of cpp.matchAll(blockPattern)) {
    const markerId = match[1];
    const initializer = match[2].match(initializerPattern);
    if (!initializer) fail(`generated JUCE initializer is malformed for ${markerId}`);
    const id = JSON.parse(initializer[1]);
    if (id !== markerId) fail(`generated JUCE marker ${markerId} contains id ${id}`);
    const byteText = initializer[2].trim();
    if (!/^\d+(?:\s*,\s*\d+)*$/.test(byteText)) fail(`generated JUCE bytes are malformed for ${id}`);
    const values = byteText.split(',').map((value) => Number(value.trim()));
    if (values.some((value) => !Number.isInteger(value) || value < 0 || value > 255)) {
      fail(`generated JUCE bytes are out of range for ${id}`);
    }
    entries.push({ id, bytes: Buffer.from(values) });
  }
  if (entries.length === 0) fail('generated JUCE factory contains no clip initializers');
  return entries;
}

function verifyTargetPatterns(clips, emitted, target) {
  if (emitted.length !== clips.length) {
    fail(`${target} emitted ${emitted.length} clips; expected ${clips.length}`);
  }
  for (let index = 0; index < clips.length; index += 1) {
    const expected = clips[index];
    const actual = emitted[index];
    if (actual.id !== expected.id) {
      fail(`${target} clip ${index} is ${actual.id}; expected ${expected.id}`);
    }
    const expectedBytes = Buffer.from(expected.pattern, 'base64');
    if (!actual.bytes.equals(expectedBytes)) {
      fail(`${target} bytes differ for ${expected.id}`);
    }
  }
}

export function verifyByteParity(clips, ts, cpp) {
  verifyTargetPatterns(clips, extractTsPatterns(ts), 'web');
  verifyTargetPatterns(clips, extractCppPatterns(cpp), 'JUCE');
}

async function main() {
  const doc = JSON.parse(await readFile(sourcePath, 'utf8'));
  const clips = validate(doc);
  const ts = generateTs(clips);
  const cpp = generateCpp(clips);
  verifyByteParity(clips, ts, cpp);
  const outputs = [[tsPath, ts], [cppPath, cpp]];
  const check = process.argv.includes('--check');
  if (process.argv.some((arg) => arg.startsWith('-') && arg !== '--check')) fail('only --check is supported');
  for (const [file, content] of outputs) {
    if (check) {
      let current = '';
      try { current = await readFile(file, 'utf8'); } catch {}
      if (current !== content) fail(`${path.relative(root, file)} is stale; run node scripts/generate-clip-library.mjs`);
    } else {
      await writeFile(file, content);
    }
  }
  console.log(`${check ? 'checked' : 'generated'} ${clips.length} factory clips`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await main();
}
