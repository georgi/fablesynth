// SVG rotary knob. Drag vertically (shift = fine), scroll wheel, double-click
// to reset, arrow keys when focused. Bipolar params sweep from 12 o'clock.

import { normToValue, valueToNorm } from '../params.js';

const SVG_NS = 'http://www.w3.org/2000/svg';
const A0 = -135, A1 = 135;

function polar(cx, cy, r, deg) {
  const a = ((deg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}
function arcPath(cx, cy, r, from, to) {
  if (Math.abs(to - from) < 0.01) to = from + 0.01;
  const [x0, y0] = polar(cx, cy, r, from);
  const [x1, y1] = polar(cx, cy, r, to);
  const large = Math.abs(to - from) > 180 ? 1 : 0;
  const sweep = to > from ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} ${sweep} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

export class Knob {
  constructor(def, { size = 'md', label, accent, onChange } = {}) {
    this.def = def;
    this.onChange = onChange;
    this.norm = valueToNorm(def, def.def);
    this.bipolar = def.min < 0;

    const el = document.createElement('div');
    el.className = `knob knob-${size}`;
    if (accent) el.dataset.accent = accent;
    el.tabIndex = 0;
    el.setAttribute('role', 'slider');
    el.setAttribute('aria-label', label || def.label || def.id);

    el.innerHTML = `
      <svg viewBox="0 0 80 80">
        <circle class="k-body" cx="40" cy="40" r="26"/>
        <path class="k-track" d="${arcPath(40, 40, 33, A0, A1)}"/>
        <path class="k-arc" d=""/>
        <line class="k-ptr" x1="40" y1="40" x2="40" y2="17"/>
      </svg>
      <div class="k-label">${label || def.label || ''}</div>
      <div class="k-value"></div>`;
    this.el = el;
    this.arc = el.querySelector('.k-arc');
    this.ptr = el.querySelector('.k-ptr');
    this.valEl = el.querySelector('.k-value');

    this.bindEvents();
    this.render();
  }

  get value() { return normToValue(this.def, this.norm); }

  set(value, fire = false) {
    this.norm = Math.min(1, Math.max(0, valueToNorm(this.def, value)));
    this.render();
    if (fire && this.onChange) this.onChange(this.value);
  }

  nudge(deltaNorm, fire = true) {
    this.norm = Math.min(1, Math.max(0, this.norm + deltaNorm));
    this.render();
    if (fire && this.onChange) this.onChange(this.value);
  }

  render() {
    const deg = A0 + (A1 - A0) * this.norm;
    if (this.bipolar) {
      const mid = valueToNorm(this.def, 0);
      const midDeg = A0 + (A1 - A0) * mid;
      this.arc.setAttribute('d', arcPath(40, 40, 33, midDeg, deg));
    } else {
      this.arc.setAttribute('d', arcPath(40, 40, 33, A0, deg));
    }
    this.ptr.setAttribute('transform', `rotate(${deg} 40 40)`);
    const v = this.value;
    this.valEl.textContent = this.def.fmt ? this.def.fmt(v) : v.toFixed(2);
    this.el.setAttribute('aria-valuemin', this.def.min);
    this.el.setAttribute('aria-valuemax', this.def.max);
    this.el.setAttribute('aria-valuenow', v.toFixed(3));
    this.el.setAttribute('aria-valuetext', this.valEl.textContent);
  }

  bindEvents() {
    const el = this.el;
    let dragging = false, lastY = 0;

    el.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      dragging = true;
      lastY = e.clientY;
      el.setPointerCapture(e.pointerId);
      el.classList.add('dragging');
      e.preventDefault();
    });
    el.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const dy = lastY - e.clientY;
      lastY = e.clientY;
      this.nudge(dy * (e.shiftKey ? 0.0008 : 0.005));
    });
    const end = (e) => {
      if (!dragging) return;
      dragging = false;
      el.classList.remove('dragging');
      try { el.releasePointerCapture(e.pointerId); } catch {}
    };
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
    el.addEventListener('dblclick', () => this.set(this.def.def, true));
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.nudge((e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.03));
    }, { passive: false });
    el.addEventListener('keydown', (e) => {
      const step = e.shiftKey ? 0.005 : 0.02;
      if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { this.nudge(step); e.preventDefault(); }
      else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { this.nudge(-step); e.preventDefault(); }
    });
  }
}

// Compact stepper for enum params: ◂ VALUE ▸
export class Stepper {
  constructor(def, { label, accent, onChange } = {}) {
    this.def = def;
    this.onChange = onChange;
    this.index = def.def | 0;
    const el = document.createElement('div');
    el.className = 'stepper';
    if (accent) el.dataset.accent = accent;
    el.innerHTML = `
      ${label ? `<span class="st-label">${label}</span>` : ''}
      <button class="st-btn st-prev" aria-label="previous">◂</button>
      <span class="st-value"></span>
      <button class="st-btn st-next" aria-label="next">▸</button>`;
    this.el = el;
    this.valEl = el.querySelector('.st-value');
    el.querySelector('.st-prev').addEventListener('click', () => this.step(-1));
    el.querySelector('.st-next').addEventListener('click', () => this.step(1));
    this.render();
  }
  step(d) {
    const n = this.def.options.length;
    this.index = (this.index + d + n) % n;
    this.render();
    if (this.onChange) this.onChange(this.index);
  }
  set(i, fire = false) {
    this.index = Math.min(this.def.options.length - 1, Math.max(0, i | 0));
    this.render();
    if (fire && this.onChange) this.onChange(this.index);
  }
  render() { this.valEl.textContent = this.def.options[this.index]; }
}

// LED power toggle
export class PowerButton {
  constructor(def, { onChange } = {}) {
    this.onChange = onChange;
    this.on = !!def.def;
    const el = document.createElement('button');
    el.className = 'power-btn';
    el.setAttribute('aria-label', 'power');
    this.el = el;
    el.addEventListener('click', () => { this.set(!this.on, true); });
    this.render();
  }
  set(on, fire = false) {
    this.on = !!on;
    this.render();
    if (fire && this.onChange) this.onChange(this.on ? 1 : 0);
  }
  render() { this.el.classList.toggle('on', this.on); }
}

// Vertical slider used for wavetable position, with a "ghost" marker that
// tracks the live modulated position coming back from the DSP thread.
export class VSlider {
  constructor(def, { accent, onChange } = {}) {
    this.def = def;
    this.onChange = onChange;
    this.norm = valueToNorm(def, def.def);
    const el = document.createElement('div');
    el.className = 'vslider';
    if (accent) el.dataset.accent = accent;
    el.tabIndex = 0;
    el.setAttribute('role', 'slider');
    el.setAttribute('aria-label', 'wavetable position');
    el.innerHTML = `<div class="vs-track"><div class="vs-fill"></div><div class="vs-ghost"></div><div class="vs-handle"></div></div><div class="vs-label">POS</div>`;
    this.el = el;
    this.fill = el.querySelector('.vs-fill');
    this.ghost = el.querySelector('.vs-ghost');
    this.handle = el.querySelector('.vs-handle');
    this.track = el.querySelector('.vs-track');
    this.bind();
    this.render();
    this.setGhost(-1);
  }
  get value() { return normToValue(this.def, this.norm); }
  set(value, fire = false) {
    this.norm = Math.min(1, Math.max(0, valueToNorm(this.def, value)));
    this.render();
    if (fire && this.onChange) this.onChange(this.value);
  }
  setGhost(norm) {
    if (norm < 0) { this.ghost.style.opacity = '0'; return; }
    this.ghost.style.opacity = '1';
    this.ghost.style.bottom = `${norm * 100}%`;
  }
  render() {
    const pct = this.norm * 100;
    this.fill.style.height = `${pct}%`;
    this.handle.style.bottom = `${pct}%`;
    this.el.setAttribute('aria-valuenow', this.value.toFixed(3));
  }
  bind() {
    const el = this.el;
    let dragging = false;
    const move = (e) => {
      const r = this.track.getBoundingClientRect();
      const n = 1 - (e.clientY - r.top) / r.height;
      this.norm = Math.min(1, Math.max(0, n));
      this.render();
      if (this.onChange) this.onChange(this.value);
    };
    el.addEventListener('pointerdown', (e) => {
      dragging = true;
      el.setPointerCapture(e.pointerId);
      move(e);
    });
    el.addEventListener('pointermove', (e) => { if (dragging) move(e); });
    const end = () => { dragging = false; };
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.norm = Math.min(1, Math.max(0, this.norm + (e.deltaY < 0 ? 0.03 : -0.03)));
      this.render();
      if (this.onChange) this.onChange(this.value);
    }, { passive: false });
    el.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowUp') { this.set(Math.min(1, this.norm + 0.02), true); e.preventDefault(); }
      else if (e.key === 'ArrowDown') { this.set(Math.max(0, this.norm - 0.02), true); e.preventDefault(); }
    });
  }
}
