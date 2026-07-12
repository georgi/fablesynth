# SQ-4 Device Focus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Navigate from the SQ-4 session grid into each device's full UI: the sequencer minimizes to the focused scene row + a scene rail, and the rest of the viewport hosts the focused machine (DR-1 / BL-1 / WT-1) wired to the engine the rig already runs, with immediate in-place clip and patch editing.

**Architecture:** Spec at `docs/superpowers/specs/2026-07-12-sq4-device-focus-design.md`. Three layers of work, bottom-up: (1) a new `clipupdate` worklet message + `updateClip` engine/adapter method for phase-preserving hot swaps; (2) seq-store additions (`focus` state, `updateClipBytes`, `createClip`, `setTrackPatch`) with the session doc as source of truth; (3) hosted mode in the three device stores (swappable engine singleton + `attachHosted`) and a `DeviceView` bridge that mounts the existing device panels inside SQ-4.

**Key simplification vs. spec §4:** the clip payload layouts are byte-identical to the device stores' pattern buffers (`dr1Idx(bar,pad,step) = patIdx(pat,pad,step)` with bar↔pat; BL-1/WT-1 both `(bar*16+step)*3` = `stepOff(pat,step)`). So decode = copy clip bytes over an empty pattern buffer, encode = `patterns.slice(0, bars * bytesPerBar)`, and the device's `editPattern` IS the bar selector. And since focus is exclusive (one device visible at a time), the device stores stay **singletons with a swappable engine reference** — re-attached on focus change — which covers the two WT-1 tracks without an instantiable-store refactor.

**Tech Stack:** React 18, zustand 4, TypeScript 5.6, Vite 5, vitest 4. Plain-JS AudioWorklets (self-contained, no imports).

## Global Constraints

- No new dependencies.
- Standalone apps (`/drum/`, `/bass/`, `/app/`) stay byte-for-byte behavior-compatible: same protocols, same localStorage, same UI when not hosted.
- Worklets are self-contained plain JS (no imports/exports beyond Vite's loader convention); they must keep evaluating under the `new Function` test harnesses.
- Session persistence key stays `fable.session.v1`; all edits flow through the session doc.
- Hosted clip editing caps at 4 bars (`HOSTED_MAX_BARS` = device `NPATTERNS`); longer clips still play but are edit-locked in the hosted editor.
- The conductor owns transport: device-local play/stop/chain are inert in hosted mode.
- Run tests with `npx vitest run <file>`; typecheck + bundle with `npm run build`.
- Commit after every task (conventional commits, `feat(seq):` / `test(seq):` etc.).

---

### Task 1: Protocol helpers — `emptyClipBytes` + `HOSTED_MAX_BARS`

**Files:**
- Modify: `src/seq/protocol.ts` (after the `noteIdx` helper, ~line 27)
- Test: `src/seq/protocol.test.ts`

**Interfaces:**
- Consumes: existing `bytesPerBar`, `NOTE_STRIDE`, `MachineId` in `protocol.ts`.
- Produces: `emptyClipBytes(machine: MachineId, bars: number): Uint8Array` and `export const HOSTED_MAX_BARS = 4`. Used by Task 4 (`createClip`) and Task 8 (bridge decode).

- [ ] **Step 1: Write the failing test** — append to `src/seq/protocol.test.ts`:

```ts
describe('emptyClipBytes', () => {
  it('sizes DR1 clips at 256 bytes/bar, all zero', () => {
    const b = emptyClipBytes('DR1', 2);
    expect(b.length).toBe(2 * bytesPerBar('DR1'));
    expect(b.every((x) => x === 0)).toBe(true);
  });

  it('note machines default the oct byte to 1 (= oct 0) on every step', () => {
    for (const m of ['BL1', 'WT1'] as const) {
      const b = emptyClipBytes(m, 1);
      expect(b.length).toBe(bytesPerBar(m));
      for (let i = 0; i < b.length; i += 3) {
        expect([b[i], b[i + 1], b[i + 2]]).toEqual([0, 0, 1]);
      }
    }
  });
});
```

Add `emptyClipBytes` to the existing import from `./protocol` at the top of the test file.

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/seq/protocol.test.ts`
Expected: FAIL — `emptyClipBytes` is not exported.

- [ ] **Step 3: Implement** — in `src/seq/protocol.ts`, after `noteIdx`:

```ts
/** Hosted editor edits at most this many bars (= device NPATTERNS). */
export const HOSTED_MAX_BARS = 4;

/** A silent clip payload; note machines get the neutral oct byte (=1). */
export function emptyClipBytes(machine: MachineId, bars: number): Uint8Array {
  const out = new Uint8Array(bars * bytesPerBar(machine));
  if (machine !== 'DR1') {
    for (let i = 2; i < out.length; i += NOTE_STRIDE) out[i] = 1;
  }
  return out;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx vitest run src/seq/protocol.test.ts` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/seq/protocol.ts src/seq/protocol.test.ts
git commit -m "feat(seq): empty-clip payload helper + hosted bar cap"
```

---

### Task 2: Worklet `clipupdate` message (all three machines)

**Files:**
- Modify: `src/drum/engine/worklet-drum.js` (message switch, after `case 'clipstop'` ~line 148)
- Modify: `src/bass/engine/worklet-bass.js` (same spot, after its `case 'clipstop'` ~line 146)
- Modify: `src/engine/worklet.js` (same spot, after its `case 'clipstop'` ~line 410)
- Test: `src/drum/engine/worklet-drum.test.ts`, `src/bass/engine/worklet-bass.test.ts`

**Interfaces:**
- Consumes: existing hosted clip state per worklet: `this.clip { data, bars }`, `this.clipPend { data, bars, at }`, `this.clipStep`, constant `STEPS` (=16, exists in all three).
- Produces: message `{ t: 'clipupdate', data: Uint8Array, bars: number }` — replaces the pattern of the **live** clip in place (play position preserved, `clipStep` re-wrapped modulo the new length) or of the **pending** clip (its `at` frame untouched). Ignored when neither exists. Handling pending in the worklet means the conductor never needs to re-stamp a queued launch (refines spec §5's "re-stamp" — this is race-free even if the boundary passes mid-flight).

- [ ] **Step 1: Write the failing tests** — append to `src/drum/engine/worklet-drum.test.ts`:

```ts
// The harness evaluates the worklet source with bare-global `currentFrame`
// resolving via globalThis, like the real AudioWorkletGlobalScope.
const g = globalThis as unknown as { currentFrame: number };

function runBlocks(h: DrumHarness, blocks: number): void {
  for (let b = 0; b < blocks; b++) {
    h.proc.process([], [[new Float32Array(128), new Float32Array(128)]]);
    g.currentFrame += 128;
  }
}

type ClipState = {
  clip: { data: Uint8Array; bars: number } | null;
  clipPend: { data: Uint8Array; bars: number; at: number } | null;
  clipStep: number;
};

describe('clipupdate (hosted hot-swap)', () => {
  it('swaps a live clip in place and preserves the play position', () => {
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 0 });
    // step = 60/120/4*48000 = 6000 frames; 50 blocks = 6400 frames → step 1 fired
    runBlocks(h, 50);
    const p = h.proc as unknown as ClipState;
    expect(p.clip).not.toBeNull();
    const before = p.clipStep;
    expect(before).toBeGreaterThanOrEqual(1);

    const next = new Uint8Array(2 * 256);
    next[0] = 1; // bar 0, pad 0, step 0
    h.send({ t: 'clipupdate', data: next, bars: 2 });
    expect(p.clip!.bars).toBe(2);
    expect(p.clip!.data[0]).toBe(1);
    expect(p.clipStep).toBe(before); // phase untouched

    runBlocks(h, 50); // keeps ticking: pos messages continue past the swap
    const poses = h.sent.filter((m) => m.t === 'pos');
    expect(poses.length).toBeGreaterThanOrEqual(3);
  });

  it('re-wraps clipStep when the clip shrinks below the current position', () => {
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(2 * 256), bars: 2, atFrame: 0 });
    runBlocks(h, 900); // ~19 steps in → clipStep in bar 1
    const p = h.proc as unknown as ClipState;
    expect(p.clipStep).toBeGreaterThanOrEqual(16);
    h.send({ t: 'clipupdate', data: new Uint8Array(256), bars: 1 });
    expect(p.clipStep).toBeLessThan(16);
  });

  it('updates a pending clip without touching its launch frame', () => {
    g.currentFrame = 0;
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 96000 });
    const p = h.proc as unknown as ClipState;
    const next = new Uint8Array(256);
    next[16] = 2; // pad 1, step 0
    h.send({ t: 'clipupdate', data: next, bars: 1 });
    expect(p.clipPend!.at).toBe(96000);
    expect(p.clipPend!.data[16]).toBe(2);
    expect(p.clip).toBeNull();
  });

  it('is a no-op when nothing is live or pending', () => {
    const h = makeDrumProcessor();
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'clipupdate', data: new Uint8Array(256), bars: 1 });
    const p = h.proc as unknown as ClipState;
    expect(p.clip).toBeNull();
    expect(p.clipPend).toBeNull();
  });
});
```

Import `DrumHarness` alongside `makeDrumProcessor` from `./workletHarness` if not already imported. Then append the equivalent suite to `src/bass/engine/worklet-bass.test.ts` using `makeBassProcessor` from `./bassHarness` — identical structure with note-machine sizes: 1 bar = `48` bytes (16 steps × 3), so `new Uint8Array(48)` / `new Uint8Array(2 * 48)`, and the "pad 1 step 0" probe becomes `next[0] = 1` (flags byte of step 0). BL-1's `clipStep` semantics are identical.

- [ ] **Step 2: Run tests to verify they fail**

Run: `npx vitest run src/drum/engine/worklet-drum.test.ts src/bass/engine/worklet-bass.test.ts`
Expected: FAIL — `clipupdate` messages ignored, `clip.bars` stays 1 / pend data unchanged.

- [ ] **Step 3: Implement in all three worklets** — add this case directly after `case 'clipstop':`'s `break;` in each of `worklet-drum.js`, `worklet-bass.js`, `worklet.js` (same code verbatim in all three; each file already defines `STEPS = 16` in scope):

```js
      case 'clipupdate': {
        // Hosted hot-swap (SQ-4): replace pattern bytes in place. Position is
        // derived arithmetic, so a live swap never moves the playhead.
        const data = new Uint8Array(d.data);
        const bars = Math.max(1, d.bars | 0);
        if (this.clipPend) {
          this.clipPend = { data, bars, at: this.clipPend.at };
        } else if (this.clip) {
          this.clip = { data, bars };
          if (this.clipStep >= 0) this.clipStep %= bars * STEPS;
        }
        break;
      }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/drum/engine/worklet-drum.test.ts src/bass/engine/worklet-bass.test.ts` — Expected: PASS.
Also run the full suite to catch worklet regressions: `npx vitest run src/drum src/bass src/engine` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/drum/engine/worklet-drum.js src/drum/engine/worklet-drum.test.ts \
        src/bass/engine/worklet-bass.js src/bass/engine/worklet-bass.test.ts \
        src/engine/worklet.js
git commit -m "feat(seq): clipupdate worklet message — phase-preserving hot swap"
```

(WT-1's worklet has no in-process harness; its `clipupdate` is the same verbatim block and is exercised end-to-end in Task 10's headless verify.)

---

### Task 3: Engine + adapter `updateClip`

**Files:**
- Modify: `src/drum/engine/drum-synth.ts` (hosted transport section, after `scheduleStop` ~line 391)
- Modify: `src/bass/engine/bass-synth.ts` (same section, after its `scheduleStop`)
- Modify: `src/engine/synth.ts` (same section, after its `scheduleStop` ~line 356)
- Modify: `src/seq/devices.ts` (interface + `EngineDevice`)
- Modify: `src/seq/store.test.ts` (`FakeDevice`)

**Interfaces:**
- Consumes: Task 2's `{ t: 'clipupdate' }` message.
- Produces: `updateClip(data: Uint8Array, bars: number): void` on `DrumEngine`, `BassEngine`, `SynthEngine`, and on the `SeqDevice` interface; plus `readonly engine` exposed on `SeqDevice` (optional) so Task 8 can hand the running engine to a device store. `FakeDevice` gains `updates: Array<{ bars: number; bytes: number }>` used by Task 4's tests.

- [ ] **Step 1: Add the engine method** — in each of the three engine classes, next to `scheduleStop` (match each file's local style; this is `drum-synth.ts`'s):

```ts
  updateClip(data: Uint8Array, bars: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'clipupdate', data, bars });
  }
```

- [ ] **Step 2: Extend the adapter** — in `src/seq/devices.ts`:

Add to the `SeqDevice` interface (after `scheduleStop`):

```ts
  updateClip(pattern: Uint8Array, bars: number): void;
  /** The live engine behind this device (absent on test fakes). */
  readonly engine?: DrumEngine | BassEngine | SynthEngine;
```

Add to `EngineDevice` (after `scheduleStop`):

```ts
  updateClip(pattern: Uint8Array, bars: number): void {
    this.engine.updateClip(pattern, bars);
  }
```

(`EngineDevice.engine` is already a public field, which satisfies the interface.)

- [ ] **Step 3: Update the fake** — in `src/seq/store.test.ts`, add to `FakeDevice`:

```ts
  updates: Array<{ bars: number; bytes: number }> = [];
```

and the method:

```ts
  updateClip(pattern: Uint8Array, bars: number): void {
    this.updates.push({ bars, bytes: pattern.length });
  }
```

- [ ] **Step 4: Verify it compiles and existing tests pass**

Run: `npm run build && npx vitest run src/seq` — Expected: PASS (no behavior change yet).

- [ ] **Step 5: Commit**

```bash
git add src/drum/engine/drum-synth.ts src/bass/engine/bass-synth.ts src/engine/synth.ts \
        src/seq/devices.ts src/seq/store.test.ts
git commit -m "feat(seq): updateClip on engines and the SeqDevice adapter"
```

---

### Task 4: Seq store — clip editing actions

**Files:**
- Modify: `src/seq/store.ts`
- Test: `src/seq/store.test.ts`

**Interfaces:**
- Consumes: `emptyClipBytes` (Task 1), `SeqDevice.updateClip` (Task 3), existing `bytesToB64`, `clipBytes` cache, `persist()`.
- Produces on `SeqStore`:
  - `updateClipBytes(s: number, t: number, bytes: Uint8Array, bars: number): void` — rewrites the clip in the session doc, refreshes the decode cache, persists, and hot-swaps the device iff that clip is live **or** queued on its track.
  - `createClip(s: number, t: number): void` — writes a silent 1-bar clip named `'NEW'` into an empty cell.
  - `setTrackPatch(t: number, patch: PatchDoc): void` — replaces `session.tracks[t].patch` and persists (no engine call — the engine already has the params; this is the persistence snapshot).

- [ ] **Step 1: Write the failing tests** — append to `src/seq/store.test.ts`:

```ts
describe('clip editing', () => {
  it('updateClipBytes rewrites the doc, cache and persists', () => {
    const before = st().session.scenes[0].clips[0]!;
    const bytes = new Uint8Array(2 * 256).fill(0);
    bytes[0] = 1;
    st().updateClipBytes(0, 0, bytes, 2);
    const clip = st().session.scenes[0].clips[0]!;
    expect(clip.bars).toBe(2);
    expect(clip.pattern).not.toBe(before.pattern);
    expect(clipPattern(st().session, 0, 0)![0]).toBe(1);
  });

  it('hot-swaps the device when the clip is live on its track', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(0); // ack → owner[0] = 0
    st().updateClipBytes(0, 0, new Uint8Array(256), 1);
    expect(rig.dev(0).updates).toEqual([{ bars: 1, bytes: 256 }]);
  });

  it('hot-swaps when the clip is queued, not yet live', () => {
    st().launch(0, 0); // queued, no ack yet
    st().updateClipBytes(0, 0, new Uint8Array(256), 1);
    expect(rig.dev(0).updates).toHaveLength(1);
  });

  it('sends nothing when the clip is idle or another scene owns the track', () => {
    st().launch(0, 1);
    rig.dev(0).onClipStart!(0); // scene 1 owns track 0
    st().updateClipBytes(0, 0, new Uint8Array(256), 1); // editing scene 0's clip
    expect(rig.dev(0).updates).toHaveLength(0);
  });

  it('createClip writes a silent 1-bar clip into an empty cell only', () => {
    const empty = st().session.scenes.findIndex((sc) => !sc.clips[1]);
    expect(empty).toBeGreaterThanOrEqual(0);
    st().createClip(empty, 1);
    const clip = st().session.scenes[empty].clips[1]!;
    expect(clip.bars).toBe(1);
    expect(clipPattern(st().session, empty, 1)!.length).toBe(48); // BL1: 16*3
    const again = st().session.scenes[empty].clips[1];
    st().createClip(empty, 1); // no overwrite
    expect(st().session.scenes[empty].clips[1]).toBe(again);
  });

  it('setTrackPatch swaps the patch doc in place', () => {
    st().setTrackPatch(0, { kind: 'inline', data: { params: { x: 1 } } });
    expect(st().session.tracks[0].patch).toEqual({ kind: 'inline', data: { params: { x: 1 } } });
    expect(st().session.tracks[1].patch.kind).toBe('factory');
  });
});
```

Add `clipPattern` to the store import at the top of the test file. Note: these tests assume the factory session's track 0 is DR-1 (256 bytes/bar) and track 1 is BL-1 (48 bytes/bar), and that scene-0/track-0 has a clip — check `src/seq/factory.ts` and adjust indices if the factory layout differs.

- [ ] **Step 2: Run tests to verify they fail**

Run: `npx vitest run src/seq/store.test.ts` — Expected: FAIL — actions missing.

- [ ] **Step 3: Implement** — in `src/seq/store.ts`:

Add to the `SeqStore` interface (after `togglePassThrough`):

```ts
  updateClipBytes: (s: number, t: number, bytes: Uint8Array, bars: number) => void;
  createClip: (s: number, t: number) => void;
  setTrackPatch: (t: number, patch: PatchDoc) => void;
```

Extend the protocol import with `bytesToB64, emptyClipBytes` and add `PatchDoc` to the type imports. Update the stale comment on the `clipBytes` map (it says "session docs are immutable in v1" — now they're edited in place; the cache is refreshed on every write). Add the actions (after `togglePassThrough`):

```ts
    updateClipBytes: (s, t, bytes, bars) => {
      const st = get();
      const clip = st.session.scenes[s]?.clips[t];
      if (!clip) return;
      const scenes = st.session.scenes.map((sc, i) => {
        if (i !== s) return sc;
        const clips = sc.clips.slice();
        clips[t] = { ...clip, bars, pattern: bytesToB64(bytes) };
        return { ...sc, clips };
      });
      set({ session: { ...st.session, scenes } });
      clipBytes.set(`${s}:${t}`, bytes);
      persist();
      // Live or pending on this track → hot-swap in the worklet. The worklet
      // updates whichever exists (clip or clipPend), so no re-stamping.
      if (st.rig && (st.owner[t] === s || st.queue[t] === s)) {
        st.rig.devices[t].updateClip(bytes, bars);
      }
    },

    createClip: (s, t) => {
      const st = get();
      if (!st.session.scenes[s] || st.session.scenes[s].clips[t]) return;
      const bytes = emptyClipBytes(st.session.tracks[t].machine, 1);
      const scenes = st.session.scenes.map((sc, i) => {
        if (i !== s) return sc;
        const clips = sc.clips.slice();
        clips[t] = { name: 'NEW', bars: 1, pattern: bytesToB64(bytes) };
        return { ...sc, clips };
      });
      set({ session: { ...st.session, scenes } });
      clipBytes.set(`${s}:${t}`, bytes);
      persist();
    },

    setTrackPatch: (t, patch) => {
      set((st) => ({
        session: {
          ...st.session,
          tracks: st.session.tracks.map((tr, i) => (i === t ? { ...tr, patch } : tr)),
        },
      }));
      persist();
    },
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/seq/store.test.ts` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/seq/store.ts src/seq/store.test.ts
git commit -m "feat(seq): clip editing actions — updateClipBytes, createClip, setTrackPatch"
```

---

### Task 5: Seq store — focus state

**Files:**
- Modify: `src/seq/store.ts`
- Test: `src/seq/store.test.ts`

**Interfaces:**
- Consumes: existing `owner` map, `session.scenes`.
- Produces on `SeqStore`:
  - `focus: { track: number; scene: number } | null` (null = session mode; not persisted)
  - `enterFocus(t: number, s?: number): void` — explicit `s` wins; else the scene owning track `t`; else the last focused scene; else 0. Result clamped to valid scenes.
  - `exitFocus(): void` — remembers the scene for the next `enterFocus`.
  - `focusScene(s: number): void` — moves the scene, clamped; no-op in session mode.

- [ ] **Step 1: Write the failing tests** — append to `src/seq/store.test.ts`:

```ts
describe('focus', () => {
  it('starts in session mode and enters on a track head', () => {
    expect(st().focus).toBeNull();
    st().enterFocus(2);
    expect(st().focus).toEqual({ track: 2, scene: 0 });
  });

  it('prefers the scene owning the track', () => {
    st().launch(1, 3);
    rig.dev(1).onClipStart!(0);
    st().enterFocus(1);
    expect(st().focus).toEqual({ track: 1, scene: 3 });
  });

  it('explicit scene wins (the ✎ path) and clamps to valid scenes', () => {
    st().enterFocus(0, 2);
    expect(st().focus).toEqual({ track: 0, scene: 2 });
    st().focusScene(99);
    expect(st().focus!.scene).toBe(st().session.scenes.length - 1);
    st().focusScene(-5);
    expect(st().focus!.scene).toBe(0);
  });

  it('remembers the last focused scene across exit/enter', () => {
    st().enterFocus(0, 2);
    st().exitFocus();
    expect(st().focus).toBeNull();
    st().enterFocus(3); // no owner on track 3, no explicit scene
    expect(st().focus).toEqual({ track: 3, scene: 2 });
  });

  it('switching heads keeps the scene', () => {
    st().enterFocus(0, 4);
    st().enterFocus(1);
    expect(st().focus).toEqual({ track: 1, scene: 4 });
  });
});
```

Note: the "switching heads keeps the scene" case works because `enterFocus` falls back to `focus?.scene` before `lastFocusScene` — see Step 3.

- [ ] **Step 2: Run tests to verify they fail**

Run: `npx vitest run src/seq/store.test.ts` — Expected: FAIL.

- [ ] **Step 3: Implement** — in `src/seq/store.ts`:

Interface additions:

```ts
  focus: { track: number; scene: number } | null;
  enterFocus: (t: number, s?: number) => void;
  exitFocus: () => void;
  focusScene: (s: number) => void;
```

Inside the `create` callback, next to `lastScheduled`:

```ts
  // Scene to reopen on the next head-click focus (survives exit, not reload).
  let lastFocusScene = 0;

  const clampScene = (s: number): number =>
    Math.max(0, Math.min(get().session.scenes.length - 1, s));
```

State init: `focus: null,` — and add `focus: null,` to `resetSeqStore` too. Actions:

```ts
    enterFocus: (t, s) => {
      const st = get();
      const scene = s ?? st.owner[t] ?? st.focus?.scene ?? lastFocusScene;
      set({ focus: { track: t, scene: clampScene(scene) } });
    },

    exitFocus: () => {
      const f = get().focus;
      if (f) lastFocusScene = f.scene;
      set({ focus: null });
    },

    focusScene: (s) => {
      const f = get().focus;
      if (f) set({ focus: { ...f, scene: clampScene(s) } });
    },
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/seq/store.test.ts` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/seq/store.ts src/seq/store.test.ts
git commit -m "feat(seq): focus state — enter/exit/scene navigation"
```

---

### Task 6: Device adapters apply inline patches

**Files:**
- Modify: `src/seq/devices.ts` (the three `applyPatch` implementations)

**Interfaces:**
- Consumes: `PatchDoc` `{ kind: 'inline'; data: unknown }`; the inline payload convention is `{ params: Record<string, number> }` (a raw engine-param snapshot — Task 8's bridge writes exactly this shape).
- Produces: session reload restores hosted patch edits. Limitations (documented in the spec): pad names and user wavetables are not part of the snapshot in v1.

- [ ] **Step 1: Implement** — in `src/seq/devices.ts`, add one shared helper above `Dr1Device`:

```ts
// Hosted patch edits snapshot raw engine params ({ params }) — docs spec
// 2026-07-12-sq4-device-focus-design.md §4. Pad names / user tables are v2.
function inlineParams(patch: PatchDoc): Record<string, number> | null {
  if (patch.kind !== 'inline') return null;
  const data = patch.data as { params?: Record<string, number> } | null;
  return data?.params ?? null;
}
```

Then in each device, replace the factory-only lookup. `Dr1Device.applyPatch` becomes:

```ts
  applyPatch(patch: PatchDoc): void {
    const inline = inlineParams(patch);
    if (inline) {
      this.applyParams(inline);
      return;
    }
    const index = patch.kind === 'factory' ? patch.index : 0;
    const kit = FACTORY_KITS[index] ?? FACTORY_KITS[0];
    this.applyParams(kitToState(kit).params);
  }
```

Apply the same three-line prologue to `Bl1Device.applyPatch` and `Wt1Device.applyPatch` (their factory fallbacks stay as-is). Delete the now-stale "v1: factory kits only" comment in `Dr1Device`.

- [ ] **Step 2: Verify build + suite**

Run: `npm run build && npx vitest run src/seq` — Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/seq/devices.ts
git commit -m "feat(seq): apply inline param-snapshot patches on session load"
```

---

### Task 7: Hosted mode in the three device stores

**Files:**
- Modify: `src/drum/store.ts`
- Modify: `src/bass/store.ts`
- Modify: `src/store.ts` (WT-1)
- Test: `src/drum/store.test.ts` (one hosted-mode suite; bass/WT-1 follow the identical pattern and are covered by the bridge + headless verify)

**Interfaces:**
- Consumes: the rig's live engine instances (via `SeqDevice.engine`, Task 3).
- Produces per store:
  - `hosted: boolean` (initial `false`) — UI chrome + persistence gate.
  - `attachHosted(engine)` — swaps the module engine singleton to the given instance, mirrors its current `params` into the store, forces `powered: true`, clears transport state. Engine singletons become `export let` (ES-module live bindings keep every importer current).
  - Hosted behavior changes: `play`/`stop` (drum, bass) and `seqPlay`/`seqStop` (WT-1) are no-ops; kit/patch/preset loads apply **params only** (never patterns/chain — those belong to the clip); WT-1's `_setPatterns` skips `saveSeqState` (don't pollute the standalone app's localStorage).

- [ ] **Step 1: Write the failing test** — append to `src/drum/store.test.ts` (match the file's existing setup conventions):

```ts
describe('hosted mode', () => {
  it('attachHosted swaps the engine singleton and mirrors its params', () => {
    const foreign = new DrumEngine();
    foreign.params = { 'pad0.oscA.table': 3 };
    useDrumStore.getState().attachHosted(foreign);
    const s = useDrumStore.getState();
    expect(s.hosted).toBe(true);
    expect(s.powered).toBe(true);
    expect(s.playing).toBe(false);
    expect(s.params['pad0.oscA.table']).toBe(3);
    expect(drumEngine).toBe(foreign);
  });

  it('play/stop are inert while hosted', () => {
    useDrumStore.getState().attachHosted(new DrumEngine());
    useDrumStore.getState().play();
    expect(useDrumStore.getState().playing).toBe(false);
  });

  it('kit loads apply params only — patterns stay untouched', () => {
    useDrumStore.getState().attachHosted(new DrumEngine());
    const before = useDrumStore.getState().patterns;
    useDrumStore.getState().loadKitByValue('f1');
    const s = useDrumStore.getState();
    expect(s.patterns).toBe(before);
    expect(s.kitValue).toBe('f1');
  });
});
```

Import `DrumEngine` and `drumEngine` in the test file. If `drum/store.test.ts` resets store state between tests, add `hosted: false` to that reset; if there is no store test file yet, create it with just this suite.

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/drum/store.test.ts` — Expected: FAIL.

- [ ] **Step 3: Implement drum store** — in `src/drum/store.ts`:

```ts
export let drumEngine = new DrumEngine();
```

(replaces `export const drumEngine = ...`). Interface additions:

```ts
  hosted: boolean;
  attachHosted: (engine: DrumEngine) => void;
```

State init `hosted: false,` and the action:

```ts
  // SQ-4 hosted mode: adopt the rig's running engine. The conductor owns the
  // transport and the clip owns the patterns; this store keeps patch editing.
  attachHosted: (engine) => {
    drumEngine = engine;
    set({
      hosted: true,
      powered: true,
      playing: false,
      curStep: -1,
      chaining: false,
      chainFresh: false,
      params: { ...engine.params },
    });
  },
```

Guard the transport:

```ts
  play: () => {
    if (get().hosted) return;
    drumEngine.play();
    set({ playing: true });
  },

  stop: () => {
    if (get().hosted) return;
    drumEngine.stop();
    set({ playing: false, curStep: -1 });
  },
```

Gate `loadKitByValue`: after the `if (!kit) return;` and `const state = kitToState(kit);` lines insert:

```ts
    if (get().hosted) {
      // Hosted: a kit is a sound, not a song — params only, clip keeps its pattern.
      const userTables = state.tables.map((table) => deserializeUserTable(table).table);
      drumEngine.panic();
      drumEngine.setUserTables(userTables);
      drumEngine.params = { ...state.params };
      drumEngine.applyAllParams();
      set({ params: state.params, padNames: state.padNames, userTables, kitValue: value, patchValue: '' });
      return;
    }
```

- [ ] **Step 4: Implement bass store the same way** — in `src/bass/store.ts`: `export let bassEngine = new BassEngine();`, add `hosted: boolean` + `attachHosted: (engine: BassEngine) => void` with:

```ts
  attachHosted: (engine) => {
    bassEngine = engine;
    set({
      hosted: true,
      powered: true,
      playing: false,
      curStep: -1,
      curSemi: -100,
      chaining: false,
      chainFresh: false,
      params: { ...engine.params },
    });
  },
```

Same `if (get().hosted) return;` guard at the top of `play` and `stop`. Gate `loadPatchByValue` — hosted branch applies params only:

```ts
    if (get().hosted) {
      bassEngine.panic();
      bassEngine.params = { ...state.params };
      bassEngine.applyAllParams();
      set({ params: state.params, patchValue: value });
      return;
    }
```

(inserted after `const state = patchToState(patch);`).

- [ ] **Step 5: Implement WT-1 store** — in `src/store.ts`: `export let engine = new SynthEngine();`, add `hosted: boolean` + `attachHosted: (e: SynthEngine) => void`:

```ts
  attachHosted: (e) => {
    engine = e;
    set({
      hosted: true,
      powered: true,
      seqPlaying: false,
      curStep: -1,
      chaining: false,
      chainFresh: false,
      params: { ...e.params },
    });
  },
```

Guard `seqPlay`/`seqStop` with `if (get().hosted) return;`. In `_setPatterns`, persist only when standalone:

```ts
  _setPatterns(next: Patterns) {
    set({ patterns: next });
    engine.setSeqPatterns(next);
    if (!get().hosted) saveSeqState(next, get().chain);
  },
```

`applyPreset` needs no gating (WT-1 presets never carry patterns). Check `powered: boolean` exists on the WT-1 store interface (it does — `PowerOverlay` reads it); `attachHosted` forcing it true keeps panels enabled.

- [ ] **Step 6: Run tests + build**

Run: `npx vitest run src/drum/store.test.ts && npm run build` — Expected: PASS. Then the full suite: `npx vitest run` — Expected: PASS (standalone behavior unchanged; the only observable difference is `export let`).

- [ ] **Step 7: Commit**

```bash
git add src/drum/store.ts src/drum/store.test.ts src/bass/store.ts src/store.ts
git commit -m "feat(seq): hosted mode in device stores — attachHosted + transport/persistence gates"
```

---

### Task 8: Host bridge — codec helpers + the `DeviceView` wiring

**Files:**
- Create: `src/seq/hostBridge.ts`
- Create: `src/seq/components/DeviceView.tsx`
- Test: `src/seq/hostBridge.test.ts`

**Interfaces:**
- Consumes: `clipPattern`, `useSeqStore` (focus, session, owner, pos, `updateClipBytes`, `setTrackPatch`, `createClip`); device stores' `attachHosted`/`patterns`/`params`/`hosted` (Task 7); `SeqDevice.engine` (Task 3); `emptyClipBytes`, `bytesPerBar`, `HOSTED_MAX_BARS` (Task 1).
- Produces:
  - Pure codecs: `clipToPatterns(bytes: Uint8Array, empty: Uint8Array): Uint8Array` (copy clip bytes over a fresh empty pattern buffer) and `patternsToClip(machine: MachineId, patterns: Uint8Array, bars: number): Uint8Array` (slice the first `bars` bars).
  - `DeviceView(): JSX.Element | null` — mounts nothing in session mode; in focus mode attaches the store, runs the sync effects, and renders the machine's panels (Task 9 fills in the panel bodies; this task renders a placeholder `<div className="sq-device-body" />` so the wiring is testable/committable on its own).

- [ ] **Step 1: Write the failing codec tests** — `src/seq/hostBridge.test.ts`:

```ts
import { describe, expect, it } from 'vitest';
import { makeEmptyPatterns as drumEmpty } from '../drum/seq';
import { makeEmptyPatterns as bassEmpty } from '../bass/seq';
import { bytesPerBar, emptyClipBytes } from './protocol';
import { clipToPatterns, patternsToClip } from './hostBridge';

describe('clip ↔ pattern codec', () => {
  it('round-trips DR1 bytes through the pattern buffer', () => {
    const clip = emptyClipBytes('DR1', 2);
    clip[0] = 1; // bar 0, pad 0, step 0
    clip[256 + 16] = 2; // bar 1, pad 1, step 0
    const pats = clipToPatterns(clip, drumEmpty());
    expect(pats.length).toBe(4 * 256); // NPATTERNS buffer
    expect(pats[0]).toBe(1);
    expect(pats[256 + 16]).toBe(2);
    expect(patternsToClip('DR1', pats, 2)).toEqual(clip);
  });

  it('round-trips BL1 bytes and keeps the neutral oct default beyond the clip', () => {
    const clip = emptyClipBytes('BL1', 1);
    clip[0] = 1; clip[1] = 7; // step 0 on, note 7
    const pats = clipToPatterns(clip, bassEmpty());
    expect(patternsToClip('BL1', pats, 1)).toEqual(clip);
    // growing to 2 bars pulls a silent-but-neutral bar 1 from the buffer
    const grown = patternsToClip('BL1', pats, 2);
    expect(grown.length).toBe(2 * bytesPerBar('BL1'));
    expect(grown[bytesPerBar('BL1') + 2]).toBe(1); // oct byte neutral
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/seq/hostBridge.test.ts` — Expected: FAIL (module missing).

- [ ] **Step 3: Implement `src/seq/hostBridge.ts`:**

```ts
// Bridge between the SQ-4 session doc and the hosted device stores.
// The clip payloads and the device pattern buffers share one byte layout
// (bar-major == pattern-major), so the codec is copy/slice — editPattern
// in a hosted store means "bar".

import { bytesPerBar, type MachineId } from './protocol';

/** Clip bytes → a fresh device pattern buffer (bars beyond the clip stay empty). */
export function clipToPatterns(bytes: Uint8Array, empty: Uint8Array): Uint8Array {
  const out = empty.slice();
  out.set(bytes.subarray(0, Math.min(bytes.length, out.length)), 0);
  return out;
}

/** First `bars` bars of a device pattern buffer → clip bytes. */
export function patternsToClip(machine: MachineId, patterns: Uint8Array, bars: number): Uint8Array {
  return patterns.slice(0, bars * bytesPerBar(machine));
}
```

- [ ] **Step 4: Run codec tests** — `npx vitest run src/seq/hostBridge.test.ts` — Expected: PASS. Commit the codec:

```bash
git add src/seq/hostBridge.ts src/seq/hostBridge.test.ts
git commit -m "feat(seq): clip/pattern bridge codec"
```

- [ ] **Step 5: Create `src/seq/components/DeviceView.tsx`** — the wiring component. Complete code:

```tsx
// Hosted device view (focus mode): attaches the focused track's running
// engine to that machine's standalone store, keeps the target clip and the
// track patch in sync with the session doc, and renders the device panels.

import { useEffect, useMemo, useRef } from 'react';
import { makeEmptyPatterns as bassEmpty } from '../../bass/seq';
import { useBassStore } from '../../bass/store';
import { makeEmptyPatterns as drumEmpty } from '../../drum/seq';
import { useDrumStore } from '../../drum/store';
import { makeEmptyPatterns as wtEmpty } from '../../noteseq';
import { useStore as useWtStore } from '../../store';
import { clipToPatterns, patternsToClip } from '../hostBridge';
import { HOSTED_MAX_BARS, type MachineId } from '../protocol';
import { clipPattern, useSeqStore } from '../store';

// One uniform handle per machine over the three (differently-typed) stores.
interface HostedStore {
  attach: (engine: unknown) => void;
  getPatterns: () => Uint8Array;
  setPatterns: (p: Uint8Array) => void;
  getParams: () => Record<string, number>;
  setPos: (step: number, bar: number, playing: boolean) => void;
  subscribe: (fn: () => void) => () => void;
  empty: () => Uint8Array;
}

const HOSTS: Record<MachineId, HostedStore> = {
  DR1: {
    attach: (e) => useDrumStore.getState().attachHosted(e as never),
    getPatterns: () => useDrumStore.getState().patterns,
    setPatterns: (p) => useDrumStore.setState({ patterns: p, editPattern: 0 }),
    getParams: () => useDrumStore.getState().params,
    setPos: (step, bar, playing) => useDrumStore.setState({ curStep: step, curPat: bar, playing }),
    subscribe: (fn) => useDrumStore.subscribe(fn),
    empty: drumEmpty,
  },
  BL1: {
    attach: (e) => useBassStore.getState().attachHosted(e as never),
    getPatterns: () => useBassStore.getState().patterns,
    setPatterns: (p) => useBassStore.setState({ patterns: p, editPattern: 0 }),
    getParams: () => useBassStore.getState().params,
    setPos: (step, bar, playing) => useBassStore.setState({ curStep: step, curPat: bar, playing }),
    subscribe: (fn) => useBassStore.subscribe(fn),
    empty: bassEmpty,
  },
  WT1: {
    attach: (e) => useWtStore.getState().attachHosted(e as never),
    getPatterns: () => useWtStore.getState().patterns,
    setPatterns: (p) => useWtStore.setState({ patterns: p, editPattern: 0 }),
    getParams: () => useWtStore.getState().params,
    setPos: (step, bar, playing) => useWtStore.setState({ curStep: step, curPat: bar, seqPlaying: playing }),
    subscribe: (fn) => useWtStore.subscribe(fn),
    empty: wtEmpty,
  },
};

export function DeviceView() {
  const focus = useSeqStore((s) => s.focus);
  const session = useSeqStore((s) => s.session);
  const rig = useSeqStore((s) => s.rig);

  const track = focus ? session.tracks[focus.track] : null;
  const clip = focus ? session.scenes[focus.scene]?.clips[focus.track] : null;
  const machine = track?.machine ?? null;
  const host = machine ? HOSTS[machine] : null;
  const editable = !!clip && clip.bars <= HOSTED_MAX_BARS;

  // Guards the pattern-subscription against echoing our own writes.
  const syncing = useRef(false);

  // 1. Attach the rig engine whenever the focused track changes.
  useEffect(() => {
    if (!focus || !rig || !host) return;
    const engine = rig.devices[focus.track].engine;
    if (engine) host.attach(engine);
  }, [focus?.track, rig, host]);

  // 2. Load the target clip's bytes into the device store on target change.
  useEffect(() => {
    if (!focus || !host || !machine) return;
    syncing.current = true;
    const bytes = clipPattern(session, focus.scene, focus.track);
    host.setPatterns(bytes ? clipToPatterns(bytes, host.empty()) : host.empty());
    syncing.current = false;
    // Intentionally NOT keyed on the pattern string: our own write-backs must
    // not reload (they'd reset editPattern); createClip flips `!!clip`.
  }, [focus?.scene, focus?.track, !!clip, host, machine]);

  // 3. Write pattern edits back: device store → session doc (+ live hot-swap).
  useEffect(() => {
    if (!focus || !host || !machine || !editable) return;
    let prev = host.getPatterns();
    return host.subscribe(() => {
      const next = host.getPatterns();
      if (next === prev || syncing.current) { prev = next; return; }
      prev = next;
      const st = useSeqStore.getState();
      const cur = st.session.scenes[focus.scene]?.clips[focus.track];
      if (!cur) return;
      st.updateClipBytes(focus.scene, focus.track, patternsToClip(machine, next, cur.bars), cur.bars);
    });
  }, [focus?.scene, focus?.track, host, machine, editable]);

  // 4. Snapshot patch edits (debounced) into the track's inline patch doc.
  useEffect(() => {
    if (!focus || !host) return;
    let prev = host.getParams();
    let timer: ReturnType<typeof setTimeout> | undefined;
    const unsub = host.subscribe(() => {
      const next = host.getParams();
      if (next === prev) return;
      prev = next;
      clearTimeout(timer);
      timer = setTimeout(() => {
        useSeqStore.getState().setTrackPatch(focus.track, {
          kind: 'inline',
          data: { params: { ...host.getParams() } },
        });
      }, 400);
    });
    return () => { clearTimeout(timer); unsub(); };
  }, [focus?.track, host]);

  // 5. Mirror the conductor's per-track playhead into the device step LEDs —
  //    only while the focused clip is the one actually sounding.
  const owner = useSeqStore((s) => s.owner);
  const pos = useSeqStore((s) => focus ? s.pos[focus.track] : null);
  const playing = useSeqStore((s) => s.playing);
  useEffect(() => {
    if (!focus || !host) return;
    const live = owner[focus.track] === focus.scene && playing;
    if (live && pos) host.setPos(pos.step, pos.bar, true);
    else host.setPos(-1, 0, false);
  }, [focus, host, owner, pos, playing]);

  if (!focus || !track || !machine) return null;

  return (
    <section className="sq-device" style={{ '--tc': track.color } as React.CSSProperties}>
      <div className="sq-device-body" />
    </section>
  );
}
```

Add `import type * as React from 'react';` (matching the codebase's style for the CSS-var cast). The `useMemo` import is unused — omit it.

- [ ] **Step 6: Build + full seq suite**

Run: `npm run build && npx vitest run src/seq` — Expected: PASS. (`DeviceView` isn't mounted yet; this proves the wiring compiles against real store types.)

- [ ] **Step 7: Commit**

```bash
git add src/seq/components/DeviceView.tsx
git commit -m "feat(seq): DeviceView bridge — attach, clip sync, patch snapshot, playhead"
```

---

### Task 9: Device panels inside SQ-4 — hosted chrome, clip bar, CSS

**Files:**
- Modify: `src/seq/components/DeviceView.tsx` (replace the placeholder body)
- Create: `src/seq/components/HostedClipBar.tsx`
- Modify: `src/drum/components/StepSeq.tsx`, `src/bass/components/PitchSeq.tsx`, `src/components/panels/SeqPanel.tsx` (hide transport + pattern/chain rows when hosted)
- Modify: `src/seq/main.tsx` (stylesheet imports)
- Modify: `src/seq/seq.css` (hosted-rack scoping + overrides)

**Interfaces:**
- Consumes: the device stores' `hosted` flag (Task 7), `createClip`/`updateClipBytes` (Task 4), `patternsToClip` (Task 8), `HOSTED_MAX_BARS` (Task 1), each app's existing panel components.
- Produces: a fully rendered hosted device. `HostedClipBar` provides clip name, BAR chips (1..bars, driving the device store's `setEditPattern`), a ±BARS stepper (1..`HOSTED_MAX_BARS`), and the edit-lock banner for >4-bar clips.

- [ ] **Step 1: Gate device-local chrome behind `hosted`** — in each of the three seq panels, select the flag and wrap the transport button and the pattern/chain selector block:

`src/drum/components/StepSeq.tsx`: add `const hosted = useDrumStore((s) => s.hosted);` and wrap the transport `<button className={\`pb-btn dr-transport...\`}>` **and** the pattern A–D + CHAIN block in `{!hosted && ( ... )}`. The step grid, pad row and everything else stay.

`src/bass/components/PitchSeq.tsx`: same treatment with `useBassStore((s) => s.hosted)` — hide the `bl-transport` button and the pattern/chain block.

`src/components/panels/SeqPanel.tsx`: same with `useStore((s) => s.hosted)` — hide its transport and pattern A–D/CHAIN chrome.

The grids still read `editPattern`, which the `HostedClipBar` now drives — that's the bar selector.

- [ ] **Step 2: Create `src/seq/components/HostedClipBar.tsx`:**

```tsx
// Focus-mode clip toolbar: name, bar selector (device editPattern = bar),
// bars stepper, edit-lock banner, and CREATE CLIP for empty targets.

import { useBassStore } from '../../bass/store';
import { useDrumStore } from '../../drum/store';
import { useStore as useWtStore } from '../../store';
import { patternsToClip } from '../hostBridge';
import { HOSTED_MAX_BARS, type MachineId } from '../protocol';
import { useSeqStore } from '../store';

const BAR_STORE = {
  DR1: {
    use: () => useDrumStore((s) => s.editPattern),
    set: (i: number) => useDrumStore.getState().setEditPattern(i),
    patterns: () => useDrumStore.getState().patterns,
  },
  BL1: {
    use: () => useBassStore((s) => s.editPattern),
    set: (i: number) => useBassStore.getState().setEditPattern(i),
    patterns: () => useBassStore.getState().patterns,
  },
  WT1: {
    use: () => useWtStore((s) => s.editPattern),
    set: (i: number) => useWtStore.getState().setEditPattern(i),
    patterns: () => useWtStore.getState().patterns,
  },
} as const;

export function HostedClipBar({ machine }: { machine: MachineId }) {
  const focus = useSeqStore((s) => s.focus)!;
  const clip = useSeqStore((s) => s.session.scenes[focus.scene]?.clips[focus.track]);
  const { createClip, updateClipBytes } = useSeqStore.getState();
  const bars = BAR_STORE[machine];
  const editBar = bars.use();

  if (!clip) {
    return (
      <div className="sq-clipbar">
        <button className="sq-clipbar-create" onClick={() => createClip(focus.scene, focus.track)}>
          ＋ CREATE CLIP
        </button>
        <span className="sq-clipbar-hint">EMPTY SLOT — THE PATCH PANEL STILL EDITS THIS TRACK'S SOUND</span>
      </div>
    );
  }

  if (clip.bars > HOSTED_MAX_BARS) {
    return (
      <div className="sq-clipbar">
        <span className="sq-clipbar-name">{clip.name}</span>
        <span className="sq-clipbar-lock">CLIP IS {clip.bars} BARS — EDITING CAPS AT {HOSTED_MAX_BARS} (PLAYBACK UNAFFECTED)</span>
      </div>
    );
  }

  const setBars = (n: number) => {
    const next = Math.max(1, Math.min(HOSTED_MAX_BARS, n));
    if (next === clip.bars) return;
    updateClipBytes(focus.scene, focus.track, patternsToClip(machine, bars.patterns(), next), next);
    if (editBar >= next) bars.set(next - 1);
  };

  return (
    <div className="sq-clipbar">
      <span className="sq-clipbar-name">{clip.name}</span>
      <div className="sq-clipbar-bars">
        {Array.from({ length: clip.bars }, (_, i) => (
          <button
            key={i}
            className={`sq-bar-chip${editBar === i ? ' active' : ''}`}
            onClick={() => bars.set(i)}
          >
            {i + 1}
          </button>
        ))}
      </div>
      <div className="sq-clipbar-len">
        <button className="sq-mini" onClick={() => setBars(clip.bars - 1)} title="Remove bar">−</button>
        <span>{clip.bars} BAR{clip.bars > 1 ? 'S' : ''}</span>
        <button className="sq-mini" onClick={() => setBars(clip.bars + 1)} title="Add bar">＋</button>
      </div>
    </div>
  );
}
```

Note on shrinking: `updateClipBytes` slices the pattern buffer, but the dropped bar's bytes stay in the device store buffer until the next clip load — so growing right back restores them within a focus session. Acceptable and pleasant.

- [ ] **Step 3: Fill in the DeviceView body** — replace `<div className="sq-device-body" />` in `DeviceView.tsx`:

```tsx
  return (
    <section className="sq-device" style={{ '--tc': track.color } as React.CSSProperties}>
      <HostedClipBar machine={machine} />
      <div className="sq-device-body">
        {machine === 'DR1' && <DrumPanels />}
        {machine === 'BL1' && <BassPanels />}
        {machine === 'WT1' && <WtPanels />}
      </div>
    </section>
  );
```

with the three panel assemblies in the same file (compositions copied from each standalone App, minus PowerOverlay/Header/keyboard hooks; each wrapped in the app's root id so its stylesheet applies):

```tsx
function DrumPanels() {
  return (
    <div id="drum-rack" className="sq-hosted-rack">
      <div className="dr-main">
        <div className="dr-left">
          <div id="dr-pads"><PadGrid /></div>
          <div id="dr-padstrip"><PadStrip /></div>
        </div>
        <div className="dr-right">
          <div id="dr-selbar"><SelBar /></div>
          <div id="dr-oscrow">
            <OscSection osc="oscA" />
            <OscSection osc="oscB" />
            <NoiseSection />
          </div>
          <div id="dr-editrow">
            <PitchEnvPanel />
            <AmpEnvPanel />
            <FilterSection />
            <ModPanel />
          </div>
        </div>
      </div>
      <div id="dr-stepseq"><StepSeq /></div>
      <div id="dr-fxrack"><FxRack /></div>
    </div>
  );
}

function BassPanels() {
  return (
    <div id="bass-rack" className="sq-hosted-rack">
      <div id="bl-editrow">
        <BassOscSection />
        <SubSection />
        <BassFilterSection />
        <BassEnvPanel />
      </div>
      <div id="bl-modrow">
        <BassLfoPanel />
        <AccentPanel />
        <KeysPanel />
      </div>
      <div id="bl-seq"><PitchSeq /></div>
      <div id="bl-fxrack"><BassFxRack /></div>
    </div>
  );
}

function WtPanels() {
  return (
    <div id="rack" className="sq-hosted-rack">
      <WavetableEditor />
      <div className="panels">
        <OscPanel prefix="oscA" accentKey="a" title="OSC A" gridArea="oscA" />
        <OscPanel prefix="oscB" accentKey="b" title="OSC B" gridArea="oscB" />
        <UtilPanel />
        <FilterPanel />
        <EnvPanel id="env1" title="AMP ENV" gridArea="env1" viewAccent="#e8edf7" knobAccent="n" />
        <EnvPanel id="env2" title="MOD ENV" gridArea="env2" viewAccent="#b18cff" knobAccent="f" modSource={3} />
        <LfoPanel />
        <MatrixPanel />
        <FxPanel />
        <SeqPanel />
      </div>
      <KeyboardBar />
    </div>
  );
}
```

Import each component from its home (`../../drum/components/...`, `../../bass/components/...`, `../../components/...`). BL-1 and DR-1 both export `OscSection`/`FilterSection`/`EnvPanel`-alikes — alias on import (`import { OscSection as BassOscSection } from '../../bass/components/OscSection'` etc.) exactly as shown above. **Do not** mount: power overlays, app Headers/TopBar, `useDrumKeys`/`useBassKeys`/`useComputerKeys`/MIDI hooks. WT-1's `KeyboardBar` stays — live note playing inside SQ-4 works because the store routes to the attached engine.

- [ ] **Step 4: Stylesheets** — in `src/seq/main.tsx`, import the device stylesheets **before** `seq.css` so SQ-4's page chrome wins ties:

```ts
import '../index.css';
import '../drum/drum.css';
import '../bass/bass.css';
import './seq.css';
```

Then append a hosted-scoping section to `src/seq/seq.css`:

```css
/* ---------- hosted device (focus mode) ---------- */
.sq-device {
  border: 1px solid #1a2030;
  border-radius: 10px;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.sq-device-body { overflow-y: auto; min-height: 0; }
/* Device stylesheets style their own root ids; neutralize page-level sizing
   so the rack renders as an embedded panel, not a full page. */
.sq-hosted-rack { width: auto; min-height: 0; margin: 0; padding: 12px; }

.sq-clipbar {
  display: flex; align-items: center; gap: 12px;
  padding: 8px 12px; border-bottom: 1px solid #1a2030;
  background: #0d1119;
}
.sq-clipbar-name { font-weight: 700; letter-spacing: 0.08em; }
.sq-clipbar-bars { display: flex; gap: 4px; }
.sq-bar-chip {
  min-width: 26px; padding: 3px 6px; border-radius: 5px;
  border: 1px solid #232b3d; background: #121826; color: #8b94a7;
  cursor: pointer; font: inherit; font-size: 11px;
}
.sq-bar-chip.active { border-color: var(--tc); color: var(--tc); }
.sq-clipbar-len { display: flex; align-items: center; gap: 6px; margin-left: auto; font-size: 11px; }
.sq-clipbar-hint, .sq-clipbar-lock { font-size: 10px; color: #667; letter-spacing: 0.06em; }
.sq-clipbar-create {
  padding: 6px 14px; border-radius: 6px; border: 1px dashed var(--tc);
  background: transparent; color: var(--tc); cursor: pointer; font: inherit;
}
```

The exact body/`main` rules that leak from the device stylesheets can't be fully predicted from here — after Step 6's visual check, add any further `#sq-rack`-scoped overrides at the END of `seq.css` (they win by order).

- [ ] **Step 5: Build**

Run: `npm run build` — Expected: clean compile.

- [ ] **Step 6: Visual smoke-check (headless)** — follow `.claude/skills/verify/SKILL.md`: `npx vite --port 5199 &`, launch headless Chrome with the autoplay flag, load `/seq/`, power on, then `window.__fableSq.store.getState().enterFocus(0)` via eval and screenshot. The DR-1 panel should render inside the page under the clip bar without blowing up the page layout. Repeat for tracks 1–3 (BL-1 renders, both WT-1 tracks render, switching heads re-attaches). Fix CSS leaks found here (Step 4's override tail).

- [ ] **Step 7: Commit**

```bash
git add src/seq src/drum/components/StepSeq.tsx src/bass/components/PitchSeq.tsx src/components/panels/SeqPanel.tsx
git commit -m "feat(seq): hosted device panels — clip bar, chrome gating, scoped styles"
```

---

### Task 10: Focus-mode layout — mini strip, scene rail, ✎, keyboard, transition

**Files:**
- Modify: `src/seq/SeqApp.tsx`
- Modify: `src/seq/components/TrackHeads.tsx`
- Modify: `src/seq/components/SceneRow.tsx`
- Create: `src/seq/components/SceneRail.tsx`
- Modify: `src/seq/seq.css`

**Interfaces:**
- Consumes: `focus`/`enterFocus`/`exitFocus`/`focusScene` (Task 5), `DeviceView` (Tasks 8–9), existing `SceneRow`, `TrackHeads`, `FooterRow`.
- Produces: the two-mode SeqApp. Session mode is pixel-identical to today except the ✎ affordance on clip cells.

- [ ] **Step 1: SceneRail** — `src/seq/components/SceneRail.tsx`:

```tsx
// Focus-mode scene rail: one chip per scene (number + live dot), the focused
// chip lit. Keyboard: ↑/↓ move it (handled in SeqApp).

import { pad2 } from '../model';
import { useSeqStore } from '../store';

export function SceneRail() {
  const scenes = useSeqStore((s) => s.session.scenes);
  const focus = useSeqStore((s) => s.focus)!;
  const owner = useSeqStore((s) => s.owner);
  const queue = useSeqStore((s) => s.queue);
  const { focusScene } = useSeqStore.getState();

  return (
    <div className="sq-rail">
      {scenes.map((sc, s) => {
        const live = Object.values(owner).includes(s);
        const queued = Object.values(queue).includes(s);
        return (
          <button
            key={s}
            className={`sq-rail-chip${focus.scene === s ? ' active' : ''}${live ? ' live' : ''}${queued ? ' queued' : ''}`}
            onClick={() => focusScene(s)}
            title={sc.name}
          >
            {pad2(s + 1)}
            <span className="sq-rail-dot" />
          </button>
        );
      })}
    </div>
  );
}
```

- [ ] **Step 2: ✎ on clip cells** — in `SceneRow.tsx`'s `ClipCell`, inside the main `<button className="sq-cell">`'s body (after the `sq-cell-head` div), add a hover-revealed edit affordance (a `span[role=button]`, not a nested `<button>`):

```tsx
        <span
          className="sq-cell-editbtn"
          role="button"
          tabIndex={0}
          title="Edit clip in its device"
          onClick={(e) => { e.stopPropagation(); useSeqStore.getState().enterFocus(t, s); }}
          onKeyDown={(e) => { if (e.key === 'Enter') { e.stopPropagation(); useSeqStore.getState().enterFocus(t, s); } }}
        >
          ✎
        </span>
```

CSS (in `seq.css`):

```css
.sq-cell-editbtn {
  position: absolute; top: 6px; right: 6px;
  width: 20px; height: 20px; line-height: 20px; text-align: center;
  border-radius: 5px; background: #0d1119cc; color: var(--tc);
  opacity: 0; transition: opacity 0.12s; font-size: 11px;
}
.sq-cell:hover .sq-cell-editbtn { opacity: 1; }
```

(`.sq-cell` already gets `position: relative` from the queued/muted overlays — verify, add it if not.)

- [ ] **Step 3: TrackHeads become the tab strip** — in `TrackHeads.tsx`:

Select `focus` and the actions. The scenes-card flips role:

```tsx
      {focus ? (
        <button className="sq-scenes-card sq-back" onClick={() => exitFocus()}>
          <div className="sq-scenes-title">◂ SESSION</div>
          <div className="sq-scenes-sub">ESC · 1–4 SWITCH DEVICE · ↑↓ SCENE</div>
        </button>
      ) : (
        <div className="sq-scenes-card">
          <div className="sq-scenes-title">SCENES</div>
          <div className="sq-scenes-sub">EMPTY CELLS STOP THEIR TRACK · ≈ PASSES THROUGH</div>
        </div>
      )}
```

Each track head: the id block becomes the focus trigger and the focused head lights up:

```tsx
          <div
            key={t}
            className={`sq-track-head${focus?.track === t ? ' focused' : ''}`}
            style={{ '--tc': tr.color } as React.CSSProperties}
          >
            ...
            <div
              className="sq-track-id sq-track-id-btn"
              role="button"
              tabIndex={0}
              title={focus?.track === t ? undefined : 'Open device'}
              onClick={() => enterFocus(t)}
              onKeyDown={(e) => { if (e.key === 'Enter') enterFocus(t); }}
            >
```

(M/S buttons and the knob are siblings of `.sq-track-id`, so their clicks never pass through it — no stopPropagation needed.) CSS:

```css
.sq-track-id-btn { cursor: pointer; }
.sq-track-head.focused { border-color: var(--tc); box-shadow: 0 0 12px color-mix(in srgb, var(--tc) 35%, transparent); }
.sq-back { cursor: pointer; text-align: left; font: inherit; color: inherit; }
.sq-back .sq-scenes-title { color: #cfd6e4; }
```

(match `.sq-scenes-card`'s existing background/border on the `<button>` variant — inspect and reuse its selector.)

- [ ] **Step 4: SeqApp two modes + keyboard** — replace the scene-rows block in `SeqApp.tsx`:

```tsx
  const focus = useSeqStore((s) => s.focus);
  ...
  // focus-mode keys: Esc exits, 1–4 switch devices, ↑/↓ move the scene rail
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const st = useSeqStore.getState();
      if (!st.focus) return;
      const tag = (e.target as HTMLElement | null)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
      if (e.key === 'Escape') st.exitFocus();
      else if (e.key >= '1' && e.key <= String(st.session.tracks.length)) st.enterFocus(Number(e.key) - 1);
      else if (e.key === 'ArrowUp') { e.preventDefault(); st.focusScene(st.focus.scene - 1); }
      else if (e.key === 'ArrowDown') { e.preventDefault(); st.focusScene(st.focus.scene + 1); }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  return (
    <>
      <SqPowerOverlay />
      <main id="sq-rack" className={focus ? 'focused' : ''}>
        <Header />
        <TrackHeads />
        {focus ? (
          <div className="sq-focus" key={`f${focus.track}`}>
            <div className="sq-strip">
              <SceneRail />
              <div className="sq-strip-row"><SceneRow s={focus.scene} /></div>
            </div>
            <DeviceView />
          </div>
        ) : (
          session.scenes.map((_, s) => <SceneRow key={s} s={s} />)
        )}
        <FooterRow />
        <div className="sq-hint">
          {focus
            ? 'MINI STRIP STAYS LIVE — TAP CELLS TO LAUNCH · ✎ RETARGETS THE EDITOR · ESC BACK TO SESSION'
            : `TAP CLIP TO LAUNCH · TAP AGAIN TO STOP · LAUNCHES QUANTIZE TO ${quant} · RIGHT-CLICK EMPTY CELL TO TOGGLE PASS-THROUGH · CLICK A TRACK NAME TO OPEN ITS DEVICE`}
        </div>
      </main>
    </>
  );
```

Layout + entrance transition CSS (append to `seq.css`):

```css
/* ---------- focus mode ---------- */
#sq-rack.focused { height: 100vh; display: flex; flex-direction: column; }
.sq-focus {
  display: flex; flex-direction: column; gap: 10px;
  flex: 1; min-height: 0;
  animation: sq-focus-in 0.2s ease-out;
}
@keyframes sq-focus-in {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: none; }
}
.sq-strip { display: grid; grid-template-columns: auto 1fr; gap: 8px; align-items: stretch; }
.sq-strip-row { min-width: 0; }
.sq-rail { display: flex; flex-direction: column; gap: 4px; justify-content: center; }
.sq-rail-chip {
  display: flex; align-items: center; gap: 5px;
  padding: 2px 7px; border-radius: 5px; font-size: 10px;
  border: 1px solid #232b3d; background: #121826; color: #8b94a7;
  cursor: pointer; font-family: inherit;
}
.sq-rail-chip.active { border-color: #cfd6e4; color: #cfd6e4; }
.sq-rail-dot { width: 5px; height: 5px; border-radius: 50%; background: #1a1f2a; }
.sq-rail-chip.live .sq-rail-dot { background: #63e6a5; box-shadow: 0 0 6px #63e6a5; }
.sq-rail-chip.queued .sq-rail-dot { background: #e8c268; animation: sq-blink 0.5s steps(2) infinite; }
.sq-device { flex: 1; }
```

(reuse the existing queued-blink keyframes name if `seq.css` already defines one — check before adding `sq-blink`.) The `key={\`f${focus.track}\`}` on `.sq-focus` replays the entrance animation on device switch — cheap, feels like FLIP without the bookkeeping.

- [ ] **Step 5: Build + tests**

Run: `npm run build && npx vitest run src/seq` — Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/seq
git commit -m "feat(seq): focus-mode layout — mini strip, scene rail, edit affordance, keys"
```

---

### Task 11: End-to-end headless verification + docs

**Files:**
- Modify: `docs/sq4-clips.md` (§1 status note: Phase 4 shipped, pointer to the focus spec)
- Modify: `.claude/skills/verify/SKILL.md` (add a "Drive (focus mode)" recipe)

**Interfaces:**
- Consumes: everything above, the `verify` skill launch recipe, `window.__fableSq`.

- [ ] **Step 1: Full local gates**

Run: `npx vitest run && npm run build` — Expected: all green.

- [ ] **Step 2: Headless end-to-end** — per `.claude/skills/verify/SKILL.md` (vite on 5199, Chrome with `--autoplay-policy=no-user-gesture-required`), then via chromectl eval on `/seq/`:

1. `localStorage.clear(); location.reload()`; power on; wait ~4s.
2. Launch scene 0 (`document.querySelectorAll('.sq-scene-launch')[0].click()`); wait ~3s; assert `__fableSq.store.getState().owner` is populated (proves audio acks).
3. `__fableSq.store.getState().enterFocus(0)` → assert `focus` targets the owning scene; screenshot: strip + DR-1 panel.
4. **Live hot-swap:** read `session.scenes[s].clips[0].pattern`, click a step in the hosted StepSeq (`document.querySelector('#dr-stepseq .dr-step')`-style selector — inspect the DOM), assert the pattern string **changed** and `owner` did **not** (playback uninterrupted), and the beat counter keeps advancing.
5. **Patch snapshot:** turn a knob via the store (`__fableDr` is absent here — drive `useDrumStore` through a DOM knob drag or `window.__fableSq` eval of the drum store import is unavailable; simplest: click a kit in SelBar), wait 600ms, assert `session.tracks[0].patch.kind === 'inline'`.
6. **Reload persistence:** `location.reload()`, power on, assert the edited pattern string survived in `localStorage['fable.session.v1']`.
7. Switch heads 1→3 (BL-1, WT-1 ×2) asserting each renders; `exitFocus()` → full grid returns.
8. Standalone regression: load `/drum/`, power on, toggle a step, play — behaves as before.

Expected: every assertion holds; screenshots look right in both modes.

- [ ] **Step 3: Update docs** — `docs/sq4-clips.md` §1: change "Phase 4 (session editing) is open" to note it shipped via the device-focus design (link `docs/superpowers/specs/2026-07-12-sq4-device-focus-design.md`). Append the focus-mode drive recipe (step 2's selectors) to the verify skill.

- [ ] **Step 4: Commit**

```bash
git add docs/sq4-clips.md .claude/skills/verify/SKILL.md
git commit -m "docs(seq): mark Phase 4 shipped; add focus-mode verify recipe"
```
