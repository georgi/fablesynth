// The 16-step pitch sequencer: 12 note lanes per step (tap = set note,
// tap again = rest), draggable note lengths, octave / accent / slide rows,
// bars 1–4 and sequence length. Shift-drag a cell to sweep a step ×
// note-lane rectangle (highlighted); drag inside the rect to move it
// (Alt = copy). Cmd-A/Esc/Cmd-C/X/V/D/Delete work the clipboard verbs
// from useBassKeys.

import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { NoteLengthHandle } from '../../components/NoteLengthHandle';
import { SeqSelectionMenu } from '../../components/SeqSelectionMenu';
import { useSeqGhostPaste } from '../../components/useSeqGhostPaste';
import { useSeqNoteDrag } from '../../components/useSeqNoteDrag';
import { useSeqRectSelect } from '../../components/useSeqRectSelect';
import { copyRectChain, rectNorm } from '../../shared/seqEdit';
import { getStep, LAYOUT, NOTE_LANES, STEPS } from '../seq';
import { useBassStore } from '../store';

export function PitchSeq({ bars }: { bars?: number } = {}) {
  const hosted = useBassStore((s) => s.hosted);
  const playing = useBassStore((s) => s.playing);
  const curStep = useBassStore((s) => s.curStep);
  const curPat = useBassStore((s) => s.curPat);
  const editPattern = useBassStore((s) => s.editPattern);
  const chain = useBassStore((s) => s.chain);
  const patterns = useBassStore((s) => s.patterns);
  const rectSel = useBassStore((s) => s.rectSel);
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
  const setRectSel = useBassStore((s) => s.setRectSel);
  const moveRectSel = useBassStore((s) => s.moveRectSel);
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
    const bar = useBassStore.getState().chain.indexOf(pattern);
    if (bar >= 0) setRectSel({ stepFrom: bar * STEPS + to, stepTo: bar * STEPS + to, noteFrom: note, noteTo: note });
  });

  // Rectangle selection + in-rect block-move
  // (docs/superpowers/specs/2026-07-19-seq-rect-selection-design.md):
  // shift-drag a cell to sweep a step × note-lane rect; drag inside the
  // current rect to move (Alt-drag copies) it. Both gestures commit once, on
  // pointer release, so each costs a single undo entry.
  const { pending, startRectSelect, startRectMove, consumeRectClick } = useSeqRectSelect({
    onSelect: setRectSel,
    onMove: (dStep, dNote, copy) => moveRectSel(dStep, dNote, { copy }),
  });
  const rect = pending ?? rectSel;
  const inRect = (step: number, note: number): boolean => {
    if (!rect) return false;
    const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rect);
    return step >= stepLo && step <= stepHi && note >= noteLo && note <= noteHi;
  };

  // Ghost paste: menu CUT/COPY picks the selection up — the menu closes, the
  // cells trail the cursor as ghosts, and the next click drops them (Escape
  // or clicking outside the grid cancels; a cancelled CUT changes nothing).
  const dropRect = useBassStore((s) => s.dropRect);
  const { ghost, beginGhost, ghostAt, isCutSrc } = useSeqGhostPaste({ onDrop: dropRect });
  const pickUpSelection = (cut: boolean) => {
    if (!rectSel) return;
    copySelection(); // keep the clipboard in sync so Cmd-V still pastes the same cells
    beginGhost(copyRectChain(patterns, LAYOUT, chain, rectSel), { cut, src: rectSel });
    clearStepSelection();
  };

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
              return (
                <div
                  className={`bl-seq-col${step === 0 && bar > 0 ? ' bar-start' : ''}`}
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
                            data-abs-step={absoluteStep}
                            data-note={note}
                            data-pattern={pattern}
                            className={'bl-cell' + (note === 0 ? ' root' : '') + (active ? ' on' : '') + (dragSrc ? ' drag-src' : '') + (dragOver ? ` drag-over${drag.copy ? ' copy' : ''}` : '') + (ghost ? (ghostAt(absoluteStep, note) ? ' ghost' : isCutSrc(absoluteStep, note) ? ' drag-src' : '') : '')}
                            aria-label={`bar ${bar + 1}, step ${step + 1}, note ${note}`}
                            aria-pressed={active}
                            onPointerDown={(event) => {
                              // Grab a lit cell — or the painted body of a longer
                              // note covering this cell — to move it (Alt = copy,
                              // Esc cancels). Shift-drag sweeps a selection rect;
                              // a plain drag inside the current rect moves it.
                              if (hosted) return;
                              // Selection is timeline-wide: shift-drag sweeps
                              // across bars, and both gestures use absolute steps.
                              if (event.shiftKey) { startRectSelect(event, absoluteStep, note); return; }
                              if (rectSel && inRect(absoluteStep, note) && !pending) { startRectMove(event, absoluteStep, note); return; }
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
                            onClick={() => {
                              if (consumeRectClick() || consumeDragClick()) return;
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
                  <span className="bl-step-num">
                    {step === 0 ? `BAR ${bar + 1}` : step + 1}
                  </span>
                  <div className={`bl-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
                </div>
              );
            })}
          </div>
          {!hosted && rect && (() => {
            // One translucent, bordered rectangle over the selection —
            // geometry mirrors the flex columns (5px gaps) and the fixed
            // 12 × 11px (+1px gap) lane stack.
            const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rect);
            const gaps = (totalSteps - 1) * 5;
            const span = stepHi - stepLo + 1;
            return (
              <div
                className="bl-sel-rect"
                aria-hidden="true"
                style={{
                  left: `calc((100% - ${gaps}px) / ${totalSteps} * ${stepLo} + ${stepLo * 5}px)`,
                  width: `calc((100% - ${gaps}px) / ${totalSteps} * ${span} + ${(span - 1) * 5}px)`,
                  top: `${(NOTE_LANES - 1 - noteHi) * 12}px`,
                  height: `${(noteHi - noteLo + 1) * 12 - 1}px`,
                }}
              />
            );
          })()}
          {!hosted && rectSel && (() => {
            const { stepLo, stepHi } = rectNorm(rectSel);
            return (
              <SeqSelectionMenu
                visibleLo={stepLo}
                visibleHi={stepHi}
                totalSteps={totalSteps}
                onCut={() => pickUpSelection(true)}
                onCopy={() => pickUpSelection(false)}
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
