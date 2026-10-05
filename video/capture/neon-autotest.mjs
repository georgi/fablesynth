// Solo BL-1 in BUILD: no lane, then a flat low lane, then a flat high lane.
import { launch, boot, center, take } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'seq');
await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
await page.evaluate(() => { const sel = document.querySelector('header select'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(1200);
await page.evaluate(() => { const s = window.__fableSq.store.getState(); s.enterFocus(1, 1); s.toggleSolo(1); s.launchScene(1); });
await page.waitForTimeout(600);
const add = await center(page, '.sq-auto-add');
await page.evaluate(() => document.querySelector('button[aria-label="Start sequencer"]')?.click());
const BAR = 240000 / 132;
const log = [];
await take(page, 'neon-autotest', async (c) => {
const t0 = c.T0;
const flat = async (u) => {
  const ed = (await center(page, '.sq-auto svg[role=img]')).box;
  const y = ed.y + ed.height * (1 - u);
  await page.mouse.move(ed.x + 4, y); await page.mouse.down();
  for (let i = 0; i <= 40; i++) await page.mouse.move(ed.x + 4 + (ed.width - 8) * i / 40, y);
  await page.mouse.up();
};
await page.waitForTimeout(BAR * 4);
await page.mouse.click(add.x, add.y); await page.waitForTimeout(500);
log.push(['riser', Date.now() - t0]);
{ const ed = (await center(page, '.sq-auto svg[role=img]')).box; await page.mouse.move(ed.x + 4, ed.y + ed.height * 0.95); await page.mouse.down();
  for (let i = 0; i <= 48; i++) { const u = i / 48; await page.mouse.move(ed.x + 4 + (ed.width - 8) * u, ed.y + ed.height * (0.95 - 0.9 * u)); } await page.mouse.up(); }
await page.mouse.click(add.x, add.y); await page.waitForTimeout(400);
{ const a2 = await center(page, '.sq-auto-add'); await page.mouse.click(a2.x, a2.y); await page.waitForTimeout(400);
  await page.selectOption('.sq-auto-target select', 'flt.env'); await page.waitForTimeout(300);
  const ed = (await center(page, '.sq-auto svg[role=img]')).box; await page.mouse.move(ed.x + 4, ed.y + ed.height * 0.95); await page.mouse.down();
  for (let i = 0; i <= 48; i++) { const u = i / 48; await page.mouse.move(ed.x + 4 + (ed.width - 8) * u, ed.y + ed.height * (0.95 - 0.9 * u)); } await page.mouse.up(); }
log.push(['env', Date.now() - t0]);
await page.waitForTimeout(BAR * 9);
});
const lanes = await page.evaluate(() => JSON.stringify(window.__fableSq.store.getState().session.scenes[1].clips[1].automation ?? window.__fableSq.store.getState().session.scenes[1].clips[1]).slice(0, 400));
console.log(log, lanes);
await browser.close();
