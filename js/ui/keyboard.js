// On-screen piano keyboard. Mouse/touch with Y-position velocity; glissando
// while held. Exposes setActive() so MIDI / computer-key notes light up too.

const WHITE_SEMIS = [0, 2, 4, 5, 7, 9, 11];
const BLACK_SEMIS = { 1: 0, 3: 1, 6: 3, 8: 4, 10: 5 }; // semi -> white index it follows

export class Keyboard {
  constructor(container, { low = 36, high = 84, onNote } = {}) {
    this.onNote = onNote;
    this.low = low;
    this.high = high;
    this.keys = new Map(); // note -> element
    this.pointers = new Map(); // pointerId -> note
    this.el = container;
    container.classList.add('kb');
    this.build();
  }

  build() {
    const whites = [];
    for (let n = this.low; n <= this.high; n++) {
      if (WHITE_SEMIS.includes(n % 12)) whites.push(n);
    }
    const ww = 100 / whites.length;

    whites.forEach((n, i) => {
      const k = document.createElement('div');
      k.className = 'kb-key kb-white';
      k.dataset.note = n;
      k.style.left = `${i * ww}%`;
      k.style.width = `${ww}%`;
      if (n % 12 === 0) {
        const lbl = document.createElement('span');
        lbl.className = 'kb-oct';
        lbl.textContent = 'C' + (n / 12 - 1);
        k.appendChild(lbl);
      }
      this.el.appendChild(k);
      this.keys.set(n, k);
    });

    let wi = 0;
    for (let n = this.low; n <= this.high; n++) {
      const s = n % 12;
      if (WHITE_SEMIS.includes(s)) { wi++; continue; }
      const k = document.createElement('div');
      k.className = 'kb-key kb-black';
      k.dataset.note = n;
      const bw = ww * 0.62;
      k.style.left = `${wi * ww - bw / 2}%`;
      k.style.width = `${bw}%`;
      this.el.appendChild(k);
      this.keys.set(n, k);
    }

    this.el.addEventListener('pointerdown', (e) => this.down(e));
    this.el.addEventListener('pointermove', (e) => this.move(e));
    this.el.addEventListener('pointerup', (e) => this.up(e));
    this.el.addEventListener('pointercancel', (e) => this.up(e));
    this.el.addEventListener('pointerleave', (e) => { if (!e.buttons) this.up(e); });
    this.el.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  noteAt(e) {
    const t = document.elementFromPoint(e.clientX, e.clientY);
    if (!t || !t.dataset || !t.dataset.note) return null;
    return parseInt(t.dataset.note, 10);
  }

  velAt(e, note) {
    const k = this.keys.get(note);
    if (!k) return 0.8;
    const r = k.getBoundingClientRect();
    return Math.min(1, Math.max(0.25, (e.clientY - r.top) / r.height + 0.15));
  }

  down(e) {
    const n = this.noteAt(e);
    if (n == null) return;
    e.preventDefault();
    try { this.el.setPointerCapture(e.pointerId); } catch {}
    this.pointers.set(e.pointerId, n);
    this.onNote(n, this.velAt(e, n));
    this.setActive(n, true);
  }

  move(e) {
    if (!this.pointers.has(e.pointerId)) return;
    const prev = this.pointers.get(e.pointerId);
    const n = this.noteAt(e);
    if (n == null || n === prev) return;
    this.onNote(prev, 0);
    this.setActive(prev, false);
    this.pointers.set(e.pointerId, n);
    this.onNote(n, this.velAt(e, n));
    this.setActive(n, true);
  }

  up(e) {
    if (!this.pointers.has(e.pointerId)) return;
    const n = this.pointers.get(e.pointerId);
    this.pointers.delete(e.pointerId);
    this.onNote(n, 0);
    this.setActive(n, false);
  }

  setActive(note, on) {
    const k = this.keys.get(note);
    if (k) k.classList.toggle('held', !!on);
  }
}

// Computer keyboard -> notes. Two rows: AWSEDFTGYHUJKOLP;'
const KEYMAP = {
  KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7,
  KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14,
  KeyP: 15, Semicolon: 16, Quote: 17,
};

export class ComputerKeys {
  constructor({ onNote, onOctave, onPanic }) {
    this.octave = 0;
    this.held = new Set();
    window.addEventListener('keydown', (e) => {
      if (e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
      const tag = document.activeElement && document.activeElement.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
      if (e.code === 'KeyZ') { this.octave = Math.max(-3, this.octave - 1); onOctave(this.octave); return; }
      if (e.code === 'KeyX') { this.octave = Math.min(3, this.octave + 1); onOctave(this.octave); return; }
      if (e.code === 'Escape') { onPanic(); return; }
      const off = KEYMAP[e.code];
      if (off === undefined) return;
      const note = 60 + this.octave * 12 + off;
      if (this.held.has(note)) return;
      this.held.add(note);
      onNote(note, 0.85);
    });
    window.addEventListener('keyup', (e) => {
      const off = KEYMAP[e.code];
      if (off === undefined) return;
      const note = 60 + this.octave * 12 + off;
      if (this.held.delete(note)) onNote(note, 0);
    });
    window.addEventListener('blur', () => {
      for (const n of this.held) onNote(n, 0);
      this.held.clear();
    });
  }
}
