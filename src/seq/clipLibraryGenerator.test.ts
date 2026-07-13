// @ts-nocheck -- this focused Node-side generator test is run by Vitest; the app does not ship Node typings.
import { readFile } from 'node:fs/promises';
import { describe, expect, it } from 'vitest';
// The generator is deliberately plain ESM so it can run without a TypeScript loader.
// @ts-expect-error The build script has no declaration file.
import { extractCppPatterns, extractTsPatterns, verifyByteParity } from '../../scripts/generate-clip-library.mjs';

const root = new URL('../../', import.meta.url);

async function fixture() {
  const [source, ts, cpp] = await Promise.all([
    readFile(new URL('shared/seq-clips.json', root), 'utf8'),
    readFile(new URL('src/seq/clipLibrary.gen.ts', root), 'utf8'),
    readFile(new URL('juce/source/seq/dsp/ClipLibrary.gen.h', root), 'utf8'),
  ]);
  return { clips: JSON.parse(source).clips, ts, cpp };
}

describe('generated clip byte parity', () => {
  it('parses all emitted web and JUCE patterns in canonical order', async () => {
    const { clips, ts, cpp } = await fixture();
    expect(() => verifyByteParity(clips, ts, cpp)).not.toThrow();
    expect(extractTsPatterns(ts).map(({ id }: { id: string }) => id)).toEqual(clips.map(({ id }: { id: string }) => id));
    expect(extractCppPatterns(cpp).map(({ id }: { id: string }) => id)).toEqual(clips.map(({ id }: { id: string }) => id));
  });

  it('detects mutations in actual web and JUCE pattern payloads', async () => {
    const { clips, ts, cpp } = await fixture();
    const changedPattern = `${clips[0].pattern[0] === 'A' ? 'B' : 'A'}${clips[0].pattern.slice(1)}`;
    const mutatedTs = ts.replace(clips[0].pattern, changedPattern);
    expect(mutatedTs).not.toBe(ts);
    expect(() => verifyByteParity(clips, mutatedTs, cpp)).toThrow('web bytes differ');

    const mutatedCpp = cpp.replace(
      /(\/\/ clip-begin dr1-neon-drive[\s\S]*?\n\s*)2, 0, 0, 0/,
      (_match: string, prefix: string) => `${prefix}1, 0, 0, 0`,
    );
    expect(mutatedCpp).not.toBe(cpp);
    expect(() => verifyByteParity(clips, ts, mutatedCpp)).toThrow('JUCE bytes differ');
  });
});
