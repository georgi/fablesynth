// SQ-4 top bar: logo, transport (pause = ctx.suspend — everything resumes
// in phase), launch quantize, beat dots + bar/BPM readout from the shared
// timebase, master scope and the swing/volume master knobs.

import { pad2 } from '../model';
import { useSeqStore } from '../store';
import { Scope } from './Scope';
import { SeqKnob } from './SeqKnob';

export function Header() {
  const playing = useSeqStore((s) => s.playing);
  const powered = useSeqStore((s) => s.powered);
  const beat = useSeqStore((s) => s.beat);
  const bar = useSeqStore((s) => s.bar);
  const bpm = useSeqStore((s) => s.session.bpm);
  const quant = useSeqStore((s) => s.quant);
  const swing = useSeqStore((s) => s.swing);
  const masterVol = useSeqStore((s) => s.masterVol);
  const { togglePlay, stopAll, cycleQuant, setSwing, setMasterVol } = useSeqStore.getState();

  return (
    <header className="sq-top">
      <div className="sq-logo">
        FABLE<span className="sq-logo-hot">SEQ</span>
        <span className="sq-logo-model">SQ-4</span>
      </div>

      <div className="sq-transport">
        <button
          className={`sq-play${playing ? ' on' : ''}`}
          onClick={togglePlay}
          disabled={!powered}
          title="Play / pause clock"
        >
          {playing ? '❚❚' : '▶'}
        </button>
        <button className="sq-stop" onClick={stopAll} disabled={!powered} title="Stop all clips">■</button>
      </div>

      <div className="sq-quant">
        <span className="sq-quant-tag">QUANT</span>
        <button className="sq-quant-step" onClick={() => cycleQuant(-1)}>◂</button>
        <span className="sq-quant-val">{quant}</span>
        <button className="sq-quant-step" onClick={() => cycleQuant(1)}>▸</button>
      </div>

      <div className="sq-clock">
        <div className="sq-beats">
          {[0, 1, 2, 3].map((i) => (
            <span key={i} className={`sq-beat${playing && beat === i ? ' on' : ''}`} />
          ))}
        </div>
        <div className="sq-clock-line">
          BAR <b>{pad2(bar)}</b> · <em>{bpm}</em> BPM
        </div>
      </div>

      <div className="sq-top-right">
        <Scope />
      </div>
      <div className="sq-master-knobs">
        <SeqKnob value={swing} onChange={setSwing} label="SWING" defaultValue={0} />
        <SeqKnob value={masterVol} onChange={setMasterVol} label="VOL" defaultValue={0.75} />
      </div>
    </header>
  );
}
