import { Knob } from '../Knob';
import { ScopeView } from '../displays/ScopeView';
import { SpectrumView } from '../displays/SpectrumView';
import { ACCENTS } from '../../constants';
import { FACTORY_PRESETS } from '../../presets';
import { engine, useStore } from '../../store';

export function TopBar() {
  const powered = useStore((s) => s.powered);
  const midiActive = useStore((s) => s.midiActive);
  const voiceCount = useStore((s) => s.voiceCount);
  const userPresets = useStore((s) => s.userPresets);
  const presetValue = useStore((s) => s.presetValue);
  const loadPresetByValue = useStore((s) => s.loadPresetByValue);
  const stepPreset = useStore((s) => s.stepPreset);
  const savePreset = useStore((s) => s.savePreset);

  const onSave = () => {
    const name = (window.prompt('Preset name:') || '').trim().toUpperCase();
    if (!name) return;
    savePreset(name);
  };

  return (
    <header className="top-bar">
      <div className="brand">FABLE<em>SYNTH</em><small>WT‑1</small></div>
      <div className="preset-bar">
        <button className="pb-btn" id="preset-prev" aria-label="previous preset" onClick={() => stepPreset(-1)}>◂</button>
        <select
          id="preset-select"
          aria-label="preset"
          value={presetValue}
          onChange={(e) => {
            loadPresetByValue(e.target.value);
            // Release focus so the computer keyboard plays notes instead of
            // navigating the select (which swallows key events while focused).
            e.currentTarget.blur();
          }}
        >
          <optgroup label="FACTORY">
            {FACTORY_PRESETS.map((p, i) => (
              <option key={'f' + i} value={'f' + i}>{p.name}</option>
            ))}
          </optgroup>
          {userPresets.length ? (
            <optgroup label="USER">
              {userPresets.map((p, i) => (
                <option key={'u' + i} value={'u' + i}>{p.name}</option>
              ))}
            </optgroup>
          ) : null}
        </select>
        <button className="pb-btn" id="preset-next" aria-label="next preset" onClick={() => stepPreset(1)}>▸</button>
        <button className="pb-btn pb-save" id="preset-save" onClick={onSave}>SAVE</button>
      </div>
      <div className="hud">
        <div className="hud-cell">
          {powered ? <ScopeView analyser={engine.scopeAnalyser} accent={ACCENTS.a} /> : <canvas />}
          <span>SCOPE</span>
        </div>
        <div className="hud-cell">
          {powered ? <SpectrumView analyser={engine.specAnalyser} accent={ACCENTS.b} /> : <canvas />}
          <span>SPECTRUM</span>
        </div>
      </div>
      <div className="top-clock" aria-label="Sequencer timing">
        <Knob paramId="seq.bpm" size="sm" accent="a" />
        <Knob paramId="seq.swing" size="sm" accent="a" />
      </div>
      <div className="status">
        <div className="status-row"><span className={`led${midiActive ? ' on' : ''}`} id="midi-led" />MIDI</div>
        <div className="status-row"><span id="voice-count">{voiceCount}</span>VOICES</div>
      </div>
      <div id="master-knob"><Knob paramId="master.volume" size="md" accent="n" /></div>
    </header>
  );
}
