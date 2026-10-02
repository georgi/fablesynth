import { launch, boot, take } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'bass');
console.log(await page.evaluate(() => { const e = window.__fableBl.engine; return Object.keys(e || {}).join(','); }));
await page.evaluate(() => { const s = window.__fableBl.store.getState(); s.setParam('flt.cut', 90); s.play(); });
await take(page, 'tapdebug', async (c) => {
  await c.wait(2000);
  await page.evaluate(() => window.__fableBl.store.getState().setParam('flt.cut', 12000));
  await c.wait(2000);
});
console.log(await page.evaluate(() => [...window.__taps.values()].map((t) => `${t.ctx.state} sr=${t.ctx.sampleRate} base=${t.ctx.baseLatency} chunks=${t.chunks.length}`).join(' | ')));
await browser.close();
