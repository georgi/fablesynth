import { chromium } from 'playwright-core';
const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'] });
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
for (const s of ['app', 'drum', 'bass', 'seq']) {
  await page.goto(`http://localhost:5199/${s}/`);
  await page.waitForTimeout(1200);
  const info = await page.evaluate(() => {
    const out = [];
    document.querySelectorAll('section, [class*="panel"], header').forEach((el) => {
      const r = el.getBoundingClientRect();
      if (r.width < 150 || r.height < 60) return;
      const t = (el.querySelector('h1,h2,h3,.title,[class*="title"]')?.textContent || '').trim().slice(0, 30);
      out.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''}.${[...el.classList].join('.')} [${Math.round(r.x)},${Math.round(r.y)} ${Math.round(r.width)}x${Math.round(r.height)}] ${t}`);
    });
    const btns = [...document.querySelectorAll('button')].map((b) => b.id || b.getAttribute('aria-label') || b.textContent.trim()).filter(Boolean).slice(0, 60);
    return out.slice(0, 50).join('\n') + '\nBUTTONS: ' + btns.join(' | ') + '\nGLOBALS: ' + Object.keys(window).filter((k) => k.startsWith('__')).join(',');
  });
  console.log(`=== ${s}\n${info}`);
}
await browser.close();
