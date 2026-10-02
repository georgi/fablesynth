import { chromium } from 'playwright-core';
const [, , url, out, w = '1440', h = '900', full = '1'] = process.argv;
const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', args: ['--mute-audio'] });
const page = await browser.newPage({ viewport: { width: +w, height: +h } });
await page.goto(url);
await page.waitForTimeout(1500);
// reveal-on-scroll content: force visible
await page.evaluate(async () => { for (let y = 0; y < document.body.scrollHeight; y += 600) { window.scrollTo(0, y); await new Promise((r) => setTimeout(r, 60)); } window.scrollTo(0, 0); });
await page.evaluate(() => document.querySelectorAll('.reveal').forEach((e) => e.classList.add('in')));
await page.evaluate(async () => { for (const v of document.querySelectorAll('.clip video')) { v.src = v.dataset.src; v.muted = true; await new Promise((r) => { v.onloadeddata = r; v.currentTime = +v.closest('.clip').dataset.start + 2; setTimeout(r, 2500); }); } });
await page.waitForTimeout(1500);
await page.screenshot({ path: out, fullPage: full === '1' });
console.log(await page.evaluate(() => document.documentElement.scrollHeight));
await browser.close();
