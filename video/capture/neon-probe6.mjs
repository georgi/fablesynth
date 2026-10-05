import { launch, boot, center } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'seq');
await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
await page.evaluate(() => { const sel = document.querySelector('header select'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(1200);
await page.evaluate(() => window.__fableSq.store.getState().enterFocus(2, 4));
await page.waitForTimeout(800);
await page.evaluate(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'TRACK FX')?.click());
await page.waitForTimeout(800);
console.log(await page.evaluate(() => { const echo = document.querySelector('.panel-echo'); const body = echo.closest('.sq-device-body') ?? document.scrollingElement; const before = body.scrollTop; body.scrollTop += echo.getBoundingClientRect().top - 330; return `${body.className} ${before} -> ${body.scrollTop} sh ${body.scrollHeight} ch ${body.clientHeight}`; }));
await page.waitForTimeout(500);
for (const sel of ['.lab-card[aria-label="SHIFT"] .panel-head button', '.lab-card[aria-label="SHIFT"] [role=slider][aria-label="SHIFT"]', '.lab-card[aria-label="SHIFT"] [role=slider][aria-label="SPIRAL"]', '.panel-echo [role=slider][aria-label="FDBK"]']) {
  const c = await center(page, sel).catch((e) => ({ err: e.message.slice(0, 60) }));
  const hit = c.x ? await page.evaluate(([x, y]) => { const e = document.elementFromPoint(x, y); return e ? `${e.tagName}.${e.className?.baseVal ?? e.className} ${e.getAttribute('aria-label') ?? ''}` : 'none'; }, [c.x, c.y]) : '';
  console.log(sel, c.x ? `${Math.round(c.x)},${Math.round(c.y)}` : c.err, '→', hit);
}
const pw = await center(page, '.lab-card[aria-label="SHIFT"] .panel-head button');
await page.mouse.click(pw.x, pw.y); await page.waitForTimeout(300);
const k = await center(page, '.lab-card[aria-label="SHIFT"] [role=slider][aria-label="SPIRAL"]');
await page.mouse.move(k.x, k.y); await page.mouse.down();
for (let i = 1; i <= 20; i++) { await page.mouse.move(k.x, k.y - i * 4); await page.waitForTimeout(16); }
await page.mouse.up(); await page.waitForTimeout(300);
console.log('after', await page.evaluate(() => { const p = window.__fableSq.wt.getState().params; return `shift.on ${p['fx.shift.on']} fb ${p['fx.shift.fb']} hz ${p['fx.shift.hz']}`; }), await page.getAttribute('.lab-card[aria-label="SHIFT"] [role=slider][aria-label="SPIRAL"]', 'aria-valuetext'));
console.log('k box', JSON.stringify(k.box));
await page.screenshot({ path: '/private/tmp/claude-501/-Users-mg-dev-fablesynth/d33d8d18-ba75-4da1-94a1-d8922c0291b3/scratchpad/sq-fx.png' });
await browser.close();
