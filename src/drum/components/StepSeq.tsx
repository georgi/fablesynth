import { useEffect, useRef, type ReactNode, type PointerEvent as ReactPointerEvent } from 'react';
import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { inStepSel, STEPS, patIdx } from '../seq';
import { PAD_COUNT } from '../params';
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
  row: HTMLElement;
  pointerId: number;
}

export function StepSeq({ headerExtra }: { headerExtra?: ReactNode }) {
  const hosted = useDrumStore((s) => s.hosted);
  const mode = useDrumStore((s) => s.mode);
  const playing = useDrumStore((s) => s.playing);
  const curStep = useDrumStore((s) => s.curStep);
  const curPat = useDrumStore((s) => s.curPat);
  const editPattern = useDrumStore((s) => s.editPattern);
  const chain = useDrumStore((s) => s.chain);
  const patterns = useDrumStore((s) => s.patterns);
  const sel = useDrumStore((s) => s.sel);
  const padNames = useDrumStore((s) => s.padNames);
  const padName = padNames[sel];
  const stepSel = useDrumStore((s) => s.stepSel);
  const play = useDrumStore((s) => s.play);
  const stop = useDrumStore((s) => s.stop);
  const toggleStep = useDrumStore((s) => s.toggleStep);
  const setEditPattern = useDrumStore((s) => s.setEditPattern);
  const setSequenceLength = useDrumStore((s) => s.setSequenceLength);
  const setStepSelHead = useDrumStore((s) => s.setStepSelHead);
  const shiftSelection = useDrumStore((s) => s.shiftSelection);
  const movePattern = useDrumStore((s) => s.movePattern);
  const randomizePad = useDrumStore((s) => s.randomizePad);
  const selectPad = useDrumStore((s) => s.selectPad);

  const drag = useRef<DragState | null>(null);
  const suppressClick = useRef(false);

  const stepAt = (clientX: number, row: HTMLElement) => {
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

  const onStepPointerDown = (e: ReactPointerEvent<HTMLButtonElement>, padI: number, step: number) => {
    if (hosted) return;
    // In lane mode a press anywhere retargets editing to that lane's pad, so
    // the selection/shift verbs below always act on the row under the pointer.
    if (padI !== sel) selectPad(padI);
    const row = e.currentTarget.parentElement as HTMLElement;
    if (e.shiftKey) {
      setStepSelHead(step);
      drag.current = { mode: 'sweep', startX: e.clientX, moved: false, el: e.currentTarget, row, pointerId: e.pointerId };
      e.currentTarget.setPointerCapture(e.pointerId);
      return;
    }
    if (inStepSel(stepSel, step)) {
      drag.current = { mode: 'shift', startX: e.clientX, moved: false, el: e.currentTarget, row, pointerId: e.pointerId };
      e.currentTarget.setPointerCapture(e.pointerId);
    }
  };

  const onStepPointerMove = (e: ReactPointerEvent<HTMLButtonElement>) => {
    const d = drag.current;
    if (!d || !e.currentTarget.hasPointerCapture(e.pointerId)) return;
    if (!d.moved && Math.abs(e.clientX - d.startX) < DRAG_THRESHOLD) return;
    d.moved = true;
    if (d.mode === 'sweep') setStepSelHead(stepAt(e.clientX, d.row));
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
    if (d.mode === 'shift') shiftSelection(stepAt(e.clientX, d.row), { copy: e.altKey });
  };

  const onStepClick = (padI: number, step: number) => {
    if (suppressClick.current) { suppressClick.current = false; return; }
    toggleStep(step, padI);
  };

  // One lane per pad in STEP mode (the pad grid is hidden there); PADS mode
  // keeps the single tall row for the pad being played. Lanes run high pad to
  // low, top to bottom, so the stack reads like the pad grid's bottom-left
  // origin rather than inverted from it.
  // Hosted in SQ-4 the panel only mounts in the rig's SEQ mode, which hides
  // the pad grid the same way — always lanes there.
  const laneView = hosted || mode === 'step';
  const lanes = laneView
    ? Array.from({ length: PAD_COUNT }, (_, i) => PAD_COUNT - 1 - i)
    : [sel];

  const renderStep = (padI: number, step: number) => {
    const value = patterns[patIdx(editPattern, padI, step)];
    const current = playing && curStep === step && curPat === editPattern;
    const selected = padI === sel && inStepSel(stepSel, step);
    return (
      <button
        className={`step${value >= 1 ? ' on' : ''}${value === 2 ? ' accented' : ''}${current ? ' cur' : ''}${selected ? ' selected' : ''}`}
        type="button"
        aria-label={`${padNames[padI]} step ${step + 1}: ${value === 2 ? 'accent' : value === 1 ? 'on' : 'off'}`}
        aria-pressed={value >= 1}
        key={step}
        onClick={() => onStepClick(padI, step)}
        onPointerDown={(e) => onStepPointerDown(e, padI, step)}
        onPointerMove={onStepPointerMove}
        onPointerUp={onStepPointerUp}
      >
        <span className="step-accent" aria-hidden="true" />
        <span className="step-fill" aria-hidden="true" />
        <span className="step-num" aria-hidden="true">{step + 1}</span>
      </button>
    );
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
        <span className="dr-stepseq-target">{padName}</span>
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
            <button className="dr-seq-btn" type="button" onClick={randomizePad}>RAND</button>
          </>
        )}
        {headerExtra}
        <div className="dr-step-editing">
          <span>TAP STEP · ON → ACCENT → OFF · SHIFT-DRAG TO SELECT</span>
        </div>
      </div>
      <div className={laneView ? 'dr-lanes' : undefined}>
        {lanes.map((padI) => (
          <div className="dr-lane" key={padI}>
            {laneView && (
              <button
                className={`dr-lane-name${padI === sel ? ' sel' : ''}`}
                type="button"
                onClick={() => selectPad(padI)}
              >
                <span className="dr-lane-num">{String(padI + 1).padStart(2, '0')}</span>
                {padNames[padI]}
              </button>
            )}
            <div className="step-row">
              {Array.from({ length: STEPS }, (_, step) => renderStep(padI, step))}
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}
