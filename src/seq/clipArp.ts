import { arpNotes, newArp, type ArpState } from '../arp';
import type { ClipDoc, MachineId } from './protocol';

export interface ClipArp { enabled: boolean; settings: ArpState }
export interface ArpConfig {
  notes: number[]; hits: boolean[]; accents: boolean[]; slides?: boolean[];
  rate: number; gate: number;
}
export function copyClipArp(arp: ClipArp): ClipArp {
  const a = arp.settings;
  return { enabled: arp.enabled, settings: { ...a, notes: [...a.notes], hits: [...a.hits], accents: [...a.accents],
    ...(a.slides ? { slides: [...a.slides] } : {}) } };
}
export function newClipArp(machine: MachineId): ClipArp {
  return { enabled: false, settings: { ...newArp(),
    ...(machine === 'BL1' ? { notes: [36, 39, 43, 46], slides: Array(16).fill(false) } : {}) } };
}
export function compileClipArp(clip: ClipDoc): ArpConfig | undefined {
  if (!clip.arp?.enabled) return;
  const a = clip.arp.settings;
  return { notes: arpNotes(a), hits: [...a.hits], accents: [...a.accents],
    ...(a.slides ? { slides: [...a.slides] } : {}), rate: a.rate, gate: a.gate };
}
export function validClipArp(value: unknown): value is ClipArp {
  if (!value || typeof value !== 'object') return false;
  const { enabled, settings: a } = value as ClipArp;
  const lane = (v: unknown) => Array.isArray(v) && v.length === 16 && v.every(b => typeof b === 'boolean');
  return typeof enabled === 'boolean' && !!a &&
    Array.isArray(a.notes) && a.notes.length <= 128 && a.notes.every(n => Number.isInteger(n) && n >= 0 && n <= 127) &&
    ['up', 'down', 'updown', 'played', 'random'].includes(a.order) &&
    [.125, .25, .5, 1, 1/3, 1/6, .375, .75].includes(a.rate) &&
    [1, 2, 3].includes(a.octaves) && Number.isFinite(a.gate) && a.gate >= .05 && a.gate <= .95 &&
    lane(a.hits) && lane(a.accents) && (a.slides === undefined || lane(a.slides)) &&
    Number.isInteger(a.seed) && a.input === 'stored' && a.latch === false;
}
