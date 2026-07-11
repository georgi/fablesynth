# SQ-4 Device Focus — design

Navigation from the SQ-4 session launcher into each device's full UI. The
sequencer minimizes to the current scene; the remaining viewport hosts the
focused device (DR-1 / BL-1 / WT-1), wired to the engine the rig is already
running. This is Phase 4 ("session editing") of `docs/sq4-clips.md`.

## 1. Goals

- Focus any of the four tracks and see that machine's **full UI** — patch
  panel and pattern editor — inside the SQ-4 page.
- Edits apply **immediately, in place**: step/note toggles hot-swap into the
  running engine phase-preserved; patch tweaks are instant. The session doc
  is the source of truth.
- Session performance gestures are untouched: cell click launches, scene
  cards launch/mute/stop, quantization semantics unchanged.
- Standalone apps stay byte-for-byte compatible.

Non-goals (v1): per-clip patches, DR-1 pattern chains in hosted mode,
persisting focus state, JUCE port.

## 2. Interaction & layout

Two modes, one anatomy. Focus mode repurposes the existing rack rather than
adding chrome:

```
┌ Header — transport · BPM · QUANT (unchanged) ────────────────────────┐
├ Track heads — now also the device tab strip ─────────────────────────┤
│ [◂ SESSION]   [DRUMS DR-1]  [BASS BL-1]  [LEAD WT-1]  [PAD WT-1]     │
│  (scenes card)     ▔▔▔▔▔ focused head lit in its track color         │
├ Mini strip — the focused scene row, cells aligned under their heads ─┤
│ [02 CHORUS ▶M■]   [KICK 4B]   [SUB 2B]    [ARP 1B]    [—]            │
│ scene rail ▸ ①②③④⑤⑥  chips with live/queued dots                     │
├ Device panel — rest of the viewport ─────────────────────────────────┤
│         full DR-1 UI, playing/editing clip (scene 02, DRUMS)         │
└──────────────────────────────────────────────────────────────────────┘
```

- **Enter:** click a track head to focus that device. Or hover any clip
  cell: a small `✎` appears in its corner; clicking it opens exactly that
  clip. Plain cell click stays *launch*.
- **While focused:** the track-heads row is the tab strip — click another
  head to switch devices; the focused head glows in its track color.
  Mute/solo/vol on the heads keep working.
- **The scenes card flips role:** "SCENES" label in session mode, `◂
  SESSION` back button in focus mode. Esc exits too. Keys 1–4 switch
  devices, ↑/↓ move the scene rail.
- **Mini strip:** one full-size scene row (scene card + 4 cells) for the
  focused scene — still launchable, still shows live/queued/progress — plus
  a slim rail of scene chips (number + live dot) to jump rows. Cell columns
  line up with the heads above, so the editing target is the visible
  intersection.
- **Transition:** FLIP-style collapse (~200 ms) — the focused scene row
  slides into the strip slot, other rows fold away, the device panel rises.
  Reverse on exit. No route change; one page, one state field.

## 3. Focus & targeting model

One new field in the seq store:

```ts
focus: { track: number; scene: number } | null   // null = session mode
```

- The editor target is always `(focus.scene, focus.track)` — deterministic,
  no follow-playback magic.
- Entering via a track head picks the scene: the scene currently *owning*
  that track if live, else the last focused scene, else scene 0. Entering
  via `✎` sets both exactly.
- Switching heads keeps the scene; switching scenes keeps the track.
- Empty target cell: the device panel still renders (patch is per-track),
  with the pattern area replaced by a "＋ CREATE CLIP" placeholder that
  writes a blank clip into the session doc and hands it to the editor.
- Launch state and focus state are orthogonal; the mini strip's indicators
  always reflect real playback.
- Focus is not persisted; reload lands in session mode.

## 4. Hosting bridge — device UIs on the rig's engines

Each standalone store gains a **hosted mode**; SQ-4 mounts the existing
panel components unchanged.

- **Attach, don't create.** Each device store gets an `attachHosted(engine)`
  path: it adopts the rig's running engine for that track (exposed through
  the `SeqDevice` adapter) instead of constructing its own. Power overlay,
  `ctx.resume()` handling, and the store's transport logic are bypassed —
  the conductor owns the transport.
- **`DeviceView` in `src/seq/`** switches on `track.machine` and renders the
  machine's panels (DR-1 PadGrid/StepSeq/OscSection…, BL-1
  PitchSeq/FilterSection…, WT-1 wavetable editor/keyboard). Device headers,
  power overlays and transport buttons don't mount.
- **Two sync directions, both through the session doc:**
  - *Pattern:* on focus change the bridge decodes the target clip's bytes
    into the device store; every edit re-encodes to bytes, writes
    `session.scenes[s].clips[t]`, and (if live) hot-swaps the engine (§5).
  - *Patch:* param edits hit the shared engine directly, exactly as
    standalone; the bridge snapshots them into `track.patch` as a custom
    patch doc, keeping session files self-contained. All clips on a track
    share the sound (v1 contract).
- **Two WT-1 tracks** require the WT-1 store to be instantiable
  (store-per-track via context) rather than a module singleton — the one
  real refactor. DR-1/BL-1 stay singletons (one track each in v1).
- Standalone apps keep constructing their own engines; behavior unchanged.

## 5. Live edit path — hot-swap in the worklets

One new hosted message; everything else exists.

- **`updateClip { pattern, bars }`** on all three worklets replaces the
  playing clip's pattern bytes in place, effective next render quantum.
  Phase is preserved for free: position is derived arithmetic from
  `songStartFrame`, not state in the pattern, so swapping bytes never moves
  the playhead.
- **Bars changes** re-derive loop length under the same modular arithmetic
  (`position mod newLength`), seamless on live clips.
- **Routing:** bridge writes bytes to the session doc, then the conductor
  routes — **live**: send `updateClip` now; **queued**: re-stamp the pending
  `scheduleClip` with fresh bytes; **idle**: no engine traffic.
- **Patch edits** need no new protocol — param messages already reach the
  shared engine.
- Device playheads (step LEDs) chase the clip via the existing `onPos`
  callbacks, forwarded into the hosted store by the bridge.

## 6. Persistence & edge cases

- Every edit lands in the session doc; the existing debounced write to
  `fable.session.v1` and file export/import cover persistence.
- Scene/clip removed while focused: focus clamps to a valid target; a
  vanished clip falls back to the CREATE CLIP placeholder.
- Machine features with no clip mapping (DR-1 pattern chains, device-local
  pattern slots) are hidden in hosted mode; a clip is one pattern of N bars.
- Narrow viewports: the scene rail wraps under the scene card; the device
  panel scrolls vertically inside its area.

## 7. Testing

- Store unit tests (vitest, alongside `store.test.ts`): focus
  enter/switch/exit rules, target clamping, bridge byte round-trip
  (decode → edit → encode → doc), live/queued/idle routing of `updateClip`.
- Worklet tests where they exist: `updateClip` preserves phase and handles
  bars changes.
- Headless verification via `/verify` and `__fableSq`: enter focus, toggle a
  step on a live clip, assert the doc changed and audio keeps running.
