// Computer keyboard -> notes. Two rows: AWSEDFTGYHUJKOLP;'

import { useEffect } from 'react';
import { useStore } from '../store';

const KEYMAP: Record<string, number> = {
  KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7,
  KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14,
  KeyP: 15, Semicolon: 16, Quote: 17,
};

export function useComputerKeys() {
  const playNote = useStore((s) => s.playNote);
  const panic = useStore((s) => s.panic);

  useEffect(() => {
    const held = new Set<number>();

    const keydown = (e: KeyboardEvent) => {
      if (e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
      const tag = document.activeElement && document.activeElement.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
      const { octave, setOctave } = useStore.getState();
      if (e.code === 'KeyZ') { setOctave(Math.max(-3, octave - 1)); return; }
      if (e.code === 'KeyX') { setOctave(Math.min(3, octave + 1)); return; }
      if (e.code === 'Escape') { panic(); return; }
      const off = KEYMAP[e.code];
      if (off === undefined) return;
      const note = 60 + octave * 12 + off;
      if (held.has(note)) return;
      held.add(note);
      playNote(note, 0.85);
    };

    const keyup = (e: KeyboardEvent) => {
      const off = KEYMAP[e.code];
      if (off === undefined) return;
      const { octave } = useStore.getState();
      const note = 60 + octave * 12 + off;
      if (held.delete(note)) playNote(note, 0);
    };

    const blur = () => {
      for (const n of held) playNote(n, 0);
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
  }, [playNote, panic]);
}
