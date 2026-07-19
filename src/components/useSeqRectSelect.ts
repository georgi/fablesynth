// Shift-drag rectangle selection + in-rect block-move for the WT-1 / BL-1
// note grids (docs/superpowers/specs/2026-07-19-seq-rect-selection-design.md).
// Pointer targets are resolved via elementFromPoint → [data-seq-cell] of the
// edited pattern, matching useSeqNoteDrag. Both gestures commit once, on
// pointerup (one undo entry); Escape cancels. The pending rect is local state
// so the drag never touches the store until release.

import { useRef, useState } from 'react';
import type { RectSel } from '../shared/seqEdit';

interface HookOpts {
  editPattern: number;
  onSelect: (rect: RectSel) => void;
  onMove: (dStep: number, dNote: number, copy: boolean) => void;
}

export function useSeqRectSelect({ editPattern, onSelect, onMove }: HookOpts) {
  const [pending, setPending] = useState<RectSel | null>(null);
  const suppressClick = useRef(false);

  const findCell = (ev: PointerEvent): { step: number; note: number } | null => {
    const el = document.elementFromPoint(ev.clientX, ev.clientY);
    const cell = el instanceof Element ? el.closest<HTMLElement>('[data-seq-cell]') : null;
    if (!cell || Number(cell.dataset.pattern) !== editPattern) return null;
    return { step: Number(cell.dataset.step), note: Number(cell.dataset.note) };
  };

  const track = (
    onPointerMove: (ev: PointerEvent) => void,
    onUp: (ev: PointerEvent) => void,
    onCancel: () => void,
  ) => {
    const cleanup = () => {
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('keydown', keydown);
    };
    const up = (ev: PointerEvent) => { cleanup(); onUp(ev); };
    const keydown = (ev: KeyboardEvent) => { if (ev.key === 'Escape') { cleanup(); onCancel(); } };
    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup', up);
    window.addEventListener('keydown', keydown);
  };

  const startRectSelect = (ev: React.PointerEvent, step: number, note: number) => {
    ev.preventDefault();
    let rect: RectSel = { stepFrom: step, stepTo: step, noteFrom: note, noteTo: note };
    setPending(rect);
    track(
      (e) => {
        const c = findCell(e);
        if (!c) return;
        rect = { stepFrom: step, stepTo: c.step, noteFrom: note, noteTo: c.note };
        setPending(rect);
      },
      () => { setPending(null); suppressClick.current = true; onSelect(rect); },
      () => setPending(null),
    );
  };

  const startRectMove = (ev: React.PointerEvent, step: number, note: number) => {
    ev.preventDefault();
    let dest = { step, note };
    track(
      (e) => { const c = findCell(e); if (c) dest = c; },
      (e) => {
        if (dest.step === step && dest.note === note) return; // plain tap inside rect: fall through to click
        suppressClick.current = true;
        onMove(dest.step - step, dest.note - note, e.altKey);
      },
      () => undefined,
    );
  };

  const consumeRectClick = (): boolean => {
    const v = suppressClick.current;
    suppressClick.current = false;
    return v;
  };

  return { pending, startRectSelect, startRectMove, consumeRectClick };
}
