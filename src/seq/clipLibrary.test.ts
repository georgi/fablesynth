import { describe, expect, it } from 'vitest';
import {
  CLIP_FAMILIES,
  CLIP_ROLES,
  CLIP_TAGS,
  decodeClipLibrary,
  encodeClipLibrary,
  type ClipLibraryDoc,
  validateClipLibrary,
} from './clipLibrary';
import { bytesPerBar, bytesToB64, emptyClipBytes } from './protocol';
import { FACTORY_CLIP_BYTE_FINGERPRINTS, FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';

function validDoc(): ClipLibraryDoc {
  return {
    v: 1,
    clips: [
      {
        id: 'factory.dr1.four-floor-01', name: 'FOUR FLOOR 01', machine: 'DR1', bars: 1,
        pattern: bytesToB64(emptyClipBytes('DR1', 1)), family: 'house', role: 'four-on-floor',
        energy: 4, tags: ['driving', 'straight'], transpose: false,
      },
      {
        id: 'factory.wt1.glass-hook-01', name: 'GLASS HOOK', machine: 'WT1', bars: 2,
        pattern: bytesToB64(emptyClipBytes('WT1', 2)), family: 'ambient', role: 'hook',
        energy: 2, tags: ['bright', 'melodic'], root: 0, scale: 'minor', transpose: true,
      },
    ],
  };
}

function mutate(change: (doc: Record<string, unknown>) => void): unknown {
  const doc = structuredClone(validDoc()) as unknown as Record<string, unknown>;
  change(doc);
  return doc;
}

describe('clip library taxonomy', () => {
  it('has canonical roles for all machines and shared family/tag vocabularies', () => {
    expect(Object.keys(CLIP_ROLES)).toEqual(['DR1', 'BL1', 'WT1']);
    expect(CLIP_ROLES.DR1).toContain('four-on-floor');
    expect(CLIP_ROLES.BL1).toContain('acid');
    expect(CLIP_ROLES.WT1).toContain('hook');
    expect(CLIP_FAMILIES).toContain('lo-fi');
    expect(CLIP_TAGS).toContain('triplet-feel');
  });
});

describe('validateClipLibrary', () => {
  it('accepts a complete portable v1 document', () => {
    expect(validateClipLibrary(validDoc())).toBeNull();
  });

  it.each([
    [null, 'library must be an object'],
    [{ v: 2, clips: [] }, 'unknown clip library version 2'],
    [{ v: 1 }, 'library: clips must be an array'],
    [{ v: 1, clips: [], extra: true }, 'library: unknown field extra'],
  ])('rejects malformed document %j', (doc, reason) => {
    expect(validateClipLibrary(doc)).toBe(reason);
  });

  it('rejects duplicate stable ids', () => {
    const doc = validDoc();
    doc.clips.push({ ...doc.clips[0] });
    expect(validateClipLibrary(doc)).toContain('duplicate id');
  });

  it('rejects missing, unknown and wrongly typed fields', () => {
    expect(validateClipLibrary(mutate((d) => { delete (d.clips as Record<string, unknown>[])[0].name; }))).toContain('missing name');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].extra = 1; }))).toContain('unknown field extra');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].transpose = 'yes'; }))).toContain('transpose must be boolean');
  });

  it('rejects invalid ids and blank names', () => {
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].id = 'bad id'; }))).toContain('invalid id');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].name = '  '; }))).toContain('invalid name');
  });

  it('enforces machines, bar limits and exact machine byte lengths', () => {
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].machine = 'NOPE'; }))).toContain('unknown machine');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].bars = 0; }))).toContain('bars out of range');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].bars = 1.5; }))).toContain('bars out of range');
    expect(validateClipLibrary(mutate((d) => {
      (d.clips as Record<string, unknown>[])[0].pattern = bytesToB64(new Uint8Array(bytesPerBar('BL1')));
    }))).toBe('clip 0: pattern is 48 bytes, expected 256');
    expect(validateClipLibrary(mutate((d) => {
      const bytes = emptyClipBytes('DR1', 1); bytes[7] = 3;
      (d.clips as Record<string, unknown>[])[0].pattern = bytesToB64(bytes);
    }))).toContain('invalid DR1 value');
    expect(validateClipLibrary(mutate((d) => {
      const bytes = emptyClipBytes('WT1', 2); bytes[1] = 12;
      (d.clips as Record<string, unknown>[])[1].pattern = bytesToB64(bytes);
    }))).toContain('invalid note');
  });

  it('requires canonical base64 rather than accepting the permissive protocol decoder', () => {
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].pattern = '!!!!'; }))).toContain('not canonical base64');
    expect(validateClipLibrary(mutate((d) => {
      const clip = (d.clips as Record<string, unknown>[])[0];
      clip.pattern = (clip.pattern as string).replace(/=+$/, '');
    }))).toContain('not canonical base64');
  });

  it('enforces machine-specific roles and canonical family/tags', () => {
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].role = 'hook'; }))).toContain('role is not valid for DR1');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].family = 'jazz'; }))).toContain('unknown family');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].tags = ['jazzy']; }))).toContain('unknown tag jazzy');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].tags = ['dark', 'dark']; }))).toContain('duplicate tag dark');
  });

  it('enforces energy, root and scale metadata bounds', () => {
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].energy = 6; }))).toContain('energy out of range');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].root = 12; }))).toContain('root out of range');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].scale = ''; }))).toContain('invalid scale');
    expect(validateClipLibrary(mutate((d) => { (d.clips as Record<string, unknown>[])[0].root = 0; }))).toContain('DR1 clips cannot have a root');
    expect(validateClipLibrary(mutate((d) => {
      delete (d.clips as Record<string, unknown>[])[1].root;
    }))).toContain('transpose requires a root');
  });
});

describe('clip library codec', () => {
  it('round-trips JSON metadata and packed bytes losslessly', () => {
    const doc = validDoc();
    const runtime = decodeClipLibrary(doc);
    expect(runtime.clips[0].pattern).toBeInstanceOf(Uint8Array);
    expect(runtime.clips[0].pattern.length).toBe(bytesPerBar('DR1'));
    expect(runtime.clips[1].pattern.length).toBe(2 * bytesPerBar('WT1'));
    expect(encodeClipLibrary(runtime)).toEqual(doc);
  });

  it('throws on invalid serialized and runtime libraries', () => {
    expect(() => decodeClipLibrary({ v: 1, clips: [{}] })).toThrow('missing id');
    const runtime = decodeClipLibrary(validDoc());
    runtime.clips[0].pattern = new Uint8Array(1);
    expect(() => encodeClipLibrary(runtime)).toThrow('pattern is 1 bytes, expected 256');
  });
});

describe('generated factory library', () => {
  it('loads the canonical shared data with unique IDs/names and the expected machine coverage', () => {
    const doc = { v: 1, clips: [...FACTORY_CLIP_LIBRARY] };
    expect(validateClipLibrary(doc)).toBeNull();
    const runtime = decodeClipLibrary(doc);
    expect(runtime.clips).toHaveLength(72);
    expect(runtime.clips.filter((clip) => clip.machine === 'DR1')).toHaveLength(32);
    expect(runtime.clips.filter((clip) => clip.machine === 'BL1')).toHaveLength(20);
    expect(runtime.clips.filter((clip) => clip.machine === 'WT1')).toHaveLength(20);
    expect(new Set(runtime.clips.map((clip) => clip.id)).size).toBe(runtime.clips.length);
    expect(new Set(runtime.clips.map((clip) => clip.name.toLowerCase())).size).toBe(runtime.clips.length);
    expect(Object.keys(FACTORY_CLIP_BYTE_FINGERPRINTS)).toEqual(runtime.clips.map((clip) => clip.id));
  });
});
