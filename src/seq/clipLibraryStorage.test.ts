import { describe, expect, it } from 'vitest';
import { decodeClipLibrary } from './clipLibrary';
import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';
import {
  IMPORTED_CLIPS_KEY,
  USER_CLIPS_KEY,
  assertUniqueClips,
  exportSqclip,
  importSqclip,
  readPersistentClips,
  sourceFactoryClips,
  uniqueClipName,
  writePersistentClips,
  type ClipStorage,
  type SourcedClip,
} from './clipLibraryStorage';

class MemoryStorage implements ClipStorage {
  values = new Map<string, string>();
  getItem(key: string) { return this.values.get(key) ?? null; }
  setItem(key: string, value: string) { this.values.set(key, value); }
}

const factory = sourceFactoryClips(decodeClipLibrary({ v: 1, clips: FACTORY_CLIP_LIBRARY }).clips);

function copy(overrides: Partial<SourcedClip> = {}): SourcedClip {
  return {
    ...factory[0], id: 'user-dr1-test', name: 'USER TEST',
    pattern: new Uint8Array(factory[0].pattern), tags: [...factory[0].tags],
    source: 'USER', ...overrides,
  };
}

describe('clip library sources and persistent storage', () => {
  it('marks factory clips explicitly and keeps USER/IMPORTED in separate stores', () => {
    const storage = new MemoryStorage();
    const user = copy();
    const imported = copy({ id: 'import-dr1-test', name: 'IMPORT TEST', source: 'IMPORTED' });
    writePersistentClips(storage, 'USER', [user]);
    writePersistentClips(storage, 'IMPORTED', [imported]);
    expect(storage.values.has(USER_CLIPS_KEY)).toBe(true);
    expect(storage.values.has(IMPORTED_CLIPS_KEY)).toBe(true);
    expect(readPersistentClips(storage, factory)).toEqual({ users: [user], imported: [imported] });
    expect(factory.every((clip) => clip.source === 'FACTORY')).toBe(true);
  });

  it('rejects corrupt storage and cross-source ID or case-insensitive name collisions', () => {
    const storage = new MemoryStorage();
    storage.setItem(USER_CLIPS_KEY, '{bad');
    expect(() => readPersistentClips(storage, factory)).toThrow();
    expect(() => assertUniqueClips([copy(), copy({ id: 'another', name: ' user test ' })])).toThrow('Duplicate clip name');
    expect(() => assertUniqueClips([copy(), copy({ name: 'OTHER' })])).toThrow('Duplicate clip id');
  });
});

describe('.sqclip import/export', () => {
  it('round-trips metadata and packed bytes losslessly', () => {
    const original = copy({ source: 'IMPORTED' });
    const encoded = exportSqclip([original]);
    const [decoded] = importSqclip(encoded, factory);
    expect(decoded).toEqual(original);
    expect(decoded.pattern).toBeInstanceOf(Uint8Array);
  });

  it('strictly validates JSON and rejects imported ID/name collisions', () => {
    expect(() => importSqclip('not json', factory)).toThrow('Invalid .sqclip JSON');
    expect(() => importSqclip('{"v":1,"clips":[]}', factory)).toThrow('at least one clip');
    expect(() => importSqclip(exportSqclip([factory[0]]), factory)).toThrow('Duplicate clip id');
    expect(() => importSqclip(exportSqclip([copy({ name: factory[0].name })]), factory)).toThrow('Duplicate clip name');
  });

  it('creates deterministic collision-free copy names', () => {
    expect(uniqueClipName('USER TEST', [copy()])).toBe('USER TEST 2');
    expect(uniqueClipName('USER TEST', [copy(), copy({ id: 'u2', name: 'USER TEST 2' })])).toBe('USER TEST 3');
  });
});
