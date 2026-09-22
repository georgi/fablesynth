export const DRUM_RHYTHM_VERSION = 1 as const;
export const DRUM_RHYTHM_LANE_COUNT = 16;
export const DRUM_RHYTHM_MAX_STEPS = 16;

export type DrumLaneTiming = { mode: 'grid' } | { mode: 'fit'; cycleBeats: 4 | 8 };

export interface DrumLaneRhythm {
  enabled: boolean;
  sourceBar: number;
  steps: number;
  rotation: number;
  timing: DrumLaneTiming;
}

export interface DrumRhythm {
  v: typeof DRUM_RHYTHM_VERSION;
  lanes: Array<DrumLaneRhythm | null>;
}

const record = (v: unknown): v is Record<string, unknown> =>
  typeof v === 'object' && v !== null && !Array.isArray(v);
const int = (v: unknown, lo: number, hi: number): v is number =>
  typeof v === 'number' && Number.isInteger(v) && v >= lo && v <= hi;

export function validateDrumRhythm(value: unknown, sourceBars = 16): string | null {
  if (!record(value)) return 'rhythm must be an object';
  if (value.v !== 1) return `unknown rhythm version ${String(value.v)}`;
  if (!Array.isArray(value.lanes) || value.lanes.length !== 16) return 'rhythm: expected sixteen lanes';
  for (let i = 0; i < value.lanes.length; i++) {
    const lane = value.lanes[i];
    if (lane === null) continue;
    if (!record(lane)) return `rhythm lane ${i}: must be an object or null`;
    if (typeof lane.enabled !== 'boolean') return `rhythm lane ${i}: enabled must be boolean`;
    if (!int(lane.sourceBar, 0, sourceBars - 1)) return `rhythm lane ${i}: sourceBar out of range`;
    if (!int(lane.steps, 1, 16)) return `rhythm lane ${i}: steps out of range`;
    if (!int(lane.rotation, 0, lane.steps - 1)) return `rhythm lane ${i}: rotation out of range`;
    if (!record(lane.timing)) return `rhythm lane ${i}: timing must be an object`;
    if (lane.timing.mode === 'grid') continue;
    if (lane.timing.mode !== 'fit' || (lane.timing.cycleBeats !== 4 && lane.timing.cycleBeats !== 8)) {
      return `rhythm lane ${i}: invalid timing mode/cycle`;
    }
  }
  return null;
}

export function cloneDrumRhythm(value: DrumRhythm | undefined, sourceBars = 16): DrumRhythm | undefined {
  if (value === undefined) return undefined;
  const error = validateDrumRhythm(value, sourceBars);
  if (error) throw new Error(`Invalid drum rhythm: ${error}`);
  return {
    v: 1,
    lanes: value.lanes.map((lane) => lane && {
      enabled: lane.enabled,
      sourceBar: lane.sourceBar,
      steps: lane.steps,
      rotation: lane.rotation,
      timing: lane.timing.mode === 'grid' ? { mode: 'grid' } : { mode: 'fit', cycleBeats: lane.timing.cycleBeats },
    }),
  };
}

export function assertDrumRhythm(value: unknown, sourceBars = 16): DrumRhythm {
  return cloneDrumRhythm(value as DrumRhythm, sourceBars)!;
}
