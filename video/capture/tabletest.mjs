import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'app');
await page.evaluate(() => { const s = window.__fable.store.getState(); s.loadPresetByValue('f0'); for (const n of [50, 57, 60, 65]) s.playNote(n, 0.9); });
const spec = () => page.evaluate(async () => {
  const e = window.__fable.engine; const a = e.scopeAnalyser || e.analyser || e.spectrumAnalyser;
  if (!a) return Object.keys(e).join(',');
  const d = new Float32Array(a.frequencyBinCount); const acc = new Float32Array(a.frequencyBinCount);
  for (let k = 0; k < 10; k++) { a.getFloatFrequencyData(d); for (let i = 0; i < d.length; i++) acc[i] += Math.pow(10, d[i] / 20); await new Promise((r) => setTimeout(r, 30)); }
  return Array.from(acc);
});
const first = await spec();
if (typeof first === 'string') { console.log('engine keys', first); process.exit(); }
const dist = (a, b) => { // log-spectral distance on 1/3-octave-ish bands 100 Hz–10 kHz
  const n = a.length, bw = 22050 / n; let s = 0, c = 0;
  for (let f = 100; f < 10000; f *= 1.26) { const i0 = Math.floor(f / bw), i1 = Math.floor(f * 1.26 / bw) + 1;
    let ea = 1e-9, eb = 1e-9; for (let i = i0; i < i1; i++) { ea += a[i] ** 2; eb += b[i] ** 2; }
    s += (10 * Math.log10(ea) - 10 * Math.log10(eb)) ** 2; c++; }
  return Math.sqrt(s / c).toFixed(1);
};
for (let t = 0; t < 6; t++) {
  const sp = [];
  for (const pos of [0, 0.5, 1]) { await page.evaluate(([t, pos]) => { const s = window.__fable.store.getState(); s.setParam('oscA.table', t); s.setParam('oscA.pos', pos); }, [t, pos]); await page.waitForTimeout(200); sp.push(await spec()); }
  console.log(`table ${t}: d(0,.5)=${dist(sp[0], sp[1])} d(.5,1)=${dist(sp[1], sp[2])} d(0,1)=${dist(sp[0], sp[2])} dB`);
}
await browser.close();
