import { PATTERN_NAMES, STEPS, patIdx } from '../seq';
import { useDrumStore } from '../store';

export function StepSeq() {
  const hosted = useDrumStore((s) => s.hosted);
  const playing = useDrumStore((s) => s.playing);
  const curStep = useDrumStore((s) => s.curStep);
  const curPat = useDrumStore((s) => s.curPat);
  const editPattern = useDrumStore((s) => s.editPattern);
  const chain = useDrumStore((s) => s.chain);
  const chaining = useDrumStore((s) => s.chaining);
  const patterns = useDrumStore((s) => s.patterns);
  const sel = useDrumStore((s) => s.sel);
  const padName = useDrumStore((s) => s.padNames[s.sel]);
  const play = useDrumStore((s) => s.play);
  const stop = useDrumStore((s) => s.stop);
  const toggleStep = useDrumStore((s) => s.toggleStep);
  const setChaining = useDrumStore((s) => s.setChaining);
  const chainClick = useDrumStore((s) => s.chainClick);

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
            <div className="dr-patterns" aria-label="Edit pattern">
              {PATTERN_NAMES.map((name, i) => (
                <button
                  className={`dr-seq-btn dr-pattern${editPattern === i ? ' active' : ''}`}
                  type="button"
                  aria-label={`Pattern ${name}`}
                  aria-pressed={editPattern === i}
                  key={name}
                  onClick={() => chainClick(i)}
                >
                  {name}
                </button>
              ))}
            </div>
            <button
              className={`dr-seq-btn dr-chain-toggle${chaining ? ' active' : ''}`}
              type="button"
              aria-pressed={chaining}
              onClick={() => setChaining(!chaining)}
            >
              CHAIN
            </button>
            <span className="dr-chain-readout">CHAIN {chain.map((i) => PATTERN_NAMES[i] ?? '?').join('→')}</span>
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
