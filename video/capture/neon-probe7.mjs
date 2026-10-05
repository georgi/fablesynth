import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'seq');
await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
await page.evaluate(() => { const sel = document.querySelector('header select'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(1200);
console.log(await page.evaluate(() => {
  const out = [];
  for (const el of document.querySelectorAll('[class]')) {
    const r = el.getBoundingClientRect(); if (r.width < 120 || r.height < 40 || r.width > 1500) continue;
    const cls = el.className.toString().split(' ').filter(Boolean).slice(0, 3).join('.');
    if (/scene|cell|clip|track|row|col/.test(cls)) out.push(`${cls} ${Math.round(r.x)},${Math.round(r.y)} ${Math.round(r.width)}x${Math.round(r.height)}`);
  }
  return [...new Set(out)].slice(0, 40).join('\n');
}));
await browser.close();
