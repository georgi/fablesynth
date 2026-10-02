// Captures real FableSynth UI footage from the Vite dev server (port 5199).
// Each "clip" is a timed sequence of element screenshots taken while the
// instrument plays (muted), so live canvases (wavetable, scope, LFO, FX
// meters) animate. Remotion resamples the sequences by timestamp.
import { chromium } from 'playwright-core';
import { mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';

const OUT = new URL('../public/capture/', import.meta.url).pathname;
const BASE = 'http://localhost:5199';
const only = process.argv[2]; // optional: app | drum | bass | seq
const manifest = {};

const browser = await chromium.launch({
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'],
});
const ctx = await browser.newContext({ viewport: { width: 1920, height: 1720 }, deviceScaleFactor: 2 });
const page = await ctx.newPage();

async function clip(name, selector, { ms = 3000, during, scale = 'device', maxFrames = 120 } = {}) {
  const dir = join(OUT, name);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const el = page.locator(selector).first();
  const box = await el.boundingBox();
  if (!box) throw new Error(`no element for ${name}: ${selector}`);
  const times = [];
  const t0 = Date.now();
  for (let i = 0; i < maxFrames; i++) {
    const t = Date.now() - t0;
    if (t > ms) break;
    if (during) await page.evaluate(during, t / ms);
    await page.screenshot({ path: join(dir, `${String(i).padStart(3, '0')}.jpg`), clip: box, type: 'jpeg', quality: 90, scale });
    times.push(t);
  }
  manifest[name] = { frames: times.length, times, w: Math.round(box.width), h: Math.round(box.height) };
  console.log(`${name}: ${times.length} frames over ${times.at(-1)} ms (${Math.round(box.width)}x${Math.round(box.height)})`);
}

async function boot(path) {
  await page.goto(`${BASE}/${path}/`);
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  await page.waitForTimeout(1200);
  await page.evaluate(() => document.getElementById('power-on')?.click());
  await page.waitForTimeout(2500);
}
const clickLabel = (label) => page.evaluate((l) => document.querySelector(`button[aria-label="${l}"]`)?.click(), label);

// ---------------------------------------------------------------- WT-1
if (!only || only === 'app') {
  await boot('app');
  await page.evaluate(() => {
    const st = window.__fable.store.getState();
    st.loadPresetByValue('f7'); // NEURO WOBBLE: osc B, LFO wobble, 3 mod routes
    for (const fx of ['chorus', 'delay', 'reverb']) st.setParam(`fx.${fx}.on`, 1);
  });
  await page.evaluate(() => { const s = window.__fable.store.getState(); if (!s.seqPlaying) s.seqPlay?.(); });
  await clickLabel('Play sequencer');
  await page.waitForTimeout(1500);
  await page.evaluate(() => { window.__cut0 = window.__fable.store.getState().params['filter.cutoff']; });

  await clip('wt-full', 'body', { ms: 4000, scale: 'css' });
  await clip('wt-top', 'header.top-bar', { ms: 3000 });
  await clip('wt-oscA', '#panel-oscA', {
    ms: 4000,
    during: (u) => window.__fable.store.getState().setParam('oscA.pos', 0.5 - 0.5 * Math.cos(u * Math.PI * 2)),
  });
  await clip('wt-oscB', '#panel-oscB', { ms: 2500 });
  await clip('wt-filter', '.panel-filter', {
    ms: 4000,
    during: (u) => {
      const c0 = window.__cut0;
      const k = 0.12 + 0.88 * (0.5 + 0.5 * Math.cos(u * Math.PI * 2));
      window.__fable.store.getState().setParam('filter.cutoff', c0 * k);
    },
  });
  await page.evaluate(() => window.__fable.store.getState().setParam('filter.cutoff', window.__cut0));
  await clip('wt-lfos', '.panel-lfos', { ms: 3000 });
  await clip('wt-envs', '.panel-filter ~ .panel-env', { ms: 2000 });
  await clip('wt-matrix', '.panel-matrix', { ms: 2000 });
  await clip('wt-eq', '.panel-eq', { ms: 2500 });
  await clip('wt-ott', '.panel-dynamics', { ms: 3000 });
  await clip('wt-comp', '.panel-dynamics ~ .panel-dynamics', { ms: 3000 });
  await clip('wt-drive', '.panel-drive', { ms: 2500 });
  await clip('wt-chorus', '.panel-chorus', { ms: 2500 });
  await clip('wt-echo', '.panel-echo', { ms: 3000 });
  await clip('wt-reverb', '.panel-reverb', { ms: 3000 });
  await clip('wt-noteseq', '.ns-section', { ms: 3000 });
  await clip('wt-row1', '.panels', { ms: 2500, scale: 'css' });
  await clickLabel('edit wavetable');
  await page.waitForTimeout(800);
  await page.screenshot({ path: join(OUT, 'wt-editor.png'), scale: 'css' });
}

// ---------------------------------------------------------------- DR-1
if (!only || only === 'drum') {
  await boot('drum');
  await clickLabel('Play sequencer');
  await page.waitForTimeout(1200);
  await clip('dr-full', 'body', { ms: 4000, scale: 'css' });
  await clip('dr-pads', '.dr-pads-panel', { ms: 3000 });
  await clip('dr-osc', '.dr-osc-section', { ms: 2500 });
  await clip('dr-sample', '.dr-osc-section ~ .dr-osc-section', { ms: 2000 });
  await clip('dr-noise', '.dr-noise-section', { ms: 2500 });
  await clip('dr-seq', '.dr-stepseq', { ms: 4000 });
}

// ---------------------------------------------------------------- BL-1
if (!only || only === 'bass') {
  await boot('bass');
  await clickLabel('Play sequencer');
  await page.waitForTimeout(1200);
  await page.evaluate(() => { window.__cut0 = window.__fableBl.store.getState().params['flt.cut']; });
  await clip('bl-full', 'body', { ms: 4000, scale: 'css' });
  await clip('bl-filter', '.bl-filter-section', {
    ms: 4000,
    during: (u) => {
      const k = 0.25 + 1.6 * (0.5 - 0.5 * Math.cos(u * Math.PI * 2));
      window.__fableBl.store.getState().setParam('flt.cut', window.__cut0 * k);
    },
  });
  await clip('bl-osc', '.bl-osc-section', { ms: 2500 });
  await clip('bl-env', '.bl-env-section', { ms: 2000 });
  await clip('bl-acc', '.bl-acc-section', { ms: 2000 });
  await clip('bl-seq', '.bl-seq-section', { ms: 4000 });
}

// ---------------------------------------------------------------- SQ-4
if (!only || only === 'seq') {
  await boot('seq');
  await clickLabel('Close tour');
  await page.evaluate(() => document.querySelector('button[aria-label="Start sequencer"]')?.click());
  await page.waitForTimeout(3500);
  await page.evaluate(() => document.querySelectorAll('.sq-scene-launch')[2]?.click());
  await page.waitForTimeout(3500);
  await page.setViewportSize({ width: 1920, height: 1080 });
  await clip('sq-full', 'body', { ms: 5000, scale: 'css' });
  await page.evaluate(() => document.querySelectorAll('.sq-scene-launch')[3]?.click());
  await clip('sq-launch', 'body', { ms: 4000, scale: 'css' });
  for (const [t, name] of [[0, 'sq-focus-dr'], [1, 'sq-focus-bl'], [2, 'sq-focus-wt']]) {
    await page.evaluate((i) => window.__fableSq.store.getState().enterFocus(i), t);
    await page.waitForTimeout(900);
    await clip(name, 'body', { ms: 2500, scale: 'css' });
  }
  await page.evaluate(() => window.__fableSq.store.getState().exitFocus());
  await page.evaluate(() => document.querySelector('.sq-agent-toggle, button.agent-toggle')?.click());
  await page.evaluate(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'AGENT')?.click());
  await page.waitForTimeout(800);
  await page.screenshot({ path: join(OUT, 'sq-agent.png'), scale: 'css' });
  await page.evaluate(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'MASTER FX')?.click());
  await page.waitForTimeout(800);
  await page.screenshot({ path: join(OUT, 'sq-masterfx.png'), scale: 'css' });
}

const mfPath = join(OUT, 'manifest.json');
let prev = {};
try { prev = JSON.parse((await import('node:fs')).readFileSync(mfPath, 'utf8')); } catch {}
writeFileSync(mfPath, JSON.stringify({ ...prev, ...manifest }, null, 1));
await browser.close();
