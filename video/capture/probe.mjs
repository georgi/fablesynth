import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, process.argv[2]);
const r = await page.evaluate((sel) => [...document.querySelectorAll(sel)].map((e) => {
  const b = e.getBoundingClientRect();
  return `${e.getAttribute('aria-label')} @${Math.round(b.x + b.width / 2)},${Math.round(b.y + b.height / 2)} ${e.getAttribute('aria-valuetext') ?? ''}`;
}).join('\n'), process.argv[3] || '[role=slider]');
console.log(r);
console.log('taps', await page.evaluate(() => window.__taps.size));
await browser.close();
