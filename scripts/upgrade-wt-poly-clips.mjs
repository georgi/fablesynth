import { readFile, writeFile } from 'node:fs/promises';
const path = new URL('../shared/seq-clips.json', import.meta.url);
const doc = JSON.parse(await readFile(path, 'utf8'));
const blSlides = {
  'bl1-acid-crawl': [3, 11],
  'bl1-cinema-hold': [1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15],
  'bl1-slide-wire': [2, 5, 11],
  'bl1-redline-acid': [3, 10, 15],
  'bl1-rubber-dub': [4, 12],
  'bl1-voltage-hop': [13],
  'bl1-rewind-bass': [15],
  'bl1-tide-drone': [1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15],
  'bl1-dread-pedal': [1, 2, 3, 4, 9, 10, 11, 12],
  'bl1-wrong-turn': [2, 8, 15],
  'bl1-acid-ascent': [15],
  'bl1-turnaround-fill': [15],
};
for (const clip of doc.clips) {
  if (clip.machine === 'BL1') {
    const next = Buffer.from(clip.pattern, 'base64');
    for (const step of blSlides[clip.id] ?? []) next[step * 3 + 1] |= 0x80;
    clip.pattern = next.toString('base64');
    continue;
  }
  if (clip.machine !== 'WT1') continue;
  const source = Buffer.from(clip.pattern, 'base64');
  const expectedMono = clip.bars * 16 * 3;
  const expectedThreeVoice = expectedMono * 3;
  const oldStride = source.length === expectedMono ? 3 : source.length === expectedThreeVoice ? 9 : 0;
  const next = oldStride ? Buffer.alloc((source.length / oldStride) * 24) : Buffer.from(source);
  if (oldStride) for (let step = 0; step < source.length / oldStride; step++) {
    source.copy(next, step * 24, step * oldStride, step * oldStride + oldStride);
  }
  for (let i = 2; i < next.length; i += 3) if (next[i] === 0) next[i] = 1;

  // Harmonic roles get full triads. Melodic/texture roles get selective
  // dyads on strong steps so the library stays varied rather than uniformly dense.
  const triad = clip.role === 'chord' || clip.role === 'pad-pulse';
  const selective = ['hook', 'countermelody', 'texture', 'riser', 'transition'].includes(clip.role);
  for (let step = 0; step < clip.bars * 16; step++) {
    const root = step * 24;
    if ((next[root] & 1) === 0 || (!triad && (!selective || step % 4 !== 0))) continue;
    const pitch = next[root + 1] + 12 * (next[root + 2] - 1);
    const minor = /minor|dorian|phrygian/i.test(clip.scale ?? '') || ['dark', 'cold'].some((tag) => clip.tags.includes(tag));
    const intervals = triad ? [minor ? 3 : 4, 7] : [7];
    intervals.forEach((interval, index) => {
      let voiced = pitch + interval;
      while (voiced > 23) voiced -= 12;
      while (voiced < -12) voiced += 12;
      const lane = root + (index + 1) * 3;
      next[lane] = next[root];
      next[lane + 1] = ((voiced % 12) + 12) % 12;
      next[lane + 2] = Math.floor((voiced + 12) / 12);
    });
  }
  clip.pattern = next.toString('base64');
}
await writeFile(path, `${JSON.stringify(doc, null, 2)}\n`);
