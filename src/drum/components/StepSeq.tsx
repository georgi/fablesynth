import { useEffect, useRef, type PointerEvent as ReactPointerEvent } from 'react';
import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { inStepSel, STEPS, patIdx } from '../seq';
import { useDrumStore } from '../store';

// Movement past this many pixels turns a pointerdown into a drag gesture
// instead of a click — mirrors NoteLengthHandle.tsx's threshold.
const DRAG_THRESHOLD = 4;

type DragMode = 'sweep' | 'shift';

interface DragState {
  mode: DragMode;
  startX: number;
  moved: boolean;
  el: HTMLButtonElement;
  pointerId: number;
}

export function StepSeq() {
  const hosted = useDrumStore((s) => s.hosted);
  const playing = useDrumStore((s) => s.playing);
  const curStep = useDrumStore((s) => s.curStep);
  const curPat = useDrumStore((s) => s.curPat);
  const editPattern = useDrumStore((s) => s.editPattern);
  const chain = useDrumStore((s) => s.chain);
  const patterns = useDrumStore((s) => s.patterns);
  const sel = useDrumStore((s) => s.sel);
  const padName = useDrumStore((s) => s.padNames[s.sel]);
  const stepSel = useDrumStore((s) => s.stepSel);
  const play = useDrumStore((s) => s.play);
  const stop = useDrumStore((s) => s.stop);
  const toggleStep = useDrumStore((s) => s.toggleStep);
  const setEditPattern = useDrumStore((s) => s.setEditPattern);
  const setSequenceLength = useDrumStore((s) => s.setSequenceLength);
  const setStepSelHead = useDrumStore((s) => s.setStepSelHead);
  const shiftSelection = useDrumStore((s) => s.shiftSelection);
  const movePattern = useDrumStore((s) => s.movePattern);

  const rowRef = useRef<HTMLDivElement | null>(null);
  const drag = useRef<DragState | null>(null);
  const suppressClick = useRef(false);

  const stepAt = (clientX: number) => {
    const row = rowRef.current;
    if (!row) return 0;
    const rect = row.getBoundingClientRect();
    const pitch = rect.width / STEPS;
    return Math.min(STEPS - 1, Math.max(0, Math.floor((clientX - rect.left) / pitch)));
  };

  // Escape cancels an in-progress drag without committing anything (the
  // pattern buffer is only touched at pointerup).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape' || !drag.current) return;
      try { drag.current.el.releasePointerCapture(drag.current.pointerId); } catch { /* already released */ }
      drag.current = null;
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const onStepPointerDown = (e: ReactPointerEvent<HTMLButtonElement>, step: number) => {
    if (hosted) return;
    if (e.shiftKey) {
      setStepSelHead(step);
      drag.current = { mode: 'sweep', startX: e.clientX, moved: false, el: e.currentTarget, pointerId: e.pointerId };
      e.currentTarget.setPointerCapture(e.pointerId);
      return;
    }
    if (inStepSel(stepSel, step)) {
      drag.current = { mode: 'shift', startX: e.clientX, moved: false, el: e.currentTarget, pointerId: e.pointerId };
      e.currentTarget.setPointerCapture(e.pointerId);
    }
  };

  const onStepPointerMove = (e: ReactPointerEvent<HTMLButtonElement>) => {
    const d = drag.current;
    if (!d || !e.currentTarget.hasPointerCapture(e.pointerId)) return;
    if (!d.moved && Math.abs(e.clientX - d.startX) < DRAG_THRESHOLD) return;
    d.moved = true;
    if (d.mode === 'sweep') setStepSelHead(stepAt(e.clientX));
  };

  const onStepPointerUp = (e: ReactPointerEvent<HTMLButtonElement>) => {
    const d = drag.current;
    drag.current = null;
    if (!d) return;
    e.currentTarget.releasePointerCapture(e.pointerId);
    if (!d.moved) {
      // Stationary shift-click: the selection was already set on pointerdown;
      // swallow the trailing click so it doesn't ALSO toggle the step. A
      // stationary press inside the selection ('shift' mode) stays a click.
      if (d.mode === 'sweep') suppressClick.current = true;
      return;
    }
    suppressClick.current = true;
    if (d.mode === 'shift') shiftSelection(stepAt(e.clientX), { copy: e.altKey });
  };

  const onStepClick = (step: number) => {
    if (suppressClick.current) { suppressClick.current = false; return; }
    toggleStep(step);
  };

  return (
    <section className="panel dr-stepseq" data-accent="a">
      <div className="panel-head dr-stepseq-head">
        {!hosted && (
          <button
            className={`pb-btn dr-transport${playing ? ' active' : ''}`}
            type="button"
            aria-label={playing ? 'Stop sequencer' : 'Play sequencer'}
            aria-pressed={playing}
            onClick={playing ? stop : play}
          >
            {playing ? '■' : '▶'}
          </button>
        )}
        <h2>STEP SEQ</h2>
        {!hosted && (
          <>
            <SequenceLengthControl
              editBar={editPattern}
              length={chain.length}
              playingBar={playing ? curPat : null}
              onEditBar={setEditPattern}
              onLengthChange={setSequenceLength}
              onMovePattern={movePattern}
            />
          </>
        )}
        <div className="dr-step-editing">
          <span>EDITING</span>
          <strong>{padName}</strong>
          <span>TAP STEP · ON → ACCENT → OFF · SHIFT-DRAG TO SELECT</span>
        </div>
      </div>
      <div className="step-row" ref={rowRef}>
        {Array.from({ length: STEPS }, (_, step) => {
          const value = patterns[patIdx(editPattern, sel, step)];
          const current = playing && curStep === step && curPat === editPattern;
          const selected = inStepSel(stepSel, step);
          return (
            <button
              className={`step${value >= 1 ? ' on' : ''}${value === 2 ? ' accented' : ''}${current ? ' cur' : ''}${selected ? ' selected' : ''}`}
              type="button"
              aria-label={`Step ${step + 1}: ${value === 2 ? 'accent' : value === 1 ? 'on' : 'off'}`}
              aria-pressed={value >= 1}
              key={step}
              onClick={() => onStepClick(step)}
              onPointerDown={(e) => onStepPointerDown(e, step)}
              onPointerMove={onStepPointerMove}
              onPointerUp={onStepPointerUp}
            >
              <span className="step-accent" aria-hidden="true" />
              <span className="step-fill" aria-hidden="true" />
              <span className="step-num" aria-hidden="true">{step + 1}</span>
            </button>
          );
        })}
      </div>
    </section>
  );
}
