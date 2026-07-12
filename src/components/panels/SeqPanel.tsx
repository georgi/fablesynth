// The WT-1 note sequencer: 12 note lanes per step (tap = set note, tap again
// = rest), per-step octave / accent / tie rows, pattern A–D select + chaining,
// RAND, transport and BPM / SWING / GATE / ROOT clock controls. Ties retune
// the sounding voice legato — the GLIDE knob decides snap vs slide.

import { getStep, NOTE_LANES, PATTERN_NAMES, STEPS, tiesInto } from '../../noteseq';
import { useStore } from '../../store';
import { Knob } from '../Knob';
import { Stepper } from '../Stepper';

const LANES_H = 143; // svg viewBox height for the connector overlay

export function SeqPanel() {
  const hosted = useStore((s) => s.hosted);
  const seqPlaying = useStore((s) => s.seqPlaying);
  const curStep = useStore((s) => s.curStep);
  const curPat = useStore((s) => s.curPat);
  const editPattern = useStore((s) => s.editPattern);
  const chain = useStore((s) => s.chain);
  const chaining = useStore((s) => s.chaining);
  const patterns = useStore((s) => s.patterns);
  const seqPlay = useStore((s) => s.seqPlay);
  const seqStop = useStore((s) => s.seqStop);
  const toggleCell = useStore((s) => s.toggleCell);
  const cycleStepOct = useStore((s) => s.cycleStepOct);
  const toggleStepAcc = useStore((s) => s.toggleStepAcc);
  const toggleStepTie = useStore((s) => s.toggleStepTie);
  const setChaining = useStore((s) => s.setChaining);
  const chainClick = useStore((s) => s.chainClick);
  const randomizeSeq = useStore((s) => s.randomizeSeq);

  const steps = Array.from({ length: STEPS }, (_, i) => getStep(patterns, editPattern, i));

  // tie connectors, drawn INTO a step from its predecessor
  const segs: Array<{ x1: number; y1: number; x2: number; y2: number }> = [];
  const yOf = (s: { note: number; oct: number }) =>
    (NOTE_LANES - 1 - s.note + 0.5) * (LANES_H / NOTE_LANES) - s.oct * 4;
  for (let i = 1; i < STEPS; i++) {
    if (tiesInto(steps[i - 1], steps[i])) {
      segs.push({
        x1: (i - 0.5) * 10, y1: yOf(steps[i - 1]),
        x2: (i + 0.5) * 10, y2: yOf(steps[i]),
      });
    }
  }

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
            <div className="ns-patterns" aria-label="Edit pattern">
              {PATTERN_NAMES.map((name, i) => (
                <button
                  className={`ns-btn ns-pattern${editPattern === i ? ' active' : ''}`}
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
              className={`ns-btn ns-chain-toggle${chaining ? ' active' : ''}`}
              type="button"
              aria-pressed={chaining}
              onClick={() => setChaining(!chaining)}
            >
              CHAIN {chain.map((i) => PATTERN_NAMES[i] ?? '?').join('→')}
            </button>
          </>
        )}
        <button className="ns-btn" type="button" onClick={randomizeSeq}>RAND</button>
        <span className="ns-hint">TAP LANE = NOTE · TIE HOLDS FROM PREV STEP — GLIDE MAKES IT SLIDE</span>
      </div>

      <div className="ns-body">
        <div className="ns-legend">
          <div className="ns-legend-lanes"><span>B</span><span>NOTE</span><span>C</span></div>
          <div className="ns-legend-oct">OCT</div>
          <div className="ns-legend-acc">ACC</div>
          <div className="ns-legend-tie">TIE</div>
        </div>

        <div className="ns-grid">
          <div className="ns-cols">
            {steps.map((s, i) => {
              const current = seqPlaying && curStep === i && curPat === editPattern;
              return (
                <div className="ns-col" key={i}>
                  <div className="ns-lanes">
                    {Array.from({ length: NOTE_LANES }, (_, r) => {
                      const note = NOTE_LANES - 1 - r;
                      const active = s.on && s.note === note;
                      return (
                        <button
                          key={r}
                          type="button"
                          className={
                            'ns-cell' +
                            (note === 0 ? ' root' : '') +
                            (active ? (s.acc ? ' on acc' : ' on') : '')
                          }
                          aria-label={`step ${i + 1} note ${note}`}
                          aria-pressed={active}
                          onClick={() => toggleCell(i, note)}
                        />
                      );
                    })}
                  </div>
                  <button
                    type="button"
                    className={`ns-oct-btn${s.oct !== 0 ? ' set' : ''}`}
                    aria-label={`step ${i + 1} octave`}
                    onClick={() => cycleStepOct(i)}
                  >
                    {s.oct === 0 ? '0' : s.oct > 0 ? '+1' : '−1'}
                  </button>
                  <button
                    type="button"
                    className={`ns-acc-btn${s.on && s.acc ? ' on' : ''}`}
                    aria-label={`step ${i + 1} accent`}
                    aria-pressed={s.on && s.acc}
                    onClick={() => toggleStepAcc(i)}
                  />
                  <button
                    type="button"
                    className={`ns-tie-btn${s.on && s.tie ? ' on' : ''}`}
                    aria-label={`step ${i + 1} tie`}
                    aria-pressed={s.on && s.tie}
                    onClick={() => toggleStepTie(i)}
                  />
                  <span className="ns-step-num">{i + 1}</span>
                  <div className={`ns-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
                </div>
              );
            })}
          </div>
          <svg
            className="ns-tie-overlay"
            viewBox={`0 0 160 ${LANES_H}`}
            preserveAspectRatio="none"
            aria-hidden="true"
          >
            {segs.map((seg, i) => (
              <line
                key={i}
                x1={seg.x1} y1={seg.y1} x2={seg.x2} y2={seg.y2}
                stroke="#4de8ff" strokeWidth="1.4" opacity="0.75"
                vectorEffect="non-scaling-stroke"
              />
            ))}
          </svg>
        </div>

        <div className="ns-clock">
          <Knob paramId="seq.bpm" size="sm" accent="a" />
          <Knob paramId="seq.swing" size="sm" accent="a" />
          <Knob paramId="seq.gate" size="sm" accent="a" />
          <Stepper paramId="seq.root" label="ROOT" accent="a" />
        </div>
      </div>
    </section>
  );
}
