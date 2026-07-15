// Bridge between the SQ-4 session doc and the hosted device stores.
// The clip payloads and the device pattern buffers share one byte layout
// (bar-major == pattern-major), so the codec is copy/slice — editPattern
// in a hosted store means "bar".

import { bytesPerBar, type MachineId, noteIdx, STEPS_PER_BAR, wtNoteIdx } from './protocol';

/** Clip bytes → a fresh device pattern buffer (bars beyond the clip stay empty). */
export function clipToPatterns(machine: MachineId, bytes: Uint8Array, empty: Uint8Array): Uint8Array {
  const out = empty.slice();
  if (machine !== 'WT1') out.set(bytes.subarray(0, Math.min(bytes.length, out.length)), 0);
  else for (let bar = 0; bar < bytes.length / bytesPerBar('WT1'); bar++) for (let step = 0; step < STEPS_PER_BAR; step++) {
    const from = wtNoteIdx(bar, step, 0), to = noteIdx(bar, step);
    out.set(bytes.subarray(from, from + 3), to);
  }
  return out;
}

/** First `bars` bars of a device pattern buffer → clip bytes. */
export function patternsToClip(machine: MachineId, patterns: Uint8Array, bars: number, base?: Uint8Array): Uint8Array {
  if (machine !== 'WT1') return patterns.slice(0, bars * bytesPerBar(machine));
  const out = base?.slice(0, bars * bytesPerBar('WT1')) ?? new Uint8Array(bars * bytesPerBar('WT1'));
  for (let i = 2; i < out.length; i += 3) if (out[i] === 0) out[i] = 1;
  for (let bar = 0; bar < bars; bar++) for (let step = 0; step < STEPS_PER_BAR; step++) {
    const from = noteIdx(bar, step), to = wtNoteIdx(bar, step, 0);
    out.set(patterns.subarray(from, from + 3), to);
  }
  return out;
}
