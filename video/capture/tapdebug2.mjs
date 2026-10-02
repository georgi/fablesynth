import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'bass');
await page.evaluate(() => { const s = window.__fableBl.store.getState(); s.setParam('flt.cut', 90); s.play(); });
const hf = () => page.evaluate(async () => {
  const a = window.__fableBl.engine.scopeAnalyser; const d = new Float32Array(a.frequencyBinCount);
  let acc = 0, hi = 0;
  for (let k = 0; k < 40; k++) { a.getFloatFrequencyData(d); const bw = a.context.sampleRate / a.fftSize;
    d.forEach((v, i) => { const p = Math.pow(10, v / 10); acc += p; if (i * bw > 1000) hi += p; }); await new Promise((r) => setTimeout(r, 50)); }
  return (10 * Math.log10(hi / acc)).toFixed(1) + ' dB';
});
console.log('cut 90 analyser >1k:', await hf(), await page.evaluate(() => window.__fableBl.engine.params['flt.cut']));
await page.evaluate(() => window.__fableBl.store.getState().setParam('flt.cut', 12000));
await page.waitForTimeout(300);
console.log('cut 12k analyser >1k:', await hf(), await page.evaluate(() => window.__fableBl.engine.params['flt.cut']));
await browser.close();
