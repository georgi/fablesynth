import { type ReactNode, type PointerEvent as ReactPointerEvent } from 'react';
import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { SeqSelectionMenu } from '../../components/SeqSelectionMenu';
import { useSeqRectSelect } from '../../components/useSeqRectSelect';
import { padRectNorm, type RectSel } from '../../shared/seqEdit';
import { STEPS, patIdx } from '../seq';
import { PAD_COUNT } from '../params';
import { useDrumStore } from '../store';
import { useDrumGhostPaste } from './useDrumGhostPaste';

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
  const rectSel = useDrumStore((s) => s.rectSel);
  const play = useDrumStore((s) => s.play);
  const stop = useDrumStore((s) => s.stop);
  const toggleStep = useDrumStore((s) => s.toggleStep);
  const setEditPattern = useDrumStore((s) => s.setEditPattern);
  const setSequenceLength = useDrumStore((s) => s.setSequenceLength);
  const setRectSel = useDrumStore((s) => s.setRectSel);
  const moveRectSel = useDrumStore((s) => s.moveRectSel);
  const dropRect = useDrumStore((s) => s.dropRect);
  const copySelection = useDrumStore((s) => s.copySelection);
  const duplicateSelection = useDrumStore((s) => s.duplicateSelection);
  const deleteSelection = useDrumStore((s) => s.deleteSelection);
  const clearStepSel = useDrumStore((s) => s.clearStepSel);
  const movePattern = useDrumStore((s) => s.movePattern);
  const randomizePad = useDrumStore((s) => s.randomizePad);
  const selectPad = useDrumStore((s) => s.selectPad);

  // Shift-drag rectangle selection + in-rect block move over the step × pad
  // grid — the shared WT-1/BL-1 pointer hook, with the note axis reused as the
  // pad-lane index (data-note = pad). Both gestures commit once on release, so
  // each costs one undo entry; Escape cancels mid-gesture. Standalone-only.
  const { pending, startRectSelect, startRectMove, consumeRectClick } = useSeqRectSelect({
    onSelect: (r: RectSel) => setRectSel({ stepFrom: r.stepFrom, stepTo: r.stepTo, padFrom: r.noteFrom, padTo: r.noteTo }),
    onMove: (dStep, dPad, copy) => moveRectSel(dStep, dPad, { copy }),
  });
  // While sweeping, `pending` (a note-axis RectSel) mirrors the live rect; fall
  // back to the committed pad rect otherwise.
  const rect = pending
    ? { stepLo: Math.min(pending.stepFrom, pending.stepTo), stepHi: Math.max(pending.stepFrom, pending.stepTo),
        padLo: Math.min(pending.noteFrom, pending.noteTo), padHi: Math.max(pending.noteFrom, pending.noteTo) }
    : rectSel ? padRectNorm(rectSel) : null;
  const inRect = (step: number, padI: number): boolean =>
    !!rect && step >= rect.stepLo && step <= rect.stepHi && padI >= rect.padLo && padI <= rect.padHi;

  // Ghost paste: menu CUT/COPY picks the selection up — the menu closes, the
  // cells trail the cursor, and the next click drops them (Escape or an
  // outside-grid click cancels; a cancelled CUT changes nothing).
  const { ghost, beginGhost, ghostAt, isCutSrc } = useDrumGhostPaste({ onDrop: dropRect });
  const pickUpSelection = (cut: boolean) => {
    if (!rectSel) return;
    copySelection(); // keep the Cmd-V clipboard in sync
    const clip = useDrumStore.getState().clipboard;
    if (clip?.kind !== 'rect') return;
    beginGhost(clip.data, { cut, src: rectSel });
    clearStepSel();
  };

  const onStepPointerDown = (e: ReactPointerEvent<HTMLButtonElement>, padI: number, step: number) => {
    if (hosted) return;
    if (e.shiftKey) { startRectSelect(e, step, padI); return; }
    if (rectSel && inRect(step, padI) && !pending) { startRectMove(e, step, padI); return; }
    // Plain press retargets editing to that lane's pad so the click toggles the
    // row under the pointer regardless of selection order.
    if (padI !== sel) selectPad(padI);
  };

  const onStepClick = (padI: number, step: number) => {
    if (consumeRectClick()) return;
    toggleStep(step, padI);
  };

  // One lane per pad in STEP mode (the pad grid is hidden there); PADS mode
  // keeps the single tall row for the pad being played. Lanes run high pad to
  // low, top to bottom, so the stack reads like the pad grid's bottom-left
  // origin rather than inverted from it.
  const laneView = hosted || mode === 'step';
  const lanes = laneView
    ? Array.from({ length: PAD_COUNT }, (_, i) => PAD_COUNT - 1 - i)
    : [sel];

  const renderStep = (padI: number, step: number) => {
    const value = patterns[patIdx(editPattern, padI, step)];
    const current = playing && curStep === step && curPat === editPattern;
    const selected = !hosted && inRect(step, padI);
    const isGhost = ghost ? ghostAt(step, padI) : false;
    const cutSrc = ghost ? isCutSrc(step, padI) : false;
    return (
      <button
        className={`step${value >= 1 ? ' on' : ''}${value === 2 ? ' accented' : ''}${current ? ' cur' : ''}${selected ? ' selected' : ''}${isGhost ? ' ghost' : ''}${cutSrc ? ' cut-src' : ''}`}
        type="button"
        data-seq-cell
        data-abs-step={step}
        data-note={padI}
        aria-label={`${padNames[padI]} step ${step + 1}: ${value === 2 ? 'accent' : value === 1 ? 'on' : 'off'}`}
        aria-pressed={value >= 1}
        key={step}
        onClick={() => onStepClick(padI, step)}
        onPointerDown={(e) => onStepPointerDown(e, padI, step)}
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
      <div className={`dr-lanes-wrap${laneView ? '' : ' single'}`}>
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
        {!hosted && rectSel && !ghost && (() => {
          const { stepLo, stepHi } = padRectNorm(rectSel);
          return (
            <SeqSelectionMenu
              visibleLo={stepLo}
              visibleHi={stepHi}
              totalSteps={STEPS}
              onCut={() => pickUpSelection(true)}
              onCopy={() => pickUpSelection(false)}
              onDuplicate={duplicateSelection}
              onDelete={deleteSelection}
              onDismiss={clearStepSel}
            />
          );
        })()}
      </div>
    </section>
  );
}
