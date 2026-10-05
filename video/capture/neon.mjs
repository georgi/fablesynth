// NEON CHASE demo: every part is a live SQ-4 performance of the same session
// (132 BPM, C minor), so the parts join into one track in the edit.
// Each take marks 'bar0' on a song-bar downbeat and runs one bar past its section as a handle.
import { launch, boot, take, center } from './rec.mjs';

export const NEON_BPM = 132;
export const NEON_BAR = (4 * 60000) / NEON_BPM;
const SCENE = { INTRO: 0, BUILD: 1, DROP_A: 2, DROP_B: 3, BREAK: 4, OUTRO: 5 };
const TRACK = { DRUMS: 0, BASS: 1, LEAD: 2, PADS: 3 };

const only = process.argv.slice(2);
const want = (n) => !only.length || only.includes(n);
const { browser, page } = await launch();
const sq = (fn, arg) => page.evaluate(fn, arg);
const linear = (u) => u;

async function session() {
  await boot(page, 'seq');
  await sq(() => document.querySelector('button[aria-label="Close tour"]')?.click());
  await sq(() => { const sel = document.querySelector('header select'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); });
  await page.waitForTimeout(1200);
  await sq((t) => { const s = window.__fableSq.store.getState(); s.loadTrackFactoryPatch(t, 12); s.setTrackVol(t, 0.70); }, TRACK.DRUMS); // UZU
  await page.waitForTimeout(400);
}
async function start(scene) {
  await sq((s) => window.__fableSq.store.getState().launchScene(s), scene);
  await sq(() => document.querySelector('button[aria-label="Start sequencer"]')?.click());
}
/** Resolves when the SQ-4 bar counter (1-based, UI clock) reaches `n`. */
const waitBar = (n) => sq((n) => new Promise((res) => {
  const st = window.__fableSq.store;
  if (st.getState().bar >= n) return res(st.getState().bar);
  const un = st.subscribe((s) => { if (s.bar >= n) { un(); res(s.bar); } });
}), n);
// Clips loop on the global 4-bar grid, so song bar 0 is SQ-4 bar 5: every
// section starts on a phrase, and SQ-4 bar 1-4 is a pre-roll for the pad.
const SONG0 = 5;

/** Records the watched sliders' aria-valuetext on the wall clock. */
async function watch(list) {
  await sq((list) => {
    window.__watch = [];
    clearInterval(window.__watchTimer);
    window.__watchTimer = setInterval(() => {
      const row = { t: Date.now() };
      for (const [key, sel] of list) row[key] = document.querySelector(sel)?.getAttribute('aria-valuetext') ?? null;
      window.__watch.push(row);
    }, 40);
  }, list);
}
/** Components the edit can spotlight, sampled with the take (page CSS px). */
const RECTS = {
  head0: '.sq-track-head:nth-of-type(1)', head1: '.sq-track-head:nth-of-type(2)', head2: '.sq-track-head:nth-of-type(3)', head3: '.sq-track-head:nth-of-type(4)',
  heads: '.sq-heads', rail: '.sq-launcher', device: '.sq-device', clipbar: '.sq-clipbar',
  oscA: '#panel-oscA', oscB: '#panel-oscB', noteSeq: '.ns-section', matrix: '.panel-matrix', lfos: '.panel-lfos', lab: '.panel-lab', filter: '.panel-filter', echo: '.panel-echo', shift: '.lab-card[aria-label="SHIFT"]',
  blOsc: '.bl-osc-section', blSeq: '.bl-seq-section', auto: '.sq-auto', drSeq: '.dr-stepseq', drPads: '.dr-pads-panel',
};
async function rectWatch() {
  await sq((RECTS) => {
    window.__rects = [];
    clearInterval(window.__rectTimer);
    const sceneCards = () => [...document.querySelectorAll('.sq-scene-card')];
    window.__rectTimer = setInterval(() => {
      const r = {};
      const put = (k, el) => { if (!el) return; const b = el.getBoundingClientRect(); if (b.width && b.height) r[k] = [b.x, b.y, b.width, b.height].map(Math.round); };
      for (const [k, sel] of Object.entries(RECTS)) {
        const m = /^(.*):nth-of-type\((\d+)\)$/.exec(sel);
        put(k, m ? document.querySelectorAll(m[1])[+m[2] - 1] : document.querySelector(sel));
      }
      sceneCards().forEach((el, i) => put(`scene${i}`, el));
      // Session view: a track column is its head plus every clip cell below it.
      document.querySelectorAll('.sq-track-head').forEach((h, i) => {
        const hb = h.getBoundingClientRect();
        const cells = [...document.querySelectorAll('.sq-cell')].map((c) => c.getBoundingClientRect()).filter((b) => Math.abs(b.x - hb.x) < 4);
        if (!cells.length) return;
        const y1 = Math.max(...cells.map((b) => b.y + b.height));
        r[`col${i}`] = [hb.x, hb.y, hb.width, y1 - hb.y].map(Math.round);
      });
      window.__rects.push({ t: Date.now(), r });
    }, 80);
  }, RECTS);
}

/** Records from song bar `songBar` (take bar 0) on. */
async function record(name, songBar, perform, watchList) {
  await waitBar(SONG0 + songBar - 2); // a full bar of handle before take bar 0
  if (watchList) await watch(watchList);
  await rectWatch();
  let T0 = 0;
  await take(page, name, async (c) => {
    T0 = c.T0;
    const got = await waitBar(SONG0 + songBar);
    if (got !== SONG0 + songBar) throw new Error(`${name}: missed bar ${SONG0 + songBar} (at ${got})`);
    c.mark('bar0');
    const b0 = c.now();
    await perform({ ...c, bar: (n) => c.until(b0 + NEON_BAR * n) });
  });
  const fs = await import('node:fs');
  const rects = await sq(() => { clearInterval(window.__rectTimer); return window.__rects; });
  fs.writeFileSync(new URL(`../public/takes/${name}/rects.json`, import.meta.url).pathname, JSON.stringify(rects.map((r) => ({ ...r, t: r.t - T0 }))));
  if (watchList) {
    const rows = await sq(() => { clearInterval(window.__watchTimer); return window.__watch; });
    fs.writeFileSync(new URL(`../public/takes/${name}/values.json`, import.meta.url).pathname, JSON.stringify(rows.map((r) => ({ ...r, t: r.t - T0 }))));
  }
}

// ---- Part 1 · INTRO: the pad opens from a sine over 8 bars, then the drums drop in
if (want('neon-intro')) {
  await session();
  await sq((t) => window.__fableSq.store.getState().enterFocus(t, 0), TRACK.PADS);
  await page.waitForTimeout(900);
  await sq(() => window.__fableSq.wt.getState().setParam('oscA.pos', 0));
  await sq((t) => window.__fableSq.store.getState().toggleTrackMute(t), TRACK.DRUMS);
  // VSlider maps the pointer to its own track: value = 1 - (y - top) / height.
  const pos = await center(page, '#panel-oscA .pos-holder [role=slider]');
  const x = pos.x, yAt = (v) => pos.box.y + pos.box.height * (1 - v);
  const yBot = yAt(0.005), target = yAt(0.66);
  const mute = await center(page, 'button[title="Mute track"]', TRACK.DRUMS);
  const build = await center(page, '.sq-scene-launch, button[aria-label="Launch scene"]', SCENE.BUILD).catch(() => null);
  await start(SCENE.INTRO);
  await record('neon-intro', 0, async (c) => {
    await c.move(x + 60, yBot + 90, 700);
    await c.move(x, yBot, 400);
    await c.bar(0.3);
    await c.down();
    // A 6-bar rise. PRIME barely changes below ~20 %, so a straight drag opens it evenly.
    // It ends at bar 6.5: the drag backs up the page, which needs a bar to catch up
    // before the drums click has to land.
    await c.move(x, target, NEON_BAR * 6.2, linear, 60);
    await c.up();
    await c.move(mute.x + 40, mute.y + 70, NEON_BAR * 0.3);
    await c.bar(8 - 0.25); // the click lands on mouse-up, ~210 ms later
    await c.click(mute.x, mute.y, 140);
    c.mark('drums');
    await c.move(mute.x + 220, mute.y + 300, 900);
    // Queue BUILD for bar 12 so the take carries the bass entrance as a handle.
    await c.bar(11.4);
    if (build) await c.click(build.x, build.y, 500);
    else await sq((s) => window.__fableSq.store.getState().launchScene(s), SCENE.BUILD);
    c.mark('build');
    await c.move(build ? build.x + 120 : 900, build ? build.y + 90 : 700, 500);
    await c.bar(13.1);
  }, [['pos', '#panel-oscA .pos-holder [role=slider]']]);
}

/** Session state at the end of Part 1: pad opened, drums in, INTRO playing. */
async function afterIntro() {
  await session();
  await sq((t) => window.__fableSq.store.getState().enterFocus(t, 0), TRACK.PADS);
  await page.waitForTimeout(700);
  await sq(() => window.__fableSq.wt.getState().setParam('oscA.pos', 0.59));
}

// ---- Part 2 · BUILD: acid bass enters, a painted cutoff riser into DROP A ---
// Take bar 0 = song bar 11 (INTRO handle); take bar 1 = song bar 12; DROP A on take bar 9 (song 20).
if (want('neon-build')) {
  await afterIntro();
  await sq((t) => window.__fableSq.store.getState().enterFocus(t, 1), TRACK.BASS);
  await page.waitForTimeout(900);
  const rail = (s) => center(page, '.sq-scene-launch', s);
  const buildBtn = await rail(SCENE.BUILD);
  const dropBtn = await rail(SCENE.DROP_A);
  const add = await center(page, '.sq-auto-add');
  if (process.env.SOLO) await sq((t) => window.__fableSq.store.getState().toggleSolo(t), TRACK.BASS);
  const mute = await center(page, 'button[title="Mute track"]', TRACK.DRUMS);
  await start(SCENE.INTRO);
  await record(process.env.SOLO ? 'neon-build-solo' : 'neon-build', 11, async (c) => {
    await c.move(buildBtn.x + 90, buildBtn.y + 80, 600);
    await c.bar(0.35);
    await c.click(buildBtn.x, buildBtn.y, 300);
    c.mark('queued');
    // Two painted risers in the BUILD bass clip: dark on its first bar, wide open on its last.
    // Lane value v (0 = bottom, 1 = top) climbs from v0 with an accelerating curve.
    // FILTER CUT is log 20 Hz–20 kHz (0.4 ≈ 320 Hz); ENV MOD is bipolar (0.5 = 0 %).
    const riser = async (lane, startBar, v0, v1) => {
      const ed = (await center(page, '.sq-auto svg[role=img]')).box;
      const x0 = ed.x + 8, x1 = ed.x + ed.width - 8;
      const yAt = (u) => ed.y + ed.height * (1 - (v0 + (v1 - v0) * Math.pow(Math.max(0, (u - 0.08) / 0.92), 1.7)));
      await c.move(x0, yAt(0), 400);
      await c.bar(startBar);
      await c.down();
      c.mark(`draw-${lane}`);
      const pts = [];
      for (let i = 0; i <= 48; i++) { const u = i / 48; pts.push([x0 + (x1 - x0) * u, yAt(u)]); }
      await c.path(pts, NEON_BAR * 1.15);
      await c.up();
      c.mark(`drawn-${lane}`);
      return ed;
    };
    await c.move(add.x - 80, add.y - 60, NEON_BAR * 0.5);
    await c.bar(1.05);
    await c.click(add.x, add.y, 250);
    c.mark('lane-cut');
    await c.wait(500);
    const ed = await riser('cut', 1.4, 0.4, 0.95);
    const add2 = await center(page, '.sq-auto-add');
    await c.click(add2.x, add2.y, 450);
    c.mark('lane-env');
    await c.wait(350);
    const tgt = await center(page, '.sq-auto-target select');
    await c.click(tgt.x, tgt.y, 350);
    await page.selectOption('.sq-auto-target select', 'flt.env');
    c.mark('env-target');
    await c.wait(250);
    await riser('env', 3.0, 0.55, 0.95);
    await c.move(ed.x + ed.width - 200, ed.y - 200, 700);
    // Queue DROP A, then cut the drums for the last bar of the build.
    await c.move(dropBtn.x + 120, dropBtn.y + 90, NEON_BAR * 0.8);
    await c.bar(6.9);
    await c.click(dropBtn.x, dropBtn.y, 400);
    c.mark('dropQueued');
    await c.move(mute.x + 40, mute.y + 60, NEON_BAR * 0.35);
    await c.bar(8 - 0.2); // the click lands on mouse-up, ~210 ms later
    await c.click(mute.x, mute.y, 140);
    c.mark('drumsOut');
    await c.bar(9 - 0.2);
    await c.click(mute.x, mute.y, 140);
    c.mark('drumsIn');
    await c.move(mute.x + 260, mute.y + 320, 900);
    await c.bar(10.15);
  });
  await page.screenshot({ path: new URL(`../public/takes/${process.env.SOLO ? 'neon-build-solo' : 'neon-build'}/end.png`, import.meta.url).pathname });
}

// ---- Part 3 · DROP A: write a crash and a tom fill into the UZU beat, live ----
// Take bar 0 = song bar 19 (BUILD, drums cut as in Part 2); DROP A on take bar 1 (song 20).
// The clip loops 20–23, 24–27: the fill plays on 23 and 27, the crash on 24 and 28.
if (want('neon-dropa')) {
  await afterIntro();
  await sq((t) => window.__fableSq.store.getState().enterFocus(t, 2), TRACK.DRUMS);
  await page.waitForTimeout(900);
  const step = (pad, n) => center(page, `button[aria-label^="${pad} step ${n}:"]`);
  const tab = async (n) => {
    const r = await sq((n) => { const b = [...document.querySelectorAll('.dr-stepseq-head button')].find((e) => e.textContent.trim() === String(n)); const q = b.getBoundingClientRect(); return { x: q.x + q.width / 2, y: q.y + q.height / 2 }; }, n);
    return r;
  };
  const crash = await step('CRASH', 1);
  const tab4 = await tab(4);
  const fill = [await step('TOM HI', 13), await step('TOM MD', 14), await step('TOM LO', 15), await step('TOM LO', 16)];
  const brk = await center(page, '.sq-scene-launch', SCENE.BREAK);
  await start(SCENE.BUILD);
  await record('neon-dropa', 19, async (c) => {
    // Handle bar: the same drum cut as Part 2, then DROP A on the next bar line.
    await sq((s) => { const st = window.__fableSq.store.getState(); st.toggleTrackMute(0); st.launchScene(s); }, SCENE.DROP_A);
    await c.bar(0.97); // a store toggle is instant, so it can sit right before the bar line
    await sq(() => window.__fableSq.store.getState().toggleTrackMute(0));
    await c.move(crash.x + 90, crash.y + 80, NEON_BAR * 0.4);
    await c.bar(1.3);
    await c.click(crash.x, crash.y, 350);
    c.mark('crash');
    await c.bar(1.75);
    await c.click(tab4.x, tab4.y, 450);
    c.mark('tab4');
    await c.wait(250);
    await c.bar(2.15);
    for (let i = 0; i < fill.length; i++) await c.click(fill[i].x, fill[i].y, i ? 260 : 450);
    c.mark('fill');
    await c.move(fill[3].x + 120, fill[3].y + 140, 700);
    await c.move(brk.x + 120, brk.y + 90, NEON_BAR * 0.8);
    await c.bar(8.4);
    await c.click(brk.x, brk.y, 400);
    c.mark('break');
    await c.move(brk.x + 260, brk.y + 200, 800);
    await c.bar(10.15);
  });
}

// ---- Part 4 · BREAK: LAB SHIFT spirals the exposed lead, then a tape-echo throw ----
// Take bar n = song bar 27 + n: BREAK on take bar 1 (song 28), DROP B on take bar 9 (song 36).
if (want('neon-break')) {
  await afterIntro();
  await sq((t) => window.__fableSq.store.getState().enterFocus(t, 4), TRACK.LEAD);
  await page.waitForTimeout(800);
  await sq(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'TRACK FX')?.click());
  await page.waitForTimeout(800);
  // Dub echo on the dotted eighth; SHIFT starts gentle so SPIRAL has room to grow.
  await sq(() => {
    const s = window.__fableSq.wt.getState();
    for (const [k, v] of Object.entries({ 'fx.delay.sync': 1, 'fx.delay.div': 3, 'fx.shift.on': 0, 'fx.shift.hz': 40, 'fx.shift.fb': 0.3, 'fx.shift.mix': 0.55 })) s.setParam(k, v);
  });
  // Bring the echo and LAB rows into view inside the device body.
  await sq(() => { const echo = document.querySelector('.panel-echo'); const body = echo.closest('.sq-device-body') ?? document.scrollingElement; body.scrollTop += echo.getBoundingClientRect().top - 240; }); // LAB knobs must clear the sticky AUTOMATION bar
  await page.waitForTimeout(500);
  const lab = (n) => `.lab-card[aria-label="${n}"]`;
  const power = await center(page, `${lab('SHIFT')} .panel-head button`);
  const hz = await center(page, `${lab('SHIFT')} [role=slider][aria-label="SHIFT"]`);
  const spiral = await center(page, `${lab('SHIFT')} [role=slider][aria-label="SPIRAL"]`);
  const fdbk = await center(page, '.panel-echo [role=slider][aria-label="FDBK"]');
  const mix = await center(page, '.panel-echo [role=slider][aria-label="MIX"]');
  const dropB = await center(page, '.sq-scene-launch', SCENE.DROP_B);
  const leadVol = await center(page, '.sq-track-head [role=slider][aria-label="VOL"]', TRACK.LEAD);
  if (process.env.SOLO) await sq((t) => window.__fableSq.store.getState().toggleSolo(t), TRACK.LEAD);
  await start(SCENE.DROP_A);
  await record(process.env.SOLO ? 'neon-break-solo' : 'neon-break', 27, async (c) => {
    await sq((s) => window.__fableSq.store.getState().launchScene(s), SCENE.BREAK);
    // The drums leave: bring the lead forward (+9 dB) so its effects carry the break.
    await c.move(leadVol.x + 50, leadVol.y + 70, NEON_BAR * 0.5);
    await c.bar(0.85);
    await c.turn(leadVol.x, leadVol.y, [[90, NEON_BAR * 0.4]], 250);
    c.mark('leadUp');
    await c.move(power.x + 70, power.y - 60, NEON_BAR * 0.35);
    await c.bar(1.5);
    await c.click(power.x, power.y, 250);
    c.mark('shiftOn');
    await c.bar(2.1);
    await c.turn(spiral.x, spiral.y, [[70, NEON_BAR * 0.8]], 400);
    c.mark('spiral');
    await c.bar(3.9);
    await c.turn(hz.x, hz.y, [[30, NEON_BAR * 1.0, linear], [-60, NEON_BAR * 1.2, linear]], 400);
    c.mark('swept');
    await c.bar(6.55);
    await c.click(power.x, power.y, 450);
    c.mark('shiftOff');
    await c.bar(6.95);
    await c.turn(fdbk.x, fdbk.y, [[75, NEON_BAR * 0.6]], 300);
    c.mark('throw');
    await c.turn(mix.x, mix.y, [[45, NEON_BAR * 0.3]], 250);
    await c.move(dropB.x + 120, dropB.y + 90, NEON_BAR * 0.25);
    await c.bar(8.45);
    await c.click(dropB.x, dropB.y, 250);
    c.mark('dropB');
    await c.move(fdbk.x + 60, fdbk.y + 80, NEON_BAR * 0.45);
    await c.bar(9.3);
    await c.turn(fdbk.x, fdbk.y, [[-60, NEON_BAR * 0.5]], 200);
    c.mark('settle');
    await c.bar(10.15);
  }, [['hz', `${lab('SHIFT')} [role=slider][aria-label="SHIFT"]`], ['spiral', `${lab('SHIFT')} [role=slider][aria-label="SPIRAL"]`], ['fdbk', '.panel-echo [role=slider][aria-label="FDBK"]'], ['mix', '.panel-echo [role=slider][aria-label="MIX"]'], ['vol', '.sq-track-head:nth-of-type(4) [role=slider][aria-label="VOL"]']]);
}

// ---- Part 5 · DROP B + OUTRO: the whole session, then a clean stop on bar 48 ----
// Take bar n = song bar 35 + n: DROP B on take bar 1 (song 36), OUTRO on take bar 9 (song 44).
if (want('neon-final')) {
  await afterIntro();
  // State after Part 4: lead forward, dotted-1/8 echo settled after the throw.
  await sq((t) => window.__fableSq.store.getState().enterFocus(t, 4), TRACK.LEAD);
  await page.waitForTimeout(800);
  await sq(() => {
    const s = window.__fableSq.wt.getState();
    for (const [k, v] of Object.entries({ 'fx.delay.sync': 1, 'fx.delay.div': 3, 'fx.delay.fb': 0.49, 'fx.delay.mix': 0.52, 'fx.shift.on': 0 })) s.setParam(k, v);
  });
  await sq((t) => { const st = window.__fableSq.store.getState(); st.setTrackVol(t, 1.0); st.exitFocus(); }, TRACK.LEAD);
  await page.waitForTimeout(900);
  const outro = await center(page, '.sq-scene-launch', SCENE.OUTRO);
  const stopAll = await sq(() => { const b = [...document.querySelectorAll('button')].find((e) => /STOP ALL/.test(e.textContent)); const q = b.getBoundingClientRect(); return { x: q.x + q.width / 2, y: q.y + q.height / 2 }; });
  await start(SCENE.BREAK);
  await record('neon-final', 35, async (c) => {
    await sq((s) => window.__fableSq.store.getState().launchScene(s), SCENE.DROP_B);
    await c.bar(5.2);
    await c.move(outro.x + 120, outro.y + 90, NEON_BAR * 1.2);
    await c.bar(8.4);
    await c.click(outro.x, outro.y, 400);
    c.mark('outro');
    await c.move(stopAll.x + 100, stopAll.y + 80, NEON_BAR * 1.5);
    await c.bar(12.4);
    await c.click(stopAll.x, stopAll.y, 400);
    c.mark('stop');
    await c.move(stopAll.x + 400, stopAll.y + 300, 1200);
    await c.bar(15.2); // two bars of tails after the stop
  });
}

await browser.close();
