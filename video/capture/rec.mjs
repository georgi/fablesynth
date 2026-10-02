// Performance recorder: drives the real FableSynth UI with a real mouse and
// records, on one wall clock, (1) a CDP screencast, (2) the page's audio via an
// AudioWorklet tap on every connection to ctx.destination, (3) the cursor path.
// Output per take: public/takes/<name>/{NNNN.jpg, audio.wav, take.json}.
import { chromium } from 'playwright-core';
import { mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';

export const BASE = 'http://localhost:5199';
export const OUT = new URL('../public/takes/', import.meta.url).pathname;
export const BPM = 126;
export const BAR_MS = (4 * 60000) / BPM;

const TAP = () => {
  const SRC = `
    class Tap extends AudioWorkletProcessor {
      constructor() { super(); this.on = false; this.L = new Float32Array(4096); this.R = new Float32Array(4096); this.n = 0; this.f0 = 0;
        this.port.onmessage = (e) => { this.on = e.data; this.n = 0; }; }
      process(inputs) {
        const i = inputs[0];
        if (this.on && i && i.length) {
          const l = i[0], r = i[1] || i[0];
          if (this.n === 0) this.f0 = currentFrame;
          this.L.set(l, this.n); this.R.set(r, this.n); this.n += l.length;
          if (this.n >= 4096) { this.port.postMessage({ f0: this.f0, L: this.L, R: this.R }, [this.L.buffer, this.R.buffer]);
            this.L = new Float32Array(4096); this.R = new Float32Array(4096); this.n = 0; }
        }
        return true;
      }
    }
    registerProcessor('fable-tap', Tap);`;
  const url = URL.createObjectURL(new Blob([SRC], { type: 'application/javascript' }));
  const orig = AudioNode.prototype.connect;
  const taps = new Map();
  window.__taps = taps;
  const tapFor = (ctx) => {
    let t = taps.get(ctx);
    if (t) return t;
    t = { ctx, node: null, pending: [], chunks: [], ref: null };
    taps.set(ctx, t);
    ctx.audioWorklet.addModule(url).then(() => {
      t.node = new AudioWorkletNode(ctx, 'fable-tap', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit' });
      t.node.port.onmessage = (e) => t.chunks.push(e.data);
      const z = ctx.createGain();
      z.gain.value = 0;
      orig.call(t.node, z);
      orig.call(z, ctx.destination);
      for (const s of t.pending) orig.call(s, t.node);
      t.pending = [];
    });
    return t;
  };
  AudioNode.prototype.connect = function (dest, ...rest) {
    const r = orig.call(this, dest, ...rest);
    if (dest instanceof AudioDestinationNode) {
      const t = tapFor(dest.context);
      if (t.node) orig.call(this, t.node);
      else t.pending.push(this);
    }
    return r;
  };
  window.__tapStart = () => {
    for (const t of taps.values()) {
      if (!t.node) continue;
      t.chunks = [];
      const ots = t.ctx.getOutputTimestamp();
      t.ref = { wall: performance.timeOrigin + ots.performanceTime, ctxTime: ots.contextTime, sr: t.ctx.sampleRate };
      t.node.port.postMessage(true);
    }
    return taps.size;
  };
  window.__tapStop = () => {
    const out = [];
    for (const t of taps.values()) {
      if (!t.node) continue;
      t.node.port.postMessage(false);
      if (!t.chunks.length) continue;
      const n = t.chunks.length * 4096;
      const L = new Float32Array(n), R = new Float32Array(n);
      t.chunks.forEach((c, k) => { L.set(c.L, k * 4096); R.set(c.R, k * 4096); });
      // Wall time (ms) of the first recorded sample, via the output timestamp.
      const wall0 = t.ref.wall + (t.chunks[0].f0 / t.ref.sr - t.ref.ctxTime) * 1000;
      out.push({ sr: t.ref.sr, wall0, contiguous: t.chunks.every((c, k) => !k || c.f0 - t.chunks[k - 1].f0 === 4096), L: Array.from(L), R: Array.from(R) });
    }
    return out;
  };
};

function wav(path, L, R, sr) {
  const n = L.length;
  const buf = Buffer.alloc(44 + n * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 4, 4); buf.write('WAVE', 8); buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22); buf.writeUInt32LE(sr, 24);
  buf.writeUInt32LE(sr * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(n * 4, 40);
  const s = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
  for (let i = 0; i < n; i++) { buf.writeInt16LE(s(L[i]), 44 + i * 4); buf.writeInt16LE(s(R[i]), 46 + i * 4); }
  writeFileSync(path, buf);
}

export async function launch() {
  const browser = await chromium.launch({
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio', '--force-device-scale-factor=2'],
  });
  const ctx = await browser.newContext({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 2 });
  await ctx.addInitScript(TAP);
  const page = await ctx.newPage();
  page.on('pageerror', (e) => console.log('pageerror', e.message));
  return { browser, page };
}

export async function boot(page, path) {
  await page.goto(`${BASE}/${path}/`);
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  await page.waitForTimeout(1200);
  await page.evaluate(() => document.getElementById('power-on')?.click());
  await page.waitForTimeout(2500);
}

/** Records one take. `perform` receives a cursor API with logged real mouse actions. */
export async function take(page, name, perform) {
  const dir = join(OUT, name);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const cdp = await page.context().newCDPSession(page);
  const frames = [];
  const cursor = [];
  const marks = {};
  let pos = { x: 960, y: 1180 }; // starts off-screen below
  let down = false;
  cdp.on('Page.screencastFrame', async (f) => {
    frames.push({ t: f.metadata.timestamp * 1000, data: f.data });
    cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }).catch(() => {});
  });
  const taps = await page.evaluate(() => window.__tapStart());
  const T0 = Date.now();
  await cdp.send('Page.startScreencast', { format: 'jpeg', quality: 88, maxWidth: 3840, maxHeight: 2160, everyNthFrame: 1 });
  const log = () => cursor.push({ t: Date.now() - T0, x: pos.x, y: pos.y, d: down ? 1 : 0 });
  const ease = (u) => (u < 0.5 ? 4 * u * u * u : 1 - Math.pow(-2 * u + 2, 3) / 2);
  const api = {
    T0,
    now: () => Date.now() - T0,
    wait: (ms) => page.waitForTimeout(ms),
    /** Waits until `ms` since take start (for bar-aligned actions). */
    until: async (ms) => { const d = ms - (Date.now() - T0); if (d > 0) await page.waitForTimeout(d); },
    move: async (x, y, ms = 450, curve = ease) => {
      const a = { ...pos };
      const t0 = Date.now();
      for (;;) {
        const u = Math.min(1, (Date.now() - t0) / ms);
        const k = curve(u);
        pos = { x: a.x + (x - a.x) * k, y: a.y + (y - a.y) * k };
        await page.mouse.move(pos.x, pos.y);
        log();
        if (u >= 1) break;
        await page.waitForTimeout(12);
      }
    },
    down: async () => { down = true; await page.mouse.down(); log(); },
    up: async () => { down = false; await page.mouse.up(); log(); },
    click: async (x, y, ms) => { await api.move(x, y, ms); await api.down(); await page.waitForTimeout(70); await api.up(); },
    /** Vertical knob drag along a path of y offsets (px, positive = up). */
    turn: async (x, y, steps, approach = 450) => {
      await api.move(x, y, approach);
      await api.down();
      for (const [dy, ms, curve] of steps) await api.move(x, pos.y - dy, ms, curve || ease);
      await api.up();
    },
    /** Follows a list of points on a fixed schedule; skips waits when behind. */
    path: async (pts, ms) => {
      const t0 = Date.now();
      for (let i = 0; i < pts.length; i++) {
        const due = t0 + (ms * i) / (pts.length - 1);
        const d = due - Date.now();
        if (d > 4) await page.waitForTimeout(d);
        pos = { x: pts[i][0], y: pts[i][1] };
        await page.mouse.move(pos.x, pos.y);
        log();
      }
    },
    drag: async (x0, y0, x1, y1, ms = 700) => { await api.move(x0, y0); await api.down(); await api.move(x1, y1, ms); await api.up(); },
    log,
    page,
    mark: (k) => { marks[k] = Date.now() - T0; },
  };
  await perform(api);
  const T1 = Date.now();
  await cdp.send('Page.stopScreencast');
  await page.waitForTimeout(200);
  const audio = await page.evaluate(() => window.__tapStop());
  // Frames: write jpgs, keep times relative to T0.
  const kept = frames.filter((f) => f.t >= T0 - 50 && f.t <= T1 + 50);
  kept.forEach((f, i) => writeFileSync(join(dir, `${String(i).padStart(4, '0')}.jpg`), Buffer.from(f.data, 'base64')));
  const times = kept.map((f) => Math.round(f.t - T0));
  // Audio: mix all contexts onto the T0 timeline.
  const dur = T1 - T0;
  let sr = 48000, L, R;
  if (audio.length) {
    sr = audio[0].sr;
    const n = Math.round((dur / 1000) * sr);
    L = new Float32Array(n); R = new Float32Array(n);
    for (const a of audio) {
      const off = Math.round(((a.wall0 - T0) / 1000) * sr);
      for (let i = 0; i < a.L.length; i++) { const j = i + off; if (j >= 0 && j < n) { L[j] += a.L[i]; R[j] += a.R[i]; } }
    }
    wav(join(dir, 'audio.wav'), L, R, sr);
  }
  let peak = 0;
  if (L) for (let i = 0; i < L.length; i++) peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
  const meta = { name, dur, times, cursor, marks, audio: audio.map((a) => ({ sr: a.sr, offsetMs: Math.round(a.wall0 - T0), contiguous: a.contiguous, len: a.L.length })), peak };
  writeFileSync(join(dir, 'take.json'), JSON.stringify(meta));
  const fps = (times.length / (dur / 1000)).toFixed(1);
  console.log(`${name}: ${dur} ms, ${times.length} frames (${fps} fps), taps=${taps}, audio=${JSON.stringify(meta.audio)}, peak=${peak.toFixed(3)}`);
  await cdp.detach();
  return meta;
}

/** Centre of the first element matching a selector, in CSS px. */
export async function center(page, selector, nth = 0) {
  const b = await page.locator(selector).nth(nth).boundingBox();
  if (!b) throw new Error(`missing ${selector}`);
  return { x: b.x + b.width / 2, y: b.y + b.height / 2, box: b };
}
