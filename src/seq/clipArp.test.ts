import { describe, expect, it } from 'vitest';
import { compileClipArp, newClipArp, validClipArp } from './clipArp';
import { factorySession } from './factory';
import { copySession } from './sessionPresets';
import { embedSessionPatches } from './sessionExport';
import { validateSession } from './protocol';

describe('portable clip arp data', () => {
  it('round-trips complete settings and deep copies them on recall and export', () => {
    const doc = factorySession(); const clip = doc.scenes[2].clips[1]!;
    clip.arp = { ...newClipArp('BL1'), enabled: true };
    clip.arp.settings.slides![3] = true;
    const recalled = copySession(doc), exported = embedSessionPatches(doc);
    expect(validateSession(JSON.parse(JSON.stringify(exported)))).toBeNull();
    expect(exported.scenes[2].clips[1]?.arp).toEqual(clip.arp);
    recalled.scenes[2].clips[1]!.arp!.settings.notes[0] = 99;
    exported.scenes[2].clips[1]!.arp!.settings.slides![3] = false;
    expect(clip.arp.settings.notes[0]).toBe(36); expect(clip.arp.settings.slides![3]).toBe(true);
  });
  it('rejects malformed timing, lanes, live-input state and arp data on drums', () => {
    const arp = newClipArp('WT1');
    expect(validClipArp(arp)).toBe(true);
    for (const patch of [{ rate: NaN }, { notes: [Infinity] }, { hits: [] }, { input: 'keys' }, { gate: 2 }]) {
      expect(validClipArp({ ...arp, settings: { ...arp.settings, ...patch } })).toBe(false);
    }
    const doc = factorySession(); doc.scenes[2].clips[0]!.arp = arp;
    expect(validateSession(doc)).toContain('invalid arpeggiator');
  });
  it('retains settings while disabled without sending an arp to the audio engine', () => {
    const clip = factorySession().scenes[2].clips[1]!;
    clip.arp = newClipArp('BL1'); expect(compileClipArp(clip)).toBeUndefined();
    clip.arp.enabled = true; expect(compileClipArp(clip)?.notes).toHaveLength(16);
  });
});
