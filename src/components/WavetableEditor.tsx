// User-wavetable editor — a modal with two creation modes:
//   AUDIO: load a file, slice into 2048-sample frames (single-cycle, auto-detect
//          pitch, or a fixed cycle length) and band-limit each frame.
//   DRAW:  sketch one cycle on a canvas; it is band-limited on commit.
// Both produce a UserTable that is added to the registry (store + localStorage)
// and assigned to the osc that opened the editor.

import { useEffect, useRef, useState } from 'react';
import { useStore, engine, factoryTables } from '../store';
import { setupCanvas } from './displays/canvas';
import {
  makeUserTable, mixToMono, detectCycleLength, sliceToFrames, singleCycleFrame,
  frameFromDrawing, MAX_FRAMES, type UserTable,
} from '../engine/usertables';
import { ACCENTS } from '../constants';
import { TABLE_NAMES } from '../params';
import { TableLibrary } from './wavetable/TableLibrary';
import { DRAW_N, seedShape, snapValue, smoothAround, type Seed, type Brush } from './wavetable/drawmodel';

type Tab = 'audio' | 'draw';
type AudioMode = 'single' | 'auto' | 'fixed';

function decodeFile(file: File): Promise<AudioBuffer> {
  const Ctor = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
  const ctx = engine.ctx || new Ctor();
  return file.arrayBuffer().then((buf) => ctx.decodeAudioData(buf));
}

export function WavetableEditor() {
  const editorOsc = useStore((s) => s.editorOsc);
  const userTables = useStore((s) => s.userTables);
  const addUserTable = useStore((s) => s.addUserTable);
  const deleteUserTable = useStore((s) => s.deleteUserTable);
  const closeEditor = useStore((s) => s.closeEditor);
  const renameUserTable = useStore((s) => s.renameUserTable);
  const updateUserTable = useStore((s) => s.updateUserTable);
  const duplicateUserTable = useStore((s) => s.duplicateUserTable);
  const duplicateFactoryTable = useStore((s) => s.duplicateFactoryTable);
  const setParam = useStore((s) => s.setParam);

  const [tab, setTab] = useState<Tab>('draw');
  const [name, setName] = useState('');
  const [mode, setMode] = useState<AudioMode>('single');
  const [fixedLen, setFixedLen] = useState(2048);
  const [audio, setAudio] = useState<{ samples: Float32Array; sr: number; label: string } | null>(null);
  const [status, setStatus] = useState('');
  // working table: which library entry is loaded/assigned. 'u<i>' is editable
  // (1-frame only); 'f<i>' and multi-frame user tables load read-only.
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [brush, setBrush] = useState<Brush>('pen');
  const [snap, setSnap] = useState(false);

  const drawRef = useRef<HTMLCanvasElement>(null);
  const pointsRef = useRef<number[]>(new Array(DRAW_N).fill(0));
  const drawingRef = useRef(false);
  const lastIdxRef = useRef(-1);
  const [drawVersion, setDrawVersion] = useState(0); // force canvas repaint
  const [readOnly, setReadOnly] = useState(false); // pad locked (multi-frame/factory)

  const accentColor = ACCENTS[editorOsc === 'oscB' ? 'b' : 'a'];

  // Reset transient state each time the editor opens.
  useEffect(() => {
    if (editorOsc) {
      setTab('draw'); setName(''); setMode('single'); setAudio(null); setStatus('');
      setSelectedId(null); setReadOnly(false); setBrush('pen'); setSnap(false);
      pointsRef.current = new Array(DRAW_N).fill(0);
      setDrawVersion((v) => v + 1);
    }
  }, [editorOsc]);

  // Repaint the draw canvas whenever its tab is shown or the curve changes.
  useEffect(() => {
    if (tab !== 'draw') return;
    const canvas = drawRef.current;
    if (!canvas) return;
    const { ctx, w, h } = setupCanvas(canvas);
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2); ctx.stroke();
    const pts = pointsRef.current;
    ctx.beginPath();
    for (let i = 0; i < DRAW_N; i++) {
      const x = (i / (DRAW_N - 1)) * w;
      const y = h / 2 - pts[i] * (h / 2 - 4);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = editorOsc ? ACCENTS[editorOsc === 'oscA' ? 'a' : 'b'] : ACCENTS.a;
    ctx.lineWidth = 1.5;
    ctx.shadowColor = ctx.strokeStyle as string;
    ctx.shadowBlur = 6;
    ctx.stroke();
  }, [tab, drawVersion, editorOsc]);

  if (!editorOsc) return null;

  const onFile = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files && e.target.files[0];
    if (!file) return;
    setStatus('decoding…');
    try {
      const buf = await decodeFile(file);
      const samples = mixToMono(buf);
      setAudio({ samples, sr: buf.sampleRate, label: file.name });
      if (!name) setName(file.name.replace(/\.[^.]+$/, '').slice(0, 14).toUpperCase());
      setStatus(`${(buf.duration).toFixed(2)} s · ${buf.sampleRate | 0} Hz · ${samples.length} samples`);
    } catch (err) {
      setStatus('decode failed: ' + (err as Error).message);
    }
  };

  const finalName = (name.trim().toUpperCase() || 'USER').slice(0, 14);

  const commit = (u: UserTable) => { addUserTable(u); closeEditor(); };

  const createFromAudio = () => {
    if (!audio) return;
    let frames: Float32Array[];
    if (mode === 'single') {
      frames = singleCycleFrame(audio.samples);
    } else if (mode === 'auto') {
      const cyc = detectCycleLength(audio.samples, audio.sr);
      frames = sliceToFrames(audio.samples, cyc);
    } else {
      frames = sliceToFrames(audio.samples, Math.max(2, fixedLen | 0));
    }
    commit(makeUserTable(finalName, frames));
  };

  const createFromDraw = () => {
    const frame = frameFromDrawing(pointsRef.current);
    const u = makeUserTable(finalName, [frame]);
    if (selectedId && selectedId[0] === 'u' && !readOnly) {
      const idx = +selectedId.slice(1);
      // True in-place update: keep the pool index (and thus row selection + any
      // osc references) stable. Stay open so the user can keep refining.
      updateUserTable(idx, u);
      assign(TABLE_NAMES.length + idx);
    } else {
      commit(u);
    }
  };

  const assign = (combinedIndex: number) => {
    if (editorOsc) setParam(`${editorOsc}.table`, combinedIndex);
  };

  const loadPoints = (wave: Float32Array, frames: number) => {
    // populate the pad from frame 0 (downsample SIZE -> DRAW_N)
    const step = wave.length / frames / DRAW_N;
    const pts = new Array(DRAW_N).fill(0);
    for (let i = 0; i < DRAW_N; i++) pts[i] = wave[Math.floor(i * step)] || 0;
    pointsRef.current = pts;
    setDrawVersion((v) => v + 1);
  };

  const selectFactory = (i: number) => {
    const ft = factoryTables()[i];
    setSelectedId('f' + i);
    setName(ft.name);
    setReadOnly(true);
    setTab('draw');
    loadPoints(ft.data.slice(0, ft.size), 1); // show frame 0 (read-only)
    assign(i); // factory tables occupy [0, TABLE_NAMES.length)
  };

  const selectUser = (i: number) => {
    const ut = userTables[i];
    setSelectedId('u' + i);
    setName(ut.name);
    // User tables are always editable. Frame 0 loads into the single-cycle pad;
    // drawing + UPDATE collapses a multi-frame table to one cycle (the hint
    // warns "→ 1 frame"). Selecting just to assign loses nothing.
    setReadOnly(false);
    setTab('draw');
    loadPoints(ut.wave, ut.frames);
    assign(TABLE_NAMES.length + i);
  };

  // ----- draw canvas pointer handling -----
  const paint = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (readOnly) return;
    const canvas = drawRef.current;
    if (!canvas) return;
    const r = canvas.getBoundingClientRect();
    const idx = Math.max(0, Math.min(DRAW_N - 1, Math.round(((e.clientX - r.left) / r.width) * (DRAW_N - 1))));
    let val = Math.max(-1, Math.min(1, 1 - 2 * ((e.clientY - r.top) / r.height)));
    val = snapValue(val, snap);
    const pts = pointsRef.current;
    const last = lastIdxRef.current;
    if (last >= 0 && last !== idx) {
      const lo = Math.min(last, idx), hi = Math.max(last, idx);
      const v0 = pts[last];
      for (let i = lo; i <= hi; i++) {
        const t = hi === lo ? 1 : (i - last) / (idx - last);
        pts[i] = v0 + (val - v0) * t;
      }
    } else {
      pts[idx] = val;
    }
    if (brush === 'smooth') smoothAround(pts, idx);
    lastIdxRef.current = idx;
    setDrawVersion((v) => v + 1);
  };

  const applySeed = (kind: Seed) => {
    if (readOnly) return;
    pointsRef.current = seedShape(kind);
    setSelectedId(null); setReadOnly(false);
    setDrawVersion((v) => v + 1);
  };

  // + NEW: create a fresh single-cycle (sine) user table, append it to the USER
  // section, assign it to this osc, and select it for editing.
  const newTable = () => {
    const pts = seedShape('sine');
    const u = makeUserTable('USER', [frameFromDrawing(pts)]);
    const idx = userTables.length; // addUserTable appends at this pool index
    addUserTable(u);               // persists + re-pushes engine + assigns to osc
    pointsRef.current = pts;
    setSelectedId('u' + idx);
    setName('USER');
    setReadOnly(false);
    setTab('draw');
    setDrawVersion((v) => v + 1);
  };
  const onDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    drawingRef.current = true; lastIdxRef.current = -1;
    e.currentTarget.setPointerCapture(e.pointerId);
    paint(e);
  };
  const onMove = (e: React.PointerEvent<HTMLCanvasElement>) => { if (drawingRef.current) paint(e); };
  const onUp = () => { drawingRef.current = false; lastIdxRef.current = -1; };

  return (
    <div className="wte-backdrop" onPointerDown={(e) => { if (e.target === e.currentTarget) closeEditor(); }}>
      <div className="wte" data-accent={editorOsc === 'oscA' ? 'a' : 'b'}>
        <div className="wte-scan" />
        <div className="wte-head">
          <h2>WAVETABLE → {editorOsc === 'oscA' ? 'OSC A' : 'OSC B'}</h2>
          <button className="wte-x" aria-label="close" onClick={closeEditor}>✕</button>
        </div>
        <div className="wte-cols">
          <TableLibrary
            userTables={userTables}
            selectedId={selectedId}
            accent={accentColor}
            onSelectFactory={selectFactory}
            onSelectUser={selectUser}
            onNew={newTable}
            onRename={renameUserTable}
            onDuplicateUser={duplicateUserTable}
            onDuplicateFactory={duplicateFactoryTable}
            onDelete={deleteUserTable}
          />
          <section className="wte-editor">
            <div className="wte-tabs">
              <button className={tab === 'draw' ? 'on' : ''} onClick={() => setTab('draw')}>DRAW</button>
              <button className={tab === 'audio' ? 'on' : ''} onClick={() => setTab('audio')}>IMPORT AUDIO</button>
            </div>
            <label className="wte-row">
              <span>NAME</span>
              <input value={name} maxLength={14} placeholder={finalName} onChange={(e) => setName(e.target.value)} />
            </label>

            {tab === 'draw' ? (
              <div className="wte-body">
                <canvas ref={drawRef} className={'wte-draw' + (readOnly ? ' ro' : '')}
                  onPointerDown={onDown} onPointerMove={onMove} onPointerUp={onUp} onPointerLeave={onUp} />
                <p className="wte-hint">
                  {readOnly
                    ? 'Read-only (multi-frame / factory). Duplicate to edit.'
                    : 'Drag to draw one cycle. Band-limited on commit. → 1 frame.'}
                </p>
                <div className="wte-tools">
                  <span className="wte-tools-label">SEED</span>
                  <button onClick={() => applySeed('sine')}>SINE</button>
                  <button onClick={() => applySeed('saw')}>SAW</button>
                  <button onClick={() => applySeed('square')}>SQUARE</button>
                  <button onClick={() => applySeed('tri')}>TRI</button>
                  <span className="wte-sep" />
                  <button className={brush === 'pen' ? 'on' : ''} onClick={() => setBrush('pen')}>PEN</button>
                  <button className={brush === 'smooth' ? 'on' : ''} onClick={() => setBrush('smooth')}>SMOOTH</button>
                  <button className={snap ? 'on' : ''} onClick={() => setSnap((s) => !s)}>SNAP</button>
                  <button className="wte-clear" onClick={() => { pointsRef.current = new Array(DRAW_N).fill(0); setSelectedId(null); setReadOnly(false); setDrawVersion((v) => v + 1); }}>CLEAR</button>
                  <button className="wte-create" disabled={readOnly} onClick={createFromDraw}>
                    {selectedId && selectedId[0] === 'u' && !readOnly ? 'UPDATE TABLE' : 'CREATE TABLE'}
                  </button>
                </div>
              </div>
            ) : (
              <div className="wte-body">
                <label className="wte-row">
                  <span>FILE</span>
                  <input type="file" accept="audio/*" onChange={onFile} />
                </label>
                <div className="wte-modes">
                  <button className={mode === 'single' ? 'on' : ''} onClick={() => setMode('single')}>SINGLE CYCLE</button>
                  <button className={mode === 'auto' ? 'on' : ''} onClick={() => setMode('auto')}>AUTO-DETECT</button>
                  <button className={mode === 'fixed' ? 'on' : ''} onClick={() => setMode('fixed')}>FIXED LEN</button>
                </div>
                {mode === 'fixed' ? (
                  <label className="wte-row">
                    <span>CYCLE</span>
                    <input type="number" min={2} step={1} value={fixedLen} onChange={(e) => setFixedLen(+e.target.value)} />
                    <small>samples / frame</small>
                  </label>
                ) : null}
                <div className="wte-status">{status || 'load an audio file to begin'}</div>
                <p className="wte-hint">
                  SINGLE CYCLE: whole clip = 1 frame. AUTO-DETECT: estimate pitch, slice per cycle.
                  FIXED LEN: slice every N samples. Up to {MAX_FRAMES} frames.
                </p>
                <button className="wte-create" disabled={!audio} onClick={createFromAudio}>CREATE TABLE</button>
              </div>
            )}
          </section>
        </div>
      </div>
    </div>
  );
}
