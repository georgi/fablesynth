import { useState } from 'react';
import { arpNotes, noteName, type ArpOrder, type ArpState } from '../../arp';
import './arp.css';

export function ArpModeSwitch({ mode, setMode }: { mode: boolean; setMode: (on: boolean) => void }) {
  return <div className="arp-mode" aria-label="Sequencer mode">
    <button type="button" aria-pressed={!mode} onClick={() => setMode(false)}>SEQ</button>
    <button type="button" aria-pressed={mode} onClick={() => setMode(true)}>ARP</button>
  </div>;
}

export interface ArpPanelProps {
  a: ArpState; keys: number[]; update: (patch: Partial<ArpState>) => void;
  playing: boolean; current: number; play: () => void; stop: () => void; clear: () => void;
  bpm: number; swing: number; setParam: (id: string, value: number) => void;
  setMode: (on: boolean) => void; bass?: boolean;
  hosted?: boolean; queued?: boolean;
}
export function ArpPanel({ a, keys, update, playing, current, play, stop, clear, bpm, swing, setParam, setMode, bass = false, hosted = false, queued = false }: ArpPanelProps) {
  const swingParam = bass ? 'master.swing' : 'seq.swing';
  const [editing, setEditing] = useState(false);
  const [octave, setOctave] = useState(bass ? 2 : 3);
  const notes = a.input === 'stored' ? a.notes : keys;
  const preview = arpNotes(a, notes);
  const low = Math.min(...preview.filter(n => n >= 0), bass ? 36 : 48);
  const high = Math.max(...preview, low + 12);
  const sounding = playing && current >= 0 && a.hits[current] ? preview[current] : -1;
  const toggleNote = (n: number) => update({ notes: a.notes.includes(n) ? a.notes.filter(k => k !== n) : [...a.notes, n] });
  return <section className={`panel ns-section arp-panel${bass ? ' arp-bass' : ''}`} style={{ gridArea: 'seq' }} data-accent="a">
    <div className="panel-head ns-head">
      <button className={`pb-btn ns-transport${playing ? ' active' : ''}`} type="button" aria-label={hosted ? playing || queued ? 'Stop arp clip' : 'Launch arp clip' : playing ? 'Stop arpeggiator' : 'Play arpeggiator'} aria-pressed={playing} onClick={playing || queued ? stop : play}>{playing || queued ? '■' : '▶'}</button>
      <h2>{bass ? 'PITCH SEQ' : 'NOTE SEQ'}</h2><ArpModeSwitch mode setMode={setMode} />
      <span className="arp-status">{queued ? 'QUEUED' : !notes.length ? 'WAITING FOR NOTES' : playing ? 'RUNNING' : 'READY'}<span className={playing ? 'arp-led on' : 'arp-led'} /></span>
      {hosted ? <span className="arp-tempo">SQ-4 CLOCK · {bpm} BPM</span> : <label className="arp-tempo">BPM <input aria-label="Arpeggiator tempo" type="number" min="60" max="200" value={bpm} onChange={e => setParam('seq.bpm', Math.max(60, Math.min(200, +e.target.value || 120)))} /></label>}
    </div>
    <div className="arp-source">
      {hosted ? <span className="arp-field">CLIP NOTES</span> : <label className="arp-field">INPUT<select aria-label="Arpeggiator input" value={a.input} onChange={e => update({ input: e.target.value as 'stored' | 'keys' })}><option value="stored">STORED NOTES</option><option value="keys">PLAYED KEYS</option></select></label>}
      <div className="arp-note-pool" aria-label="Arpeggiator notes">
        {notes.map(n => <span key={n} className={`arp-note${sounding === n ? ' sounding' : ''}`}>{noteName(n)}</span>)}
        {!notes.length && <span className="arp-empty">{a.input === 'keys' ? 'Play a chord on MIDI or the keyboard below' : 'Add notes to begin'}</span>}
      </div>
      {a.input === 'stored' ? <button className="ns-btn" aria-expanded={editing} onClick={() => setEditing(!editing)}>{editing ? 'DONE' : 'EDIT NOTES'}</button> : <>
        <button className="ns-btn" aria-pressed={a.latch} onClick={() => update({ latch: !a.latch })}>LATCH</button>
        <button className="ns-btn" disabled={!keys.length} onClick={() => update({ notes: [...keys], input: 'stored' })}>STORE NOTES</button>
        <button className="ns-btn" onClick={clear}>CLEAR</button>
      </>}
    </div>
    {editing && a.input === 'stored' && <div className="arp-editor">
      <div className="arp-octave"><button aria-label="Lower keyboard octave" disabled={octave <= 1} onClick={() => setOctave(o => o - 1)}>−</button><span>OCT {octave}</span><button aria-label="Raise keyboard octave" disabled={octave >= 6} onClick={() => setOctave(o => o + 1)}>+</button></div>
      <div className="arp-keys">{Array.from({ length: 12 }, (_, i) => {
        const n = (octave + 1) * 12 + i;
        return <button key={i} type="button" className={[1, 3, 6, 8, 10].includes(i) ? 'sharp' : ''} aria-label={`Include ${noteName(n)}`} aria-pressed={a.notes.includes(n)} onClick={() => toggleNote(n)}>{noteName(n)}</button>;
      })}</div>
      <button className="ns-btn" onClick={() => update({ notes: [] })}>CLEAR</button>
    </div>}
    <div className="arp-controls">
      <label className="arp-field">ORDER<select aria-label="Arpeggiator order" value={a.order} onChange={e => update({ order: e.target.value as ArpOrder })}>
        <option value="up">↑ UP</option><option value="down">↓ DOWN</option><option value="updown">↕ UP / DOWN</option><option value="played">AS PLAYED</option><option value="random">RANDOM</option>
      </select></label>
      <label className="arp-field">RATE<select aria-label="Arpeggiator rate" value={a.rate} onChange={e => update({ rate: +e.target.value })}>
        <option value={1}>1/4</option><option value={.75}>1/8 DOTTED</option><option value={.5}>1/8</option><option value={1/3}>1/8 TRIPLET</option><option value={.375}>1/16 DOTTED</option><option value={.25}>1/16</option><option value={1/6}>1/16 TRIPLET</option><option value={.125}>1/32</option>
      </select></label>
      <label className="arp-field">OCTAVES<select aria-label="Arpeggiator octaves" value={a.octaves} onChange={e => update({ octaves: +e.target.value })}><option>1</option><option>2</option><option>3</option></select></label>
      <label className="arp-field arp-slider">GATE <output>{Math.round(a.gate * 100)}%</output><input aria-label="Arpeggiator gate" type="range" min="5" max="95" value={Math.round(a.gate * 100)} onDoubleClick={() => update({ gate: .65 })} onChange={e => update({ gate: +e.target.value / 100 })} /></label>
      <label className="arp-field arp-slider">{hosted ? 'GLOBAL SWING' : 'SWING'} <output>{Math.round(swing * 100)}%</output><input aria-label="Arpeggiator swing" type="range" min="0" max="100" value={Math.round(swing * 100)} onDoubleClick={() => setParam(swingParam, 0)} onChange={e => setParam(swingParam, +e.target.value / 100)} /></label>
      {a.order === 'random' && <button className="ns-btn" onClick={() => update({ seed: a.seed + 1 })}>NEW SEED</button>}
    </div>
    <div className="arp-display" aria-label="Generated phrase preview">
      <div className="arp-register"><span>{noteName(high)}</span><span>{noteName(low)}</span></div>
      <div className="arp-phrase">{preview.map((n, i) => <div key={i} className={`arp-column${playing && current === i ? ' current' : ''}${i % 4 === 0 ? ' beat' : ''}`}>
        {n >= 0 && <div className={`arp-generated${a.hits[i] ? '' : ' rest'}${a.accents[i] ? ' accented' : ''}${bass && a.slides?.[i] && a.hits[i] && a.hits[(i + 15) % 16] ? ' slide' : ''}`} style={{ bottom: `${8 + (n - low) / (high - low) * 75}%`, width: `${Math.max(12, a.gate * 90)}%` }}><span>{noteName(n)}</span></div>}
      </div>)}</div>
    </div>
    <div className="arp-rhythm">{(['hits', 'accents'] as const).map((lane, row) => <div className="arp-lane" key={lane}>
      <span>{row ? 'ACC' : 'HIT'}</span>{a[lane].map((on, i) => <button key={i} type="button" aria-label={`${row ? 'Accent' : 'Hit'} step ${i + 1}`} aria-pressed={on} className={`${on ? 'on' : ''}${row ? ' accent' : ''}${i % 4 === 0 ? ' beat' : ''}`} onClick={() => update({ [lane]: a[lane].map((v, j) => j === i ? !v : v) })}>{on ? '●' : '·'}</button>)}
    </div>)}{bass && a.slides && <div className="arp-lane"><span>SLD</span>{a.slides.map((on, i) => <button key={i} type="button" aria-label={`Slide into step ${i + 1}`} title="Slide from the previous note; rests break slides" aria-pressed={on} className={on ? 'on' : ''} onClick={() => update({ slides: a.slides!.map((v, j) => j === i ? !v : v) })}>{on ? '↗' : '·'}</button>)}</div>}<div className="arp-lane arp-numbers"><span />{preview.map((_, i) => <span key={i} className={playing && current === i ? 'current' : ''}>{String(i + 1).padStart(2, '0')}</span>)}</div></div>
    <div className="arp-footer"><span>16 STEP LOOP · {hosted ? 'SAVED WITH THIS CLIP' : a.input === 'keys' && a.latch ? 'LATCHED INPUT' : 'AUDIO CLOCK'}</span><span>{bass ? 'SLD = GLIDE INTO NOTE · RESTS BREAK SLIDES' : 'HIT = REST / NOTE · ACC = EMPHASIS'}</span></div>
  </section>;
}
