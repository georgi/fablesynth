export type ArpOrder = 'up' | 'down' | 'updown' | 'played' | 'random';
export interface ArpState {
  notes: number[];
  order: ArpOrder;
  rate: number;
  octaves: number;
  gate: number;
  hits: boolean[];
  accents: boolean[];
  slides?: boolean[];
  seed: number;
  input: 'stored' | 'keys';
  latch: boolean;
}
export const newArp = (): ArpState => ({ notes: [50, 53, 57, 60], order: 'up', rate: .25,
  octaves: 1, gate: .65, hits: Array(16).fill(true), accents: Array.from({ length: 16 }, (_, i) => i % 4 === 0),
  seed: 1, input: 'stored', latch: false });
export const noteName = (n: number) => ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'][n % 12] + (Math.floor(n / 12) - 1);
export function arpNotes(a: ArpState, input = a.notes): number[] {
  const base = [...new Set(input)].filter(n => Number.isInteger(n) && n >= 0 && n <= 127);
  if (a.order !== 'played') base.sort((x, y) => x - y);
  const pool = Array.from({ length: a.octaves }, (_, oct) => base.map(n => n + oct * 12)).flat().filter(n => n <= 127);
  if (a.order === 'down') pool.reverse();
  if (a.order === 'updown' && pool.length > 2) pool.push(...pool.slice(1, -1).reverse());
  let seed = a.seed >>> 0;
  return Array.from({ length: 16 }, (_, i) => {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    return pool.length ? pool[(a.order === 'random' ? seed >>> 8 : i) % pool.length] : -1;
  });
}
export function loadArp(storageKey = 'wt1-arp-v1', fresh = newArp()): ArpState {
  try {
    const a = JSON.parse(localStorage.getItem(storageKey) || 'null');
    if (!a || !Array.isArray(a.notes) || !Array.isArray(a.hits) || !Array.isArray(a.accents)) return fresh;
    return { ...fresh, notes: a.notes.filter((n: number) => Number.isInteger(n) && n >= 24 && n <= 96).slice(0, 24),
      order: ['up', 'down', 'updown', 'played', 'random'].includes(a.order) ? a.order : 'up',
      rate: [.125, .25, .5, 1, 1/3, 1/6, .375, .75].includes(a.rate) ? a.rate : .25,
      octaves: [1, 2, 3].includes(a.octaves) ? a.octaves : 1,
      gate: Number.isFinite(a.gate) ? Math.max(.05, Math.min(.95, a.gate)) : .65,
      hits: fresh.hits.map((_, i) => a.hits[i] !== false), accents: fresh.accents.map((_, i) => a.accents[i] === true),
      ...(fresh.slides ? { slides: fresh.slides.map((_, i) => a.slides?.[i] === true) } : {}),
      seed: Number.isInteger(a.seed) ? a.seed : 1 };
  } catch { return fresh; }
}
