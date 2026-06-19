# Wavetable Frame Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the wavetable editor author multi-frame (Serum-style) wavetables — a frame list with a frame strip, per-frame draw, add/duplicate/delete/reorder, and a live 3D stack preview — in both the web app and the JUCE plugin.

**Architecture:** Web-first, then JUCE port (the repo's established direction). The editor holds a frame list at the canonical frame size **SIZE (2048)** so selecting/reordering/assigning is lossless; a frame is only re-rounded to draw-pad resolution (DRAW_N=256) when the user actually draws on it. Playback morphs between frames via the existing `POS` param — no new engine code. This **supersedes** the read-only/collapse rules from the redesign: multi-frame user tables become fully editable; factory tables stay read-only (browse + duplicate-to-edit).

**Tech Stack:** TypeScript + React + Zustand + Vite (web); C++17 + JUCE (plugin); JUCE hand-rolled `check()` test harness via CTest.

**Spec:** `docs/superpowers/specs/2026-06-19-wavetable-frame-editing-design.md`
**Pixel reference:** `design-handoff/synthesizer-mockup-design/project/Wavetable Editor.dc.html` (aesthetic); the frame strip is new (not in the mockup) — match the existing `.wte-tools` button styling.
**Accent:** per-osc (`ACCENTS.a` cyan / `ACCENTS.b` amber; JUCE `col::acA`/`col::acB`).

**Canonical constants:** web `SIZE = 2048` (`src/engine/wavetables.ts`), `DRAW_N = 256` (`src/components/wavetable/drawmodel.ts`), `MAX_FRAMES = 64` (`src/engine/usertables.ts`). JUCE `fable::SIZE`, `DrawPad::DRAW_N = 256`, `fable::MAX_FRAMES`.

---

## File structure

**Web**
- Create `src/components/wavetable/frames.ts` — pure frame-list ops + SIZE↔pad sampling.
- Create `src/components/wavetable/StackPreview.tsx` — perspective terrain of the frame list, current frame highlighted.
- Create `src/components/wavetable/FrameStrip.tsx` — thumbnail strip: select / add / delete / drag-reorder.
- Modify `src/components/WavetableEditor.tsx` — frame-list state; draw edits current frame; wire strip + preview; create from all frames; select loads all frames; import populates the strip; remove read-only/collapse.
- Modify `src/index.css` — strip, stack preview, frame thumbs, editor-column layout.

**JUCE**
- Create `juce/source/dsp/FrameOps.h` — headless pure frame-list ops (duplicate/delete/move) + SIZE→pad downsample, for parity + TDD.
- Modify `juce/test/engine_test.cpp` — TDD `FrameOps`.
- Modify `juce/source/ui/WavetableEditor.h` / `.cpp` — `DrawPad` edit callback; `TablePreview` current-frame highlight; new `FrameStrip` component; editor frame-list state + wiring + layout; select loads all frames; create from all frames; import populates.

---

## Phase 1 — Web: frame model + UI

### Task 1: Pure frame-list helpers (web)

**Files:**
- Create: `src/components/wavetable/frames.ts`

- [ ] **Step 1: Write the module**

```typescript
// Pure helpers for the editor's frame list. Frames are stored at the canonical
// SIZE (2048 samples each); the draw pad works at DRAW_N (256). Sampling between
// the two is only needed to display a frame in the pad / thumbnails and to write
// a *drawn* frame back. Untouched frames keep their exact SIZE samples (lossless
// select / reorder / assign).
import { SIZE } from '../../engine/wavetables';
import { MAX_FRAMES } from '../../engine/usertables';
import { DRAW_N } from './drawmodel';

// Sample `n` points out of a SIZE-sample frame for the pad / a thumbnail.
export function framePoints(frame: Float32Array, n: number = DRAW_N): number[] {
  const out = new Array(n).fill(0);
  const step = frame.length / n;
  for (let i = 0; i < n; i++) out[i] = frame[Math.floor(i * step)] || 0;
  return out;
}

// Insert a copy of frame `i` right after it; returns a new array. No-op at cap.
export function duplicateAt(frames: Float32Array[], i: number): Float32Array[] {
  if (i < 0 || i >= frames.length || frames.length >= MAX_FRAMES) return frames;
  const next = frames.slice();
  next.splice(i + 1, 0, frames[i].slice());
  return next;
}

// Remove frame `i`; returns a new array. Refuses to drop the last frame.
export function deleteAt(frames: Float32Array[], i: number): Float32Array[] {
  if (frames.length <= 1 || i < 0 || i >= frames.length) return frames;
  const next = frames.slice();
  next.splice(i, 1);
  return next;
}

// Move frame from `from` to index `to` (clamped); returns a new array.
export function moveFrame(frames: Float32Array[], from: number, to: number): Float32Array[] {
  if (from < 0 || from >= frames.length) return frames;
  const dest = Math.max(0, Math.min(frames.length - 1, to));
  if (dest === from) return frames;
  const next = frames.slice();
  const [f] = next.splice(from, 1);
  next.splice(dest, 0, f);
  return next;
}

// Split a packed user-table wave (frames*SIZE) into independent SIZE frames.
export function framesFromWave(wave: Float32Array, frameCount: number): Float32Array[] {
  const out: Float32Array[] = [];
  for (let f = 0; f < frameCount; f++) out.push(wave.slice(f * SIZE, (f + 1) * SIZE));
  return out;
}
```

- [ ] **Step 2: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/wavetable/frames.ts
git commit -m "feat(web): pure frame-list helpers (duplicate/delete/move/sample)"
```

### Task 2: Stack preview component (web)

A perspective terrain of the whole frame list with the current frame highlighted (port of the JUCE `TablePreview.paint` math).

**Files:**
- Create: `src/components/wavetable/StackPreview.tsx`
- Reference: `juce/source/ui/WavetableEditor.cpp:84-119` (terrain math), `src/components/displays/canvas.ts` (`setupCanvas`)

- [ ] **Step 1: Write the component**

```tsx
import { useEffect, useRef } from 'react';
import { setupCanvas } from '../displays/canvas';
import { framePoints } from './frames';

// Perspective "waterfall" of the frame list; the current frame is drawn bright
// in the accent, the rest receding and dim. Static (not POS-animated) — it just
// reflects the frames as they are edited / added / reordered.
export function StackPreview({ frames, current, accent }: {
  frames: Float32Array[]; current: number; accent: string;
}) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const c = ref.current;
    if (!c) return;
    const { ctx, w, h } = setupCanvas(c);
    ctx.clearRect(0, 0, w, h);
    const nf = frames.length;
    if (!nf) return;
    const PTS = 160;
    const depthX = w * 0.20, depthY = h * 0.40;
    const waveW = w * 0.70, amp = h * 0.16;
    const x0 = w * 0.07, y0 = h * 0.78;
    const maxDraw = Math.min(nf, 48);
    for (let k = maxDraw - 1; k >= 0; k--) { // back-to-front
      const f = nf === 1 ? 0 : Math.round((k / (maxDraw - 1)) * (nf - 1));
      const d = maxDraw === 1 ? 1 : k / (maxDraw - 1);
      const pts = framePoints(frames[f], PTS);
      const ox = x0 + d * depthX, oy = y0 - d * depthY;
      ctx.beginPath();
      for (let i = 0; i < PTS; i++) {
        const x = ox + (i / (PTS - 1)) * waveW;
        const y = oy - pts[i] * amp;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      if (f === current) {
        ctx.strokeStyle = accent; ctx.globalAlpha = 1; ctx.lineWidth = 1.8;
        ctx.shadowColor = accent; ctx.shadowBlur = 10;
      } else {
        ctx.strokeStyle = '#5b6a86'; ctx.globalAlpha = 0.18 + d * 0.5; ctx.lineWidth = 1;
        ctx.shadowBlur = 0;
      }
      ctx.stroke(); ctx.shadowBlur = 0; ctx.globalAlpha = 1;
    }
  }, [frames, current, accent]);
  return <canvas ref={ref} className="wte-stack" />;
}
```

- [ ] **Step 2: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/wavetable/StackPreview.tsx
git commit -m "feat(web): frame-stack perspective preview"
```

### Task 3: Frame strip component (web)

**Files:**
- Create: `src/components/wavetable/FrameStrip.tsx`
- Reference: `src/components/wavetable/frames.ts` (`framePoints`), `src/components/displays/canvas.ts`

- [ ] **Step 1: Write the component**

```tsx
import { useEffect, useRef, useState } from 'react';
import { setupCanvas } from '../displays/canvas';
import { framePoints } from './frames';

function FrameThumb({ frame, accent, on }: { frame: Float32Array; accent: string; on: boolean }) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const c = ref.current;
    if (!c) return;
    const { ctx, w, h } = setupCanvas(c);
    ctx.clearRect(0, 0, w, h);
    const pts = framePoints(frame, 64);
    ctx.beginPath();
    for (let i = 0; i < pts.length; i++) {
      const x = (i / (pts.length - 1)) * w;
      const y = h / 2 - pts[i] * h * 0.38;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = on ? accent : '#8893a8';
    ctx.lineWidth = 1.2;
    if (on) { ctx.shadowColor = accent; ctx.shadowBlur = 5; }
    ctx.stroke();
  }, [frame, accent, on]);
  return <canvas ref={ref} className="wte-frame-c" width={40} height={26} />;
}

// Horizontal strip of frame thumbnails: click to select, [+] to duplicate the
// current frame, [✕] on the selected one to delete, drag to reorder.
export function FrameStrip({ frames, current, accent, readOnly, onSelect, onAdd, onDelete, onReorder }: {
  frames: Float32Array[]; current: number; accent: string; readOnly: boolean;
  onSelect: (i: number) => void; onAdd: () => void; onDelete: (i: number) => void;
  onReorder: (from: number, to: number) => void;
}) {
  const rowRef = useRef<HTMLDivElement>(null);
  const [drag, setDrag] = useState<number | null>(null);

  const indexAtX = (clientX: number): number => {
    const cells = rowRef.current?.querySelectorAll('.wte-frame');
    if (!cells) return 0;
    for (let i = 0; i < cells.length; i++) {
      const r = (cells[i] as HTMLElement).getBoundingClientRect();
      if (clientX < r.left + r.width / 2) return i;
    }
    return cells.length - 1;
  };

  return (
    <div className="wte-frames">
      <span className="wte-frames-label">FRAMES</span>
      <div className="wte-frames-row" ref={rowRef}>
        {frames.map((fr, i) => (
          <div
            key={i}
            className={'wte-frame' + (i === current ? ' on' : '') + (drag === i ? ' drag' : '')}
            onPointerDown={(e) => { if (!readOnly) { setDrag(i); (e.target as HTMLElement).setPointerCapture(e.pointerId); } onSelect(i); }}
            onPointerMove={(e) => { if (drag !== null) { const t = indexAtX(e.clientX); if (t !== drag) { onReorder(drag, t); setDrag(t); } } }}
            onPointerUp={() => setDrag(null)}
          >
            <FrameThumb frame={fr} accent={accent} on={i === current} />
            {i === current && !readOnly && frames.length > 1 ? (
              <button className="wte-frame-x" aria-label="delete frame" onPointerDown={(e) => e.stopPropagation()} onClick={() => onDelete(i)}>✕</button>
            ) : null}
          </div>
        ))}
        {!readOnly ? <button className="wte-frame-add" aria-label="add frame" onClick={onAdd}>＋</button> : null}
      </div>
      <span className="wte-frames-count">{frames.length}f</span>
    </div>
  );
}
```

- [ ] **Step 2: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/wavetable/FrameStrip.tsx
git commit -m "feat(web): frame strip (select/add/delete/drag-reorder)"
```

### Task 4: Rewire WavetableEditor for the frame list (web)

Replace the single-curve model with a frame list; the draw pad edits the current frame; wire the strip + stack preview; create from all frames; select loads all frames editable; import populates the strip; remove the read-only-for-multiframe / collapse rules.

**Files:**
- Modify: `src/components/WavetableEditor.tsx`

- [ ] **Step 1: Update imports + state**

Replace the imports block (`src/components/WavetableEditor.tsx:8-18`) with:

```tsx
import { useEffect, useRef, useState } from 'react';
import { useStore, engine, factoryTables } from '../store';
import { setupCanvas } from './displays/canvas';
import {
  makeUserTable, mixToMono, detectCycleLength, sliceToFrames, singleCycleFrame,
  framesFromGenerated, MAX_FRAMES, type UserTable,
} from '../engine/usertables';
import { ACCENTS } from '../constants';
import { TABLE_NAMES } from '../params';
import { TableLibrary } from './wavetable/TableLibrary';
import { DRAW_N, seedShape, snapValue, smoothAround, type Seed, type Brush } from './wavetable/drawmodel';
import { framePoints, duplicateAt, deleteAt, moveFrame, framesFromWave } from './wavetable/frames';
import { FrameStrip } from './wavetable/FrameStrip';
import { StackPreview } from './wavetable/StackPreview';
import { frameFromDrawing } from '../engine/usertables';
```

Replace the state block (`src/components/WavetableEditor.tsx:41-60`) with:

```tsx
  const [tab, setTab] = useState<Tab>('draw');
  const [name, setName] = useState('');
  const [mode, setMode] = useState<AudioMode>('single');
  const [fixedLen, setFixedLen] = useState(2048);
  const [audio, setAudio] = useState<{ samples: Float32Array; sr: number; label: string } | null>(null);
  const [status, setStatus] = useState('');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [brush, setBrush] = useState<Brush>('pen');
  const [snap, setSnap] = useState(false);
  const [readOnly, setReadOnly] = useState(false); // factory only

  const drawRef = useRef<HTMLCanvasElement>(null);
  // The frame list at canonical SIZE. The pad edits the current frame; untouched
  // frames keep their exact samples (lossless select / reorder / assign).
  const framesRef = useRef<Float32Array[]>([new Float32Array(2048)]);
  const pointsRef = useRef<number[]>(new Array(DRAW_N).fill(0)); // current frame in pad space
  const [current, setCurrent] = useState(0);
  const drawingRef = useRef(false);
  const lastIdxRef = useRef(-1);
  const [drawVersion, setDrawVersion] = useState(0); // repaint pad
  const [frameVersion, setFrameVersion] = useState(0); // repaint strip + stack

  const accentColor = ACCENTS[editorOsc === 'oscB' ? 'b' : 'a'];
```

- [ ] **Step 2: Add frame load/sync helpers**

Replace `loadPoints` (`src/components/WavetableEditor.tsx:149-156`) with frame-aware helpers:

```tsx
  // Load the whole frame list, show frame 0 in the pad.
  const loadFrames = (frames: Float32Array[]) => {
    framesRef.current = frames.length ? frames : [new Float32Array(2048)];
    setCurrent(0);
    pointsRef.current = framePoints(framesRef.current[0]);
    setDrawVersion((v) => v + 1);
    setFrameVersion((v) => v + 1);
  };

  // Switch which frame the pad edits (no write-back — untouched frames stay exact).
  const gotoFrame = (i: number) => {
    const f = Math.max(0, Math.min(framesRef.current.length - 1, i));
    setCurrent(f);
    pointsRef.current = framePoints(framesRef.current[f]);
    setDrawVersion((v) => v + 1);
  };

  // Write the pad's current curve back into the current frame (called on edit).
  const syncCurrentFrame = () => {
    framesRef.current = framesRef.current.map((fr, i) => (i === current ? frameFromDrawing(pointsRef.current) : fr));
    setFrameVersion((v) => v + 1);
  };
```

- [ ] **Step 3: Update the reset effect + draw effect**

Replace the reset effect (`src/components/WavetableEditor.tsx:62-70`) with:

```tsx
  useEffect(() => {
    if (editorOsc) {
      setTab('draw'); setName(''); setMode('single'); setAudio(null); setStatus('');
      setSelectedId(null); setReadOnly(false); setBrush('pen'); setSnap(false);
      framesRef.current = [new Float32Array(2048)];
      setCurrent(0);
      pointsRef.current = new Array(DRAW_N).fill(0);
      setDrawVersion((v) => v + 1); setFrameVersion((v) => v + 1);
    }
  }, [editorOsc]);
```

The draw-canvas effect (`src/components/WavetableEditor.tsx:72-94`) is unchanged (it renders `pointsRef.current`).

- [ ] **Step 4: Update edit handlers to sync the current frame**

In `paint` (`src/components/WavetableEditor.tsx:182-205`), after `if (brush === 'smooth') smoothAround(pts, idx);` add a sync. Replace the tail of `paint`:

```tsx
    if (brush === 'smooth') smoothAround(pts, idx);
    lastIdxRef.current = idx;
    syncCurrentFrame();
    setDrawVersion((v) => v + 1);
```

Replace `applySeed` (`src/components/WavetableEditor.tsx:207-212`) with:

```tsx
  const applySeed = (kind: Seed) => {
    if (readOnly) return;
    pointsRef.current = seedShape(kind);
    syncCurrentFrame();
    setDrawVersion((v) => v + 1);
  };
```

- [ ] **Step 5: Frame ops + updated select/create/new/import**

Replace `createFromAudio` / `createFromDraw` / `selectFactory` / `selectUser` / `newTable`
(`src/components/WavetableEditor.tsx:117-179` and `214-227`) with:

```tsx
  const createFromAudio = () => {
    if (!audio) return;
    let frames: Float32Array[];
    if (mode === 'single') frames = singleCycleFrame(audio.samples);
    else if (mode === 'auto') frames = sliceToFrames(audio.samples, detectCycleLength(audio.samples, audio.sr));
    else frames = sliceToFrames(audio.samples, Math.max(2, fixedLen | 0));
    // Land the sliced frames in the editor (uncommitted) so they can be tweaked
    // in the strip before CREATE. New table (not editing an existing one).
    setSelectedId(null); setReadOnly(false);
    loadFrames(frames.slice(0, MAX_FRAMES));
    setTab('draw');
  };

  const createFromDraw = () => {
    if (readOnly) return;
    const u = makeUserTable(finalName, framesRef.current);
    if (selectedId && selectedId[0] === 'u') {
      const idx = +selectedId.slice(1);
      updateUserTable(idx, u);
      assign(TABLE_NAMES.length + idx);
    } else {
      commit(u);
    }
  };

  const selectFactory = (i: number) => {
    const ft = factoryTables()[i];
    setSelectedId('f' + i);
    setName(ft.name);
    setReadOnly(true);
    setTab('draw');
    loadFrames(framesFromGenerated(ft)); // all frames, read-only
    assign(i);
  };

  const selectUser = (i: number) => {
    const ut = userTables[i];
    setSelectedId('u' + i);
    setName(ut.name);
    setReadOnly(false);
    setTab('draw');
    loadFrames(framesFromWave(ut.wave, ut.frames)); // all frames, editable
    assign(TABLE_NAMES.length + i);
  };

  const newTable = () => {
    const frames = [frameFromDrawing(seedShape('sine'))];
    const u = makeUserTable('USER', frames);
    const idx = userTables.length;
    addUserTable(u);
    setSelectedId('u' + idx);
    setName('USER');
    setReadOnly(false);
    setTab('draw');
    loadFrames(frames);
  };

  const addFrame = () => {
    if (readOnly) return;
    const next = duplicateAt(framesRef.current, current);
    if (next === framesRef.current) { setStatus(`max ${MAX_FRAMES} frames`); return; }
    framesRef.current = next;
    gotoFrame(current + 1);
    setFrameVersion((v) => v + 1);
  };

  const removeFrame = (i: number) => {
    if (readOnly) return;
    const next = deleteAt(framesRef.current, i);
    if (next === framesRef.current) return;
    framesRef.current = next;
    gotoFrame(Math.min(i, next.length - 1));
    setFrameVersion((v) => v + 1);
  };

  const reorderFrame = (from: number, to: number) => {
    if (readOnly) return;
    framesRef.current = moveFrame(framesRef.current, from, to);
    setCurrent(to);
    setFrameVersion((v) => v + 1);
  };
```

- [ ] **Step 6: Update the DRAW pane JSX (pad + stack + strip)**

Replace the DRAW pane (`src/components/WavetableEditor.tsx:267-291`, the `tab === 'draw' ? (...)` branch up to the closing `</div>` before the import pane) with:

```tsx
            {tab === 'draw' ? (
              <div className="wte-body">
                <div className="wte-edit-row">
                  <canvas ref={drawRef} className={'wte-draw' + (readOnly ? ' ro' : '')}
                    onPointerDown={onDown} onPointerMove={onMove} onPointerUp={onUp} onPointerLeave={onUp} />
                  <StackPreview frames={framesRef.current} current={current} accent={accentColor} key={frameVersion} />
                </div>
                <FrameStrip
                  frames={framesRef.current} current={current} accent={accentColor} readOnly={readOnly}
                  onSelect={gotoFrame} onAdd={addFrame} onDelete={removeFrame} onReorder={reorderFrame} key={'fs' + frameVersion}
                />
                <p className="wte-hint">
                  {readOnly
                    ? 'Read-only (factory). Duplicate to edit.'
                    : 'Drag to draw the selected frame. POS morphs through frames on play.'}
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
                  <button className="wte-clear" onClick={() => { pointsRef.current = new Array(DRAW_N).fill(0); syncCurrentFrame(); setDrawVersion((v) => v + 1); }}>CLEAR</button>
                  <button className="wte-create" disabled={readOnly} onClick={createFromDraw}>
                    {selectedId && selectedId[0] === 'u' ? 'UPDATE TABLE' : 'CREATE TABLE'}
                  </button>
                </div>
              </div>
            ) : (
```

Update the import button label (`src/components/WavetableEditor.tsx:315`) to reflect that it now loads frames into the editor:

```tsx
                <button className="wte-create" disabled={!audio} onClick={createFromAudio}>LOAD FRAMES →</button>
```

- [ ] **Step 7: Verify build**

Run: `npm run build`
Expected: `tsc` clean + vite build succeeds. (If `noUnusedLocals` flags the leftover `singleCycleFrame`/etc., they are all still used; remove any genuinely unused import it names.)

- [ ] **Step 8: Commit**

```bash
git add src/components/WavetableEditor.tsx
git commit -m "feat(web): multi-frame editing — frame list, strip, stack preview"
```

### Task 5: CSS for the frame strip + stack preview (web)

**Files:**
- Modify: `src/index.css` (after the existing wavetable-editor block)

- [ ] **Step 1: Add styles**

Append to the wavetable-editor section of `src/index.css`:

```css
/* frame editing */
.wte-edit-row { display: flex; gap: 12px; flex: 1; min-height: 0; }
.wte-edit-row .wte-draw { flex: 1; min-height: 0; height: auto; }
.wte-stack {
  width: 220px; flex: none; border: 1px solid var(--line);
  border-radius: 10px; background: #07090e;
}
.wte-frames { display: flex; align-items: center; gap: 8px; margin: 10px 0 2px; }
.wte-frames-label { font-size: 9px; letter-spacing: 0.16em; color: #5b6679; flex: none; }
.wte-frames-count { font-size: 9px; letter-spacing: 0.12em; color: var(--text-dim); flex: none; }
.wte-frames-row {
  flex: 1; min-width: 0; display: flex; align-items: center; gap: 5px;
  overflow-x: auto; padding: 4px 2px;
}
.wte-frame {
  position: relative; flex: none; width: 44px; height: 30px;
  border: 1px solid var(--line); border-radius: 5px; background: #07090e; cursor: pointer;
  display: flex; align-items: center; justify-content: center; touch-action: none;
}
.wte-frame.on { border-color: color-mix(in srgb, var(--ac) 55%, transparent);
  box-shadow: 0 0 10px color-mix(in srgb, var(--ac) 22%, transparent); }
.wte-frame.drag { opacity: 0.6; }
.wte-frame-c { width: 40px; height: 26px; display: block; }
.wte-frame-x {
  position: absolute; top: -6px; right: -6px; width: 15px; height: 15px; line-height: 1;
  border: 1px solid rgba(255,90,90,0.4); border-radius: 50%; background: #11141c;
  color: #c46b6b; font-size: 9px; cursor: pointer; padding: 0;
}
.wte-frame-add {
  flex: none; width: 30px; height: 30px; border: 1px dashed var(--line);
  border-radius: 5px; background: transparent; color: var(--ac); font-size: 14px; cursor: pointer;
}
.wte-frame-add:hover { border-color: var(--ac); }
```

- [ ] **Step 2: Verify build**

Run: `npm run build`
Expected: passes.

- [ ] **Step 3: Commit**

```bash
git add src/index.css
git commit -m "feat(web): frame strip + stack preview styles"
```

---

## Phase 2 — Web visual verification

### Task 6: Verify frame editing in-browser

**Files:** none (verification).

- [ ] **Step 1: Run dev server**

Run: `npm run dev` (background); note the URL.

- [ ] **Step 2: Drive with the chrome-debug skill**

Open `/app/`, open the editor for OSC A (`.wt-edit`), and verify:
- DRAW shows the pad + 3D stack preview side by side, with the FRAMES strip below (`1f`).
- `[＋]` adds a frame (count rises, new frame selected, stack gains a layer); editing a frame updates its thumbnail + the highlighted layer in the stack.
- Selecting another frame in the strip loads it into the pad; drawing it differently then selecting back shows the change persisted.
- `[✕]` on the selected frame deletes it (min 1 enforced); drag a thumbnail reorders.
- Select a multi-frame user table (duplicate a factory first) → all frames load editable (not read-only); edit a middle frame → UPDATE → reselect shows the edit on that frame, others intact.
- Factory table → strip shows all frames, read-only (no `[＋]`/`[✕]`/draw); DUPLICATE makes an editable copy.
- IMPORT AUDIO → LOAD FRAMES → the sliced frames appear in the strip for tweaking, then CREATE TABLE commits.

- [ ] **Step 3: Fix gaps, re-verify, commit**

```bash
git add -A && git commit -m "fix(web): frame editing polish from visual verification"
```

---

## Phase 3 — JUCE: pure frame ops + tests

### Task 7: FrameOps header + TDD

**Files:**
- Create: `juce/source/dsp/FrameOps.h`
- Modify: `juce/test/engine_test.cpp`

- [ ] **Step 1: Write the failing test**

In `juce/test/engine_test.cpp`, add near the other table checks inside `main`:

```cpp
    {
        // FrameOps — duplicate/delete/move on a frame list, and pad-downsample.
        using Frame = std::vector<float>;
        std::vector<Frame> fs{ Frame(SIZE, 0.1f), Frame(SIZE, 0.2f), Frame(SIZE, 0.3f) };
        auto dup = fable::duplicateFrame(fs, 1);
        check(dup.size() == 4 && dup[2][0] == 0.2f, "duplicateFrame inserts copy after i");
        auto del = fable::deleteFrame(fs, 0);
        check(del.size() == 2 && del[0][0] == 0.2f, "deleteFrame removes i");
        auto one = std::vector<Frame>{ Frame(SIZE, 0.5f) };
        check(fable::deleteFrame(one, 0).size() == 1, "deleteFrame refuses last frame");
        auto mv = fable::moveFrame(fs, 0, 2);
        check(mv.size() == 3 && mv[2][0] == 0.1f, "moveFrame relocates frame");
        auto pad = fable::framePoints(Frame(SIZE, 0.7f), 256);
        check(pad.size() == 256 && std::abs(pad[10] - 0.7f) < 1e-6f, "framePoints samples DRAW_N points");
    }
```

- [ ] **Step 2: Build to verify it fails**

Run: `cd juce/build && cmake --build . --target engine_test 2>&1 | tail -15`
Expected: compile error — `duplicateFrame` not declared.

- [ ] **Step 3: Write FrameOps.h**

```cpp
// Pure frame-list operations for the wavetable editor (headless, JUCE-free, so
// they unit-test in engine_test). A frame is a SIZE-sample single cycle; a table
// is an ordered list of frames the engine morphs through via the POS param.
#pragma once
#include "Wavetables.h"
#include <vector>
#include <algorithm>

namespace fable {

using Frame = std::vector<float>;

// Insert a copy of frame `i` right after it (cap at MAX_FRAMES). Returns a copy.
inline std::vector<Frame> duplicateFrame(const std::vector<Frame>& frames, int i) {
    if (i < 0 || i >= (int)frames.size() || (int)frames.size() >= MAX_FRAMES) return frames;
    auto out = frames;
    out.insert(out.begin() + i + 1, frames[(size_t)i]);
    return out;
}

// Remove frame `i`; refuses to drop the last remaining frame. Returns a copy.
inline std::vector<Frame> deleteFrame(const std::vector<Frame>& frames, int i) {
    if ((int)frames.size() <= 1 || i < 0 || i >= (int)frames.size()) return frames;
    auto out = frames;
    out.erase(out.begin() + i);
    return out;
}

// Move frame from `from` to index `to` (clamped). Returns a copy.
inline std::vector<Frame> moveFrame(const std::vector<Frame>& frames, int from, int to) {
    if (from < 0 || from >= (int)frames.size()) return frames;
    int dest = std::max(0, std::min((int)frames.size() - 1, to));
    if (dest == from) return frames;
    auto out = frames;
    Frame f = out[(size_t)from];
    out.erase(out.begin() + from);
    out.insert(out.begin() + dest, f);
    return out;
}

// Sample `n` points out of a frame for the pad / a thumbnail.
inline std::vector<float> framePoints(const Frame& frame, int n) {
    std::vector<float> out((size_t)n, 0.0f);
    if (frame.empty()) return out;
    const float step = (float)frame.size() / n;
    for (int i = 0; i < n; ++i) out[(size_t)i] = frame[(size_t)std::min((int)frame.size() - 1, (int)(i * step))];
    return out;
}

// Split a packed user-table wave (frameCount*SIZE) into independent SIZE frames.
inline std::vector<Frame> framesFromWave(const std::vector<float>& wave, int frameCount) {
    std::vector<Frame> out;
    for (int f = 0; f < frameCount; ++f)
        out.emplace_back(wave.begin() + (size_t)f * SIZE, wave.begin() + (size_t)(f + 1) * SIZE);
    return out;
}

} // namespace fable
```

Add the include to `juce/test/engine_test.cpp` (after the other dsp includes, ~line 12):

```cpp
#include "../source/dsp/FrameOps.h"
```

- [ ] **Step 4: Build + run the test**

Run: `cd juce/build && cmake --build . --target engine_test && ctest -R engine_test --output-on-failure 2>&1 | tail -25`
Expected: the five new `duplicateFrame`/`deleteFrame`/`moveFrame`/`framePoints` checks PASS.

- [ ] **Step 5: Commit**

```bash
git add juce/source/dsp/FrameOps.h juce/test/engine_test.cpp
git commit -m "feat(juce): FrameOps pure frame-list helpers (+tests)"
```

---

## Phase 4 — JUCE: editor frame model + UI

### Task 8: DrawPad edit callback + TablePreview current-frame highlight

**Files:**
- Modify: `juce/source/ui/WavetableEditor.h`, `juce/source/ui/WavetableEditor.cpp`

- [ ] **Step 1: Add an onEdit callback to DrawPad**

In `juce/source/ui/WavetableEditor.h`, add to `DrawPad`'s public section (after `setReadOnly`):

```cpp
    std::function<void()> onEdit; // called after any draw / seed / clear edit
```

In `juce/source/ui/WavetableEditor.cpp`, call it at the end of `paintAt` (after `repaint();` at line ~30) and in `seed` and `clear`. Replace the three tails:

```cpp
// paintAt tail (after the `repaint();` that ends the method body):
    lastIdx = idx;
    repaint();
    if (onEdit) onEdit();
```

```cpp
void DrawPad::clear() { std::fill(pts.begin(), pts.end(), 0.0f); lastIdx = -1; repaint(); if (onEdit) onEdit(); }
```

```cpp
// seed tail (after `lastIdx = -1; repaint();`):
    lastIdx = -1; repaint();
    if (onEdit) onEdit();
```

- [ ] **Step 2: Add current-frame highlight to TablePreview**

In `juce/source/ui/WavetableEditor.h`, extend `TablePreview`:

```cpp
    void setCurrent(int c) { current = c; repaint(); }
```
and add `int current = -1;` to its private members.

In `juce/source/ui/WavetableEditor.cpp` `TablePreview::paint`, replace the per-row colour block (lines ~115-117) so the current frame is drawn bright:

```cpp
        const bool isCur = (f == current);
        if (isCur) { g.setColour(accent); }
        else       { g.setColour(accent.withAlpha(0.18f + d * 0.4f)); }
        g.strokePath(path, juce::PathStrokeType(isCur ? 1.8f : 1.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
```

- [ ] **Step 3: Build**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | grep -iE "error:|Built target FableSynth" | tail`
Expected: builds.

- [ ] **Step 4: Commit**

```bash
git add juce/source/ui/WavetableEditor.h juce/source/ui/WavetableEditor.cpp
git commit -m "feat(juce): DrawPad onEdit + TablePreview current-frame highlight"
```

### Task 9: FrameStrip component (JUCE)

**Files:**
- Modify: `juce/source/ui/WavetableEditor.h` (declare), `juce/source/ui/WavetableEditor.cpp` (define)

- [ ] **Step 1: Declare FrameStrip**

In `juce/source/ui/WavetableEditor.h`, after the `TableThumb` class (line ~69), add:

```cpp
// Horizontal strip of frame thumbnails: click-select, [+] duplicate current,
// [✕] delete selected, pointer-drag to reorder.
class FrameStrip : public juce::Component {
public:
    FrameStrip();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setFrames(const std::vector<std::vector<float>>& frames, int current, juce::Colour accent, bool readOnly);
    std::function<void(int)> onSelect;
    std::function<void()>    onAdd;
    std::function<void(int)> onDelete;
    std::function<void(int,int)> onReorder;
private:
    int indexAtX(int x) const;     // which cell x falls in
    int cellW = 46, gap = 5;
    int count = 0, current = 0;
    bool readOnly = false;
    juce::Colour accent;
    juce::OwnedArray<TableThumb> thumbs;
    juce::TextButton addBtn{"+"};
    int dragFrom = -1;
};
```

- [ ] **Step 2: Define FrameStrip**

In `juce/source/ui/WavetableEditor.cpp`, after the `TableThumb::paint` definition (line ~136), add:

```cpp
// ============================ FrameStrip ============================
FrameStrip::FrameStrip() {
    addBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    addBtn.setColour(juce::TextButton::textColourOffId, col::acA);
    addBtn.onClick = [this] { if (onAdd) onAdd(); };
    addAndMakeVisible(addBtn);
}
void FrameStrip::setFrames(const std::vector<std::vector<float>>& frames, int cur, juce::Colour ac, bool ro) {
    count = (int)frames.size(); current = cur; accent = ac; readOnly = ro;
    thumbs.clear();
    for (int i = 0; i < count; ++i) {
        auto* t = new TableThumb();
        std::vector<float> viz = fable::framePoints(frames[(size_t)i], fable::VIZ_N);
        t->setData(viz, accent, i == current);
        thumbs.add(t);
        addChildComponent(t);
        t->setVisible(true);
    }
    addBtn.setVisible(!readOnly && count < fable::MAX_FRAMES);
    resized();
    repaint();
}
int FrameStrip::indexAtX(int x) const {
    int i = x / (cellW + gap);
    return juce::jlimit(0, juce::jmax(0, count - 1), i);
}
void FrameStrip::resized() {
    int x = 0;
    for (int i = 0; i < thumbs.size(); ++i) { thumbs[i]->setBounds(x, 2, cellW, getHeight() - 4); x += cellW + gap; }
    addBtn.setBounds(x, 2, 30, getHeight() - 4);
}
void FrameStrip::mouseDown(const juce::MouseEvent& e) {
    int i = indexAtX(e.x);
    if (i < count && onSelect) onSelect(i);
    dragFrom = readOnly ? -1 : i;
}
void FrameStrip::mouseDrag(const juce::MouseEvent& e) {
    if (dragFrom < 0) return;
    int to = indexAtX(e.x);
    if (to != dragFrom && onReorder) { onReorder(dragFrom, to); dragFrom = to; }
}
void FrameStrip::mouseUp(const juce::MouseEvent&) { dragFrom = -1; }
void FrameStrip::paint(juce::Graphics& g) {
    // selected-cell outline + a small delete affordance on the current cell.
    if (current < 0 || current >= count) return;
    auto r = juce::Rectangle<int>(current * (cellW + gap), 2, cellW, getHeight() - 4).toFloat();
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.2f);
    if (!readOnly && count > 1) {
        g.setColour(juce::Colour(0xffc46b6b));
        g.setFont(monoFont(10.0f));
        g.drawText("x", r.removeFromRight(12).removeFromTop(12), juce::Justification::centred);
    }
}
```

Note: the delete affordance is handled in `mouseDown` — when the click lands in the top-right ~12px of the current cell, treat it as delete. Adjust `mouseDown`:

```cpp
void FrameStrip::mouseDown(const juce::MouseEvent& e) {
    int i = indexAtX(e.x);
    if (i == current && !readOnly && count > 1) {
        auto cell = juce::Rectangle<int>(current * (cellW + gap), 2, cellW, getHeight() - 4);
        if (e.x >= cell.getRight() - 12 && e.y <= cell.getY() + 14) { if (onDelete) onDelete(i); return; }
    }
    if (i < count && onSelect) onSelect(i);
    dragFrom = readOnly ? -1 : i;
}
```

(Use the second `mouseDown` definition; do not also keep the first.)

- [ ] **Step 3: Build**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | grep -iE "error:|Built target FableSynth" | tail`
Expected: builds (FrameStrip is not yet wired into the editor — that's Task 10).

- [ ] **Step 4: Commit**

```bash
git add juce/source/ui/WavetableEditor.h juce/source/ui/WavetableEditor.cpp
git commit -m "feat(juce): FrameStrip component"
```

### Task 10: Wire the frame list into the JUCE editor

**Files:**
- Modify: `juce/source/ui/WavetableEditor.h`, `juce/source/ui/WavetableEditor.cpp`

- [ ] **Step 1: Add editor frame state + members**

In `juce/source/ui/WavetableEditor.h`, add to the private section (near `readOnlySel`):

```cpp
    // frame list (canonical SIZE each); the pad edits frames[currentFrame].
    std::vector<std::vector<float>> frames{ std::vector<float>((size_t)fable::SIZE, 0.0f) };
    int currentFrame = 0;
    void loadFrames(std::vector<std::vector<float>> fs);
    void gotoFrame(int i);
    void syncCurrentFrame();
    void addFrameOp();
    void deleteFrameOp(int i);
    void reorderFrameOp(int from, int to);
    FrameStrip frameStrip;
    TablePreview stackPreview{col::acA};
```

- [ ] **Step 2: Implement frame helpers**

In `juce/source/ui/WavetableEditor.cpp`, add (e.g. after `assignTable`):

```cpp
void WavetableEditor::loadFrames(std::vector<std::vector<float>> fs) {
    if (fs.empty()) fs.emplace_back((size_t)fable::SIZE, 0.0f);
    frames = std::move(fs);
    currentFrame = 0;
    drawPad.setPoints(frames[0]);
    frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    stackPreview.setFrames(frames); stackPreview.setCurrent(currentFrame);
}
void WavetableEditor::gotoFrame(int i) {
    currentFrame = juce::jlimit(0, (int)frames.size() - 1, i);
    drawPad.setPoints(frames[(size_t)currentFrame]);
    frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    stackPreview.setCurrent(currentFrame);
}
void WavetableEditor::syncCurrentFrame() {
    if (readOnlySel || currentFrame < 0 || currentFrame >= (int)frames.size()) return;
    frames[(size_t)currentFrame] = fable::frameFromDrawing(drawPad.points());
    frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    stackPreview.setFrames(frames); stackPreview.setCurrent(currentFrame);
}
void WavetableEditor::addFrameOp() {
    if (readOnlySel) return;
    auto next = fable::duplicateFrame(frames, currentFrame);
    if (next.size() == frames.size()) { statusLabel.setText("max " + juce::String(fable::MAX_FRAMES) + " frames", juce::dontSendNotification); return; }
    frames = std::move(next);
    gotoFrame(currentFrame + 1);
}
void WavetableEditor::deleteFrameOp(int i) {
    if (readOnlySel) return;
    auto next = fable::deleteFrame(frames, i);
    if (next.size() == frames.size()) return;
    frames = std::move(next);
    gotoFrame(juce::jmin(i, (int)frames.size() - 1));
}
void WavetableEditor::reorderFrameOp(int from, int to) {
    if (readOnlySel) return;
    frames = fable::moveFrame(frames, from, to);
    gotoFrame(to);
}
```

Add the include at the top of `juce/source/ui/WavetableEditor.cpp` (after the existing `#include "../dsp/UserTables.h"`):

```cpp
#include "../dsp/FrameOps.h"
```

- [ ] **Step 3: Wire components in the constructor**

In the `WavetableEditor` constructor (`juce/source/ui/WavetableEditor.cpp`, before `setVisible(false);`), add:

```cpp
    drawPad.onEdit = [this] { syncCurrentFrame(); };
    addChildComponent(frameStrip);
    frameStrip.onSelect  = [this](int i) { gotoFrame(i); };
    frameStrip.onAdd     = [this] { addFrameOp(); };
    frameStrip.onDelete  = [this](int i) { deleteFrameOp(i); };
    frameStrip.onReorder = [this](int a, int b) { reorderFrameOp(a, b); };
    addChildComponent(stackPreview);
```

- [ ] **Step 4: Replace single-frame select / create / new / import with frame-list versions**

Replace `selectFactory` (`juce/source/ui/WavetableEditor.cpp:502-513`):

```cpp
void WavetableEditor::selectFactory(int i) {
    const auto& fac = proc.factoryTables();
    if (i < 0 || i >= (int)fac.size()) return;
    selectedId = "f" + juce::String(i);
    nameField.setText(juce::String(fac[(size_t)i].name), juce::dontSendNotification);
    readOnlySel = true; drawPad.setReadOnly(true);
    loadFrames(fable::framesFromGenerated(fac[(size_t)i]));
    setTab(Tab::Draw);
    assignTable(i);
    refreshLibrary();
}
```

Replace `selectUser` (`juce/source/ui/WavetableEditor.cpp:515-529`):

```cpp
void WavetableEditor::selectUser(int i) {
    const auto& pool = proc.getUserTables();
    if (i < 0 || i >= (int)pool.size()) return;
    selectedId = "u" + juce::String(i);
    nameField.setText(juce::String(pool[(size_t)i].name), juce::dontSendNotification);
    readOnlySel = false; drawPad.setReadOnly(false);
    loadFrames(fable::framesFromWave(pool[(size_t)i].wave, pool[(size_t)i].frames));
    setTab(Tab::Draw);
    assignTable((int)proc.factoryTables().size() + i);
    refreshLibrary();
}
```

Replace `createFromDraw` (`juce/source/ui/WavetableEditor.cpp:481-495`):

```cpp
void WavetableEditor::createFromDraw() {
    if (readOnlySel) return;
    auto u = fable::makeUserTable(finalName(nameField.getText()), frames);
    if (selectedId.isNotEmpty() && selectedId[0] == 'u') {
        const int idx = selectedId.substring(1).getIntValue();
        proc.updateUserTable(idx, std::move(u));
        assignTable((int)proc.factoryTables().size() + idx);
        refreshLibrary();
        return;
    }
    commit(std::move(u));
}
```

Replace `createFromAudio` (`juce/source/ui/WavetableEditor.cpp:475-479`) so import lands frames in the editor:

```cpp
void WavetableEditor::createFromAudio() {
    auto fs = framesFromCurrentSettings();
    if (fs.empty()) return;
    selectedId = {}; readOnlySel = false; drawPad.setReadOnly(false);
    loadFrames(std::move(fs));
    setTab(Tab::Draw);
}
```

Replace the `newBtn.onClick` body (`juce/source/ui/WavetableEditor.cpp:308-320`) to use the frame list:

```cpp
    newBtn.onClick = [this] {
        std::vector<std::vector<float>> fs{ fable::frameFromDrawing(
            [this]{ drawPad.seed(0); return drawPad.points(); }()) };
        const int idx = (int)proc.getUserTables().size();
        if (proc.addUserTable(fable::makeUserTable("USER", fs)) < 0) return;
        selectedId = "u" + juce::String(idx); readOnlySel = false; drawPad.setReadOnly(false);
        nameField.setText("USER", juce::dontSendNotification);
        assignTable((int)proc.factoryTables().size() + idx);
        loadFrames(fs);
        setTab(Tab::Draw);
        refreshLibrary();
    };
```

Update the `clearBtn.onClick` (`juce/source/ui/WavetableEditor.cpp:270-274`) to clear only the current frame:

```cpp
    clearBtn.onClick = [this] {
        if (readOnlySel) return;
        drawPad.clear(); // fires onEdit -> syncCurrentFrame
    };
```

Update the seed lambda `seedTo` (`juce/source/ui/WavetableEditor.cpp:289-293`) to edit the current frame in place (it already calls `drawPad.seed`, which now fires `onEdit`); replace it so it no longer resets selection/library:

```cpp
    auto seedTo = [this](int kind) {
        if (readOnlySel) return;
        drawPad.seed(kind); // fires onEdit -> syncCurrentFrame
    };
```

- [ ] **Step 5: Show/hide + lay out the strip and stack in the DRAW pane**

In `setTab` (`juce/source/ui/WavetableEditor.cpp:368-394`), add the strip + stack to the draw-tab visibility group. After the `snapBtn.setVisible(!audio);` line add:

```cpp
    frameStrip.setVisible(!audio);
    stackPreview.setVisible(!audio);
```

In `layoutPanel` DRAW branch (`juce/source/ui/WavetableEditor.cpp:662-680`), replace the body so the pad and stack sit side by side with the strip beneath:

```cpp
    } else {
        // Tools row (bottom) + hint, then strip, then [pad | stack] fill the rest.
        auto tools = panel.removeFromBottom(34);
        createDrawBtn.setBounds(tools.removeFromRight(150).reduced(2, 0));
        tools.removeFromRight(6);
        clearBtn.setBounds(tools.removeFromRight(70).reduced(2, 0));
        tools.removeFromRight(8);
        seedLabel.setBounds(tools.removeFromLeft(40));
        auto tool = [&tools](juce::Component& c, int w) { c.setBounds(tools.removeFromLeft(w).reduced(2, 0)); };
        tool(seedSine, 58); tool(seedSaw, 50); tool(seedSquare, 72); tool(seedTri, 50);
        tools.removeFromLeft(8);
        tool(brushPen, 50); tool(brushSmooth, 72); tool(snapBtn, 56);
        panel.removeFromBottom(8);
        drawHint.setBounds(panel.removeFromBottom(16));
        panel.removeFromBottom(8);
        frameStrip.setBounds(panel.removeFromBottom(34));
        panel.removeFromBottom(8);
        auto stack = panel.removeFromRight(220);
        stackPreview.setBounds(stack);
        panel.removeFromRight(12);
        drawPad.setBounds(panel); // fills the rest
    }
```

In `setTab`, the DRAW-only block that sets the hint text (`juce/source/ui/WavetableEditor.cpp:383-391`) — update the hint string and keep refreshing the strip for the new selection. Replace that block:

```cpp
    if (!audio) {
        drawHint.setText(readOnlySel
            ? "Read-only (factory). Duplicate to edit."
            : "Drag to draw the selected frame. POS morphs through frames on play.",
            juce::dontSendNotification);
        createDrawBtn.setButtonText(selectedId.isNotEmpty() && selectedId[0] == 'u' ? "UPDATE TABLE" : "CREATE TABLE");
        createDrawBtn.setEnabled(!readOnlySel);
        frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    }
```

In `openFor` (`juce/source/ui/WavetableEditor.cpp:334-359`), reset the frame list. After `drawPad.clear();` add:

```cpp
    loadFrames({ std::vector<float>((size_t)fable::SIZE, 0.0f) });
```

- [ ] **Step 6: Build the plugin**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | grep -iE "error:|Built target FableSynth" | tail -20`
Expected: builds with no errors. Fix any dangling references.

- [ ] **Step 7: Commit**

```bash
git add juce/source/ui/WavetableEditor.h juce/source/ui/WavetableEditor.cpp
git commit -m "feat(juce): multi-frame editing — frame list, strip, stack preview"
```

---

## Phase 5 — Final verification

### Task 11: Full build + test sweep, manual checks

**Files:** none (verification).

- [ ] **Step 1: Web build**

Run: `npm run build`
Expected: passes.

- [ ] **Step 2: JUCE full build + tests**

Run: `cd juce/build && cmake --build . 2>&1 | tail -8 && ctest --output-on-failure 2>&1 | tail -15`
Expected: `engine_test` (incl. the new FrameOps checks) and `plugin_host_test` PASS.

- [ ] **Step 3: Manual JUCE smoke (use the `run` skill)**

Launch the plugin, open the editor: add frames, draw each differently, watch the stack preview gain layers, reorder via drag, delete, CREATE; assign to an osc and confirm the rack's `WavetableView` morphs through the authored frames as POS sweeps. Confirm factory read-only + duplicate-to-edit, and multi-frame user round-trip.

- [ ] **Step 4: Final commit**

```bash
git add -A && git commit -m "test: verify wavetable frame editing (web + juce)" || true
```

---

## Self-review notes (author)

- **Spec coverage:** frame list/explicit-frame model (T1/T4/T7/T10); frame strip select/add/delete/reorder (T3/T4/T9/T10); live 3D stack preview w/ current highlight (T2/T4/T8/T10); per-frame seed/pen/smooth/snap/clear (T4/T10 — act on the current frame via the pad); select loads all frames editable, factory read-only (T4/T10); import populates strip (T4/T10, "LOAD FRAMES"); supersedes read-only/collapse (T4/T10 remove the frames===1 gate and the collapse — `createFromDraw` builds from the whole `frames` list); MAX_FRAMES=64 enforced (T1/T7 caps, status message T4/T10); persistence unchanged (UserTable already stores frames*SIZE). All spec sections map to tasks.
- **Lossless model:** frames stored at SIZE; `framePoints` only samples for display; `syncCurrentFrame` writes back **only the edited frame** (`frameFromDrawing` DRAW_N→SIZE), so untouched frames keep exact samples on select/reorder/assign. Editing a frame re-rounds *that* frame to pad resolution (expected — it was drawn).
- **Verification reality:** web has no unit runner — pure ops (`frames.ts`) verified by `tsc` + browser (T6); the equivalent pure ops are TDD'd headlessly in JUCE `engine_test` via `FrameOps.h` (T7), the meaningful correctness gate, and mirror the web `frames.ts` 1:1.
- **Type/name consistency:** web `framePoints/duplicateAt/deleteAt/moveFrame/framesFromWave`; JUCE `fable::framePoints/duplicateFrame/deleteFrame/moveFrame/framesFromWave`; `loadFrames/gotoFrame/syncCurrentFrame/addFrameOp/deleteFrameOp/reorderFrameOp`; `frames`/`currentFrame`(JUCE)/`current`(web). `DrawPad::onEdit`, `TablePreview::setCurrent`, `FrameStrip::{onSelect,onAdd,onDelete,onReorder}` consistent across declaring/using tasks.
- **Known soft spots (implementer, verify in T6/T10):** (1) the web `key={frameVersion}` on StackPreview/FrameStrip forces remount on each frame change — fine for correctness, watch for input-focus loss; if janky, drop the key and rely on prop changes. (2) JUCE FrameStrip delete-affordance is a hit-test corner of the current cell (no separate button) — verify the 12px target feels right; enlarge if fiddly. (3) `createFromAudio` now loads frames instead of committing (button relabeled "LOAD FRAMES"); the audio `TablePreview` in JUCE still previews pre-load — that's fine.
```
