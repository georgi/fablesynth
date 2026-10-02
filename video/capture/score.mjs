import { chromium } from 'playwright-core';
const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', args: ['--mute-audio'] });
const page = await browser.newPage();
await page.goto('http://localhost:5199/app/');
await page.waitForTimeout(1200);
const r = await page.evaluate(() => {
  const st = window.__fable.store;
  const res = [];
  for (let i = 0; i < 61; i++) {
    st.getState().loadPresetByValue('f' + i);
    const s = st.getState();
    const p = s.params;
    const fx = Object.keys(p).filter((k) => /^fx\.[a-z]+\.on$/.test(k) && p[k]).map((k) => k.split('.')[1]);
    const mods = Object.keys(p).filter((k) => /^mat\d+\.src$/.test(k) && p[k]).length;
    res.push(`${i} ${s.presetValue} oscB=${p['oscB.on']} f2=${p['filter2.on'] ?? p['f2.on'] ?? '?'} mods=${mods} fx=${fx.join(',')}`);
  }
  return res.join('\n') + '\nKEYS:' + Object.keys(st.getState()).filter(k=>/mod|seq|note/i.test(k)).join(',');
});
console.log(r);
await browser.close();
