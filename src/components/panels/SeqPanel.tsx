// The WT-1 note sequencer: 12 note lanes per step (tap = set note, tap again
// = rest), draggable note lengths, octave / accent rows, bars 1–4 + sequence
// length, RAND, transport and ROOT controls.

import { useEffect, useRef } from 'react';
import { getStep, NOTE_LANES, STEPS, type SeqStep } from '../../noteseq';
import { useStore } from '../../store';
import { NoteLengthHandle } from '../NoteLengthHandle';
import { SeqSelectionMenu } from '../SeqSelectionMenu';
import { SequenceLengthControl } from '../SequenceLengthControl';
import { Stepper } from '../Stepper';
import { useSeqNoteDrag } from '../useSeqNoteDrag';

// Piano-style shading for the 12 chromatic lanes (lane 0 = root/tonic), so
// the grid reads at a glance the way a keyboard does: natural-degree lanes
// sit slightly lighter than sharp-degree lanes.
const SHARP_LANE = [false, true, false, true, false, false, true, false, true, false, true, false];

interface SeqPanelProps {
  /** Hosted SQ-4 clips have up to eight WT voices per step; standalone patterns do not. */
  polySteps?: SeqStep[][];
  bars?: number;
  onToggleChordNote?: (step: number, note: number) => void;
  onSetChordDuration?: (step: number, note: number, duration: number) => void;
}

export function SeqPanel({ polySteps, bars, onToggleChordNote, onSetChordDuration }: SeqPanelProps = {}) {
  const hosted = useStore((s) => s.hosted);
  const seqPlaying = useStore((s) => s.seqPlaying);
  const curStep = useStore((s) => s.curStep);
  const curPat = useStore((s) => s.curPat);
  const editPattern = useStore((s) => s.editPattern);
  const chain = useStore((s) => s.chain);
  const patterns = useStore((s) => s.patterns);
  const seqPlay = useStore((s) => s.seqPlay);
  const seqStop = useStore((s) => s.seqStop);
  const toggleCell = useStore((s) => s.toggleCell);
  const cycleStepOct = useStore((s) => s.cycleStepOct);
  const toggleStepAcc = useStore((s) => s.toggleStepAcc);
  const setStepDuration = useStore((s) => s.setStepDuration);
  const setEditPattern = useStore((s) => s.setEditPattern);
  const setSequenceLength = useStore((s) => s.setSequenceLength);
  const randomizeSeq = useStore((s) => s.randomizeSeq);
  const stepSel = useStore((s) => s.stepSel);
  const setStepSel = useStore((s) => s.setStepSel);
  const shiftStepSel = useStore((s) => s.shiftStepSel);
  const movePattern = useStore((s) => s.movePattern);
  const moveStepNote = useStore((s) => s.moveStepNote);
  const copySteps = useStore((s) => s.copySteps);
  const duplicateSteps = useStore((s) => s.duplicateSteps);
  const deleteSteps = useStore((s) => s.deleteSteps);
  const clearStepSel = useStore((s) => s.clearStepSel);

  // Grid note drag (docs/superpowers/specs/2026-07-19-seq-note-drag-selection-menu-design.md):
  // grab a lit cell and drop it on another step/lane of the same pattern.
  // Standalone-only, like the step-range selection below.
  const { drag, startNoteDrag, consumeDragClick } = useSeqNoteDrag((from, to, note, copy, pattern) => {
    moveStepNote(from, to, note, { copy }, pattern);
    if (pattern === useStore.getState().editPattern) setStepSel({ from: to, to });
  });

  // Step-range selection (docs/editing-concept.md): a step-number header press
  // starts either a sweep-select (unselected step, or Shift held) or a
  // content-move drag (pressing inside the current selection). The move is
  // committed once, on release, via shiftStepSel — never per pointer-move, so
  // it costs a single undo entry.
  const sweepingRef = useRef(false);
  const moveRef = useRef<{ selFrom: number; origin: number; hover: number; altKey: boolean } | null>(null);

  useEffect(() => {
    const commitAndReset = () => {
      if (moveRef.current) {
        const { selFrom, origin, hover, altKey } = moveRef.current;
        shiftStepSel(selFrom + (hover - origin), { copy: altKey });
      }
      moveRef.current = null;
      sweepingRef.current = false;
    };
    const onPointerUp = () => commitAndReset();
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return;
      moveRef.current = null; // cancel without committing
      sweepingRef.current = false;
    };
    window.addEventListener('pointerup', onPointerUp);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('pointerup', onPointerUp);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [shiftStepSel]);

  const barCount = Math.max(1, Math.min(4, bars ?? (polySteps ? Math.ceil(polySteps.length / STEPS) : chain.length)));
  const totalSteps = barCount * STEPS;
  const steps = Array.from({ length: totalSteps }, (_, absoluteStep) => {
    const bar = Math.floor(absoluteStep / STEPS);
    const step = absoluteStep % STEPS;
    const pattern = chain[bar] ?? bar;
    return { absoluteStep, bar, step, pattern, value: getStep(patterns, pattern, step) };
  });

  return (
    <section className="panel ns-section" style={{ gridArea: 'seq' }} data-accent="a">
      <div className="panel-head ns-head">
        {!hosted && (
          <button
            className={`pb-btn ns-transport${seqPlaying ? ' active' : ''}`}
            type="button"
            aria-label={seqPlaying ? 'Stop sequencer' : 'Play sequencer'}
            aria-pressed={seqPlaying}
            onClick={seqPlaying ? seqStop : seqPlay}
          >
            {seqPlaying ? '■' : '▶'}
          </button>
        )}
        <h2>NOTE SEQ</h2>
        {!hosted && (
          <>
            <SequenceLengthControl
              editBar={editPattern}
              length={chain.length}
              playingBar={seqPlaying ? curPat : null}
              onEditBar={setEditPattern}
              onLengthChange={setSequenceLength}
              onMovePattern={movePattern}
            />
          </>
        )}
        <button className="ns-btn" type="button" onClick={randomizeSeq}>RAND</button>
        <span className="ns-hint">TAP = NOTE · DRAG = MOVE · EDGE = LENGTH</span>
      </div>

      <div className="ns-body">
        <div className="ns-legend">
          <div className="ns-legend-lanes"><span>B</span><span>NOTE</span><span>C</span></div>
          <div className="ns-legend-oct">OCT</div>
          <div className="ns-legend-acc">ACC</div>
        </div>

        <div className="ns-grid-scroll">
          <div className="ns-grid" style={{ minWidth: `${totalSteps * 32}px` }}>
          <div className="ns-cols">
            {steps.map(({ absoluteStep, bar, step, pattern, value: s }) => {
              const current = seqPlaying && curStep === step && curPat === pattern;
              const voices = polySteps?.[absoluteStep]?.length ? polySteps[absoluteStep] : [s];
              const editable = pattern === editPattern;
              const selLo = stepSel ? Math.min(stepSel.from, stepSel.to) : -1;
              const selHi = stepSel ? Math.max(stepSel.from, stepSel.to) : -1;
              const selected = editable && stepSel !== null && step >= selLo && step <= selHi;
              return (
                <div className={`ns-col${step === 0 && bar > 0 ? ' bar-start' : ''}`} key={absoluteStep}>
                  <div className="ns-lanes">
                    {Array.from({ length: NOTE_LANES }, (_, r) => {
                      const note = NOTE_LANES - 1 - r;
                      const voice = voices.find((candidate) => candidate.on && candidate.note === note);
                      const active = !!voice;
                      const dragSrc = !!drag?.active && drag.pattern === pattern && drag.srcStep === step && drag.srcNote === note;
                      const dragOver = !!drag?.active && drag.pattern === pattern && drag.overStep === step && drag.overNote === note;
                      return (
                        <div className="ns-cell-wrap" key={r}>
                          <button
                            type="button"
                            data-seq-cell
                            data-step={step}
                            data-note={note}
                            data-pattern={pattern}
                            className={'ns-cell' + (note === 0 ? ' root' : (SHARP_LANE[note] ? ' sharp' : ' natural')) + (active ? ' on' : '') + (dragSrc ? ' drag-src' : '') + (dragOver ? ` drag-over${drag.copy ? ' copy' : ''}` : '')}
                            aria-label={`bar ${bar + 1}, step ${step + 1}, note ${note}`}
                            aria-pressed={active}
                            onPointerDown={(event) => {
                              // Grab a lit cell — or the painted body of a longer
                              // note covering this cell — to move it; standalone
                              // mono grid only (hosted poly keeps chord callbacks).
                              if (hosted || onToggleChordNote || event.shiftKey) return;
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
                              if (onToggleChordNote) { onToggleChordNote(absoluteStep, note); return; }
                              if (!hosted && event.shiftKey) {
                                // Shift-click in the grid: anchor/extend the step range.
                                if (editable) setStepSel({ from: stepSel ? stepSel.from : step, to: step });
                                return;
                              }
                              toggleCell(step, note, pattern);
                            }}
                          />
                          {active && voice && (
                            <NoteLengthHandle
                              prefix="ns"
                              absoluteStep={absoluteStep}
                              totalSteps={totalSteps}
                              duration={voice.duration}
                              onChange={(duration) => onSetChordDuration
                                ? onSetChordDuration(absoluteStep, note, duration)
                                : setStepDuration(step, duration, pattern)}
                            />
                          )}
                        </div>
                      );
                    })}
                  </div>
                  <button
                    type="button"
                    className={`ns-oct-btn${s.oct !== 0 ? ' set' : ''}`}
                    aria-label={`bar ${bar + 1}, step ${step + 1} octave`}
                    onClick={() => cycleStepOct(step, pattern)}
                  >
                    {s.oct === 0 ? '0' : s.oct > 0 ? '+1' : '−1'}
                  </button>
                  <button
                    type="button"
                    className={`ns-acc-btn${s.on && s.acc ? ' on' : ''}`}
                    aria-label={`bar ${bar + 1}, step ${step + 1} accent`}
                    aria-pressed={s.on && s.acc}
                    onClick={() => toggleStepAcc(step, pattern)}
                  />
                  <button
                    type="button"
                    className={`ns-step-num${selected ? ' selected' : ''}`}
                    aria-label={`bar ${bar + 1}, step ${step + 1}, select`}
                    aria-pressed={selected}
                    onPointerDown={(event) => {
                      // Hosted (SQ-4 focus) has no edit-verb/undo/Esc key
                      // surface for step ranges — selection and drag-move are
                      // standalone-only, mirroring DR-1's StepSeq gate.
                      if (hosted || !editable) return;
                      if (event.shiftKey) {
                        setStepSel({ from: stepSel ? stepSel.from : step, to: step });
                        sweepingRef.current = true;
                        return;
                      }
                      if (selected && stepSel) {
                        moveRef.current = { selFrom: selLo, origin: step, hover: step, altKey: event.altKey };
                        return;
                      }
                      setStepSel({ from: step, to: step });
                      sweepingRef.current = true;
                    }}
                    onPointerEnter={(event) => {
                      if (hosted || !editable || event.buttons !== 1) return;
                      if (moveRef.current) { moveRef.current.hover = step; moveRef.current.altKey = event.altKey; return; }
                      if (sweepingRef.current && stepSel) setStepSel({ from: stepSel.from, to: step });
                    }}
                  >
                    {step === 0 ? `BAR ${bar + 1}` : step + 1}
                  </button>
                  <div className={`ns-step-sel${selected ? ' on' : ''}`} aria-hidden="true" />
                  <div className={`ns-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
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
                onCopy={copySteps}
                onDuplicate={duplicateSteps}
                onDelete={deleteSteps}
                onDismiss={clearStepSel}
              />
            );
          })()}
          </div>
        </div>

        <div className="ns-clock">
          <Stepper paramId="seq.root" label="ROOT" accent="a" />
        </div>
      </div>
    </section>
  );
}
