// Recorded performances for the promo. Every take runs at 126 BPM and marks
// the sequencer start ('play'), so the edit can cut on the bar grid.
import { launch, boot, take, center, BAR_MS } from './rec.mjs';

const only = process.argv.slice(2);
const want = (n) => !only.length || only.includes(n);
const { browser, page } = await launch();
const knob = (scope, label, nth = 0) => center(page, `${scope} [role=slider][aria-label="${label}"]`, nth);
const value = (scope, label) => page.getAttribute(`${scope} [role=slider][aria-label="${label}"]`, 'aria-valuetext');
const linear = (u) => u;

/** Page-side value sampler: records aria-valuetext of watched knobs on the wall clock. */
async function watch(list) {
  await page.evaluate((list) => {
    window.__watch = [];
    clearInterval(window.__watchTimer);
    window.__watchTimer = setInterval(() => {
      const row = { t: Date.now() };
      for (const [key, sel] of list) row[key] = document.querySelector(sel)?.getAttribute('aria-valuetext') ?? null;
      window.__watch.push(row);
    }, 40);
  }, list);
}
async function unwatch(meta) {
  const rows = await page.evaluate(() => { clearInterval(window.__watchTimer); return window.__watch; });
  return rows.map((r) => ({ ...r, t: r.t - meta.T0 }));
}

// WT-1 helpers ------------------------------------------------------------
const wt = (fn, arg) => page.evaluate(fn, arg);
async function wtSetup(preset, params) {
  await boot(page, 'app');
  await wt(([preset, params]) => {
    const s = window.__fable.store.getState();
    if (preset) s.loadPresetByValue(preset);
    for (const [k, v] of Object.entries(params)) s.setParam(k, v);
    s.setParam('seq.bpm', 126);
  }, [preset, params]);
}
async function wtNotes(notes) {
  await wt((notes) => {
    const s = window.__fable.store.getState();
    const p = new Uint8Array(s.patterns.length);
    // Rest every step of every pattern, then draw ours into pattern 0.
    const empty = [4, 0, 1];
    for (let i = 0; i < p.length; i += 3) p.set(empty, i);
    s._setPatterns(p);
    for (const [step, note, dur] of notes) window.__fable.store.getState().drawNote(step, note, dur, 0);
  }, notes);
}

async function record(name, perform, watchList) {
  if (watchList) await watch(watchList);
  let T0 = 0;
  const meta = await take(page, name, async (c) => { T0 = c.T0; await perform(c); });
  if (watchList) {
    const rows = await unwatch({ T0 });
    const fs = await import('node:fs');
    const p = new URL(`../public/takes/${name}/values.json`, import.meta.url).pathname;
    fs.writeFileSync(p, JSON.stringify(rows));
  }
  return meta;
}

// ---- A. WT-1 wavetable morph on a held chord -----------------------------
if (want('wt-morph')) {
  await wtSetup('f0', {
    'oscA.table': 0, 'oscA.pos': 0, 'oscA.unison': 5, 'oscA.detune': 0.18, 'oscA.spread': 0.6,
    'fx.reverb.on': 1, 'fx.reverb.mix': 0.22, 'fx.chorus.on': 1, 'amp.rel': 0.6,
  });
  const box = (await center(page, '#panel-oscA .pos-holder')).box;
  const x = box.x + box.width / 2;
  const yBottom = box.y + box.height - 6;
  await record('wt-morph', async (c) => {
    c.mark('play');
    await wt(() => { for (const n of [50, 57, 60, 65]) window.__fable.store.getState().playNote(n, 0.9); });
    await c.move(x + 40, yBottom + 80, 800);
    await c.move(x, yBottom, 350);
    await c.down();
    await c.move(x, box.y + 4, BAR_MS * 1.6, linear);
    await c.wait(250);
    await c.move(x, box.y + box.height * 0.55, BAR_MS * 0.9);
    await c.up();
    await c.move(x + 70, box.y + box.height * 0.55 + 60, 400);
    await c.until(BAR_MS * 3.3);
    await wt(() => { for (const n of [50, 57, 60, 65]) window.__fable.store.getState().playNote(n, 0); });
    await c.until(BAR_MS * 4);
  }, [['pos', '#panel-oscA .pos-holder [role=slider]']]);
}

// ---- B. WT-1 drag LFO 1 onto CUTOFF: the riff starts to wobble ------------
if (want('wt-mod')) {
  await wtSetup('f4', {
    'filter.cutoff': 380, 'filter.res': 0.55, 'filter.env': 0, 'lfo1.rate': 2.1, 'lfo1.retrig': 0, 'lfo1.sync': 0,
    'fx.delay.on': 0, 'fx.reverb.on': 1, 'fx.reverb.mix': 0.15,
  });
  for (let i = 1; i <= 16; i++) await wt((i) => { const s = window.__fable.store.getState(); s.setParam(`mat${i}.src`, 0); s.setParam(`mat${i}.amt`, 0); }, i);
  await wtNotes([[0, 0, 2], [3, 0, 1], [6, 3, 2], [8, 0, 1], [10, 7, 2], [12, 5, 1], [14, 3, 2]]);
  const chip = await center(page, '.panel-matrix [draggable=true]', 0);
  const cut = await knob('.panel-filter', 'CUTOFF');
  const rate = await knob('.panel-lfos', 'RATE', 0);
  await record('wt-mod', async (c) => {
    c.mark('play');
    await wt(() => window.__fable.store.getState().seqPlay());
    await c.move(chip.x, chip.y, BAR_MS * 0.7);
    await c.until(BAR_MS * 1.0);
    await c.down();
    await c.move(chip.x + 20, chip.y - 20, 120);
    await c.move(cut.x, cut.y, BAR_MS * 0.6);
    await c.up();
    c.mark('drop');
    await c.until(BAR_MS * 3);
    console.log('routes', await wt(() => { const p = window.__fable.store.getState().params; return [1, 2, 3].map((i) => `${p[`mat${i}.src`]}->${p[`mat${i}.dst`]}@${p[`mat${i}.amt`]}`).join(' '); }));
    await c.turn(rate.x, rate.y, [[70, BAR_MS * 1.2, linear]], BAR_MS * 0.4);
    await c.until(BAR_MS * 5.2);
  }, [['cutoff', '.panel-filter [role=slider][aria-label="CUTOFF"]'], ['rate', '.panel-lfos [role=slider][aria-label="RATE"]']]);
}

// ---- C. WT-1 FX: dry plucks, then TAPE ECHO, then REVERB ----------------
if (want('wt-fx')) {
  await wtSetup('f3', { 'fx.delay.on': 0, 'fx.reverb.on': 0, 'fx.chorus.on': 0, 'fx.delay.sync': 1, 'fx.delay.div': 3, 'fx.delay.fb': 0.55, 'fx.delay.mix': 0.42, 'fx.reverb.size': 0.55, 'fx.reverb.mix': 0.2 });
  await wtNotes([[0, 7, 1], [3, 3, 1], [6, 0, 1], [10, 10, 1]]);
  await wt(() => window.scrollTo(0, 480));
  await page.waitForTimeout(300);
  const echoPower = await center(page, '.panel-echo button[aria-label="power"], .panel-echo .power, .panel-echo button', 0);
  const revPower = await center(page, '.panel-reverb button[aria-label="power"], .panel-reverb .power, .panel-reverb button', 0);
  const mix = await knob('.panel-reverb', 'MIX');
  const size = await knob('.panel-reverb', 'SIZE');
  await record('wt-fx', async (c) => {
    c.mark('play');
    await wt(() => window.__fable.store.getState().seqPlay());
    await c.move(echoPower.x + 30, echoPower.y + 40, BAR_MS * 0.6);
    await c.until(BAR_MS * 1.0 - 120);
    await c.click(echoPower.x, echoPower.y, 150);
    c.mark('echo');
    await c.move(revPower.x - 40, revPower.y + 50, BAR_MS * 0.8);
    await c.until(BAR_MS * 2.0 - 120);
    await c.click(revPower.x, revPower.y, 150);
    c.mark('reverb');
    await c.until(BAR_MS * 2.5);
    await c.turn(size.x, size.y, [[60, BAR_MS * 0.7]], 300);
    await c.turn(mix.x, mix.y, [[50, BAR_MS * 0.7]], 300);
    await c.until(BAR_MS * 5);
  }, [['revmix', '.panel-reverb [role=slider][aria-label="MIX"]'], ['revsize', '.panel-reverb [role=slider][aria-label="SIZE"]']]);
}

// ---- D. BL-1: the acid filter sweep --------------------------------------
if (want('bl-sweep')) {
  await boot(page, 'bass');
  await page.evaluate(() => {
    const s = window.__fableBl.store.getState();
    s.setParam('seq.bpm', 126); s.setParam('osc.pos', 0.5); s.setParam('sub.level', 0.35);
    s.setParam('flt.cut', 70); s.setParam('flt.res', 0.5); s.setParam('flt.env', 0.3); s.setParam('lfo.depth', 0);
  });
  const cut = await knob('.bl-filter-section', 'CUT');
  const res = await knob('.bl-filter-section', 'RES');
  await record('bl-sweep', async (c) => {
    c.mark('play');
    await page.evaluate(() => window.__fableBl.store.getState().play());
    await c.move(cut.x + 30, cut.y + 60, 900);
    await c.until(BAR_MS * 1);
    await c.turn(cut.x, cut.y, [[150, BAR_MS * 2, linear]], 200);
    await c.turn(res.x, res.y, [[45, BAR_MS * 0.6]], 450);
    await c.until(BAR_MS * 4);
    await c.turn(cut.x, cut.y, [[-70, BAR_MS * 0.9], [45, BAR_MS * 0.6]], 450);
    await c.until(BAR_MS * 6.2);
  }, [['cut', '.bl-filter-section [role=slider][aria-label="CUT"]'], ['res', '.bl-filter-section [role=slider][aria-label="RES"]']]);
}

// ---- E. DR-1: build the beat live, one step at a time ---------------------
if (want('dr-build')) {
  await boot(page, 'drum');
  await page.evaluate(() => {
    const s = window.__fableDr.store.getState();
    s.setParam('seq.bpm', 126);
    s._setPatterns(new Uint8Array(s.patterns.length));
  });
  await page.evaluate(() => window.scrollTo(0, 190));
  await page.waitForTimeout(300);
  const step = (pad, n) => center(page, `button[aria-label^="${pad} step ${n}:"]`);
  const plan = [
    ['KICK', [1, 5, 9, 13]],
    ['CH HAT', [3, 7, 11, 15]],
    ['CLAP', [5, 13]],
    ['CH HAT', [2, 6, 10, 14, 16]],
    ['OH HAT', [15]],
    ['RIM', [8, 12]],
  ];
  const targets = [];
  for (const [pad, steps] of plan) for (const n of steps) targets.push(await step(pad, n));
  await record('dr-build', async (c) => {
    c.mark('play');
    await page.evaluate(() => window.__fableDr.store.getState().play());
    let k = 0;
    const phases = [[0.25, 4], [1.3, 4], [2.3, 2], [2.9, 5], [3.6, 1], [3.8, 2]];
    for (const [bar, count] of phases) {
      await c.until(BAR_MS * bar);
      for (let i = 0; i < count; i++, k++) await c.click(targets[k].x, targets[k].y, i ? 150 : 380);
    }
    await c.move(targets[k - 1].x + 60, targets[k - 1].y + 120, 500);
    await c.until(BAR_MS * 6);
  });
}

// SQ-4 helpers --------------------------------------------------------------
async function sqSession(v = 41, scene = null) {
  await boot(page, 'seq');
  await page.evaluate(() => document.querySelector('button[aria-label="Close tour"]')?.click());
  await page.evaluate((v) => { const sel = document.querySelector('header select'); sel.value = String(v); sel.dispatchEvent(new Event('change', { bubbles: true })); }, v);
  await page.waitForTimeout(1200);
  await page.evaluate(() => document.querySelector('button[aria-label="Start sequencer"]')?.click());
  await page.waitForTimeout(2500);
  if (scene !== null) { await page.evaluate((s) => window.__fableSq.store.getState().launchScene(s), scene); await page.waitForTimeout(BAR_MS * 2.2); }
}
/** Waits for the next SQ-4 bar line, from the header bar counter. */
async function nextBar() {
  const read = () => page.evaluate(() => window.__fableSq.store.getState().bar ?? document.querySelector('.sq-bar-num, [class*="bar"] b')?.textContent);
  await page.waitForTimeout(BAR_MS - 50);
}

// ---- F. SQ-4: paint a cutoff automation curve into the dub-chord clip ------
if (want('sq-auto')) {
  await sqSession(6, 1); // ACID · STEEL PULSE (126 BPM), BUILD: drums + acid bass + pads
  if (process.env.SOLO) await page.evaluate(() => { const heads = [...document.querySelectorAll('button')].filter((b) => b.textContent.trim() === 'S'); heads[1]?.click(); });
  await page.evaluate(() => window.__fableSq.store.getState().enterFocus(1));
  await page.waitForTimeout(900);
  await page.evaluate(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'EDIT')?.click());
  await page.waitForTimeout(700);
  const add = await center(page, '.sq-auto-add');
  await record('sq-auto', async (c) => {
    c.mark('play');
    await c.move(add.x, add.y, BAR_MS * 0.6);
    await c.click(add.x, add.y, 120);
    c.mark('lane');
    await c.wait(500);
    const ed = (await center(page, '.sq-auto svg[role=img]')).box;
    const x0 = ed.x + 8, x1 = ed.x + ed.width - 8;
    const yAt = (u) => ed.y + ed.height * (0.85 - 0.68 * (0.5 - 0.5 * Math.cos(u * Math.PI * 4)));
    await c.move(x0, yAt(0), 450);
    await c.until(BAR_MS * 1.2);
    await c.down();
    c.mark('draw');
    const pts = [];
    for (let i = 0; i <= 48; i++) { const u = i / 48; pts.push([x0 + (x1 - x0) * u, yAt(u)]); }
    await c.path(pts, BAR_MS * 1.5);
    await c.up();
    c.mark('drawn');
    await c.move(x1 - 200, ed.y - 260, 700);
    await c.until(BAR_MS * 8);
  }, [['cutoff', '.bl-filter-section [role=slider][aria-label="CUT"]']]);
}

// ---- G. SQ-4: launch the drop from the breakdown -------------------------
if (want('sq-launch')) {
  await sqSession(41);
  await page.evaluate(() => window.__fableSq.store.getState().launchScene(3));
  await page.waitForTimeout(BAR_MS * 2.2);
  const btn = await center(page, '.sq-scene-launch', 4);
  await record('sq-launch', async (c) => {
    c.mark('play');
    await c.move(btn.x + 120, btn.y + 90, BAR_MS * 0.8);
    await c.until(BAR_MS * 1.45);
    await c.click(btn.x, btn.y, 300);
    c.mark('launch');
    await c.move(btn.x + 260, btn.y + 180, 900);
    await c.until(BAR_MS * 7);
  });
}

// LAB helpers ---------------------------------------------------------------
const lab = (name) => `.lab-card[aria-label="${name}"]`;
async function labScroll() {
  await wt(() => { const r = document.querySelector('.panel-lab').getBoundingClientRect(); window.scrollTo(0, r.top + scrollY - 380); });
  await page.waitForTimeout(400);
}

// ---- H. LAB SHIFT: plain plucks, then spiralling frequency-shifted echoes ---
if (want('wt-shift')) {
  await wtSetup('f3', {
    'fx.delay.on': 0, 'fx.chorus.on': 0, 'fx.reverb.on': 1, 'fx.reverb.size': 0.6, 'fx.reverb.mix': 0.18,
    'fx.shift.on': 0, 'fx.shift.hz': 40, 'fx.shift.fb': 0.45, 'fx.shift.spread': 0.5, 'fx.shift.mix': 0.55,
  });
  await wtNotes([[0, 7, 1], [3, 3, 1], [6, 0, 1], [10, 10, 1]]);
  await labScroll();
  const power = await center(page, `${lab('SHIFT')} .panel-head button`);
  const hz = await knob(lab('SHIFT'), 'SHIFT');
  const fb = await knob(lab('SHIFT'), 'SPIRAL');
  await record('wt-shift', async (c) => {
    c.mark('play');
    await wt(() => window.__fable.store.getState().seqPlay());
    await c.move(power.x + 60, power.y + 90, BAR_MS * 0.6);
    await c.until(BAR_MS * 1.0 - 150);
    await c.click(power.x, power.y, 150);
    c.mark('on');
    await c.until(BAR_MS * 1.6);
    await c.turn(fb.x, fb.y, [[78, BAR_MS * 0.9]], 350);
    c.mark('spiral');
    await c.until(BAR_MS * 3.0);
    await c.turn(hz.x, hz.y, [[26, BAR_MS * 1.1, linear], [-62, BAR_MS * 1.4, linear]], 400);
    c.mark('swept');
    await c.move(hz.x + 80, hz.y + 120, 600);
    await c.until(BAR_MS * 6.2);
  }, [['hz', `${lab('SHIFT')} [role=slider][aria-label="SHIFT"]`], ['fb', `${lab('SHIFT')} [role=slider][aria-label="SPIRAL"]`]]);
}

// ---- I. LAB GLITCH: a steady arp, then beat repeats, rolls and tape drift ---
if (want('wt-glitch')) {
  await wtSetup('f3', {
    'fx.delay.on': 0, 'fx.chorus.on': 0, 'fx.reverb.on': 1, 'fx.reverb.size': 0.4, 'fx.reverb.mix': 0.12,
    'fx.glitch.on': 0, 'fx.glitch.div': 2, 'fx.glitch.chance': 0.35, 'fx.glitch.drift': 0, 'fx.glitch.mix': 1,
  });
  await wtNotes([[0, 0, 1], [2, 12, 1], [3, 7, 1], [4, 3, 1], [6, 10, 1], [7, 7, 1], [8, 0, 1], [10, 15, 1], [11, 12, 1], [12, 7, 1], [14, 10, 1], [15, 3, 1]]);
  await labScroll();
  const power = await center(page, `${lab('GLITCH')} .panel-head button`);
  const next = await center(page, `${lab('GLITCH')} .stepper button[aria-label="next"]`);
  const chance = await knob(lab('GLITCH'), 'CHANCE');
  const drift = await knob(lab('GLITCH'), 'DRIFT');
  await record('wt-glitch', async (c) => {
    c.mark('play');
    await wt(() => window.__fable.store.getState().seqPlay());
    await c.move(power.x + 60, power.y + 90, BAR_MS * 0.6);
    await c.until(BAR_MS * 1.0 - 150);
    await c.click(power.x, power.y, 150);
    c.mark('on');
    await c.until(BAR_MS * 1.5);
    await c.turn(chance.x, chance.y, [[110, BAR_MS * 0.9]], 350);
    c.mark('chance');
    await c.until(BAR_MS * 3.0 - 150);
    await c.click(next.x, next.y, 400);
    c.mark('roll');
    await c.until(BAR_MS * 3.6);
    await c.turn(drift.x, drift.y, [[75, BAR_MS * 0.9]], 350);
    c.mark('drift');
    await c.move(drift.x + 80, drift.y + 120, 600);
    await c.until(BAR_MS * 6.2);
  }, [['chance', `${lab('GLITCH')} [role=slider][aria-label="CHANCE"]`], ['drift', `${lab('GLITCH')} [role=slider][aria-label="DRIFT"]`]]);
}

await browser.close();
