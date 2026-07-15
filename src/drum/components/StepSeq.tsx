import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { STEPS, patIdx } from '../seq';
import { useDrumStore } from '../store';

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
  const play = useDrumStore((s) => s.play);
  const stop = useDrumStore((s) => s.stop);
  const toggleStep = useDrumStore((s) => s.toggleStep);
  const setEditPattern = useDrumStore((s) => s.setEditPattern);
  const setSequenceLength = useDrumStore((s) => s.setSequenceLength);

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
            />
          </>
        )}
        <div className="dr-step-editing">
          <span>EDITING</span>
          <strong>{padName}</strong>
          <span>TAP STEP · ON → ACCENT → OFF</span>
        </div>
      </div>
      <div className="step-row">
        {Array.from({ length: STEPS }, (_, step) => {
          const value = patterns[patIdx(editPattern, sel, step)];
          const current = playing && curStep === step && curPat === editPattern;
          return (
            <button
              className={`step${value >= 1 ? ' on' : ''}${value === 2 ? ' accented' : ''}${current ? ' cur' : ''}`}
              type="button"
              aria-label={`Step ${step + 1}: ${value === 2 ? 'accent' : value === 1 ? 'on' : 'off'}`}
              aria-pressed={value >= 1}
              key={step}
              onClick={() => toggleStep(step)}
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
