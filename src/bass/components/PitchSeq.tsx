// The 16-step pitch sequencer: 12 note lanes per step (tap = set note,
// tap again = rest), per-step octave / accent / slide rows, pattern A–D
// select + chaining, RAND, and glowing slide connectors between tied steps.

import { getStep, NOTE_LANES, PATTERN_NAMES, slidesInto, STEPS } from '../seq';
import { useBassStore } from '../store';

const LANES_H = 143; // svg viewBox height for the connector overlay

export function PitchSeq() {
  const hosted = useBassStore((s) => s.hosted);
  const playing = useBassStore((s) => s.playing);
  const curStep = useBassStore((s) => s.curStep);
  const curPat = useBassStore((s) => s.curPat);
  const editPattern = useBassStore((s) => s.editPattern);
  const chain = useBassStore((s) => s.chain);
  const chaining = useBassStore((s) => s.chaining);
  const patterns = useBassStore((s) => s.patterns);
  const play = useBassStore((s) => s.play);
  const stop = useBassStore((s) => s.stop);
  const toggleCell = useBassStore((s) => s.toggleCell);
  const cycleStepOct = useBassStore((s) => s.cycleStepOct);
  const toggleStepAcc = useBassStore((s) => s.toggleStepAcc);
  const toggleStepSlide = useBassStore((s) => s.toggleStepSlide);
  const setChaining = useBassStore((s) => s.setChaining);
  const chainClick = useBassStore((s) => s.chainClick);
  const randomize = useBassStore((s) => s.randomize);

  const steps = Array.from({ length: STEPS }, (_, i) => getStep(patterns, editPattern, i));

  // slide connectors, drawn INTO a step from its predecessor
  const segs: Array<{ x1: number; y1: number; x2: number; y2: number }> = [];
  const yOf = (s: { note: number; oct: number }) =>
    (NOTE_LANES - 1 - s.note + 0.5) * (LANES_H / NOTE_LANES) - s.oct * 4;
  for (let i = 1; i < STEPS; i++) {
    if (slidesInto(steps[i - 1], steps[i])) {
      segs.push({
        x1: (i - 0.5) * 10, y1: yOf(steps[i - 1]),
        x2: (i + 0.5) * 10, y2: yOf(steps[i]),
      });
    }
  }

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
            <div className="bl-patterns" aria-label="Edit pattern">
              {PATTERN_NAMES.map((name, i) => (
                <button
                  className={`bl-seq-btn bl-pattern${editPattern === i ? ' active' : ''}`}
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
              className={`bl-seq-btn bl-chain-toggle${chaining ? ' active' : ''}`}
              type="button"
              aria-pressed={chaining}
              onClick={() => setChaining(!chaining)}
            >
              CHAIN {chain.map((i) => PATTERN_NAMES[i] ?? '?').join('→')}
            </button>
          </>
        )}
        <button className="bl-seq-btn" type="button" onClick={randomize}>RAND</button>
        <span className="bl-seq-hint">TAP LANE = NOTE · SLIDE TIES INTO STEP FROM PREV</span>
      </div>

      <div className="bl-seq-body">
        <div className="bl-seq-legend">
          <div className="bl-legend-lanes"><span>B</span><span>NOTE</span><span>C</span></div>
          <div className="bl-legend-oct">OCT</div>
          <div className="bl-legend-acc">ACC</div>
          <div className="bl-legend-sld">SLD</div>
        </div>

        <div className="bl-seq-grid">
          <div className="bl-seq-cols">
            {steps.map((s, i) => {
              const current = playing && curStep === i && curPat === editPattern;
              return (
                <div className="bl-seq-col" key={i}>
                  <div className="bl-seq-lanes">
                    {Array.from({ length: NOTE_LANES }, (_, r) => {
                      const note = NOTE_LANES - 1 - r;
                      const active = s.on && s.note === note;
                      return (
                        <button
                          key={r}
                          type="button"
                          className={
                            'bl-cell' +
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
                    className={`bl-oct-btn${s.oct !== 0 ? ' set' : ''}`}
                    aria-label={`step ${i + 1} octave`}
                    onClick={() => cycleStepOct(i)}
                  >
                    {s.oct === 0 ? '0' : s.oct > 0 ? '+1' : '−1'}
                  </button>
                  <button
                    type="button"
                    className={`bl-acc-btn${s.on && s.acc ? ' on' : ''}`}
                    aria-label={`step ${i + 1} accent`}
                    aria-pressed={s.on && s.acc}
                    onClick={() => toggleStepAcc(i)}
                  />
                  <button
                    type="button"
                    className={`bl-sld-btn${s.on && s.slide ? ' on' : ''}`}
                    aria-label={`step ${i + 1} slide`}
                    aria-pressed={s.on && s.slide}
                    onClick={() => toggleStepSlide(i)}
                  />
                  <span className="bl-step-num">{i + 1}</span>
                  <div className={`bl-step-cursor${current ? ' cur' : ''}`} aria-hidden="true" />
                </div>
              );
            })}
          </div>
          <svg
            className="bl-slide-overlay"
            viewBox={`0 0 160 ${LANES_H}`}
            preserveAspectRatio="none"
            aria-hidden="true"
          >
            {segs.map((seg, i) => (
              <line
                key={i}
                x1={seg.x1} y1={seg.y1} x2={seg.x2} y2={seg.y2}
                stroke="#4dff9e" strokeWidth="1.4" opacity="0.75"
                vectorEffect="non-scaling-stroke"
              />
            ))}
          </svg>
        </div>
      </div>
    </section>
  );
}
