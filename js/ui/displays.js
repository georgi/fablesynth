// Canvas visualizations. Each view exposes draw() — called from one global rAF
// loop in main.js — and skips work unless dirty or animating.

function setupCanvas(canvas) {
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const r = canvas.getBoundingClientRect();
  const w = Math.max(10, r.width), h = Math.max(10, r.height);
  if (canvas.width !== (w * dpr) | 0 || canvas.height !== (h * dpr) | 0) {
    canvas.width = (w * dpr) | 0;
    canvas.height = (h * dpr) | 0;
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h };
}

// ---------- 3D wavetable terrain ----------
export class WavetableView {
  constructor(canvas, accent) {
    this.canvas = canvas;
    this.accent = accent; // css color string
    this.table = null; // {frames, viz} viz = Float32Array(frames*128)
    this.pos = 0; // knob position 0..1
    this.modPos = -1; // live modulated position from DSP (-1 = idle)
    this.dirty = true;
    this.lastDrawnMod = -2;
  }
  setTable(t) { this.table = t; this.dirty = true; }
  setPos(p) { this.pos = p; this.dirty = true; }
  setModPos(p) { this.modPos = p; }

  draw() {
    const show = this.modPos >= 0 ? this.modPos : this.pos;
    if (!this.dirty && Math.abs(show - this.lastDrawnMod) < 0.004) return;
    this.dirty = false;
    this.lastDrawnMod = show;
    const { ctx, w, h } = setupCanvas(this.canvas);
    ctx.clearRect(0, 0, w, h);
    if (!this.table) return;

    const { frames, viz } = this.table;
    const N = viz.length / frames;
    const depthX = w * 0.22, depthY = h * 0.42;
    const waveW = w * 0.68, waveAmp = h * 0.17;
    const x0 = w * 0.06, y0 = h * 0.78;

    const posF = show * (frames - 1);

    for (let f = frames - 1; f >= 0; f--) {
      const d = f / (frames - 1);
      const ox = x0 + d * depthX;
      const oy = y0 - d * depthY;
      const near = Math.max(0, 1 - Math.abs(f - posF));
      ctx.beginPath();
      for (let i = 0; i < N; i++) {
        const x = ox + (i / (N - 1)) * waveW;
        const y = oy - viz[f * N + i] * waveAmp;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      if (near > 0.02) {
        ctx.strokeStyle = this.accent;
        ctx.globalAlpha = 0.25 + near * 0.75;
        ctx.lineWidth = 1 + near * 1.4;
        ctx.shadowColor = this.accent;
        ctx.shadowBlur = near * 14;
      } else {
        ctx.strokeStyle = '#8893a8';
        ctx.globalAlpha = 0.16 + d * 0.1;
        ctx.lineWidth = 1;
        ctx.shadowBlur = 0;
      }
      ctx.stroke();
    }
    ctx.globalAlpha = 1;
    ctx.shadowBlur = 0;
  }
}

// ---------- ADSR envelope ----------
export class EnvView {
  constructor(canvas, accent) {
    this.canvas = canvas;
    this.accent = accent;
    this.a = 0.01; this.d = 0.2; this.s = 0.8; this.r = 0.3;
    this.dirty = true;
  }
  set(a, d, s, r) { this.a = a; this.d = d; this.s = s; this.r = r; this.dirty = true; }
  draw() {
    if (!this.dirty) return;
    this.dirty = false;
    const { ctx, w, h } = setupCanvas(this.canvas);
    ctx.clearRect(0, 0, w, h);
    const pad = 6, W = w - pad * 2, H = h - pad * 2;
    // log-ish time scaling so short segments stay visible
    const seg = (t) => Math.pow(t / 12, 0.4);
    const ta = seg(this.a), td = seg(this.d), tr = seg(this.r);
    const hold = 0.22;
    const total = ta + td + tr + hold;
    const X = (t) => pad + (t / total) * W;
    const Y = (v) => pad + (1 - v) * H;

    ctx.beginPath();
    ctx.moveTo(X(0), Y(0));
    const curve = (x0, y0, x1, y1, bend) => {
      ctx.quadraticCurveTo(x0 + (x1 - x0) * bend, y1 + (y0 - y1) * (1 - bend) * 0.2, x1, y1);
    };
    curve(X(0), 0, X(ta), 1, 0.35); ctx.lineTo(X(ta), Y(1));
    ctx.moveTo(X(ta), Y(1));
    curve(X(ta), 1, X(ta + td), this.s, 0.3);
    ctx.lineTo(X(ta + td + hold), Y(this.s));
    curve(X(ta + td + hold), this.s, X(total), 0, 0.3);

    ctx.strokeStyle = this.accent;
    ctx.lineWidth = 1.6;
    ctx.shadowColor = this.accent;
    ctx.shadowBlur = 6;
    ctx.stroke();
    ctx.shadowBlur = 0;
    ctx.lineTo(X(total), Y(0));
    ctx.lineTo(X(0), Y(0));
    ctx.closePath();
    const grad = ctx.createLinearGradient(0, pad, 0, h);
    grad.addColorStop(0, this.accent + '44');
    grad.addColorStop(1, this.accent + '00');
    ctx.fillStyle = grad;
    ctx.fill();
  }
}

// ---------- LFO shape ----------
const LFO_FNS = [
  (p) => Math.sin(2 * Math.PI * p),
  (p) => 1 - 4 * Math.abs(p - 0.5),
  (p) => 1 - 2 * p,
  (p) => (p < 0.5 ? 1 : -1),
  null, // s&h drawn as steps
];
export class LFOView {
  constructor(canvas, accent) {
    this.canvas = canvas;
    this.accent = accent;
    this.shape = 0;
    this.rate = 2;
    this.dirty = true;
    this.t0 = performance.now();
  }
  set(shape, rate) { this.shape = shape | 0; this.rate = rate; this.dirty = true; }
  draw() {
    // always animates (phase dot)
    const { ctx, w, h } = setupCanvas(this.canvas);
    ctx.clearRect(0, 0, w, h);
    const pad = 5, W = w - pad * 2, mid = h / 2, amp = h / 2 - pad;
    ctx.beginPath();
    if (this.shape === 4) {
      const steps = 8;
      for (let s = 0; s < steps; s++) {
        const v = Math.sin(s * 78.233 + 12.9898) * 43758.5453;
        const y = mid - (v - Math.floor(v) - 0.5) * 2 * amp * 0.9;
        const x0 = pad + (s / steps) * W, x1 = pad + ((s + 1) / steps) * W;
        if (s === 0) ctx.moveTo(x0, y); else ctx.lineTo(x0, y);
        ctx.lineTo(x1, y);
      }
    } else {
      const fn = LFO_FNS[this.shape] || LFO_FNS[0];
      for (let i = 0; i <= 96; i++) {
        const p = i / 96;
        const x = pad + p * W, y = mid - fn(p) * amp * 0.9;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
    }
    ctx.strokeStyle = this.accent;
    ctx.lineWidth = 1.5;
    ctx.globalAlpha = 0.9;
    ctx.stroke();
    ctx.globalAlpha = 1;

    // phase dot (cosmetic free-run)
    const phase = ((performance.now() - this.t0) / 1000 * this.rate) % 1;
    let y;
    if (this.shape === 4) {
      const s = Math.floor(phase * 8);
      const v = Math.sin(s * 78.233 + 12.9898) * 43758.5453;
      y = mid - (v - Math.floor(v) - 0.5) * 2 * amp * 0.9;
    } else {
      y = mid - (LFO_FNS[this.shape] || LFO_FNS[0])(phase) * amp * 0.9;
    }
    ctx.beginPath();
    ctx.arc(pad + phase * W, y, 2.5, 0, Math.PI * 2);
    ctx.fillStyle = '#fff';
    ctx.shadowColor = this.accent;
    ctx.shadowBlur = 8;
    ctx.fill();
    ctx.shadowBlur = 0;
  }
}

// ---------- filter response ----------
export class FilterView {
  constructor(canvas, accent) {
    this.canvas = canvas;
    this.accent = accent;
    this.type = 1; this.cutoff = 9000; this.res = 0.18;
    this.dirty = true;
  }
  set(type, cutoff, res, on) { this.type = type | 0; this.cutoff = cutoff; this.res = res; this.on = on; this.dirty = true; }
  draw() {
    if (!this.dirty) return;
    this.dirty = false;
    const { ctx, w, h } = setupCanvas(this.canvas);
    ctx.clearRect(0, 0, w, h);
    const pad = 6;
    const k = 2 - 1.93 * this.res;
    const fmin = 20, fmax = 20000;

    // grid
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 1;
    for (const f of [100, 1000, 10000]) {
      const x = pad + (Math.log(f / fmin) / Math.log(fmax / fmin)) * (w - pad * 2);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
    }

    ctx.beginPath();
    for (let i = 0; i <= 120; i++) {
      const f = fmin * Math.pow(fmax / fmin, i / 120);
      const wn = f / this.cutoff;
      const den = Math.sqrt(Math.pow(1 - wn * wn, 2) + Math.pow(k * wn, 2));
      let mag;
      switch (this.type) {
        case 0: mag = 1 / den; break;
        case 1: mag = 1 / (den * den); break;
        case 2: mag = (k * wn) / den; break;
        case 3: mag = (wn * wn) / den; break;
        default: mag = Math.abs(1 - wn * wn) / den; break;
      }
      const db = Math.max(-30, Math.min(24, 20 * Math.log10(Math.max(1e-6, mag))));
      const x = pad + (i / 120) * (w - pad * 2);
      const y = h * 0.45 - (db / 30) * h * 0.42;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = this.on ? this.accent : '#5a6275';
    ctx.lineWidth = 1.8;
    ctx.shadowColor = this.accent;
    ctx.shadowBlur = this.on ? 7 : 0;
    ctx.stroke();
    ctx.shadowBlur = 0;
  }
}

// ---------- scope + spectrum ----------
export class ScopeView {
  constructor(canvas, analyser, accent) {
    this.canvas = canvas;
    this.analyser = analyser;
    this.accent = accent;
    this.buf = new Float32Array(analyser.fftSize);
  }
  draw() {
    const { ctx, w, h } = setupCanvas(this.canvas);
    this.analyser.getFloatTimeDomainData(this.buf);
    ctx.clearRect(0, 0, w, h);
    // stable trigger: find rising zero-crossing in first half
    let start = 0;
    for (let i = 1; i < this.buf.length / 2; i++) {
      if (this.buf[i - 1] <= 0 && this.buf[i] > 0) { start = i; break; }
    }
    const N = Math.min(900, this.buf.length - start);
    ctx.beginPath();
    for (let i = 0; i < N; i++) {
      const x = (i / (N - 1)) * w;
      const y = h / 2 - this.buf[start + i] * h * 0.46;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = this.accent;
    ctx.lineWidth = 1.2;
    ctx.globalAlpha = 0.95;
    ctx.stroke();
    ctx.globalAlpha = 1;
  }
}

export class SpectrumView {
  constructor(canvas, analyser, accent) {
    this.canvas = canvas;
    this.analyser = analyser;
    this.accent = accent;
    this.buf = new Uint8Array(analyser.frequencyBinCount);
  }
  draw() {
    const { ctx, w, h } = setupCanvas(this.canvas);
    this.analyser.getByteFrequencyData(this.buf);
    ctx.clearRect(0, 0, w, h);
    const bars = 48;
    const sr = this.analyser.context.sampleRate;
    const fmin = 30, fmax = Math.min(18000, sr / 2);
    const grad = ctx.createLinearGradient(0, h, 0, 0);
    grad.addColorStop(0, this.accent + '55');
    grad.addColorStop(1, this.accent);
    ctx.fillStyle = grad;
    for (let b = 0; b < bars; b++) {
      const f0 = fmin * Math.pow(fmax / fmin, b / bars);
      const f1 = fmin * Math.pow(fmax / fmin, (b + 1) / bars);
      const i0 = Math.floor((f0 / (sr / 2)) * this.buf.length);
      const i1 = Math.max(i0 + 1, Math.floor((f1 / (sr / 2)) * this.buf.length));
      let m = 0;
      for (let i = i0; i < i1 && i < this.buf.length; i++) m = Math.max(m, this.buf[i]);
      const v = m / 255;
      const bh = Math.pow(v, 1.4) * (h - 2);
      const bw = w / bars;
      ctx.fillRect(b * bw + 0.5, h - bh, bw - 1.5, bh);
    }
  }
}
