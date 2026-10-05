import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'seq');
await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
await page.evaluate(() => { const sel = document.querySelector('header select'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(1200);
for (const t of [3, 1]) {
  await page.evaluate((t) => window.__fableSq.store.getState().enterFocus(t, 1), t);
  await page.waitForTimeout(900);
  console.log('--- focus', t);
  console.log(await page.evaluate(() => {
    const out = [];
    for (const el of document.querySelectorAll('[class], [id]')) {
      const r = el.getBoundingClientRect();
      if (r.width < 150 || r.height < 40 || r.width > 1700) continue;
      const cls = (el.className?.baseVal ?? el.className ?? '').toString().split(' ').filter(Boolean).slice(0, 2).join('.');
      out.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''}.${cls} ${Math.round(r.x)},${Math.round(r.y)} ${Math.round(r.width)}x${Math.round(r.height)}`);
    }
    return out.slice(0, 70).join('\n');
  }));
}
await browser.close();
