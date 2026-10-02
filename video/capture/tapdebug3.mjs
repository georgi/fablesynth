import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'bass');
await page.evaluate(() => window.__fableBl.store.getState().play());
const meas = () => page.evaluate(async () => {
  const a = window.__fableBl.engine.scopeAnalyser; const d = new Float32Array(a.frequencyBinCount); const td = new Float32Array(a.fftSize);
  let acc = 0, hi = 0, r = 0;
  for (let k = 0; k < 30; k++) { a.getFloatFrequencyData(d); a.getFloatTimeDomainData(td); const bw = a.context.sampleRate / a.fftSize;
    d.forEach((v, i) => { const p = Math.pow(10, v / 10); acc += p; if (i * bw > 1000) hi += p; }); r += td.reduce((s, v) => s + v * v, 0) / td.length; await new Promise((q) => setTimeout(q, 50)); }
  return `hf ${(10 * Math.log10(hi / acc)).toFixed(1)} rms ${(10 * Math.log10(r / 30)).toFixed(1)}`;
});
const set = (o) => page.evaluate((o) => { const s = window.__fableBl.store.getState(); for (const [k, v] of Object.entries(o)) s.setParam(k, v); }, o);
console.log('default', await meas(), await page.evaluate(() => JSON.stringify(Object.fromEntries(Object.entries(window.__fableBl.store.getState().params).filter(([k]) => /flt|osc.level|sub.level|master|fx.drive|seq.bpm/.test(k))))));
await set({ 'osc.level': 0, 'sub.level': 0 }); await page.waitForTimeout(300); console.log('osc+sub off', await meas());
await set({ 'osc.level': 0.8, 'sub.level': 0.55, 'fx.drive.on': 0, 'fx.ott.on': 0, 'fx.comp.on': 0 }); await page.waitForTimeout(300); console.log('fx off', await meas());
await set({ 'flt.cut': 60, 'flt.env': 0 }); await page.waitForTimeout(300); console.log('cut 60 env0', await meas());
await set({ 'flt.cut': 15000 }); await page.waitForTimeout(300); console.log('cut 15k env0', await meas());
console.log('playing?', await page.evaluate(() => JSON.stringify(Object.keys(window.__fableBl.store.getState()).filter(k=>/play/i.test(k)).map(k=>[k, typeof window.__fableBl.store.getState()[k]==='function'?'fn':window.__fableBl.store.getState()[k]]))));
await browser.close();
