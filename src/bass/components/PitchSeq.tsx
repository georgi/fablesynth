// The 16-step pitch sequencer: 12 note lanes per step (tap = set note,
// tap again = rest), draggable note lengths, octave / accent / slide rows,
// bars 1–4 and sequence length. Shift-click or drag across the step-number
// row selects a range (highlighted); Cmd-A/Esc/Cmd-C/X/V/D/Delete work the
// clipboard verbs from useBassKeys.

import { useRef } from 'react';
import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { NoteLengthHandle } from '../../components/NoteLengthHandle';
import { SeqSelectionMenu } from '../../components/SeqSelectionMenu';
import { useSeqNoteDrag } from '../../components/useSeqNoteDrag';
import { getStep, NOTE_LANES, STEPS } from '../seq';
import { useBassStore } from '../store';

export function PitchSeq({ bars }: { bars?: number } = {}) {
  const hosted = useBassStore((s) => s.hosted);
  const playing = useBassStore((s) => s.playing);
  const curStep = useBassStore((s) => s.curStep);
  const curPat = useBassStore((s) => s.curPat);
  const editPattern = useBassStore((s) => s.editPattern);
  const chain = useBassStore((s) => s.chain);
  const patterns = useBassStore((s) => s.patterns);
  const stepSel = useBassStore((s) => s.stepSel);
  const play = useBassStore((s) => s.play);
  const stop = useBassStore((s) => s.stop);
  const toggleCell = useBassStore((s) => s.toggleCell);
  const cycleStepOct = useBassStore((s) => s.cycleStepOct);
  const toggleStepAcc = useBassStore((s) => s.toggleStepAcc);
  const toggleStepSlide = useBassStore((s) => s.toggleStepSlide);
  const setStepDuration = useBassStore((s) => s.setStepDuration);
  const setEditPattern = useBassStore((s) => s.setEditPattern);
  const setSequenceLength = useBassStore((s) => s.setSequenceLength);
  const randomize = useBassStore((s) => s.randomize);
  const shiftClickStep = useBassStore((s) => s.shiftClickStep);
  const setStepSelection = useBassStore((s) => s.setStepSelection);
  const shiftSelection = useBassStore((s) => s.shiftSelection);
  const movePattern = useBassStore((s) => s.movePattern);
  const moveStepNote = useBassStore((s) => s.moveStepNote);
  const copySelection = useBassStore((s) => s.copySelection);
  const duplicateSelection = useBassStore((s) => s.duplicateSelection);
  const deleteSelection = useBassStore((s) => s.deleteSelection);
  const clearStepSelection = useBassStore((s) => s.clearStepSelection);

  // Grid note drag (docs/superpowers/specs/2026-07-19-seq-note-drag-selection-menu-design.md):
  // grab a lit cell and drop it on another step/lane of the same pattern.
  const { drag, startNoteDrag, consumeDragClick } = useSeqNoteDrag((from, to, note, copy, pattern) => {
    moveStepNote(from, to, note, { copy }, pattern);
    if (pattern === useBassStore.getState().editPattern) setStepSelection({ from: to, to });
  });

  // Sweep-drag across the step-number row (pointer events, not HTML5 drag):
  // pointerdown anchors the range on the edited pattern's row, pointermove
  // over any step-number cell (found via elementFromPoint, matched to the
  // edited pattern) extends it.
  const sweepAnchor = useRef<number | null>(null);
  const startSweep = (step: number, pattern: number) => {
    if (pattern !== editPattern) return;
    sweepAnchor.current = step;
    setStepSelection({ from: step, to: step });
    const move = (ev: PointerEvent) => {
      const el = document.elementFromPoint(ev.clientX, ev.clientY);
      const cell = el instanceof Element ? el.closest<HTMLElement>('[data-step-num]') : null;
      if (!cell || Number(cell.dataset.pattern) !== editPattern || sweepAnchor.current === null) return;
      setStepSelection({ from: sweepAnchor.current, to: Number(cell.dataset.step) });
    };
    const up = () => {
      sweepAnchor.current = null;
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  // Step-range drag: pointerdown *inside* the current selection shifts it
  // (move; Alt = copy) to wherever the pointer is released, via shiftRange.
  // Esc cancels — the shift only commits on pointerup.
  const shiftDrag = useRef<{ offset: number; dest: number } | null>(null);
  const startShiftDrag = (step: number, pattern: number) => {
    if (!stepSel || pattern !== editPattern) return;
    const lo = Math.min(stepSel.from, stepSel.to);
    shiftDrag.current = { offset: step - lo, dest: lo };
    const move = (ev: PointerEvent) => {
      if (!shiftDrag.current) return;
      const el = document.elementFromPoint(ev.clientX, ev.clientY);
      const cell = el instanceof Element ? el.closest<HTMLElement>('[data-step-num]') : null;
      if (!cell || Number(cell.dataset.pattern) !== editPattern) return;
      shiftDrag.current.dest = Number(cell.dataset.step) - shiftDrag.current.offset;
    };
    const cancel = () => {
      shiftDrag.current = null;
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('keydown', keydown);
    };
    const up = (ev: PointerEvent) => {
      if (shiftDrag.current) shiftSelection(shiftDrag.current.dest, { copy: ev.altKey });
      cancel();
    };
    const keydown = (ev: KeyboardEvent) => { if (ev.key === 'Escape') cancel(); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('keydown', keydown);
  };

  const isInSelection = (step: number, pattern: number) => pattern === editPattern && stepSel !== null
    && step >= Math.min(stepSel.from, stepSel.to) && step <= Math.max(stepSel.from, stepSel.to);

  const barCount = Math.max(1, Math.min(4, bars ?? chain.length));
  const totalSteps = barCount * STEPS;
  const steps = Array.from({ length: totalSteps }, (_, absoluteStep) => {
    const bar = Math.floor(absoluteStep / STEPS);
    const step = absoluteStep % STEPS;
    const pattern = chain[bar] ?? bar;
    return { absoluteStep, bar, step, pattern, value: getStep(patterns, pattern, step) };
  });

  return (
    <section className="panel bl-seq-section" data-accent="a">
      <div className="panel-head bl-seq-head">
        {!hosted && (
          <button
            className={`pb-btn bl-transport${playing ? ' active' : ''}`}
            type="button"
            aria-label={playing ? 'Stop sequencer' : 'Play sequencer'}
            aria-pressed={playing}
            onClick={playing ? stop : play}
          >
            {playing ? '■' : '▶'}
          </button>
        )}
        <h2>PITCH SEQ</h2>
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
        <button className="bl-seq-btn" type="button" onClick={randomize}>RAND</button>
        <span className="bl-seq-hint">DRAG = MOVE · EDGE = LENGTH · SLD = LEGATO</span>
      </div>

      <div className="bl-seq-body">
        <div className="bl-seq-legend">
          <div className="bl-legend-lanes"><span>B</span><span>NOTE</span><span>C</span></div>
          <div className="bl-legend-oct">OCT</div>
          <div className="bl-legend-acc">ACC</div>
          <div className="bl-legend-sld">SLD</div>
        </div>

        <div className="bl-seq-grid-scroll">
        <div className="bl-seq-grid" style={{ minWidth: `${totalSteps * 32}px` }}>
          <div className="bl-seq-cols">
            {steps.map(({ absoluteStep, bar, step, pattern, value: s }) => {
              const current = playing && curStep === step && curPat === pattern;
              const inSel = isInSelection(step, pattern);
              return (
                <div
                  className={`bl-seq-col${step === 0 && bar > 0 ? ' bar-start' : ''}${inSel ? ' selected' : ''}`}
                  key={absoluteStep}
                >
                  <div className="bl-seq-lanes">
                    {Array.from({ length: NOTE_LANES }, (_, r) => {
                      const note = NOTE_LANES - 1 - r;
                      const active = s.on && s.note === note;
                      const dragSrc = !!drag?.active && drag.pattern === pattern && drag.srcStep === step && drag.srcNote === note;
                      const dragOver = !!drag?.active && drag.pattern === pattern && drag.overStep === step && drag.overNote === note;
                      return (
                        <div className="bl-cell-wrap" key={r}>
                          <button
                            type="button"
                            data-seq-cell
                            data-step={step}
                            data-note={note}
                            data-pattern={pattern}
                            className={'bl-cell' + (note === 0 ? ' root' : '') + (active ? ' on' : '') + (dragSrc ? ' drag-src' : '') + (dragOver ? ` drag-over${drag.copy ? ' copy' : ''}` : '')}
                            aria-label={`bar ${bar + 1}, step ${step + 1}, note ${note}`}
                            aria-pressed={active}
                            onPointerDown={(event) => {
                              // Grab a lit cell — or the painted body of a longer
                              // note covering this cell — to move it (Alt = copy,
                              // Esc cancels).
                              if (hosted || event.shiftKey) return;
                              let srcStep = step;
                              if (!active) {
                                srcStep = -1;
                                for (let c = step - 1; c >= 0; c--) {
                                  const cand = getStep(patterns, pattern, c);
                                  if (cand.on && cand.note === note && c + cand.duration > step) { srcStep = c; break; }
                                }
                                if (srcStep < 0) return;
                              }
                              event.preventDefault();
                              startNoteDrag(event, srcStep, note, pattern, step);
                            }}
                            onClick={(event) => {
                              if (consumeDragClick()) return;
                              if (event.shiftKey) { if (pattern === editPattern) shiftClickStep(step); return; }
                              toggleCell(step, note, pattern);
                            }}
                          />
                          {active && (
                            <NoteLengthHandle
                              prefix="bl"
                              absoluteStep={absoluteStep}
                              totalSteps={totalSteps}
                              duration={s.duration}
                              onChange={(duration) => setStepDuration(step, duration, pattern)}
                            />
                          )}
                        </div>
                      );
                    })}
                  </div>
                  <button
                    type="button"
                    className={`bl-oct-btn${s.oct !== 0 ? ' set' : ''}`}
                    aria-label={`bar ${bar + 1}, step ${step + 1} octave`}
                    onClick={() => cycleStepOct(step, pattern)}
                  >
                    {s.oct === 0 ? '0' : s.oct > 0 ? '+1' : '−1'}
                  </button>
                  <button
                    type="button"
                    className={`bl-acc-btn${s.on && s.acc ? ' on' : ''}`}
                    aria-label={`bar ${bar + 1}, step ${step + 1} accent`}
                    aria-pressed={s.on && s.acc}
                    onClick={() => toggleStepAcc(step, pattern)}
                  />
                  <button
                    type="button"
                    className={`bl-sld-btn${s.on && s.slide ? ' on' : ''}`}
                    aria-label={`bar ${bar + 1}, step ${step + 1} slide`}
                    aria-pressed={s.on && s.slide}
                    onClick={() => toggleStepSlide(step, pattern)}
                  />
                  <span
                    className="bl-step-num"
                    data-step-num
                    data-step={step}
                    data-pattern={pattern}
                    onPointerDown={(event) => {
                      event.preventDefault();
                      if (isInSelection(step, pattern)) startShiftDrag(step, pattern);
                      else startSweep(step, pattern);
                    }}
                  >
                    {step === 0 ? `BAR ${bar + 1}` : step + 1}
                  </span>
                  <div className={`bl-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
                </div>
              );
            })}
          </div>
          {!hosted && stepSel && (() => {
            const editBarIdx = Array.from({ length: barCount }, (_, b) => chain[b] ?? b).indexOf(editPattern);
            const barOffset = Math.max(0, editBarIdx) * STEPS;
            return (
              <SeqSelectionMenu
                visibleLo={barOffset + Math.min(stepSel.from, stepSel.to)}
                visibleHi={barOffset + Math.max(stepSel.from, stepSel.to)}
                totalSteps={totalSteps}
                onCopy={copySelection}
                onDuplicate={duplicateSelection}
                onDelete={deleteSelection}
                onDismiss={clearStepSelection}
              />
            );
          })()}
        </div>
        </div>
      </div>
    </section>
  );
}
