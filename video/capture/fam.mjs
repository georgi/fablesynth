import { chromium } from 'playwright-core';
const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.goto('http://localhost:5199/'); await page.waitForTimeout(800);
await page.locator('#family .family-grid').scrollIntoViewIfNeeded(); await page.waitForTimeout(1500);
console.log(await page.evaluate(() => [...document.querySelectorAll('.family-shot')].map((i) => `${i.src.split('/').pop()} ${i.naturalWidth}x${i.naturalHeight} shown ${Math.round(i.getBoundingClientRect().height)}px opacity ${getComputedStyle(i.closest('.family-card')).opacity}`).join('\n')));
await page.screenshot({ path: process.argv[2] });
await browser.close();
