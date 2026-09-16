// Reusable, allocation-free stereo OTT-style dynamics component.
// Load with audioWorklet.addModule before an instrument's processor module.
// Original implementation, not an emulation of a proprietary preset.
// Slow, stereo-linked energy matching. Measure the uncorrected wet signal so
// this is feed-forward compensation, not a feedback leveler. Hold in silence.
class AutoGain {
  constructor(sr) {
    this.energyCoef = 1 - Math.exp(-1 / (0.3 * sr));
    this.gainCoef = 1 - Math.exp(-1 / (0.15 * sr));
    this.reset();
  }
  reset() { this.input = 0; this.wet = 0; this.gain = 1; this.target = 1; this.tick = 0; }
  next(inL, inR, wetL, wetR) {
    const input = 0.5 * (inL * inL + inR * inR);
    const wet = 0.5 * (wetL * wetL + wetR * wetR);
    this.input += (input - this.input) * this.energyCoef;
    this.wet += (wet - this.wet) * this.energyCoef;
    if (this.tick === 0 && input > 1e-12 && this.input > 1e-12 && this.wet > 1e-16) {
      // -48 to +24 dB, sufficient to tame OTT boosts without runaway makeup.
      this.target = Math.max(0.003981071706, Math.min(15.848931925, Math.sqrt(this.input / this.wet)));
    }
    this.tick = (this.tick + 1) & 15;
    this.gain += (this.target - this.gain) * this.gainCoef;
    return this.gain;
  }
}

// Shared -1 dBFS sample-peak guard. No lookahead; stereo-linked 80 ms release.
class PeakGuard {
  constructor(sr) { this.release = 1 - Math.exp(-1 / (0.08 * sr)); this.gain = 1; }
  reset() { this.gain = 1; }
  gainFor(l, r) {
    const pk = Math.max(Math.abs(l), Math.abs(r));
    const required = pk > 0.8912509381337456 ? 0.8912509381337456 / pk : 1;
    this.gain = Math.min(required, this.gain + (1 - this.gain) * this.release);
    if (required === 1 && 1 - this.gain < 1e-8) this.gain = 1;
    return this.gain;
  }
  process(L, R, n) {
    for (let i = 0; i < n; i++) {
      const l = L[i], r = R[i];
      if (!Number.isFinite(l) || !Number.isFinite(r)) { L[i] = 0; R[i] = 0; continue; }
      const g = this.gainFor(l, r);
      if (g !== 1) { L[i] = l * g; R[i] = r * g; }
    }
  }
}

// DR-1's 4:1 compressor with a 9 dB knee and measured automatic gain.
class Compressor {
  constructor(sr) {
    this.smooth = 1 - Math.exp(-1 / (0.02 * sr));
    this.attack = 1 - Math.exp(-1 / (0.003 * sr));
    this.release = 1 - Math.exp(-1 / (0.25 * sr));
    this.autoGain = new AutoGain(sr);
    this.wet = 0; this.threshold = -16;
    this.setParams(false, -16); this.reset();
  }
  setParams(on, threshold) {
    this.wetTarget = on ? 1 : 0;
    this.thresholdTarget = Math.max(-40, Math.min(0, threshold));
  }
  reset() { this.env = 0; this.gain = 1; this.step = 0; this.tick = 0; this.l = 0; this.r = 0; this.autoGain.reset(); }
  processSample(l, r) {
    if (this.wetTarget === 0 && this.wet < 1e-6) {
      if (this.wet !== 0) { this.wet = 0; this.reset(); }
      this.l = l; this.r = r; return;
    }
    const pk = Math.max(Math.abs(l), Math.abs(r));
    this.env += (pk - this.env) * (pk > this.env ? this.attack : this.release);
    this.threshold += (this.thresholdTarget - this.threshold) * this.smooth;
    if (this.tick === 0) {
      const over = 20 * Math.log10(Math.max(1e-9, this.env)) - this.threshold;
      const db = over <= 0 ? 0 : over < 9 ? -0.75 * over * over / 18 : -0.75 * (over - 4.5);
      this.step = (Math.pow(10, db / 20) - this.gain) / 32;
    }
    this.tick = (this.tick + 1) & 31;
    this.gain += this.step;
    const g = this.gain * this.autoGain.next(l, r, this.gain * l, this.gain * r);
    this.wet += (this.wetTarget - this.wet) * this.smooth;
    this.l = l + this.wet * (g * l - l);
    this.r = r + this.wet * (g * r - r);
  }
  process(L, R, n) {
    for (let i = 0; i < n; i++) { this.processSample(L[i], R[i]); L[i] = this.l; R[i] = this.r; }
  }
}

class OttCompressor {
  constructor(sr) {
    this.sr = sr;
    // Complementary one-pole splits reconstruct the input exactly at unity,
    // keeping parallel depth blending free of crossover phase cancellation.
    this.lowCoef = 1 - Math.exp(-2 * Math.PI * 120 / sr);
    this.highCoef = 1 - Math.exp(-2 * Math.PI * 2500 / sr);
    this.smooth = 1 - Math.exp(-1 / (0.015 * sr));
    this.split = new Float64Array(4);
    this.env = new Float64Array(3);
    this.gain = new Float64Array(3).fill(1);
    this.target = new Float64Array(3).fill(1);
    this.bandsL = new Float64Array(3);
    this.bandsR = new Float64Array(3);
    this.attack = new Float64Array(3);
    this.release = new Float64Array(3);
    this.depth = 0; this.tick = 0; this.l = 0; this.r = 0;
    this.autoGain = new AutoGain(sr);
    this.setParams(false, 0.35, 1, 1, 1);
  }

  setParams(on, depth, time, up, down) {
    this.depthTarget = on ? Math.max(0, Math.min(1, depth)) : 0;
    this.up = Math.max(0, Math.min(2, up));
    this.down = Math.max(0, Math.min(2, down));
    time = Math.max(0.01, Math.min(10, time));
    if (time !== this.time) {
      this.time = time;
      for (let b = 0; b < 3; b++) {
        this.attack[b] = 1 - Math.exp(-1 / ((b === 0 ? 0.008 : b === 1 ? 0.003 : 0.001) * time * this.sr));
        this.release[b] = 1 - Math.exp(-1 / ((b === 0 ? 0.18 : b === 1 ? 0.12 : 0.08) * time * this.sr));
      }
    }
  }

  reset() {
    this.split.fill(0); this.env.fill(0);
    this.gain.fill(1); this.target.fill(1); this.tick = 0;
    this.autoGain.reset();
  }

  process(L, R, n) {
    for (let i = 0; i < n; i++) { this.processSample(L[i], R[i]); L[i] = this.l; R[i] = this.r; }
  }

  processSample(inL, inR) {
    if (this.depthTarget === 0 && this.depth < 1e-6) {
      if (this.depth !== 0) { this.depth = 0; this.reset(); }
      this.l = inL; this.r = inR; return;
    }
    const s = this.split, l = this.bandsL, r = this.bandsR;
    s[0] += this.lowCoef * (inL - s[0]);
    s[1] += this.lowCoef * (inR - s[1]);
    s[2] += this.highCoef * (inL - s[0] - s[2]);
    s[3] += this.highCoef * (inR - s[1] - s[3]);
    l[0] = s[0]; l[1] = s[2]; l[2] = inL - s[0] - s[2];
    r[0] = s[1]; r[1] = s[3]; r[2] = inR - s[1] - s[3];
    let wetL = 0, wetR = 0;
    for (let b = 0; b < 3; b++) {
      // Stereo-linked detection prevents the image moving with dynamics.
      const pk = Math.max(Math.abs(l[b]), Math.abs(r[b]));
      this.env[b] += (pk - this.env[b]) * (pk > this.env[b] ? this.attack[b] : this.release[b]);
      if (this.tick === 0) {
        const db = 20 * Math.log10(Math.max(1e-9, this.env[b]));
        // At 100%: 4:1 upward below -36 dB, 10:1 downward above -18 dB.
        // Above 100% exaggerates the gain curve, including overcompression
        // (louder input can produce quieter output). Maximum boost: 48 dB.
        // Fade upward gain below -72 dB; never amplify digital silence.
        const floor = Math.max(0, Math.min(1, (db + 90) / 18));
        const boost = Math.min(24, Math.max(0, -36 - db) * 0.75) * this.up * floor;
        const cut = Math.max(0, db + 18) * 0.9 * this.down;
        this.target[b] = Math.pow(10, (boost - cut) / 20);
      }
      this.gain[b] += (this.target[b] - this.gain[b]) * this.smooth;
      wetL += l[b] * this.gain[b]; wetR += r[b] * this.gain[b];
    }
    this.tick = (this.tick + 1) & 15;
    this.depth += (this.depthTarget - this.depth) * this.smooth;
    const compensation = this.autoGain.next(inL, inR, wetL, wetR);
    this.l = inL + this.depth * (wetL * compensation - inL);
    this.r = inR + this.depth * (wetR * compensation - inR);
  }
}

globalThis.FableOttCompressor = OttCompressor;
globalThis.FableAutoGain = AutoGain;
globalThis.FablePeakGuard = PeakGuard;
globalThis.FableCompressor = Compressor;
