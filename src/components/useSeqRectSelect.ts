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

  // `pattern` is the gesture's own bar: a shift-drag may start on a bar that
  // was not the edit pattern when the handlers were registered (the panel
  // switches editPattern at gesture start, but this closure would still see
  // the stale value — so the anchor's pattern is threaded through instead).
  const findCell = (ev: PointerEvent, pattern: number): { step: number; note: number } | null => {
    const el = document.elementFromPoint(ev.clientX, ev.clientY);
    const cell = el instanceof Element ? el.closest<HTMLElement>('[data-seq-cell]') : null;
    if (!cell || Number(cell.dataset.pattern) !== pattern) return null;
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
      window.removeEventListener('keydown', keydown, { capture: true });
    };
    const up = (ev: PointerEvent) => { cleanup(); onUp(ev); };
    const keydown = (ev: KeyboardEvent) => {
      if (ev.key !== 'Escape') return;
      // Capture-phase + stopPropagation: while sweeping the FIRST selection
      // the store's rectSel is still null (this pending rect is hook-local),
      // so a bubble-phase Escape would fall through to the app's own
      // shortcut (e.g. BL-1's stop()) and kill playback mid-gesture.
      ev.stopPropagation();
      cleanup();
      onCancel();
    };
    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup', up);
    window.addEventListener('keydown', keydown, { capture: true });
  };

  const startRectSelect = (ev: React.PointerEvent, step: number, note: number, pattern?: number) => {
    ev.preventDefault();
    const pat = pattern ?? editPattern;
    let rect: RectSel = { stepFrom: step, stepTo: step, noteFrom: note, noteTo: note };
    setPending(rect);
    track(
      (e) => {
        const c = findCell(e, pat);
        if (!c) return;
        rect = { stepFrom: step, stepTo: c.step, noteFrom: note, noteTo: c.note };
        setPending(rect);
      },
      () => {
        setPending(null);
        // A drag that ends on a different cell than it started fires click on
        // the common ancestor, not a cell — consumeRectClick never runs to
        // clear the flag. Release it on the next tick so it can't eat a later
        // real tap (see useSeqNoteDrag for the same pattern).
        suppressClick.current = true;
        setTimeout(() => { suppressClick.current = false; }, 0);
        onSelect(rect);
      },
      () => setPending(null),
    );
  };

  const startRectMove = (ev: React.PointerEvent, step: number, note: number) => {
    ev.preventDefault();
    let dest = { step, note };
    track(
      (e) => { const c = findCell(e, editPattern); if (c) dest = c; },
      (e) => {
        if (dest.step === step && dest.note === note) return; // plain tap inside rect: fall through to click
        // Same seam as above: the drag may end off the common ancestor, so
        // consumeRectClick never fires to clear the flag. Release it next tick.
        suppressClick.current = true;
        setTimeout(() => { suppressClick.current = false; }, 0);
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
