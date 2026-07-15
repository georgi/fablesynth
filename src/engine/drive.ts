// Shared drive transfer used by WT-1, BL-1 and every DR-1 pad FX chain.
// Pre-gain creates the distortion; its inverse after the shaper keeps the
// effect from turning the drive control into a loudness control.

export interface DriveShape {
  k: number;
  preGain: number;
  compensation: number;
}

export function driveShape(amount: number): DriveShape {
  const amt = Math.max(0, Math.min(1, amount));
  const preGain = 1 + amt * 2;
  return {
    k: 1 + amt * 12,
    preGain,
    compensation: 1 / preGain,
  };
}

export function makeDriveCurve(amount: number, length = 513): { curve: Float32Array<ArrayBuffer>; preGain: number } {
  const { k, preGain, compensation } = driveShape(amount);
  const curve = new Float32Array(length);
  const norm = Math.tanh(k);
  const half = (length - 1) / 2;
  for (let i = 0; i < length; i++) {
    const x = i / half - 1;
    curve[i] = (Math.tanh(x * k) / norm) * compensation;
  }
  return { curve, preGain };
}
