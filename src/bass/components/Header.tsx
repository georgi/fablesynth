import { ScopeView } from '../../components/displays/ScopeView';
import { patchOptions } from '../patches';
import { bassEngine, useBassStore } from '../store';
import { BassKnob } from './BassKnob';
import { BassStepper } from './BassStepper';

export function Header() {
  const powered = useBassStore((s) => s.powered);
  const midiActive = useBassStore((s) => s.midiActive);
  const patchValue = useBassStore((s) => s.patchValue);
  const userPatches = useBassStore((s) => s.userPatches);
  const stepPatch = useBassStore((s) => s.stepPatch);
  const savePatch = useBassStore((s) => s.savePatch);

  const currentPatch = patchOptions(userPatches).find((option) => option.value === patchValue)?.name ?? 'UNTITLED';
  const onSave = () => {
    const name = (window.prompt('Patch name') || '').trim().toUpperCase();
    if (!name) return;
    savePatch(name);
  };

  return (
    <header className="top-bar bl-header">
      <div className="brand">FABLE<em>SYNTH</em><small>BL-1</small></div>
      <div className="preset-bar bl-patchbar" aria-label="patch selection">
        <span className="bl-patch-label">PATCH</span>
        <button className="pb-btn" aria-label="previous patch" onClick={() => stepPatch(-1)}>◂</button>
        <span className="bl-patchname">{currentPatch}</span>
        <button className="pb-btn" aria-label="next patch" onClick={() => stepPatch(1)}>▸</button>
        <button className="pb-btn pb-save" onClick={onSave}>SAVE</button>
      </div>
      <div className="bl-voicemode">MONO · LAST-NOTE<br />SLIDES RIDE THE ENVS</div>
      <div className="hud bl-hud">
        <div className="hud-cell">
          {powered ? <ScopeView analyser={bassEngine.scopeAnalyser} accent="#4dff9e" /> : <canvas />}
          <span>SCOPE</span>
        </div>
      </div>
      <div className="status bl-midi">
        <div className="status-row"><span className={`led${midiActive ? ' on' : ''}`} />MIDI</div>
        <div className="status-row bl-bpm"><BassStepper paramId="seq.bpm" accent="a" />BPM</div>
      </div>
      <div className="bl-master">
        <BassKnob paramId="master.swing" size="md" accent="n" />
        <BassKnob paramId="master.volume" size="md" accent="n" />
      </div>
    </header>
  );
}
