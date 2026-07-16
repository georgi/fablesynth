// The 16-step pitch sequencer: 12 note lanes per step (tap = set note,
// tap again = rest), draggable note lengths, octave / accent / slide rows,
// bars 1–4 and sequence length.

import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { NoteLengthHandle } from '../../components/NoteLengthHandle';
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
            />
          </>
        )}
        <button className="bl-seq-btn" type="button" onClick={randomize}>RAND</button>
        <span className="bl-seq-hint">DRAG EDGE = LENGTH · SLD = LEGATO</span>
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
                <div className={`bl-seq-col${step === 0 && bar > 0 ? ' bar-start' : ''}`} key={absoluteStep}>
                  <div className="bl-seq-lanes">
                    {Array.from({ length: NOTE_LANES }, (_, r) => {
                      const note = NOTE_LANES - 1 - r;
                      const active = s.on && s.note === note;
                      return (
                        <div className="bl-cell-wrap" key={r}>
                          <button
                            type="button"
                            className={'bl-cell' + (note === 0 ? ' root' : '') + (active ? ' on' : '')}
                            aria-label={`bar ${bar + 1}, step ${step + 1}, note ${note}`}
                            aria-pressed={active}
                            onClick={() => toggleCell(step, note, pattern)}
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
                  <span className="bl-step-num">{step === 0 ? `BAR ${bar + 1}` : step + 1}</span>
                  <div className={`bl-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
                </div>
              );
            })}
          </div>
        </div>
        </div>
      </div>
    </section>
  );
}
