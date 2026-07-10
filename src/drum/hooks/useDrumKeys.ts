import { useEffect } from 'react';
import { useDrumStore } from '../store';

const KEYMAP: Record<string, number> = {
  Digit1: 12, Digit2: 13, Digit3: 14, Digit4: 15,
  KeyQ: 8, KeyW: 9, KeyE: 10, KeyR: 11,
  KeyA: 4, KeyS: 5, KeyD: 6, KeyF: 7,
  KeyZ: 0, KeyX: 1, KeyC: 2, KeyV: 3,
};

export function useDrumKeys() {
  const triggerPad = useDrumStore((s) => s.triggerPad);
  const stop = useDrumStore((s) => s.stop);

  useEffect(() => {
    const keydown = (e: KeyboardEvent) => {
      if (e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
      const tag = document.activeElement?.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
      if (e.code === 'Escape') {
        stop();
        return;
      }
      const padIndex = KEYMAP[e.code];
      if (padIndex !== undefined) triggerPad(padIndex, 0.85);
    };

    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  }, [stop, triggerPad]);
}
