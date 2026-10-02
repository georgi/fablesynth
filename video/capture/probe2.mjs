import { launch, boot } from './rec.mjs';
const { browser, page } = await launch();
await boot(page, 'app');
console.log(await page.evaluate(() => { const s = window.__fable.store.getState(); return JSON.stringify({ keys: Object.keys(s).filter(k => /pattern|seq|editP|length/i.test(k)), p0: JSON.stringify(s.patterns).slice(0, 700), len: s.sequenceLength, bpm: s.params['seq.bpm'] }); }));
await boot(page, 'drum');
console.log(await page.evaluate(() => { const s = window.__fableDr.store.getState(); return JSON.stringify({ keys: Object.keys(s).filter(k => /pattern|seq|length/i.test(k)), p0: JSON.stringify(s.patterns).slice(0, 700), bpm: s.params['seq.bpm'] }); }));
await browser.close();
