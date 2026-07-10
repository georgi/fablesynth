import { ScopeView } from '../../components/displays/ScopeView';
import { DrumKnob } from './DrumKnob';
import { DrumStepper } from './DrumStepper';
import { drumEngine, kitOptions, useDrumStore } from '../store';

export function Header() {
  const powered = useDrumStore((s) => s.powered);
  const midiActive = useDrumStore((s) => s.midiActive);
  const kitValue = useDrumStore((s) => s.kitValue);
  const userKits = useDrumStore((s) => s.userKits);
  const stepKit = useDrumStore((s) => s.stepKit);
  const saveKit = useDrumStore((s) => s.saveKit);
  const mode = useDrumStore((s) => s.mode);
  const setMode = useDrumStore((s) => s.setMode);

  const currentKit = kitOptions(userKits).find((option) => option.value === kitValue)?.name ?? 'UNTITLED';
  const onSave = () => {
    const name = (window.prompt('Kit name') || '').trim().toUpperCase();
    if (!name) return;
    saveKit(name);
  };

  return (
    <header className="top-bar dr-header">
      <div className="brand">FABLE<em>SYNTH</em><small>DR-1</small></div>
      <div className="preset-bar dr-kitbar" aria-label="kit selection">
        <button className="pb-btn" aria-label="previous kit" onClick={() => stepKit(-1)}>◂</button>
        <span className="dr-kitname">{currentKit}</span>
        <button className="pb-btn" aria-label="next kit" onClick={() => stepKit(1)}>▸</button>
        <button className="pb-btn pb-save" onClick={onSave}>SAVE</button>
      </div>
      <div className="dr-mode" aria-label="performance mode">
        <button className={`pb-btn${mode === 'step' ? ' active' : ''}`} aria-pressed={mode === 'step'} onClick={() => setMode('step')}>STEP</button>
        <button className={`pb-btn${mode === 'pads' ? ' active' : ''}`} aria-pressed={mode === 'pads'} onClick={() => setMode('pads')}>PADS</button>
      </div>
      <div className="hud dr-hud">
        <div className="hud-cell">
          {powered ? <ScopeView analyser={drumEngine.scopeAnalyser} accent="#4de8ff" /> : <canvas />}
          <span>SCOPE</span>
        </div>
      </div>
      <div className="status dr-midi">
        <div className="status-row"><span className={`led${midiActive ? ' on' : ''}`} />MIDI</div>
      </div>
      <div className="dr-tempo">
        <DrumStepper paramId="seq.bpm" accent="a" />
        <span>SYNC</span>
      </div>
      <div className="dr-master">
        <DrumKnob paramId="master.swing" size="md" accent="n" />
        <DrumKnob paramId="master.volume" size="md" accent="n" />
      </div>
    </header>
  );
}
