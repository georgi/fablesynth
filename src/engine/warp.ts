// Display-side copy of the oscillator warp in engine/worklet.js (setupOsc and
// renderOscWarp), for the wavetable view. Keep the two in step.
import { WARP_CROSS } from '../params';

const SYNC = 1, SYNCWIN = 2, BEND = 3, ASYM = 4, PWM = 5, MIRROR = 6, QUANT = 7, FM = 8, AM = 9;
const FM_MAX_CYCLES = 2;

/** The mode's map parameter for an amount in 0..1 (the worklet's `w`). */
export function warpParam(mode: number, amount: number): number {
  const amt = Math.min(1, Math.max(0, amount));
  switch (mode) {
    case SYNC: case SYNCWIN: return Math.pow(2, amt * 4);
    case BEND: case ASYM: return Math.pow(2, (amt - 0.5) * 6);
    case PWM: return 1 - 0.9 * amt;
    case MIRROR: return 0.5 + 0.4 * amt;
    case QUANT: return Math.pow(2, 1 + 7 * (1 - amt));
    case FM: return amt * amt * FM_MAX_CYCLES;
    default: return amt;
  }
}

/**
 * One warped sample at oscillator phase `pn` (cycles, 0..1). `read` returns
 * the unwarped waveform at a phase in cycles (any real number). `mod` is the
 * other oscillator's waveform at the same time, for FM, AM and RM.
 */
export function warpSample(mode: number, amount: number, pn: number, read: (q: number) => number, mod = 0): number {
  const w = warpParam(mode, amount);
  switch (mode) {
    case SYNC: return read(pn * w);
    case SYNCWIN: return read(pn * w) * (0.5 - 0.5 * Math.cos(2 * Math.PI * pn));
    case BEND: {
      if (pn < 0.5) { const x = 2 * pn; return read((0.5 * x) / (x + w * (1 - x))); }
      const x = 2 - 2 * pn;
      return read(1 - (0.5 * x) / (x + w * (1 - x)));
    }
    case ASYM: return read(pn / (pn + w * (1 - pn)));
    case PWM: return read(pn < w ? pn / w : 1);
    case MIRROR: return read(pn < w ? pn / w : (1 - pn) / (1 - w));
    case QUANT: return read(Math.floor(pn * w) / w);
    case FM: return read(pn + w * mod);
    case AM: return read(pn) * (1 - 0.5 * w + 0.5 * w * mod);
    default: return mode >= WARP_CROSS ? read(pn) * (1 - w + w * mod) : read(pn);
  }
}
