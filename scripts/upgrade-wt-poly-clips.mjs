import { readFile, writeFile } from 'node:fs/promises';
const path = new URL('../shared/seq-clips.json', import.meta.url);
const doc = JSON.parse(await readFile(path, 'utf8'));
for (const clip of doc.clips) {
  if (clip.machine !== 'WT1') continue;
  const source = Buffer.from(clip.pattern, 'base64');
  const expectedMono = clip.bars * 16 * 3;
  const old = source.length === expectedMono ? source : null;
  const next = old ? Buffer.alloc(old.length * 3) : Buffer.from(source);
  if (old) for (let step = 0; step < old.length / 3; step++) old.copy(next, step * 9, step * 3, step * 3 + 3);
  for (let i = 2; i < next.length; i += 3) if (next[i] === 0) next[i] = 1;

  // Harmonic roles get full triads. Melodic/texture roles get selective
  // dyads on strong steps so the library stays varied rather than uniformly dense.
  const triad = clip.role === 'chord' || clip.role === 'pad-pulse';
  const selective = ['hook', 'countermelody', 'texture', 'riser', 'transition'].includes(clip.role);
  for (let step = 0; step < clip.bars * 16; step++) {
    const root = step * 9;
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
