import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'bass');
await page.evaluate(() => window.__fableBl.store.getState().play());
const meas = () => page.evaluate(async () => {
  const a = window.__fableBl.engine.scopeAnalyser; const d = new Float32Array(a.frequencyBinCount);
  let acc = 0, hi = 0;
  for (let k = 0; k < 25; k++) { a.getFloatFrequencyData(d); const bw = a.context.sampleRate / a.fftSize;
    d.forEach((v, i) => { const p = Math.pow(10, v / 10); acc += p; if (i * bw > 1000) hi += p; }); await new Promise((q) => setTimeout(q, 40)); }
  return (10 * Math.log10(hi / acc)).toFixed(1);
});
const set = (o) => page.evaluate((o) => { const s = window.__fableBl.store.getState(); for (const [k, v] of Object.entries(o)) s.setParam(k, v); }, o);
for (const table of [0, 1, 2, 3]) for (const pos of [0, 0.5, 1]) {
  for (const cut of [80, 12000]) { await set({ 'osc.table': table, 'osc.pos': pos, 'flt.cut': cut, 'flt.env': 0.3 }); await page.waitForTimeout(250); process.stdout.write(`t${table} p${pos} cut${cut}: ${await meas()}  `); }
  console.log();
}
await browser.close();
