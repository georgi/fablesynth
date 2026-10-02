import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'seq');
await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
for (const v of [4, 5, 6, 7, 12, 13]) {
  await page.evaluate((v) => { const sel = document.querySelector('header select'); sel.value = String(v); sel.dispatchEvent(new Event('change', { bubbles: true })); }, v);
  await page.waitForTimeout(500);
  console.log(v, await page.evaluate(() => { const s = window.__fableSq.store.getState().session; return JSON.stringify({ n: s.name, bpm: s.bpm, tracks: s.tracks.map((t) => `${t.name}:${t.machine ?? t.device}:${t.patch?.base ?? t.patch?.name ?? t.patch?.kind}`), scenes: s.scenes.map((x) => x.name + '[' + x.clips.map((c) => (c ? 1 : 0)).join('') + ']') }); }));
}
await browser.close();
