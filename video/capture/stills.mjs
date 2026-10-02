import { chromium } from 'playwright-core';
const OUT = new URL('../public/capture/', import.meta.url).pathname;
const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'] });
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
const byText = (t) => page.evaluate((t) => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === t)?.click(), t);
async function boot(p) {
  await page.goto(`http://localhost:5199/${p}/`); await page.evaluate(() => localStorage.clear()); await page.reload();
  await page.waitForTimeout(1200); await page.evaluate(() => document.getElementById('power-on')?.click()); await page.waitForTimeout(2500);
}
await boot('seq');
await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
await page.evaluate(() => document.querySelector('button[aria-label="Start sequencer"]')?.click());
await page.waitForTimeout(3000);
await byText('MASTER FX'); await page.waitForTimeout(2500);
await page.screenshot({ path: OUT + 'sq-masterfx.png' });
await byText('MASTER FX'); await byText('AGENT'); await page.waitForTimeout(600);
await page.locator('textarea').first().fill('Make the pads darker and wider, push the bass a little forward.');
await page.screenshot({ path: OUT + 'sq-agent.png' });
await boot('app');
await page.evaluate(() => window.__fable.store.getState().loadPresetByValue('f7'));
await page.evaluate(() => document.querySelector('button[aria-label="edit wavetable"]')?.click());
await page.waitForTimeout(600);
const items = await page.evaluate(() => [...document.querySelectorAll('[class*="wte"] *, [class*="wt-ed"] *')].filter((e) => e.textContent.trim() === 'VOX').map((e) => e.className).slice(0, 5));
console.log(items);
await page.getByText('VOX', { exact: true }).first().click().catch((e) => console.log('vox', e.message));
await page.waitForTimeout(800);
await page.screenshot({ path: OUT + 'wt-editor.png' });
await browser.close();
