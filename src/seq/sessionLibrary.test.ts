import { describe, expect, it } from 'vitest';
import { factorySession } from './factory';
import {
  decodeSessionLibrary, encodeSessionLibrary, exportSessionJson,
  importSessionJson, sessionFileName, type SessionLibraryEntry,
} from './sessionLibrary';
import {
  newSessionId, readSessions, SESSIONS_KEY, uniqueSessionName, writeSessions,
} from './sessionLibraryStorage';

const entry = (over: Partial<SessionLibraryEntry> = {}): SessionLibraryEntry => ({
  id: 'sabc', name: 'TAKE 1', savedAt: 1000, session: factorySession(), ...over,
});

class MemoryStorage {
  data = new Map<string, string>();
  getItem(key: string) { return this.data.get(key) ?? null; }
  setItem(key: string, value: string) { this.data.set(key, value); }
}

describe('session library codec', () => {
  it('round-trips entries', () => {
    const doc = encodeSessionLibrary([entry(), entry({ id: 'sdef', name: 'TAKE 2' })]);
    expect(decodeSessionLibrary(JSON.parse(JSON.stringify(doc))).sessions).toHaveLength(2);
  });

  it('rejects an unknown version rather than migrating', () => {
    expect(() => decodeSessionLibrary({ v: 2, sessions: [] })).toThrow(/unknown version 2/);
  });

  it('rejects unknown entry keys', () => {
    expect(() => decodeSessionLibrary({ v: 1, sessions: [{ ...entry(), extra: 1 }] })).toThrow(/unknown key extra/);
  });

  it('rejects an entry whose clip bytes disagree with its bar count', () => {
    const bad = entry();
    const scene = bad.session.scenes.find((s) => s.clips.some(Boolean))!;
    const track = scene.clips.findIndex(Boolean);
    scene.clips[track] = { ...scene.clips[track]!, bars: scene.clips[track]!.bars + 1 };
    expect(() => decodeSessionLibrary({ v: 1, sessions: [bad] })).toThrow(/expected/);
  });
});

describe('session file interchange', () => {
  it('round-trips through export and import', () => {
    const session = factorySession();
    const result = importSessionJson(exportSessionJson(session));
    expect('session' in result).toBe(true);
    if ('session' in result) {
      expect(result.session.name).toBe(session.name);
      expect(result.session.scenes).toHaveLength(session.scenes.length);
    }
  });

  it('embeds factory patches so the file stands alone', () => {
    const json = exportSessionJson(factorySession());
    for (const track of JSON.parse(json).tracks) expect(track.patch.kind).toBe('inline');
  });

  it('renames on export when a library name is given', () => {
    expect(JSON.parse(exportSessionJson(factorySession(), 'TAKE 9')).name).toBe('TAKE 9');
  });

  it('reports why a bad file was rejected', () => {
    expect(importSessionJson('{oops')).toEqual({ error: 'not valid JSON' });
    expect(importSessionJson('{"v":2}')).toEqual({ error: 'unknown session version 2' });
    expect(importSessionJson(JSON.stringify({ ...factorySession(), bpm: 900 })))
      .toEqual({ error: 'bpm out of range' });
  });

  it('slugs file names', () => {
    expect(sessionFileName('NEON TALE')).toBe('neon-tale.json');
    expect(sessionFileName('  ')).toBe('session.json');
    expect(sessionFileName('Take #3 (final)')).toBe('take-3-final.json');
  });
});

describe('session library storage', () => {
  it('persists and reads back newest first', () => {
    const storage = new MemoryStorage();
    writeSessions(storage, [entry({ id: 'a', name: 'OLD', savedAt: 1 }), entry({ id: 'b', name: 'NEW', savedAt: 9 })]);
    expect(readSessions(storage).map((s) => s.name)).toEqual(['NEW', 'OLD']);
  });

  it('refuses duplicate names', () => {
    const storage = new MemoryStorage();
    expect(() => writeSessions(storage, [entry({ id: 'a' }), entry({ id: 'b', name: 'take 1' })]))
      .toThrow(/Duplicate session name/);
  });

  it('falls back to an empty library on corrupt data', () => {
    const storage = new MemoryStorage();
    storage.setItem(SESSIONS_KEY, '{not json');
    expect(readSessions(storage)).toEqual([]);
  });

  it('swallows quota failures', () => {
    const storage = { getItem: () => null, setItem: () => { throw new Error('QuotaExceeded'); } };
    expect(() => writeSessions(storage, [entry()])).not.toThrow();
  });

  it('de-duplicates names, skipping the entry being renamed', () => {
    const existing = [entry({ id: 'a', name: 'TAKE 1' }), entry({ id: 'b', name: 'TAKE 1 2' })];
    expect(uniqueSessionName('TAKE 1', existing)).toBe('TAKE 1 3');
    expect(uniqueSessionName('TAKE 1', existing, 'a')).toBe('TAKE 1');
    expect(uniqueSessionName('  ', [])).toBe('UNTITLED SESSION');
  });

  it('mints distinct ids', () => {
    expect(newSessionId(1, () => 0.1)).not.toBe(newSessionId(1, () => 0.9));
  });
});
