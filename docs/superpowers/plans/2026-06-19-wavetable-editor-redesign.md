# Wavetable Editor Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the user-wavetable editor in the web app and the JUCE plugin to match the Claude Design mockup — a library manager (factory + user tables, search, thumbnails, rename/duplicate/delete) alongside the existing draw/import editor, with the mockup's richer draw tools (seed shapes, pen/smooth brush, snap).

**Architecture:** Web-first then JUCE port (the established direction — every JUCE file is a documented port of its `src/*.tsx` counterpart). Shared palette/typography already exist on both sides (`Theme.h` ⇆ `src/index.css :root`), so this is structural. The editor keeps the existing "open for one osc" launch model (fixed assignment target, no header dropdown). Selecting/creating a table assigns it to that osc. Factory tables are read-only and duplicated-to-edit; the single-cycle draw pad only auto-opens for 1-frame user tables.

**Tech Stack:** TypeScript + React + Zustand + Vite (web); C++17 + JUCE (plugin); JUCE hand-rolled `check()` test harness via CTest.

**Reference:** `design-handoff/synthesizer-mockup-design/project/Wavetable Editor.dc.html` is the pixel reference (inline styles are the source of truth for spacing/colour). Design spec: `docs/superpowers/specs/2026-06-19-wavetable-editor-redesign-design.md`.

**Accent note:** Use the app's per-osc accent (`ACCENTS.a` cyan / `ACCENTS.b` amber; JUCE `col::acA` / `col::acB`) everywhere the mockup hardcodes `#4de8ff`.

---

## Phase 1 — Shared logic: factory-frame extraction + web store CRUD

### Task 1: Extract source frames from a GeneratedTable (web)

Duplicating a factory table needs its raw single-cycle frames. They live in `GeneratedTable.data` at mip 0: frame `f` occupies `data[(f*MIPS + 0)*SIZE .. +SIZE]` (full-band, normalized, SIZE samples).

**Files:**
- Modify: `src/engine/usertables.ts` (add `framesFromGenerated`)

- [ ] **Step 1: Add the extractor**

In `src/engine/usertables.ts`, update the import line and append the function:

```typescript
// change existing import to pull MIPS too:
import { SIZE, MIPS, buildUserTable, type GeneratedTable } from './wavetables';
```

```typescript
// ---------- factory -> editable copy ----------
// Pull a GeneratedTable's source single-cycle frames (mip-0, full-band) back
// out as SIZE-sample Float32Arrays, so a factory table can be duplicated into
// an editable user table that re-band-limits identically via makeUserTable.
export function framesFromGenerated(t: GeneratedTable): Float32Array[] {
  const frames: Float32Array[] = [];
  for (let f = 0; f < t.frames; f++) {
    const off = (f * MIPS + 0) * SIZE;
    frames.push(t.data.slice(off, off + SIZE));
  }
  return frames;
}
```

- [ ] **Step 2: Verify it typechecks**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/engine/usertables.ts
git commit -m "feat(web): extract source frames from a GeneratedTable for factory duplicate"
```

### Task 2: Store — rename + duplicate user tables, duplicate factory table

**Files:**
- Modify: `src/store.ts` (interface + 3 actions)
- Reference: `src/store.ts:121-142` (existing add/delete pattern), `src/engine/wavetables.ts:generateTables`

The store must expose: rename a user table, duplicate a user table, and duplicate a **factory** table (sourced from `generateTables()`). Factory tables are generated on demand (the engine already holds them; we regenerate for the editable-frame source — cheap, done only on a duplicate click).

- [ ] **Step 1: Add imports and a memoized factory accessor**

At the top of `src/store.ts`, extend the usertables import and add a wavetables import:

```typescript
import {
  loadUserTablePool, saveUserTablePool, serializeUserTable, deserializeUserTable,
  makeUserTable, framesFromGenerated, type UserTable,
} from './engine/usertables';
import { generateTables, type GeneratedTable } from './engine/wavetables';
```

Add a module-level lazy cache (factory tables are deterministic) above `export const useStore`:

```typescript
// Factory tables regenerated once for the editor's library/duplicate needs.
let factoryTablesCache: GeneratedTable[] | null = null;
export function factoryTables(): GeneratedTable[] {
  if (!factoryTablesCache) factoryTablesCache = generateTables();
  return factoryTablesCache;
}
```

- [ ] **Step 2: Add the three actions to the `SynthStore` interface**

In the interface (after `deleteUserTable` at `src/store.ts:51`):

```typescript
  renameUserTable: (poolIndex: number, name: string) => void;
  duplicateUserTable: (poolIndex: number) => void;
  duplicateFactoryTable: (factoryIndex: number) => void;
```

- [ ] **Step 3: Implement the actions**

After the `deleteUserTable` implementation (`src/store.ts:142`), insert:

```typescript
  renameUserTable: (poolIndex, name) => {
    const nm = (name.trim().toUpperCase() || 'USER').slice(0, 14);
    const userTables = get().userTables.map((t, i) => (i === poolIndex ? { ...t, name: nm } : t));
    saveUserTablePool(userTables);
    set({ userTables });
  },

  duplicateUserTable: (poolIndex) => {
    const src = get().userTables[poolIndex];
    if (!src) return;
    const frames: Float32Array[] = [];
    for (let f = 0; f < src.frames; f++) frames.push(src.wave.slice(f * SIZE, (f + 1) * SIZE));
    const copy = makeUserTable((src.name + ' COPY').slice(0, 14), frames);
    get().addUserTable(copy); // appends, persists, re-pushes engine, assigns to osc
  },

  duplicateFactoryTable: (factoryIndex) => {
    const ft = factoryTables()[factoryIndex];
    if (!ft) return;
    const copy = makeUserTable((ft.name + ' COPY').slice(0, 14), framesFromGenerated(ft));
    get().addUserTable(copy);
  },
```

Add the `SIZE` import (used by `duplicateUserTable`) to the wavetables import line:

```typescript
import { generateTables, SIZE, type GeneratedTable } from './engine/wavetables';
```

- [ ] **Step 4: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add src/store.ts
git commit -m "feat(web): rename + duplicate user/factory tables in store"
```

---

## Phase 2 — Web UI: library sidebar, draw tools, layout

### Task 3: Draw-pad model — seed shapes, brush, snap (logic only)

Extract pure helpers so the canvas component stays thin and the math is reusable by JUCE later.

**Files:**
- Create: `src/components/wavetable/drawmodel.ts`

- [ ] **Step 1: Create the draw model**

```typescript
// Pure helpers for the draw pad: seed shapes, snap quantization, and the
// smooth brush. DRAW_N points in [-1, 1], one cycle left->right.
export const DRAW_N = 256;
export type Seed = 'sine' | 'saw' | 'square' | 'tri';
export type Brush = 'pen' | 'smooth';

export function seedShape(kind: Seed): number[] {
  const out = new Array(DRAW_N).fill(0);
  for (let i = 0; i < DRAW_N; i++) {
    const x = i / DRAW_N;
    if (kind === 'sine') out[i] = Math.sin(2 * Math.PI * x);
    else if (kind === 'saw') out[i] = 2 * x - 1;
    else if (kind === 'square') out[i] = x < 0.5 ? 0.9 : -0.9;
    else out[i] = 1 - 4 * Math.abs(x - 0.5); // tri
  }
  return out;
}

// Quantize a value in [-1, 1] to the nearest 1/8 step when snap is on.
export function snapValue(v: number, snap: boolean): number {
  return snap ? Math.round(v * 8) / 8 : v;
}

// 5-tap moving average around `idx` (radius `rad`), mutating `pts` in place.
export function smoothAround(pts: number[], idx: number, rad = 7): void {
  const src = pts.slice();
  const n = pts.length;
  for (let i = Math.max(0, idx - rad); i <= Math.min(n - 1, idx + rad); i++) {
    let s = 0, cnt = 0;
    for (let j = -2; j <= 2; j++) {
      const k = i + j;
      if (k >= 0 && k < n) { s += src[k]; cnt++; }
    }
    pts[i] = s / cnt;
  }
}
```

- [ ] **Step 2: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/wavetable/drawmodel.ts
git commit -m "feat(web): pure draw-pad model (seed shapes, snap, smooth)"
```

### Task 4: Thumbnail component (web)

A tiny canvas drawing a table's frame-0 waveform from `GeneratedTable.viz` (first `VIZ_N` points).

**Files:**
- Create: `src/components/wavetable/TableThumb.tsx`
- Reference: `src/engine/wavetables.ts` (`VIZ_N = 128`), `src/components/displays/canvas.ts` (`setupCanvas`)

- [ ] **Step 1: Create the component**

```tsx
import { useEffect, useRef } from 'react';
import { setupCanvas } from '../displays/canvas';
import { VIZ_N, type GeneratedTable } from '../../engine/wavetables';

export function TableThumb({ table, accent, selected }: {
  table: GeneratedTable; accent: string; selected: boolean;
}) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const c = ref.current;
    if (!c) return;
    const { ctx, w, h } = setupCanvas(c);
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.1)';
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2); ctx.stroke();
    ctx.beginPath();
    for (let i = 0; i < VIZ_N; i++) {
      const x = (i / (VIZ_N - 1)) * w;
      const y = h / 2 - table.viz[i] * h * 0.38;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = selected ? accent : '#8893a8';
    ctx.lineWidth = 1.3;
    if (selected) { ctx.shadowColor = accent; ctx.shadowBlur = 6; }
    ctx.stroke();
  }, [table, accent, selected]);
  return <canvas ref={ref} className="wte-thumb" width={46} height={28} />;
}
```

- [ ] **Step 2: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/wavetable/TableThumb.tsx
git commit -m "feat(web): wavetable library thumbnail component"
```

### Task 5: Library sidebar component (web)

Renders FACTORY + USER sections, search, `+ NEW`, per-row thumbnail + actions, inline rename.

**Files:**
- Create: `src/components/wavetable/TableLibrary.tsx`
- Reference: mockup `Wavetable Editor.dc.html:42-89` (library markup), `src/store.ts` (`factoryTables`, `userTables`, the new actions)

- [ ] **Step 1: Create the component**

```tsx
import { useState } from 'react';
import { factoryTables } from '../../store';
import { TableThumb } from './TableThumb';
import type { UserTable } from '../../engine/usertables';

// id scheme: 'f<i>' factory, 'u<i>' user — matches the osc .table selector space.
export function TableLibrary({
  userTables, selectedId, accent,
  onSelectFactory, onSelectUser, onNew,
  onRename, onDuplicateUser, onDuplicateFactory, onDelete,
}: {
  userTables: UserTable[];
  selectedId: string | null;
  accent: string;
  onSelectFactory: (i: number) => void;
  onSelectUser: (i: number) => void;
  onNew: () => void;
  onRename: (i: number, name: string) => void;
  onDuplicateUser: (i: number) => void;
  onDuplicateFactory: (i: number) => void;
  onDelete: (i: number) => void;
}) {
  const [query, setQuery] = useState('');
  const [renameIdx, setRenameIdx] = useState<number | null>(null);
  const [renameVal, setRenameVal] = useState('');
  const factory = factoryTables();
  const q = query.trim().toUpperCase();
  const matches = (name: string) => !q || name.toUpperCase().includes(q);

  const commitRename = () => {
    if (renameIdx !== null) onRename(renameIdx, renameVal);
    setRenameIdx(null);
  };

  return (
    <aside className="wte-lib">
      <div className="wte-lib-head">
        <span className="wte-lib-title">LIBRARY</span>
        <span className="wte-lib-count">{factory.length + userTables.length} TABLES</span>
        <button className="wte-new" onClick={onNew}>＋ NEW</button>
      </div>
      <div className="wte-search">
        <input placeholder="search tables" value={query} onChange={(e) => setQuery(e.target.value)} />
      </div>
      <div className="wte-lib-scroll">
        <div className="wte-lib-section">FACTORY</div>
        {factory.map((t, i) => matches(t.name) && (
          <div key={'f' + i}
               className={'wte-lib-row' + (selectedId === 'f' + i ? ' on' : '')}
               onClick={() => onSelectFactory(i)}>
            <TableThumb table={t} accent={accent} selected={selectedId === 'f' + i} />
            <div className="wte-lib-info">
              <div className="wte-lib-name">{t.name}</div>
              <div className="wte-lib-sub">{t.frames}f · FACTORY</div>
            </div>
            <button title="duplicate" className="wte-lib-btn"
                    onClick={(e) => { e.stopPropagation(); onDuplicateFactory(i); }}>⎘</button>
          </div>
        ))}
        <div className="wte-lib-section">USER</div>
        {userTables.map((t, i) => matches(t.name) && (
          <div key={'u' + i}
               className={'wte-lib-row' + (selectedId === 'u' + i ? ' on' : '')}
               onClick={() => onSelectUser(i)}>
            <TableThumb table={t.table} accent={accent} selected={selectedId === 'u' + i} />
            <div className="wte-lib-info">
              {renameIdx === i ? (
                <input autoFocus className="wte-rename" value={renameVal}
                       onClick={(e) => e.stopPropagation()}
                       onChange={(e) => setRenameVal(e.target.value)}
                       onBlur={commitRename}
                       onKeyDown={(e) => { if (e.key === 'Enter') commitRename(); if (e.key === 'Escape') setRenameIdx(null); }} />
              ) : (
                <>
                  <div className="wte-lib-name">{t.name}</div>
                  <div className="wte-lib-sub">{t.frames}f</div>
                </>
              )}
            </div>
            <div className="wte-lib-actions">
              <button title="rename" className="wte-lib-btn"
                      onClick={(e) => { e.stopPropagation(); setRenameIdx(i); setRenameVal(t.name); }}>✎</button>
              <button title="duplicate" className="wte-lib-btn"
                      onClick={(e) => { e.stopPropagation(); onDuplicateUser(i); }}>⎘</button>
              <button title="delete" className="wte-lib-btn wte-lib-del"
                      onClick={(e) => { e.stopPropagation(); onDelete(i); }}>✕</button>
            </div>
          </div>
        ))}
      </div>
    </aside>
  );
}
```

- [ ] **Step 2: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/wavetable/TableLibrary.tsx
git commit -m "feat(web): wavetable library sidebar (factory/user, search, CRUD)"
```

### Task 6: Rewire WavetableEditor — library + working-table + draw tools

Integrate the library, the working-table concept (assign + load), draw tools, and the new layout into `src/components/WavetableEditor.tsx`.

**Files:**
- Modify: `src/components/WavetableEditor.tsx` (substantial rewrite of the body)
- Reference: existing file `src/components/WavetableEditor.tsx:1-226`, mockup `Wavetable Editor.dc.html`, `src/store.ts`

- [ ] **Step 1: Add working-table state and selection wiring**

Replace the state block (`src/components/WavetableEditor.tsx:34-45`) with:

```tsx
  const [tab, setTab] = useState<Tab>('draw');
  const [name, setName] = useState('');
  const [mode, setMode] = useState<AudioMode>('single');
  const [fixedLen, setFixedLen] = useState(2048);
  const [audio, setAudio] = useState<{ samples: Float32Array; sr: number; label: string } | null>(null);
  const [status, setStatus] = useState('');
  // working table: which library entry is loaded/assigned. 'u<i>' is editable
  // (1-frame only); 'f<i>' and multi-frame user tables load read-only.
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [seed, setSeed] = useState<Seed | null>(null); // unused holder; seeds applied directly
  const [brush, setBrush] = useState<Brush>('pen');
  const [snap, setSnap] = useState(false);

  const drawRef = useRef<HTMLCanvasElement>(null);
  const pointsRef = useRef<number[]>(new Array(DRAW_N).fill(0));
  const drawingRef = useRef(false);
  const lastIdxRef = useRef(-1);
  const [drawVersion, setDrawVersion] = useState(0);
  const [readOnly, setReadOnly] = useState(false); // pad locked (multi-frame/factory)
```

Update imports at the top of the file:

```tsx
import { useStore, engine, factoryTables } from '../store';
import { TABLE_NAMES } from '../params';
import { TableLibrary } from './wavetable/TableLibrary';
import { DRAW_N, seedShape, snapValue, smoothAround, type Seed, type Brush } from './wavetable/drawmodel';
```

Remove the old local `const DRAW_N = 256;` (now imported) and pull the new store actions:

```tsx
  const renameUserTable = useStore((s) => s.renameUserTable);
  const duplicateUserTable = useStore((s) => s.duplicateUserTable);
  const duplicateFactoryTable = useStore((s) => s.duplicateFactoryTable);
  const setParam = useStore((s) => s.setParam);
```

- [ ] **Step 2: Add select/assign handlers**

Add inside the component (after the existing handlers, before the return). Assignment writes the osc `.table` param using the combined index space:

```tsx
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
    const editable = ut.frames === 1;
    setReadOnly(!editable);
    setTab('draw');
    if (editable) loadPoints(ut.wave, 1);
    assign(TABLE_NAMES.length + i);
  };
```

- [ ] **Step 3: Replace the create logic to support UPDATE of a 1-frame user table**

Replace `createFromDraw` (`src/components/WavetableEditor.tsx:115-117`) with:

```tsx
  const createFromDraw = () => {
    const frame = frameFromDrawing(pointsRef.current);
    const u = makeUserTable(finalName, [frame]);
    if (selectedId && selectedId[0] === 'u' && !readOnly) {
      const idx = +selectedId.slice(1);
      // UPDATE in place: replace then keep assignment.
      deleteUserTable(idx);
      addUserTable(u); // appended at end; addUserTable assigns to osc
    } else {
      commit(u);
    }
  };
```

(`makeUserTable` and `frameFromDrawing` are already imported at the top of the file; `deleteUserTable`/`addUserTable` come from the store hooks already in the component.)

- [ ] **Step 4: Apply seed/brush/snap to the pad**

In the `paint` function (`src/components/WavetableEditor.tsx:120-141`), guard read-only and apply snap + smooth. Replace the body of `paint` with:

```tsx
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
```

Update the open-reset effect (`src/components/WavetableEditor.tsx:48-54`) to also reset the new state:

```tsx
  useEffect(() => {
    if (editorOsc) {
      setTab('draw'); setName(''); setMode('single'); setAudio(null); setStatus('');
      setSelectedId(null); setReadOnly(false); setBrush('pen'); setSnap(false);
      pointsRef.current = new Array(DRAW_N).fill(0);
      setDrawVersion((v) => v + 1);
    }
  }, [editorOsc]);
```

- [ ] **Step 5: Replace the JSX body with the library + editor layout**

Replace the `return (...)` block (`src/components/WavetableEditor.tsx:150-225`) with the two-column layout. Tabs reorder to DRAW first (mockup), draw pane gets the seed/brush/snap row, and the old `wte-list` is removed (the library replaces it):

```tsx
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
            onNew={() => { applySeed('sine'); setName(''); }}
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
```

Add an accent-colour local near the top of the component body (after the hooks):

```tsx
  const accentColor = ACCENTS[editorOsc === 'oscB' ? 'b' : 'a'];
```

(`ACCENTS` is already imported. `userTables`, `addUserTable`, `deleteUserTable`, `closeEditor` are existing hooks.)

- [ ] **Step 6: Verify typecheck**

Run: `npx tsc --noEmit`
Expected: no errors. (Remove the now-unused `seed`/`setSeed` holder from Step 1 if `tsc` flags it as unused under `noUnusedLocals`.)

- [ ] **Step 7: Commit**

```bash
git add src/components/WavetableEditor.tsx
git commit -m "feat(web): library + working-table + draw tools in wavetable editor"
```

### Task 7: CSS — library layout, draw tools, scanline

**Files:**
- Modify: `src/index.css` (the `/* wavetable editor */` block, `src/index.css:661-774`+)
- Reference: mockup `Wavetable Editor.dc.html` inline styles for exact values

- [ ] **Step 1: Widen the modal and add the two-column grid + scanline**

Replace the `.wte` rule (`src/index.css:672-682`) and add new rules. Keep the existing `--ac` accent variable (set by `data-accent`). New/changed rules:

```css
.wte {
  position: relative;
  width: 1180px;
  max-width: calc(100vw - 32px);
  height: min(760px, calc(100vh - 48px));
  display: flex;
  flex-direction: column;
  overflow: hidden;
  padding: 0;
  border: 1px solid var(--line);
  border-radius: 14px;
  background: linear-gradient(180deg, var(--panel-hi), var(--panel-lo));
  box-shadow: 0 30px 80px rgba(0, 0, 0, 0.6), 0 0 0 1px color-mix(in srgb, var(--ac) 22%, transparent);
}
.wte-scan {
  position: absolute; top: 0; left: 0; right: 0; height: 2px; z-index: 5;
  background: linear-gradient(90deg, transparent, var(--ac), transparent);
  animation: wte-scan 3.6s ease-in-out infinite;
}
@keyframes wte-scan { 0%, 100% { opacity: .5 } 50% { opacity: 1 } }
.wte-head { display: flex; align-items: center; padding: 15px 18px; border-bottom: 1px solid var(--line); margin: 0; }
.wte-cols { flex: 1; display: flex; min-height: 0; }
.wte-editor { flex: 1; display: flex; flex-direction: column; min-width: 0; padding: 16px 18px 18px; }
```

- [ ] **Step 2: Library sidebar styles**

Append:

```css
.wte-lib { width: 306px; flex: none; display: flex; flex-direction: column;
  border-right: 1px solid var(--line); background: rgba(0,0,0,0.18); }
.wte-lib-head { display: flex; align-items: center; gap: 8px; padding: 14px 16px 10px; }
.wte-lib-title { font-family: var(--font-disp); font-size: 10px; letter-spacing: 0.2em; color: var(--text-dim); }
.wte-lib-count { font-size: 9px; letter-spacing: 0.12em; color: #5b6679; }
.wte-new { margin-left: auto; height: 26px; padding: 0 11px; border: 1px solid color-mix(in srgb, var(--ac) 40%, transparent);
  border-radius: 7px; background: color-mix(in srgb, var(--ac) 12%, transparent); color: var(--ac);
  font-family: var(--font-mono); font-size: 10px; letter-spacing: 0.1em; cursor: pointer; }
.wte-search { padding: 0 16px 10px; }
.wte-search input { width: 100%; height: 30px; box-sizing: border-box; border: 1px solid var(--line);
  border-radius: 7px; background: #07090e; color: var(--text); font-family: var(--font-mono); font-size: 11px; padding: 0 10px; }
.wte-lib-scroll { flex: 1; overflow-y: auto; padding: 0 10px 14px; }
.wte-lib-section { padding: 12px 6px 6px; font-size: 9px; letter-spacing: 0.22em; color: #5b6679; }
.wte-lib-row { display: flex; align-items: center; gap: 9px; padding: 7px 8px; border-radius: 8px;
  margin-bottom: 4px; cursor: pointer; border: 1px solid transparent; }
.wte-lib-row.on { border-color: color-mix(in srgb, var(--ac) 45%, transparent);
  background: color-mix(in srgb, var(--ac) 8%, transparent); box-shadow: 0 0 16px color-mix(in srgb, var(--ac) 12%, transparent); }
.wte-thumb { width: 46px; height: 28px; flex: none; border: 1px solid var(--line); border-radius: 5px; background: #07090e; }
.wte-lib-info { flex: 1; min-width: 0; }
.wte-lib-name { font-size: 11px; color: var(--text); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.wte-lib-sub { font-size: 9px; letter-spacing: 0.1em; color: var(--text-dim); margin-top: 2px; }
.wte-lib-actions { display: flex; gap: 4px; flex: none; }
.wte-lib-btn { width: 22px; height: 22px; border: 1px solid var(--line); border-radius: 5px;
  background: #11141c; color: var(--text-dim); font-family: var(--font-mono); font-size: 11px; cursor: pointer; }
.wte-lib-btn:hover { color: var(--ac); border-color: var(--ac); }
.wte-lib-del:hover { color: #c46b6b; border-color: rgba(255,90,90,0.4); }
.wte-rename { width: 100%; height: 22px; box-sizing: border-box; padding: 0 6px; border: 1px solid color-mix(in srgb, var(--ac) 40%, transparent);
  border-radius: 4px; background: #0a0d13; color: var(--ac); font-family: var(--font-mono); font-size: 11px; }
```

- [ ] **Step 3: Draw-tools row + read-only pad + flex draw**

Append (and note the draw pad now flexes to fill the editor column):

```css
.wte-draw { flex: 1; min-height: 0; height: auto; }
.wte-draw.ro { opacity: 0.5; cursor: not-allowed; }
.wte-tools { display: flex; align-items: center; gap: 8px; }
.wte-tools-label { font-size: 9px; letter-spacing: 0.16em; color: #5b6679; }
.wte-tools button { height: 30px; padding: 0 13px; border: 1px solid var(--line); border-radius: 7px;
  background: #11141c; color: var(--text-dim); font-family: var(--font-mono); font-size: 10px; letter-spacing: 0.1em; cursor: pointer; }
.wte-tools button.on { color: var(--ac); border-color: color-mix(in srgb, var(--ac) 45%, transparent);
  background: color-mix(in srgb, var(--ac) 14%, #11141c); }
.wte-sep { width: 1px; height: 22px; background: var(--line); margin: 0 4px; }
.wte-tools .wte-clear { margin-left: auto; }
.wte-tools .wte-create { width: auto; height: 34px; padding: 0 22px; }
.wte-editor .wte-body { display: flex; flex-direction: column; min-height: 0; flex: 1; }
```

- [ ] **Step 4: Verify the app builds**

Run: `npm run build`
Expected: `tsc` passes and Vite build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/index.css
git commit -m "feat(web): wavetable editor library + tools layout styles"
```

---

## Phase 3 — Web visual verification

### Task 8: Verify web editor against the mockup

**Files:** none (verification only).

- [ ] **Step 1: Run the dev server**

Run: `npm run dev` (background) and note the local URL.

- [ ] **Step 2: Drive it with the chrome-debug skill**

Use the `chrome-debug` skill to open the app, power on, open the wavetable editor for OSC A, and screenshot. Verify against `Wavetable Editor.dc.html`:
- Library sidebar shows FACTORY (PRIME/BLOOM/PULSE/VOX/CHIME/GLITCH) + USER with thumbnails.
- Cyan scanline animates along the top; accent is cyan for OSC A, amber for OSC B.
- DRAW tab: pad fills the column; SEED SINE/SAW/SQUARE/TRI fill the curve; PEN/SMOOTH/SNAP toggle; CLEAR + CREATE/UPDATE present.
- Clicking a factory row loads it read-only (pad dimmed, button CREATE); clicking a 1-frame user row loads editable (button UPDATE).
- Rename (✎ inline), duplicate (⎘), delete (✕) work and persist across reload.
- IMPORT AUDIO tab still imports + previews.

- [ ] **Step 3: Fix any visual/behavioral gaps**

Adjust CSS/TSX as needed, re-verify. Commit fixes:

```bash
git add -A && git commit -m "fix(web): wavetable editor polish from visual verification"
```

---

## Phase 4 — JUCE data model: factory frames + processor CRUD

### Task 9: Extract source frames from a GeneratedTable (C++) + test

**Files:**
- Modify: `juce/source/dsp/UserTables.h` (declare), `juce/source/dsp/UserTables.cpp` (define)
- Modify: `juce/test/engine_test.cpp` (add checks)

- [ ] **Step 1: Write the failing test**

In `juce/test/engine_test.cpp`, inside `main` near the other table checks, add:

```cpp
    {
        // framesFromGenerated round-trips a factory table's frame count and a
        // re-built copy is finite + non-silent.
        auto factory = generateTables();
        auto frames = framesFromGenerated(factory[0]);
        check((int)frames.size() == factory[0].frames, "framesFromGenerated frame count");
        check(frames[0].size() == (size_t)SIZE, "framesFromGenerated frame width");
        auto copy = makeUserTable("COPY", frames);
        check(copy.frames == factory[0].frames, "duplicated factory frame count");
        check(finite(copy.table.data) && peak(copy.table.data) > 0.1f, "duplicated factory audible");
    }
```

- [ ] **Step 2: Build to verify it fails**

Run: `cd juce/build && cmake --build . --target engine_test 2>&1 | tail -20`
Expected: compile error — `framesFromGenerated` not declared.

- [ ] **Step 3: Declare and implement `framesFromGenerated`**

In `juce/source/dsp/UserTables.h`, after the `userTableFromWave` declaration (line ~33), add:

```cpp
// Pull a GeneratedTable's source single-cycle frames (mip-0, full-band) back
// out as SIZE-sample frames — used to duplicate a factory table into an
// editable user table that re-band-limits identically via makeUserTable.
std::vector<std::vector<float>> framesFromGenerated(const GeneratedTable& t);
```

In `juce/source/dsp/UserTables.cpp`, add the definition (mirrors the web; `MIPS`/`SIZE` come from `Wavetables.h`):

```cpp
std::vector<std::vector<float>> framesFromGenerated(const GeneratedTable& t) {
    std::vector<std::vector<float>> frames;
    frames.reserve((size_t)t.frames);
    for (int f = 0; f < t.frames; ++f) {
        const int off = (f * t.mips + 0) * t.size;
        frames.emplace_back(t.data.begin() + off, t.data.begin() + off + t.size);
    }
    return frames;
}
```

- [ ] **Step 4: Build + run the test**

Run: `cd juce/build && cmake --build . --target engine_test && ctest -R engine_test --output-on-failure 2>&1 | tail -25`
Expected: the new `framesFromGenerated*` and `duplicated factory*` checks PASS.

- [ ] **Step 5: Commit**

```bash
git add juce/source/dsp/UserTables.h juce/source/dsp/UserTables.cpp juce/test/engine_test.cpp
git commit -m "feat(juce): framesFromGenerated for factory-table duplicate (+test)"
```

### Task 10: Processor — rename + duplicate (user + factory)

**Files:**
- Modify: `juce/source/PluginProcessor.h` (declare), `juce/source/PluginProcessor.cpp` (define)
- Reference: existing `addUserTable`/`deleteUserTable` (`juce/source/PluginProcessor.cpp:82-100`), `tables` member (factory `GeneratedTable`s)

- [ ] **Step 1: Declare the methods**

In `juce/source/PluginProcessor.h`, after `void deleteUserTable(int poolIndex);` (line ~61):

```cpp
    void renameUserTable(int poolIndex, std::string name);
    int  duplicateUserTable(int poolIndex);   // returns new combined index, or -1
    int  duplicateFactoryTable(int factoryIndex); // returns new combined index, or -1
    const std::vector<GeneratedTable>& factoryTables() const { return tables; }
```

(`GeneratedTable` and `tables` are already members/visible in the processor.)

- [ ] **Step 2: Implement them**

In `juce/source/PluginProcessor.cpp`, after `deleteUserTable` (line ~100), add. Duplicate reuses `addUserTable` (which rebuilds engine tables and returns the new index):

```cpp
void FableAudioProcessor::renameUserTable(int poolIndex, std::string name) {
    if (poolIndex < 0 || poolIndex >= (int)userTables.size()) return;
    juce::String s = juce::String(name).trim().toUpperCase();
    if (s.isEmpty()) s = "USER";
    userTables[(size_t)poolIndex].name = s.substring(0, 14).toStdString();
}

int FableAudioProcessor::duplicateUserTable(int poolIndex) {
    if (poolIndex < 0 || poolIndex >= (int)userTables.size()) return -1;
    const auto& src = userTables[(size_t)poolIndex];
    std::vector<std::vector<float>> frames;
    for (int f = 0; f < src.frames; ++f)
        frames.emplace_back(src.wave.begin() + (size_t)f * fable::SIZE,
                            src.wave.begin() + (size_t)(f + 1) * fable::SIZE);
    std::string nm = (src.name + " COPY").substr(0, 14);
    return addUserTable(fable::makeUserTable(nm, frames));
}

int FableAudioProcessor::duplicateFactoryTable(int factoryIndex) {
    if (factoryIndex < 0 || factoryIndex >= (int)tables.size()) return -1;
    auto frames = fable::framesFromGenerated(tables[(size_t)factoryIndex]);
    std::string nm = (tables[(size_t)factoryIndex].name + " COPY").substr(0, 14);
    return addUserTable(fable::makeUserTable(nm, frames));
}
```

(`fable::SIZE` is from `Wavetables.h`, already included transitively.)

- [ ] **Step 3: Build the plugin target**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | tail -20`
Expected: builds with no errors.

- [ ] **Step 4: Commit**

```bash
git add juce/source/PluginProcessor.h juce/source/PluginProcessor.cpp
git commit -m "feat(juce): processor rename + duplicate (user/factory) tables"
```

---

## Phase 5 — JUCE UI port

### Task 11: DrawPad — seed shapes, brush, snap, read-only

**Files:**
- Modify: `juce/source/ui/WavetableEditor.h` (DrawPad members), `juce/source/ui/WavetableEditor.cpp` (DrawPad methods)
- Reference: web `src/components/wavetable/drawmodel.ts` (identical math), existing `DrawPad` (`WavetableEditor.cpp:7-50`)

- [ ] **Step 1: Extend the DrawPad interface**

In `juce/source/ui/WavetableEditor.h`, replace the `DrawPad` public/private members to add seed/brush/snap/read-only:

```cpp
class DrawPad : public juce::Component {
public:
    static constexpr int DRAW_N = 256;
    enum class Brush { Pen, Smooth };
    explicit DrawPad(juce::Colour accent);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void clear();
    void setAccent(juce::Colour c) { accent = c; repaint(); }
    void setBrush(Brush b) { brush = b; }
    void setSnap(bool s) { snap = s; }
    void setReadOnly(bool ro) { readOnly = ro; repaint(); }
    void seed(int kind);                 // 0 sine 1 saw 2 square 3 tri
    void setPoints(const std::vector<float>& p); // load frame 0 (downsampled)
    const std::vector<float>& points() const { return pts; }
private:
    void paintAt(juce::Point<float>);
    void smoothAround(int idx, int rad = 7);
    std::vector<float> pts;
    int lastIdx = -1;
    juce::Colour accent;
    Brush brush = Brush::Pen;
    bool snap = false;
    bool readOnly = false;
};
```

- [ ] **Step 2: Implement the new methods + guards**

In `juce/source/ui/WavetableEditor.cpp`, replace `paintAt` and `mouseDown`/`mouseDrag` (lines 11-32) and add the new methods:

```cpp
void DrawPad::paintAt(juce::Point<float> p) {
    if (readOnly) return;
    const float w = (float)getWidth(), h = (float)getHeight();
    if (w <= 0 || h <= 0) return;
    int idx = juce::jlimit(0, DRAW_N - 1, (int)std::round((p.x / w) * (DRAW_N - 1)));
    float val = juce::jlimit(-1.0f, 1.0f, 1.0f - 2.0f * (p.y / h));
    if (snap) val = std::round(val * 8.0f) / 8.0f;
    if (lastIdx >= 0 && lastIdx != idx) {
        int lo = juce::jmin(lastIdx, idx), hi = juce::jmax(lastIdx, idx);
        float v0 = pts[lastIdx];
        for (int i = lo; i <= hi; ++i) {
            float t = (idx == lastIdx) ? 1.0f : (float)(i - lastIdx) / (idx - lastIdx);
            pts[i] = v0 + (val - v0) * t;
        }
    } else {
        pts[idx] = val;
    }
    if (brush == Brush::Smooth) smoothAround(idx);
    lastIdx = idx;
    repaint();
}
void DrawPad::mouseDown(const juce::MouseEvent& e) { if (readOnly) return; lastIdx = -1; paintAt(e.position); }
void DrawPad::mouseDrag(const juce::MouseEvent& e) { if (readOnly) return; paintAt(e.position); }

void DrawPad::smoothAround(int idx, int rad) {
    auto src = pts;
    const int n = (int)pts.size();
    for (int i = juce::jmax(0, idx - rad); i <= juce::jmin(n - 1, idx + rad); ++i) {
        float s = 0; int cnt = 0;
        for (int j = -2; j <= 2; ++j) { int k = i + j; if (k >= 0 && k < n) { s += src[(size_t)k]; ++cnt; } }
        pts[(size_t)i] = s / cnt;
    }
}

void DrawPad::seed(int kind) {
    for (int i = 0; i < DRAW_N; ++i) {
        const float x = (float)i / DRAW_N; float v = 0;
        if (kind == 0) v = std::sin(2.0f * juce::MathConstants<float>::pi * x);
        else if (kind == 1) v = 2 * x - 1;
        else if (kind == 2) v = x < 0.5f ? 0.9f : -0.9f;
        else v = 1 - 4 * std::abs(x - 0.5f);
        pts[(size_t)i] = v;
    }
    lastIdx = -1; repaint();
}

void DrawPad::setPoints(const std::vector<float>& p) {
    if (p.empty()) return;
    for (int i = 0; i < DRAW_N; ++i)
        pts[(size_t)i] = p[(size_t)juce::jlimit(0, (int)p.size() - 1, (int)((float)i / DRAW_N * (int)p.size()))];
    lastIdx = -1; repaint();
}
```

- [ ] **Step 3: Build**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | tail -20`
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add juce/source/ui/WavetableEditor.h juce/source/ui/WavetableEditor.cpp
git commit -m "feat(juce): DrawPad seed/brush/snap/read-only"
```

### Task 12: Library list rows with thumbnails + actions (JUCE)

**Files:**
- Modify: `juce/source/ui/WavetableEditor.h` (replace `Row`, add a thumbnail + list state), `juce/source/ui/WavetableEditor.cpp`
- Reference: existing `Row` (`WavetableEditor.cpp:91-105`), mockup library markup

- [ ] **Step 1: Add a thumbnail painter helper**

In `juce/source/ui/WavetableEditor.h`, add a small component above `WavetableEditor` (reuses `GeneratedTable.viz`):

```cpp
// Mini frame-0 waveform for a library row.
class TableThumb : public juce::Component {
public:
    void setData(const std::vector<float>& v, juce::Colour ac, bool sel) {
        viz = v; accent = ac; selected = sel; repaint();
    }
    void paint(juce::Graphics&) override;
private:
    std::vector<float> viz; juce::Colour accent; bool selected = false;
};
```

In `juce/source/ui/WavetableEditor.cpp`, implement (uses `fable::VIZ_N`):

```cpp
void TableThumb::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    drawDisplayBox(g, b, 4.0f);
    const int N = juce::jmin((int)viz.size(), fable::VIZ_N);
    if (N < 2) return;
    const float w = b.getWidth(), h = b.getHeight();
    juce::Path p;
    for (int i = 0; i < N; ++i) {
        float x = (float)i / (N - 1) * w;
        float y = h * 0.5f - viz[(size_t)i] * h * 0.38f;
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(selected ? accent : juce::Colour(0xff8893a8));
    g.strokePath(p, juce::PathStrokeType(1.2f));
}
```

- [ ] **Step 2: Replace `Row` with a library row (thumb + name + frames + buttons)**

In `juce/source/ui/WavetableEditor.h`, replace the existing `Row` struct and `listRows` member with a richer row plus separate factory/user state. The row carries optional rename/dup/delete buttons (factory rows omit rename/delete):

```cpp
    struct LibRow : public juce::Component {
        LibRow(bool factory, juce::Colour accent);
        void resized() override;
        TableThumb thumb;
        juce::Label name;
        juce::Label sub;       // "4f · FACTORY" or "1f"
        juce::TextButton rename{"✎"}, dup{"⎘"}, del{"✕"};
        bool isFactory;
    };
    juce::OwnedArray<LibRow> libRows;
    juce::TextEditor searchField;
    juce::Label libTitle{{}, "LIBRARY"};
    juce::TextButton newBtn{"+ NEW"};
    juce::String selectedId; // "f<i>" / "u<i>" / empty
```

- [ ] **Step 3: Implement the LibRow ctor/layout**

In `juce/source/ui/WavetableEditor.cpp`, replace the old `Row` ctor/`resized` with:

```cpp
WavetableEditor::LibRow::LibRow(bool factory, juce::Colour accent) : isFactory(factory) {
    addAndMakeVisible(thumb);
    name.setFont(monoFont(11.0f)); name.setColour(juce::Label::textColourId, col::text);
    addAndMakeVisible(name);
    sub.setFont(monoFont(9.0f)); sub.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(sub);
    for (auto* b : { &rename, &dup, &del }) {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b->setColour(juce::TextButton::textColourOffId, col::textDim);
    }
    addAndMakeVisible(dup);
    if (!factory) { addAndMakeVisible(rename); addAndMakeVisible(del); }
    juce::ignoreUnused(accent);
}
void WavetableEditor::LibRow::resized() {
    auto r = getLocalBounds();
    thumb.setBounds(r.removeFromLeft(46).reduced(0, 1));
    r.removeFromLeft(8);
    auto btns = r.removeFromRight(isFactory ? 24 : 72);
    if (!isFactory) {
        rename.setBounds(btns.removeFromLeft(22).reduced(1));
        btns.removeFromLeft(2);
    }
    dup.setBounds(btns.removeFromLeft(22).reduced(1));
    if (!isFactory) { btns.removeFromLeft(2); del.setBounds(btns.reduced(1)); }
    name.setBounds(r.removeFromTop(r.getHeight() / 2));
    sub.setBounds(r);
}
```

- [ ] **Step 4: Build**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | tail -20`
Expected: builds (the old `refreshList`/`Row` references will be updated in Task 13; if the build breaks on those, proceed to Task 13 before building).

- [ ] **Step 5: Commit**

```bash
git add juce/source/ui/WavetableEditor.h juce/source/ui/WavetableEditor.cpp
git commit -m "feat(juce): library row + thumbnail components"
```

### Task 13: WavetableEditor — wire library, selection, layout (JUCE)

**Files:**
- Modify: `juce/source/ui/WavetableEditor.h` (members + helpers), `juce/source/ui/WavetableEditor.cpp` (ctor, openFor, refreshList→refreshLibrary, layoutPanel, paint)
- Reference: web Task 6 (same behavior), existing layout (`WavetableEditor.cpp:356-454`)

- [ ] **Step 1: Add editor state + helper declarations**

In `juce/source/ui/WavetableEditor.h`, add to the private section:

```cpp
    void refreshLibrary();
    void selectFactory(int i);
    void selectUser(int i);
    void assignTable(int combinedIndex);
    void layoutLibrary(juce::Rectangle<int> area);
    bool readOnlySel = false;
    // seed/brush/snap tool buttons
    juce::TextButton seedSine{"SINE"}, seedSaw{"SAW"}, seedSquare{"SQUARE"}, seedTri{"TRI"};
    juce::TextButton brushPen{"PEN"}, brushSmooth{"SMOOTH"}, snapBtn{"SNAP"};
```

- [ ] **Step 2: Widen the panel + two-column layout**

Replace `panelBounds` (`WavetableEditor.cpp:356-360`) to scale toward the window, and split `layoutPanel` into library + editor columns. Replace `panelBounds`:

```cpp
juce::Rectangle<int> WavetableEditor::panelBounds() const {
    const int w = juce::jmin(1180, getWidth() - 40);
    const int h = juce::jmin(760, getHeight() - 40);
    return juce::Rectangle<int>(0, 0, w, h).withCentre(getLocalBounds().getCentre());
}
```

In `layoutPanel`, carve a 306px library column on the left before laying out the editor on the right:

```cpp
void WavetableEditor::layoutPanel() {
    auto panel = panelBounds().reduced(0);
    auto head = panel.removeFromTop(50);
    closeBtn.setBounds(head.removeFromRight(48).removeFromLeft(30).withSizeKeepingCentre(30, 30));
    auto lib = panel.removeFromLeft(306);
    layoutLibrary(lib.reduced(0));
    auto editor = panel.reduced(18);
    // ... existing tab/name/pane layout, operating on `editor` instead of `panel`
    // (keep the Task-12 draw/import layout; append the seed/brush/snap row under the pad)
}
```

(Lay out `seedSine/seedSaw/seedSquare/seedTri`, `brushPen/brushSmooth/snapBtn`, `clearBtn`, `createDrawBtn` in a row beneath the pad in the draw branch — mirror the web `.wte-tools` row; widths ~ equal buttons, CREATE pushed right.)

- [ ] **Step 3: Implement `layoutLibrary` + selection + refresh**

Add to `juce/source/ui/WavetableEditor.cpp`:

```cpp
void WavetableEditor::layoutLibrary(juce::Rectangle<int> area) {
    area = area.reduced(10, 8);
    auto top = area.removeFromTop(26);
    libTitle.setBounds(top.removeFromLeft(120));
    newBtn.setBounds(top.removeFromRight(70));
    area.removeFromTop(6);
    searchField.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    for (auto* row : libRows) {
        if (area.getHeight() < 40) break;
        row->setBounds(area.removeFromTop(42));
        area.removeFromTop(4);
    }
}

void WavetableEditor::assignTable(int combinedIndex) {
    if (auto* p = proc.apvts.getParameter(oscIndex == 0 ? "oscA.table" : "oscB.table"))
        p->setValueNotifyingHost(p->convertTo0to1((float)combinedIndex));
}

void WavetableEditor::selectFactory(int i) {
    const auto& fac = proc.factoryTables();
    if (i < 0 || i >= (int)fac.size()) return;
    selectedId = "f" + juce::String(i);
    nameField.setText(juce::String(fac[(size_t)i].name), juce::dontSendNotification);
    readOnlySel = true; drawPad.setReadOnly(true);
    drawPad.setPoints(fable::framesFromGenerated(fac[(size_t)i])[0]);
    setTab(Tab::Draw);
    assignTable(i);
    refreshLibrary();
}

void WavetableEditor::selectUser(int i) {
    const auto& pool = proc.getUserTables();
    if (i < 0 || i >= (int)pool.size()) return;
    selectedId = "u" + juce::String(i);
    nameField.setText(juce::String(pool[(size_t)i].name), juce::dontSendNotification);
    const bool editable = pool[(size_t)i].frames == 1;
    readOnlySel = !editable; drawPad.setReadOnly(!editable);
    if (editable) {
        std::vector<float> frame(pool[(size_t)i].wave.begin(), pool[(size_t)i].wave.begin() + fable::SIZE);
        drawPad.setPoints(frame);
    }
    setTab(Tab::Draw);
    assignTable((int)proc.factoryTables().size() + i);
    refreshLibrary();
}
```

Replace `refreshList` with `refreshLibrary` building factory + user rows, applying the search filter, wiring `onClick` for the row (selectFactory/selectUser), `dup` (duplicateFactoryTable/duplicateUserTable + refresh), `del` (deleteUserTable + refresh), and `rename` (swap name label to an editable field or use an `AlertWindow`-free inline `TextEditor`; minimal: prompt via the existing `nameField` flow — simplest is an inline `juce::TextEditor` swapped into the row). Set each row's `thumb.setData(viz, accent(), selectedId == id)`.

- [ ] **Step 4: Update ctor + openFor**

In the ctor, `addAndMakeVisible` the new controls (`libTitle`, `newBtn`, `searchField`, seed/brush/snap buttons) and wire their `onClick`:
- `seedSine.onClick = [this]{ drawPad.seed(0); selectedId = {}; readOnlySel = false; drawPad.setReadOnly(false); };` (and 1/2/3 for the others)
- `brushPen.onClick = [this]{ drawPad.setBrush(DrawPad::Brush::Pen); };` / `brushSmooth` → Smooth
- `snapBtn` toggles: keep a `bool snapOn` and `drawPad.setSnap(snapOn)`
- `newBtn.onClick = [this]{ drawPad.seed(0); selectedId = {}; readOnlySel = false; drawPad.setReadOnly(false); nameField.setText("", juce::dontSendNotification); setTab(Tab::Draw); };`
- `searchField.onTextChange = [this]{ refreshLibrary(); };`

In `openFor`, default `tab = Tab::Draw`, reset `selectedId`/`readOnlySel`, call `refreshLibrary()` instead of `refreshList()`.

Update `createFromDraw` to UPDATE in place when `selectedId` starts with 'u' and `!readOnlySel`: delete that pool index then `commit` (mirrors web).

- [ ] **Step 5: Update `paint` for the new header/scanline/title**

Keep the existing accent top edge as the scanline. Move the `WAVETABLE → OSC A` title into the new 50px header; draw the library/editor divider line at x = panel.x + 306. Remove the old single-column underline logic that referenced removed controls if any.

- [ ] **Step 6: Build the plugin**

Run: `cd juce/build && cmake --build . --target FableSynth 2>&1 | tail -30`
Expected: builds with no errors. Fix any dangling references to the removed `Row`/`refreshList`/`listHeader`.

- [ ] **Step 7: Commit**

```bash
git add juce/source/ui/WavetableEditor.h juce/source/ui/WavetableEditor.cpp
git commit -m "feat(juce): wire library, selection, two-column layout in editor"
```

---

## Phase 6 — Final verification

### Task 14: Full build + test sweep, manual JUCE check

**Files:** none (verification).

- [ ] **Step 1: Web build + lint**

Run: `npm run build`
Expected: passes.

- [ ] **Step 2: JUCE full build + tests**

Run: `cd juce/build && cmake --build . 2>&1 | tail -20 && ctest --output-on-failure 2>&1 | tail -20`
Expected: `engine_test` and `plugin_host_test` PASS.

- [ ] **Step 3: Manual JUCE smoke (use the `run` skill)**

Launch the plugin (standalone/AudioPluginHost), open the wavetable editor on OSC A and OSC B, confirm: library sidebar + thumbnails, scanline, seed/brush/snap, factory read-only + duplicate-to-edit, rename/duplicate/delete persist across reopen, accent matches osc. Screenshot for the record.

- [ ] **Step 4: Final commit / branch ready for PR**

```bash
git add -A && git commit -m "test: verify wavetable editor redesign (web + juce)" || true
```

---

## Self-review notes (author)

- **Spec coverage:** library sidebar (T5/T12), factory+user (T2/T5/T10/T12), thumbnails (T4/T12), search (T5/T13), rename (T2/T6/T10/T13), duplicate user+factory (T1/T2/T9/T10), seed/brush/snap (T3/T6/T11), import pane retained (T6/T13), per-osc accent (T6/T13), scanline+layout (T7/T13), single-cycle/multi-frame read-only rule (T6/T13), assign-on-select (T6/T13), persistence (store localStorage + processor state — existing paths reused). All spec sections map to tasks.
- **Verification reality:** web has no unit-test framework; web logic is thin store mutations verified by `tsc` + live browser (T8). Pure shared logic (factory-frame extraction, band-limit round-trip) is TDD'd in JUCE `engine_test` (T9), which is the meaningful correctness gate.
- **Type/name consistency:** `framesFromGenerated` (web + C++), `factoryTables()` (store + processor), `selectedId` `'f<i>'/'u<i>'`, `DRAW_N`, `readOnly`/`readOnlySel` used consistently across tasks.
- **Known soft spot:** JUCE inline rename (T13 Step 3) is described, not fully coded — implement as a `TextEditor` swapped into the row's name slot, committing on Enter/focus-loss via `proc.renameUserTable`. Flagged for the implementer.
