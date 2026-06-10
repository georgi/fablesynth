import { PARAMS, defaultParams } from './params.js';
import { SynthEngine } from './engine/synth.js';
import { Knob, Stepper, PowerButton, VSlider } from './ui/knob.js';
import { Keyboard, ComputerKeys } from './ui/keyboard.js';
import { WavetableView, EnvView, LFOView, FilterView, ScopeView, SpectrumView } from './ui/displays.js';
import { FACTORY_PRESETS, loadUserPresets, saveUserPreset } from './presets.js';
import { MOD_SOURCES, MOD_DESTS } from './params.js';

const ACCENTS = { a: '#4de8ff', b: '#ffa14d', f: '#b18cff', n: '#9fb4d8' };

const engine = new SynthEngine();
const registry = new Map(); // paramId -> control (set(value))
const hooks = new Map(); // paramId -> fn(value) for display updates

const $ = (id) => document.getElementById(id);

function setP(id, v) {
  engine.setParam(id, v);
  const h = hooks.get(id);
  if (h) h(v);
}

function bind(id, control, hook) {
  registry.set(id, control);
  if (hook) hooks.set(id, hook);
  return control;
}

function knob(parentId, paramId, opts = {}) {
  const def = PARAMS[paramId];
  const k = new Knob(def, {
    ...opts,
    onChange: (v) => setP(paramId, v),
  });
  $(parentId).appendChild(k.el);
  return bind(paramId, k, opts.hook);
}

function stepper(parentId, paramId, opts = {}) {
  const def = PARAMS[paramId];
  const s = new Stepper(def, { ...opts, onChange: (i) => setP(paramId, i) });
  $(parentId).appendChild(s.el);
  return bind(paramId, s, opts.hook);
}

function power(parentId, paramId, opts = {}) {
  const def = PARAMS[paramId];
  const b = new PowerButton(def, { onChange: (v) => setP(paramId, v) });
  $(parentId).appendChild(b.el);
  return bind(paramId, b, opts.hook);
}

// ---------- build UI ----------
const views = {};

function buildOsc(prefix, accentKey) {
  const ac = ACCENTS[accentKey];
  const wt = new WavetableView($(`wt3d-${prefix === 'oscA' ? 'A' : 'B'}`), ac);
  views[prefix] = wt;

  power(`${prefix}-power`, `${prefix}.on`, {
    hook: () => $(`panel-${prefix}`).classList.toggle('off', !engine.params[`${prefix}.on`]),
  });
  stepper(`${prefix}-table`, `${prefix}.table`, {
    hook: (i) => wt.setTable(engine.tables ? engine.tables[i | 0] : null),
  });

  const pos = new VSlider(PARAMS[`${prefix}.pos`], {
    accent: accentKey,
    onChange: (v) => setP(`${prefix}.pos`, v),
  });
  $(`${prefix}-pos`).appendChild(pos.el);
  bind(`${prefix}.pos`, pos, (v) => wt.setPos(v));
  views[`${prefix}Pos`] = pos;

  const kn = `${prefix}-knobs`;
  knob(kn, `${prefix}.oct`, { size: 'sm', accent: accentKey });
  knob(kn, `${prefix}.semi`, { size: 'sm', accent: accentKey });
  knob(kn, `${prefix}.fine`, { size: 'sm', accent: accentKey });
  knob(kn, `${prefix}.unison`, { size: 'sm', accent: accentKey });
  knob(kn, `${prefix}.detune`, { size: 'sm', accent: accentKey });
  knob(kn, `${prefix}.spread`, { size: 'sm', accent: accentKey });
  knob(kn, `${prefix}.level`, { size: 'md', accent: accentKey });
  knob(kn, `${prefix}.pan`, { size: 'sm', accent: accentKey });
}

buildOsc('oscA', 'a');
buildOsc('oscB', 'b');

power('sub-power', 'sub.on');
stepper('sub-shape', 'sub.shape');
knob('sub-knobs', 'sub.oct', { size: 'sm', accent: 'n' });
knob('sub-knobs', 'sub.level', { size: 'sm', accent: 'n' });
power('noise-power', 'noise.on');
stepper('noise-type', 'noise.type');
knob('noise-knobs', 'noise.level', { size: 'sm', accent: 'n' });

// filter
views.filter = new FilterView($('filter-curve'), ACCENTS.f);
const refreshFilter = () =>
  views.filter.set(
    engine.params['filter.type'],
    engine.params['filter.cutoff'],
    engine.params['filter.res'],
    engine.params['filter.on']
  );
power('filter-power', 'filter.on', { hook: refreshFilter });
stepper('filter-type', 'filter.type', { hook: refreshFilter });
knob('filter-knobs', 'filter.cutoff', { size: 'lg', accent: 'f', hook: refreshFilter });
knob('filter-knobs', 'filter.res', { size: 'md', accent: 'f', hook: refreshFilter });
knob('filter-knobs', 'filter.drive', { size: 'sm', accent: 'f' });
knob('filter-knobs', 'filter.env', { size: 'sm', accent: 'f' });
knob('filter-knobs', 'filter.key', { size: 'sm', accent: 'f' });

// envelopes
views.env1 = new EnvView($('env1-curve'), '#e8edf7');
views.env2 = new EnvView($('env2-curve'), '#b18cff');
for (const e of ['env1', 'env2']) {
  const refresh = () =>
    views[e].set(engine.params[`${e}.a`], engine.params[`${e}.d`], engine.params[`${e}.s`], engine.params[`${e}.r`]);
  for (const seg of ['a', 'd', 's', 'r']) knob(`${e}-knobs`, `${e}.${seg}`, { size: 'sm', accent: e === 'env1' ? 'n' : 'f', hook: refresh });
  refresh();
}

// lfos
views.lfo1 = new LFOView($('lfo1-curve'), ACCENTS.a);
views.lfo2 = new LFOView($('lfo2-curve'), ACCENTS.b);
for (const l of ['lfo1', 'lfo2']) {
  const refresh = () => views[l].set(engine.params[`${l}.shape`], engine.params[`${l}.rate`]);
  stepper(`${l}-shape`, `${l}.shape`, { hook: refresh });
  knob(`${l}-knobs`, `${l}.rate`, { size: 'md', accent: l === 'lfo1' ? 'a' : 'b', hook: refresh });
  refresh();
}

// mod matrix
function matrixSelect(paramId, options) {
  const sel = document.createElement('select');
  sel.className = 'mx-select';
  options.forEach((o, i) => {
    const op = document.createElement('option');
    op.value = i;
    op.textContent = o;
    sel.appendChild(op);
  });
  sel.value = PARAMS[paramId].def;
  sel.addEventListener('change', () => setP(paramId, parseInt(sel.value, 10)));
  bind(paramId, { set: (v) => { sel.value = v | 0; } });
  return sel;
}
for (let s = 1; s <= 4; s++) {
  const row = document.createElement('div');
  row.className = 'mx-row';
  row.appendChild(matrixSelect(`mat${s}.src`, MOD_SOURCES));
  const arrow = document.createElement('span');
  arrow.className = 'mx-arrow';
  arrow.textContent = '▸';
  row.appendChild(arrow);
  row.appendChild(matrixSelect(`mat${s}.dst`, MOD_DESTS));
  const amt = new Knob(PARAMS[`mat${s}.amt`], { size: 'xs', label: '', onChange: (v) => setP(`mat${s}.amt`, v) });
  bind(`mat${s}.amt`, amt);
  row.appendChild(amt.el);
  $('matrix-rows').appendChild(row);
}

// fx
power('fx-drive-power', 'fx.drive.on');
knob('fx-drive-knobs', 'fx.drive.amt', { size: 'sm', accent: 'n' });
knob('fx-drive-knobs', 'fx.drive.mix', { size: 'sm', accent: 'n' });
power('fx-chorus-power', 'fx.chorus.on');
knob('fx-chorus-knobs', 'fx.chorus.rate', { size: 'sm', accent: 'n' });
knob('fx-chorus-knobs', 'fx.chorus.depth', { size: 'sm', accent: 'n' });
knob('fx-chorus-knobs', 'fx.chorus.mix', { size: 'sm', accent: 'n' });
power('fx-delay-power', 'fx.delay.on');
knob('fx-delay-knobs', 'fx.delay.time', { size: 'sm', accent: 'n' });
knob('fx-delay-knobs', 'fx.delay.fb', { size: 'sm', accent: 'n' });
knob('fx-delay-knobs', 'fx.delay.mix', { size: 'sm', accent: 'n' });
power('fx-reverb-power', 'fx.reverb.on');
knob('fx-reverb-knobs', 'fx.reverb.size', { size: 'sm', accent: 'n' });
knob('fx-reverb-knobs', 'fx.reverb.mix', { size: 'sm', accent: 'n' });

// master + glide
knob('master-knob', 'master.volume', { size: 'md', accent: 'n' });
knob('glide-knob', 'master.glide', { size: 'sm', accent: 'n' });

// ---------- keyboard / input ----------
function playNote(n, vel) {
  if (vel > 0) engine.noteOn(n, vel);
  else engine.noteOff(n);
}

const kb = new Keyboard($('keyboard'), {
  low: 36,
  high: 84,
  onNote: playNote,
});

const ck = new ComputerKeys({
  onNote: (n, vel) => {
    playNote(n, vel);
    kb.setActive(n, vel > 0);
  },
  onOctave: (o) => { $('oct-label').textContent = 'C' + (4 + o); },
  onPanic: () => engine.panic(),
});
$('oct-down').addEventListener('click', () => { ck.octave = Math.max(-3, ck.octave - 1); $('oct-label').textContent = 'C' + (4 + ck.octave); });
$('oct-up').addEventListener('click', () => { ck.octave = Math.min(3, ck.octave + 1); $('oct-label').textContent = 'C' + (4 + ck.octave); });

// MIDI
if (navigator.requestMIDIAccess) {
  navigator.requestMIDIAccess().then((access) => {
    const update = () => {
      let any = false;
      for (const input of access.inputs.values()) {
        any = true;
        input.onmidimessage = (e) => {
          const [st, d1, d2] = e.data;
          const cmd = st & 0xf0;
          if (cmd === 0x90 && d2 > 0) { playNote(d1, d2 / 127); kb.setActive(d1, true); }
          else if (cmd === 0x80 || (cmd === 0x90 && d2 === 0)) { playNote(d1, 0); kb.setActive(d1, false); }
          else if (cmd === 0xe0) { engine.bend((((d2 << 7) | d1) - 8192) / 8192 * 2); }
        };
      }
      $('midi-led').classList.toggle('on', any);
    };
    update();
    access.onstatechange = update;
  }).catch(() => {});
}

// ---------- presets ----------
let userPresets = loadUserPresets();

function rebuildPresetSelect(selectName) {
  const sel = $('preset-select');
  sel.innerHTML = '';
  const og1 = document.createElement('optgroup');
  og1.label = 'FACTORY';
  FACTORY_PRESETS.forEach((p, i) => {
    const o = document.createElement('option');
    o.value = 'f' + i;
    o.textContent = p.name;
    og1.appendChild(o);
  });
  sel.appendChild(og1);
  if (userPresets.length) {
    const og2 = document.createElement('optgroup');
    og2.label = 'USER';
    userPresets.forEach((p, i) => {
      const o = document.createElement('option');
      o.value = 'u' + i;
      o.textContent = p.name;
      og2.appendChild(o);
    });
    sel.appendChild(og2);
  }
  if (selectName) {
    const all = [...sel.options];
    const found = all.find((o) => o.textContent === selectName);
    if (found) sel.value = found.value;
  }
}

function refreshAllDisplays() {
  const p = engine.params;
  if (engine.tables) {
    views.oscA.setTable(engine.tables[p['oscA.table'] | 0]);
    views.oscB.setTable(engine.tables[p['oscB.table'] | 0]);
  }
  views.oscA.setPos(p['oscA.pos']);
  views.oscB.setPos(p['oscB.pos']);
  refreshFilter();
  views.env1.set(p['env1.a'], p['env1.d'], p['env1.s'], p['env1.r']);
  views.env2.set(p['env2.a'], p['env2.d'], p['env2.s'], p['env2.r']);
  views.lfo1.set(p['lfo1.shape'], p['lfo1.rate']);
  views.lfo2.set(p['lfo2.shape'], p['lfo2.rate']);
  $('panel-oscA').classList.toggle('off', !p['oscA.on']);
  $('panel-oscB').classList.toggle('off', !p['oscB.on']);
}

function applyPreset(presetParams) {
  const merged = { ...defaultParams(), ...presetParams };
  engine.panic();
  for (const [id, v] of Object.entries(merged)) {
    engine.params[id] = v;
    const c = registry.get(id);
    if (c) c.set(v);
  }
  engine.applyAllParams();
  refreshAllDisplays();
}

function loadPresetByValue(val) {
  if (val[0] === 'f') applyPreset(FACTORY_PRESETS[+val.slice(1)].params);
  else applyPreset(userPresets[+val.slice(1)].params);
}

$('preset-select').addEventListener('change', (e) => loadPresetByValue(e.target.value));
$('preset-prev').addEventListener('click', () => stepPreset(-1));
$('preset-next').addEventListener('click', () => stepPreset(1));
function stepPreset(d) {
  const sel = $('preset-select');
  const opts = [...sel.options];
  let i = opts.findIndex((o) => o.value === sel.value);
  i = (i + d + opts.length) % opts.length;
  sel.value = opts[i].value;
  loadPresetByValue(sel.value);
}
$('preset-save').addEventListener('click', () => {
  const name = (window.prompt('Preset name:') || '').trim().toUpperCase();
  if (!name) return;
  userPresets = saveUserPreset(name, engine.params);
  rebuildPresetSelect(name);
});

rebuildPresetSelect();

// ---------- power on ----------
$('power-on').addEventListener('click', async () => {
  const btn = $('power-on');
  btn.disabled = true;
  btn.classList.add('booting');
  try {
    await engine.init();
  } catch (err) {
    const el = $('po-error');
    el.hidden = false;
    el.textContent =
      location.protocol === 'file:'
        ? 'AudioWorklet needs an http server. Run:  python3 -m http.server  in the project folder, then open localhost.'
        : 'Audio engine failed to start: ' + err.message;
    btn.disabled = false;
    btn.classList.remove('booting');
    return;
  }
  engine.onviz = (d) => {
    views.oscA.setModPos(d.a >= 0 ? d.a : -1);
    views.oscB.setModPos(d.b >= 0 ? d.b : -1);
    views.oscAPos.setGhost(d.a >= 0 ? d.a : -1);
    views.oscBPos.setGhost(d.b >= 0 ? d.b : -1);
    $('voice-count').textContent = d.n;
  };
  views.scope = new ScopeView($('scope'), engine.scopeAnalyser, ACCENTS.a);
  views.spectrum = new SpectrumView($('spectrum'), engine.specAnalyser, ACCENTS.b);
  refreshAllDisplays();
  document.getElementById('power-overlay').classList.add('gone');
  setTimeout(() => { document.getElementById('power-overlay').style.display = 'none'; }, 700);
});

// ---------- render loop ----------
function frame() {
  views.oscA.draw();
  views.oscB.draw();
  views.filter.draw();
  views.env1.draw();
  views.env2.draw();
  views.lfo1.draw();
  views.lfo2.draw();
  if (views.scope) views.scope.draw();
  if (views.spectrum) views.spectrum.draw();
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

window.addEventListener('resize', () => {
  views.filter.dirty = true;
  views.env1.dirty = true;
  views.env2.dirty = true;
  views.oscA.dirty = true;
  views.oscB.dirty = true;
});

// exposed for debugging / automated verification
window.__fable = { engine, applyPreset };
