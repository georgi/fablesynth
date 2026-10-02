import { chromium } from 'playwright-core';
const out = process.argv[2];
const browser = await chromium.launch({
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'],
});
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
for (const s of ['app', 'drum', 'bass', 'seq', '']) {
  await page.goto(`http://localhost:5199/${s}${s ? '/' : ''}`);
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  await page.waitForTimeout(1500);
  await page.evaluate(() => document.getElementById('power-on')?.click());
  await page.waitForTimeout(3000);
  await page.screenshot({ path: `${out}/survey-${s || 'landing'}.png` });
  const h = await page.evaluate(() => document.documentElement.scrollHeight);
  console.log(s, 'scrollHeight', h);
}
await browser.close();
