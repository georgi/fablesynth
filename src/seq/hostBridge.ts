// Bridge between the SQ-4 session doc and the hosted device stores.
// The clip payloads and the device pattern buffers share one byte layout
// (bar-major == pattern-major), so the codec is copy/slice — editPattern
// in a hosted store means "bar".

import { bytesPerBar, type MachineId } from './protocol';

/** Clip bytes → a fresh device pattern buffer (bars beyond the clip stay empty). */
export function clipToPatterns(bytes: Uint8Array, empty: Uint8Array): Uint8Array {
  const out = empty.slice();
  out.set(bytes.subarray(0, Math.min(bytes.length, out.length)), 0);
  return out;
}

/** First `bars` bars of a device pattern buffer → clip bytes. */
export function patternsToClip(machine: MachineId, patterns: Uint8Array, bars: number): Uint8Array {
  return patterns.slice(0, bars * bytesPerBar(machine));
}
