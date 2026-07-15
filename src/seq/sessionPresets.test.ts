import { describe, expect, it } from 'vitest';
import { FACTORY_SESSION_PRESETS } from './sessionPresets';

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
      { kind: 'factory', index: 4 }, { kind: 'factory', index: 11 },
    ]);
  });
});
