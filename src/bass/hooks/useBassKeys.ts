// Computer keyboard -> notes (audition when stopped, legato = slide).
// Two rows AWSEDFTGYHUJKOLP;' cover 18 semis; Z/X shift the octave window
// within the BL-1 keyboard's two-octave range. Esc stops/panics.

import { useEffect } from 'react';
import { KEY_COUNT } from '../params';
import { useBassStore } from '../store';

const KEYMAP: Record<string, number> = {
  KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7,
  KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14,
  KeyP: 15, Semicolon: 16, Quote: 17,
};

export function useBassKeys() {
  const noteOn = useBassStore((s) => s.noteOn);
  const noteOff = useBassStore((s) => s.noteOff);
  const stop = useBassStore((s) => s.stop);

  useEffect(() => {
    // Keyed by physical key (e.code): the octave can change while a key is
    // held, and the note-off must match the note that sounded.
    const held = new Map<string, number>();
    let octave = 0;

    const keydown = (e: KeyboardEvent) => {
      if (e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
      const tag = document.activeElement && document.activeElement.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
      if (e.code === 'KeyZ') { octave = Math.max(0, octave - 1); return; }
      if (e.code === 'KeyX') { octave = Math.min(1, octave + 1); return; }
      if (e.code === 'Escape') { stop(); return; }
      const off = KEYMAP[e.code];
      if (off === undefined) return;
      const semi = Math.min(KEY_COUNT - 1, octave * 12 + off);
      if (held.has(e.code)) return;
      held.set(e.code, semi);
      noteOn(semi, 0.85);
    };

    const keyup = (e: KeyboardEvent) => {
      const semi = held.get(e.code);
      if (semi === undefined) return;
      held.delete(e.code);
      noteOff(semi);
    };

    const blur = () => {
      for (const semi of held.values()) noteOff(semi);
      held.clear();
    };

    window.addEventListener('keydown', keydown);
    window.addEventListener('keyup', keyup);
    window.addEventListener('blur', blur);
    return () => {
      window.removeEventListener('keydown', keydown);
      window.removeEventListener('keyup', keyup);
      window.removeEventListener('blur', blur);
    };
  }, [noteOn, noteOff, stop]);
}
