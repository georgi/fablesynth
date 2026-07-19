# SQ-4 Device SEQ/EDIT Mode Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In SQ-4 focus mode, toggle the hosted device view between a sequencer/keys view and an advanced-panels view (FX, Modulation, Filters, Envelopes, LFOs) — never both at once.

**Architecture:** A non-persisted `deviceMode: 'seq' | 'edit'` flag on the seq zustand store (resets to `'seq'` when the focused track changes), a SEQ/EDIT segmented control in `HostedClipBar`, conditional panel rendering per machine in `DeviceView`, and per-mode CSS grid overrides in `seq.css` scoped by a `data-mode` attribute on each rack root.

**Tech Stack:** React + zustand (web app in `src/seq`), vitest (node env, no DOM), headless Chrome via the project `/verify` skill for visual checks.

**Spec:** `docs/superpowers/specs/2026-07-19-sq4-device-mode-toggle-design.md`

## Global Constraints

- Web app only (`src/seq`, `src/seq/seq.css`). Do NOT touch `juce/`.
- Osc/source panels stay visible in BOTH modes (DR-1: Osc/Sample/Noise; BL-1: Osc/Sub; WT-1: OSC A/OSC B/Util).
- DR-1's PadGrid/PadStrip/SelBar stay visible in BOTH modes (pads double as edit selection).
- Hidden panels are unmounted (conditional render), never CSS-hidden.
- The device window size must not change between modes.
- Vitest runs in a plain node environment — no DOM/component tests. Visual behavior is verified with the `/verify` headless flow (Task 4). This is the agreed substitute for the spec's "component-level check".
- Commit messages: conventional commits, ending with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: `deviceMode` store state

**Files:**
- Modify: `src/seq/store.ts` (interface ~line 48-54, actions ~line 78-94, initial state ~line 282, `enterFocus` ~line 596, `resetSeqStore` ~line 751)
- Test: `src/seq/store.test.ts`

**Interfaces:**
- Consumes: existing `useSeqStore`, `resetSeqStore`, `enterFocus`, `exitFocus`, `focusScene`.
- Produces: `deviceMode: 'seq' | 'edit'` state field and `setDeviceMode: (m: 'seq' | 'edit') => void` action on the seq store. Later tasks read `useSeqStore((s) => s.deviceMode)` and call `useSeqStore.getState().setDeviceMode(mode)`.

- [ ] **Step 1: Write the failing test**

In `src/seq/store.test.ts`, add a new top-level `describe` block (follow the file's existing pattern: tests use a `st()` helper / `useSeqStore.getState()` and `resetSeqStore()` in `beforeEach` — reuse whatever helper the surrounding tests use):

```ts
describe('deviceMode', () => {
  beforeEach(() => resetSeqStore());
  const st = () => useSeqStore.getState();

  it('defaults to seq and toggles via setDeviceMode', () => {
    expect(st().deviceMode).toBe('seq');
    st().setDeviceMode('edit');
    expect(st().deviceMode).toBe('edit');
  });

  it('survives scene changes and same-track refocus, resets on track change or refocus from null', () => {
    st().enterFocus(0);
    st().setDeviceMode('edit');
    st().focusScene(1); // same track, different scene — mode survives
    expect(st().deviceMode).toBe('edit');
    st().enterFocus(0, 2); // re-focus same track while focused — mode survives
    expect(st().deviceMode).toBe('edit');
    st().enterFocus(1); // different track — resets
    expect(st().deviceMode).toBe('seq');
    st().setDeviceMode('edit');
    st().exitFocus();
    st().enterFocus(1); // entering from null resets, even to the same track
    expect(st().deviceMode).toBe('seq');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx vitest run src/seq/store.test.ts -t deviceMode`
Expected: FAIL — `deviceMode` is `undefined` / `setDeviceMode` is not a function.

- [ ] **Step 3: Implement**

In `src/seq/store.ts`:

1. In the state interface, after `focus: { track: number; scene: number } | null;` (~line 48) add:

```ts
deviceMode: 'seq' | 'edit'; // focus-mode device view; resets to 'seq' on track change
```

2. In the actions section of the interface, after `focusScene: (s: number) => void;` add:

```ts
setDeviceMode: (m: 'seq' | 'edit') => void;
```

3. In the store's initial state (near `focus: null,` ~line 282) add:

```ts
deviceMode: 'seq',
```

4. Change `enterFocus` (~line 596) so entering a different track (or entering from null) resets the mode:

```ts
enterFocus: (t, s) => {
  advanceTourStep('devices');
  const st = get();
  const scene = s ?? st.owner[t] ?? st.focus?.scene ?? lastFocusScene;
  set({
    focus: { track: t, scene: clampScene(scene) },
    deviceMode: st.focus?.track === t ? st.deviceMode : 'seq',
  });
},
```

5. Add the action next to `focusScene`:

```ts
setDeviceMode: (m) => set({ deviceMode: m }),
```

6. In `resetSeqStore` (~line 751, near `focus: null,`) add:

```ts
deviceMode: 'seq',
```

Do NOT touch `loadSessionPreset` — it sets `focus: null`, and the next `enterFocus` already resets the mode.

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/seq/store.test.ts`
Expected: all PASS (new `deviceMode` tests plus all pre-existing tests).

- [ ] **Step 5: Commit**

```bash
git add src/seq/store.ts src/seq/store.test.ts
git commit -m "feat(seq): deviceMode store flag for the SEQ/EDIT device toggle

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: SEQ/EDIT segmented control in HostedClipBar

**Files:**
- Modify: `src/seq/components/HostedClipBar.tsx`
- Modify: `src/seq/seq.css` (append near the `.sq-clipbar-library` rules, ~line 818)

**Interfaces:**
- Consumes: `useSeqStore((s) => s.deviceMode)` and `useSeqStore.getState().setDeviceMode` from Task 1.
- Produces: a `<div className="sq-mode-toggle">` rendered in ALL THREE clip-bar variants (empty slot, locked, normal). Task 4 clicks these buttons by their `SEQ` / `EDIT` labels.

- [ ] **Step 1: Add the toggle component**

In `src/seq/components/HostedClipBar.tsx`, add below the `HostedPatchSelect` function:

```tsx
function DeviceModeToggle() {
  const mode = useSeqStore((s) => s.deviceMode);
  const setDeviceMode = useSeqStore.getState().setDeviceMode;
  return (
    <div className="sq-mode-toggle" role="group" aria-label="Device view mode">
      <button type="button" className={mode === 'seq' ? 'on' : ''} onClick={() => setDeviceMode('seq')}>SEQ</button>
      <button type="button" className={mode === 'edit' ? 'on' : ''} onClick={() => setDeviceMode('edit')}>EDIT</button>
    </div>
  );
}
```

- [ ] **Step 2: Render it in all three clip-bar variants**

In `HostedClipBar`, next to the existing `const patchSelect = <HostedPatchSelect machine={machine} />;` add:

```tsx
const modeToggle = <DeviceModeToggle />;
```

Then insert `{modeToggle}` immediately after `{patchSelect}` in each of the three returned `<div className="sq-clipbar">` variants (the empty-slot branch, the `clip.bars > HOSTED_MAX_BARS` branch, and the normal branch). The device panels render even without a clip, so the toggle must too.

- [ ] **Step 3: Style it**

Append to `src/seq/seq.css`, after the `.sq-clipbar-library` rule block:

```css
.sq-mode-toggle { display: inline-flex; border: 1px solid #303a50; border-radius: 5px; overflow: hidden; }
.sq-mode-toggle button {
  padding: 5px 10px; border: 0; background: #151c2a; color: #8791a5;
  cursor: pointer; font: inherit; font-size: 10px; letter-spacing: .08em;
}
.sq-mode-toggle button + button { border-left: 1px solid #303a50; }
.sq-mode-toggle button.on {
  background: color-mix(in srgb, var(--tc) 22%, #151c2a); color: var(--tc);
}
```

- [ ] **Step 4: Typecheck/build**

Run: `npx tsc --noEmit` (or the project's check script if `package.json` has one — inspect `scripts` and prefer it)
Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add src/seq/components/HostedClipBar.tsx src/seq/seq.css
git commit -m "feat(seq): SEQ/EDIT segmented control in the hosted clip bar

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Conditional panel groups in DeviceView + per-mode grids

**Files:**
- Modify: `src/seq/components/DeviceView.tsx` (`DrumPanels` ~line 222, `BassPanels` ~line 251, `WtPanels` ~line 271)
- Modify: `src/seq/seq.css` (append after the `.sq-hosted-rack` rule, ~line 778)

**Interfaces:**
- Consumes: `useSeqStore((s) => s.deviceMode)` from Task 1.
- Produces: each rack root (`#drum-rack`, `#bass-rack`, `#rack`) carries `data-mode="seq"` or `data-mode="edit"`; hidden groups are unmounted. Task 4 asserts on these attributes and on which panels exist in the DOM.

- [ ] **Step 1: DR-1 grouping**

Replace `DrumPanels` in `src/seq/components/DeviceView.tsx`:

```tsx
function DrumPanels() {
  const mode = useSeqStore((s) => s.deviceMode);
  return (
    <div id="drum-rack" className="sq-hosted-rack" data-mode={mode}>
      <div className="dr-main">
        <div className="dr-left">
          <div id="dr-pads"><PadGrid /></div>
          <div id="dr-padstrip"><PadStrip /></div>
        </div>
        <div className="dr-right">
          <div id="dr-selbar"><SelBar /></div>
          <div id="dr-oscrow">
            <OscSection osc="oscA" />
            <SampleSection />
            <NoiseSection />
          </div>
          {mode === 'edit' && (
            <div id="dr-editrow">
              <PitchEnvPanel />
              <AmpEnvPanel />
              <FilterSection />
              <ModPanel />
            </div>
          )}
        </div>
      </div>
      {mode === 'edit' && <div id="dr-fxrack"><FxRack /></div>}
      {mode === 'seq' && <div id="dr-stepseq"><StepSeq /></div>}
    </div>
  );
}
```

- [ ] **Step 2: BL-1 grouping**

Replace `BassPanels`:

```tsx
function BassPanels({ bars }: { bars?: number }) {
  const mode = useSeqStore((s) => s.deviceMode);
  return (
    <div id="bass-rack" className="sq-hosted-rack" data-mode={mode}>
      <div id="bl-editrow">
        <BassOscSection />
        <SubSection />
        {mode === 'edit' && <BassFilterSection />}
        {mode === 'edit' && <BassEnvPanel />}
      </div>
      <div id="bl-modrow">
        {mode === 'edit' ? (
          <>
            <BassLfoPanel />
            <AccentPanel />
          </>
        ) : (
          <KeysPanel />
        )}
      </div>
      {mode === 'seq' && <div id="bl-seq"><PitchSeq bars={bars} /></div>}
      {mode === 'edit' && <div id="bl-fxrack"><BassFxRack /></div>}
    </div>
  );
}
```

- [ ] **Step 3: WT-1 grouping**

In `WtPanels`, keep all the chord-editing logic unchanged; replace only the returned JSX:

```tsx
  const mode = useSeqStore((s) => s.deviceMode);
  return (
    <div id="rack" className="sq-hosted-rack" data-mode={mode}>
      <div className="panels">
        <OscPanel prefix="oscA" accentKey="a" title="OSC A" gridArea="oscA" />
        <OscPanel prefix="oscB" accentKey="b" title="OSC B" gridArea="oscB" />
        <UtilPanel />
        {mode === 'edit' && (
          <>
            <FilterPanel />
            <EnvPanel id="env1" title="AMP ENV" gridArea="env1" viewAccent="#e8edf7" knobAccent="n" />
            <EnvPanel id="env2" title="MOD ENV" gridArea="env2" viewAccent="#b18cff" knobAccent="f" modSource={3} />
            <LfoPanel />
            <MatrixPanel />
            <FxPanel />
          </>
        )}
        {mode === 'seq' && (
          <SeqPanel bars={clip?.bars} polySteps={polySteps} onToggleChordNote={toggleChordNote} onSetChordDuration={setChordDuration} />
        )}
      </div>
      {mode === 'seq' && <KeyboardBar />}
    </div>
  );
```

(The `const mode = ...` hook call goes at the top of `WtPanels` with the other hooks, before any early logic — it must run on every render.)

- [ ] **Step 4: Per-mode grid CSS**

Append to `src/seq/seq.css`, directly after the `.sq-hosted-rack` rule (~line 778):

```css
/* SEQ/EDIT device modes: the hidden group is unmounted, so each rack's grid
   is re-declared per mode to let the visible group fill the vacated space.
   ID-scoped selectors deliberately outrank index.css's responsive overrides. */
#bass-rack[data-mode='seq'] #bl-editrow { grid-template-columns: 1.7fr 0.7fr; }
#bass-rack[data-mode='seq'] #bl-modrow { grid-template-columns: 1fr; }
#bass-rack[data-mode='edit'] #bl-modrow { grid-template-columns: 290px 1fr; }
#rack[data-mode='seq'] .panels {
  grid-template-areas:
    'oscA oscA oscA oscA oscA oscB oscB oscB oscB oscB util util'
    'seq seq seq seq seq seq seq seq seq seq seq seq';
}
#rack[data-mode='edit'] .panels {
  grid-template-areas:
    'oscA oscA oscA oscA oscA oscB oscB oscB oscB oscB util util'
    'filter filter filter env1 env1 env2 env2 lfos lfos lfos lfos lfos'
    'matrix matrix matrix fx fx fx fx fx fx fx fx fx';
}
```

DR-1 needs no grid override: `.dr-left`/`.dr-right` are flex columns and `#dr-stepseq`/`#dr-fxrack` are plain full-width blocks, so unmounting reflows cleanly.

- [ ] **Step 5: Typecheck + full test run**

Run: `npx tsc --noEmit && npx vitest run`
Expected: no type errors, all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/seq/components/DeviceView.tsx src/seq/seq.css
git commit -m "feat(seq): device view swaps sequencer/keys vs advanced panels by mode

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Headless visual verification

**Files:**
- No source changes expected; fix CSS fallout in `src/seq/seq.css` if screenshots reveal layout breakage.

**Interfaces:**
- Consumes: the `sq-mode-toggle` buttons (Task 2) and `data-mode` rack attributes (Task 3).

- [ ] **Step 1: Read the project verify recipe**

Invoke the project's `verify` skill (Skill tool, name `verify`) and follow its build/launch/drive recipe for the SQ-4 web surface. It documents the dev-server/build commands, the headless Chrome flags (`--autoplay-policy` is required or the audio clock never runs), and the `__fable` debug handles.

- [ ] **Step 2: Drive and screenshot both modes for all three machines**

For each machine (DR1, BL1, WT1): focus a track of that machine, screenshot the default view, then click the EDIT button in the clip bar and screenshot again. Verify via DOM queries (e.g. `document.querySelector('#drum-rack').dataset.mode`) and visually:

- Default after focusing is SEQ mode, showing sequencer + keys/pads + osc panels only.
- EDIT mode shows FX/Mod/Filter/Env/LFO panels + osc panels, and the sequencer/keys are GONE from the DOM (`#dr-stepseq`, `#bl-seq`, `.panel-seq`/SeqPanel, KeyboardBar absent).
- DR-1 pads visible in both modes.
- Switching focus to a different track lands back in SEQ mode.
- No layout breakage: no empty grid rows, no overflow, device window size identical between modes.

- [ ] **Step 3: Fix any layout fallout**

If a grid override is wrong, fix it in `src/seq/seq.css` (the per-mode block from Task 3 Step 4) and re-screenshot until clean.

- [ ] **Step 4: Commit (only if fixes were needed)**

```bash
git add src/seq/seq.css
git commit -m "fix(seq): device-mode grid polish from headless verification

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
