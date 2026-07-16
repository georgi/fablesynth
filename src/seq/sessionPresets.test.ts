import { describe, expect, it } from 'vitest';
import { FACTORY_PRESETS } from '../presets';
import { FACTORY_SESSION_PRESETS } from './sessionPresets';
import { b64ToBytes, wtNoteIdx } from './protocol';
import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';

describe('SQ-4 factory session patch contract', () => {
  it('keeps the native session-library ordering and factory patch indices', () => {
    expect(FACTORY_SESSION_PRESETS).toHaveLength(24);
    expect(FACTORY_SESSION_PRESETS.map((preset) => preset.name)).toEqual([
      'NEON TALE', 'NEON CHASE', 'GLASS CIRCUIT', 'AFTERGLOW',
      'WAREHOUSE RAW', 'ACID FLASH', 'STEEL PULSE', 'PEAK SIGNAL',
      'DEEP FOG', 'GLASS BLOOM', 'FROZEN BELL', 'AIR TEMPLE',
      'DUST HOUSE', 'MIDNIGHT FLOOR', 'TAPE DISCO', 'CLEAN CLUB',
      'VHS GARDEN', 'POCKET DUST', 'TOY PARADE', 'WORN SIGNAL',
      'CHROME CATHEDRAL', 'MACHINE TENSION', 'VOID MARCH', 'FINAL HORIZON',
    ]);
    expect(FACTORY_SESSION_PRESETS[0].session.tracks.map((track) => track.patch)).toEqual([
      { kind: 'factory', index: 13 }, { kind: 'factory', index: 0 },
      { kind: 'factory', index: 3 }, { kind: 'factory', index: 11 },
    ]);
    expect(FACTORY_SESSION_PRESETS[1].session.tracks.map((track) => track.patch)).toEqual([
      { kind: 'factory', index: 13 }, { kind: 'factory', index: 2 },
      { kind: 'factory', index: 14 }, { kind: 'factory', index: 11 },
    ]);
  });

  it('uses clean lead voices and measured per-track scene faders', () => {
    const bassGains = new Set<number>();
    const leadGains = new Set<number>();
    const padGains = new Set<number>();
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      const [drums, bass, lead, pad] = preset.session.tracks;
      expect(drums!.gain).toBe(0.78);
      const leadPatch = FACTORY_PRESETS[(lead!.patch as { index: number }).index]!;
      expect(leadPatch.params['fx.drive.on'] ?? 0).toBe(0);
      expect(leadPatch.params['filter.drive'] ?? 0).toBe(0);
      bassGains.add(bass!.gain);
      leadGains.add(lead!.gain);
      padGains.add(pad!.gain);
    }
    expect(bassGains.size).toBeGreaterThan(3);
    expect(leadGains.size).toBeGreaterThan(3);
    expect(padGains.size).toBeGreaterThanOrEqual(3);
  });

  it('writes each pad chord as three bar-length notes', () => {
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      for (const scene of preset.session.scenes) {
        const pad = scene.clips[3]!;
        const bytes = b64ToBytes(pad.pattern);
        expect(pad.bars).toBe(4);
        for (let bar = 0; bar < 4; bar++) {
          expect([0, 1, 2].map((lane) => bytes[wtNoteIdx(bar, 0, lane)] & 1)).toEqual([1, 1, 1]);
          expect([0, 1, 2].map((lane) => bytes[wtNoteIdx(bar, 0, lane)] >> 2)).toEqual([16, 16, 16]);
          for (let step = 1; step < 16; step++) {
            expect([0, 1, 2].map((lane) => bytes[wtNoteIdx(bar, step, lane)] & 1)).toEqual([0, 0, 0]);
          }
        }
      }
    }
  });

  it('gives every session distinct bass, lead, and pad clips', () => {
    for (const track of [1, 2, 3]) {
      const clips = FACTORY_SESSION_PRESETS.map((preset) => preset.session.scenes[2].clips[track]!);
      expect(new Set(clips.map((clip) => clip.pattern)).size).toBe(FACTORY_SESSION_PRESETS.length);
      expect(new Set(clips.map((clip) => clip.name)).size).toBe(FACTORY_SESSION_PRESETS.length);
    }
  });

  it('composes six-note-per-bar lead phrases anchored to each chord', () => {
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      const lead = b64ToBytes(preset.session.scenes[2].clips[2]!.pattern);
      const pad = b64ToBytes(preset.session.scenes[2].clips[3]!.pattern);
      const tonic = pad[wtNoteIdx(0, 0, 0) + 1] & 0x7f;
      const minorScale = new Set([0, 2, 3, 5, 7, 8, 10]);
      for (let bar = 0; bar < 4; bar++) {
        const events = Array.from({ length: 16 }, (_, step) => {
          const offset = wtNoteIdx(bar, step, 0);
          return { step, offset, on: !!(lead[offset] & 1), duration: lead[offset] >> 2, pitch: lead[offset + 1] & 0x7f };
        }).filter((event) => event.on);
        expect(events).toHaveLength(6);

        const chord = new Set([0, 1, 2].map((lane) => pad[wtNoteIdx(bar, 0, lane) + 1] & 0x7f));
        expect(chord.has(events[0].pitch), `${preset.name} bar ${bar + 1} downbeat`).toBe(true);
        expect(chord.has(events[3].pitch), `${preset.name} bar ${bar + 1} midpoint`).toBe(true);

        events.forEach((event, index) => {
          const nextStep = events[index + 1]?.step ?? 16;
          expect(event.duration).toBeGreaterThan(0);
          expect(event.step + event.duration).toBeLessThanOrEqual(nextStep);
          const relative = (event.pitch - tonic + 12) % 12;
          const harmonicLeadingTone = relative === 11 && chord.has(event.pitch);
          expect(minorScale.has(relative) || harmonicLeadingTone, `${preset.name} bar ${bar + 1} scale`).toBe(true);
        });
      }
    }
  });

  it('voices every pad strictly below every lead note', () => {
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      for (const scene of preset.session.scenes) {
        const lead = scene.clips[2];
        const pad = scene.clips[3];
        if (!lead || !pad) continue;
        const pitches = (pattern: string) => {
          const bytes = b64ToBytes(pattern);
          const out: number[] = [];
          for (let i = 0; i < bytes.length; i += 3) {
            if (bytes[i]! & 1) out.push((bytes[i + 2]! - 1) * 12 + (bytes[i + 1]! & 0x7f));
          }
          return out;
        };
        const leadPitches = pitches(lead.pattern);
        const padPitches = pitches(pad.pattern);
        expect(Math.min(...leadPitches), `${preset.name} ${scene.name} lead floor`).toBeGreaterThanOrEqual(12);
        expect(Math.max(...leadPitches), `${preset.name} ${scene.name} lead ceiling`).toBeLessThanOrEqual(23);
        expect(Math.max(...padPitches), `${preset.name} ${scene.name} pad ceiling`).toBeLessThanOrEqual(11);
        expect(Math.min(...padPitches), `${preset.name} ${scene.name} pad floor`).toBeGreaterThanOrEqual(0);
      }
    }
  });

  it('generates a unique drum pattern for every song', () => {
    // DROP A (scene 2) carries the full groove: all 24 songs must differ.
    const drops = FACTORY_SESSION_PRESETS.map((preset) => preset.session.scenes[2]!.clips[0]!.pattern);
    expect(new Set(drops).size).toBe(FACTORY_SESSION_PRESETS.length);
  });

  it('varies the drums across scenes within each song', () => {
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      const [intro, build, dropA, dropB, brk, outro] = preset.session.scenes.map((scene) => scene.clips[0]);
      expect(brk, `${preset.name} break stays drumless`).toBeNull();
      const patterns = [intro, build, dropA, dropB, outro].map((clip) => clip!.pattern);
      expect(new Set(patterns).size, `${preset.name} scene drums`).toBe(5);
    }
  });

  it('ends busy scenes with a bar-4 fill', () => {
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      for (const sceneIndex of [1, 2, 3]) {
        const clip = preset.session.scenes[sceneIndex]!.clips[0]!;
        const bytes = b64ToBytes(clip.pattern);
        const bar = (n: number) => bytes.slice(n * 256, (n + 1) * 256).join(',');
        expect(bar(3), `${preset.name} scene ${sceneIndex}`).not.toBe(bar(0));
      }
    }
  });

  it('never reuses a library clip in a generated preset', () => {
    const library = new Set(FACTORY_CLIP_LIBRARY.map((clip) => clip.pattern));
    for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
      for (const scene of preset.session.scenes) {
        const drums = scene.clips[0];
        if (drums) expect(library.has(drums.pattern), `${preset.name} ${scene.name}`).toBe(false);
      }
    }
  });
});
