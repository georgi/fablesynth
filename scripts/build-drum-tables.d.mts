// Type declarations for the offline converter scripts/build-drum-tables.mjs, so
// the vitest test (typechecked by the browser tsconfig, no @types/node) can
// import it. Hand-maintained to match the module's exports.

export const SIZE: number;
export const FRAMES: number;

export interface ParsedWav {
  sampleRate: number;
  channels: number;
  length: number;
  samples: Float32Array;
}

export function fft(re: Float64Array, im: Float64Array, inverse: boolean): void;
export function makePhases(seed?: number): Float64Array;
export function parseWav(buf: Uint8Array | ArrayBuffer): ParsedWav;
export function encodeWav(samples: Float32Array, sampleRate?: number): Uint8Array;
export function analyzeSampleToFrames(mono: Float32Array, phases?: Float64Array): Float32Array[];
export function framesToBase64(frames: Float32Array[]): string;
export function build(): { name: string; frames: Float32Array[]; base64: string }[];
