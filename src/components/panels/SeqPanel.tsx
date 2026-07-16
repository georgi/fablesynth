// The WT-1 note sequencer: 12 note lanes per step (tap = set note, tap again
// = rest), draggable note lengths, octave / accent rows, bars 1–4 + sequence
// length, RAND, transport and ROOT controls.

import { getStep, NOTE_LANES, STEPS, type SeqStep } from '../../noteseq';
import { useStore } from '../../store';
import { NoteLengthHandle } from '../NoteLengthHandle';
import { SequenceLengthControl } from '../SequenceLengthControl';
import { Stepper } from '../Stepper';

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
            />
          </>
        )}
        <button className="ns-btn" type="button" onClick={randomizeSeq}>RAND</button>
        <span className="ns-hint">TAP = NOTE · DRAG RIGHT EDGE = LENGTH</span>
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
              return (
                <div className={`ns-col${step === 0 && bar > 0 ? ' bar-start' : ''}`} key={absoluteStep}>
                  <div className="ns-lanes">
                    {Array.from({ length: NOTE_LANES }, (_, r) => {
                      const note = NOTE_LANES - 1 - r;
                      const voice = voices.find((candidate) => candidate.on && candidate.note === note);
                      const active = !!voice;
                      return (
                        <div className="ns-cell-wrap" key={r}>
                          <button
                            type="button"
                            className={'ns-cell' + (note === 0 ? ' root' : '') + (active ? ' on' : '')}
                            aria-label={`bar ${bar + 1}, step ${step + 1}, note ${note}`}
                            aria-pressed={active}
                            onClick={() => onToggleChordNote ? onToggleChordNote(absoluteStep, note) : toggleCell(step, note, pattern)}
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
                  <span className="ns-step-num">{step === 0 ? `BAR ${bar + 1}` : step + 1}</span>
                  <div className={`ns-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
                </div>
              );
            })}
          </div>
          </div>
        </div>

        <div className="ns-clock">
          <Stepper paramId="seq.root" label="ROOT" accent="a" />
        </div>
      </div>
    </section>
  );
}
