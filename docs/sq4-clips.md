# SQ-4 Clip & Sequence Protocol

Design for hooking the FableSeq SQ-4 session launcher up to the real
instruments: how clips are represented, how they reach each machine, and how
four independent AudioWorklet sequencers stay locked to one musical grid.
Phases 1–3 of this contract are implemented: `src/seq/protocol.ts` (schema,
codecs, boundary math), the hosted messages in the three worklets,
`src/seq/devices.ts` + `src/seq/rig.ts` (adapters, shared-context graph) and
the conductor in `src/seq/store.ts`. Phase 4 (session editing) shipped via
the device-focus design — see
[`docs/superpowers/specs/2026-07-12-sq4-device-focus-design.md`](superpowers/specs/2026-07-12-sq4-device-focus-design.md)
for the focus-mode UI, hosted device panels, and the clip/patch bridge that
lets edits hot-swap live clips and snapshot patches into the session.

## 1. Goals

- One SQ-4 page drives **four tracks on three machines** (DR-1, BL-1, WT-1 ×2)
  from a single shared `AudioContext`.
- A **clip** is playable content bound to one track: pattern bytes in that
  machine's native format plus a length in bars.
- **Launch semantics match the SQ-4 mock** (which already implements them in
  UI state): launches quantize to `1 BAR` / `1/4` / `OFF`, scenes layer
  freely, the latest clip owns each track, stop is quantized too.
- Launches are **phase-locked across devices**: two clips launched at the
  same boundary start at the same audio frame, and never drift apart
  afterwards.
- The **standalone apps stay byte-for-byte compatible**: same protocols, same
  behavior, same localStorage. Hosted mode is additive.

Non-goals (v1): tempo automation, audio export, per-clip FX, MIDI clock,
follow actions, session recording, JUCE port of the launcher.

## 2. Architecture: three layers

```
┌────────────────────────────────────────────────────────────┐
│ Session document (JSON)                                    │  what to play
│   tracks · scenes · clips (pattern bytes) · patches · bpm  │
└──────────────────────────┬─────────────────────────────────┘
                           │ load / save
┌──────────────────────────▼─────────────────────────────────┐
│ Conductor (main thread — the SQ-4 zustand store)           │  when to play
│   owner/queue state machine · boundary math · track gains  │
└──────┬───────────┬───────────┬───────────┬─────────────────┘
       │ SeqDevice │           │           │   uniform adapter interface
┌──────▼────┐ ┌────▼─────┐ ┌───▼──────┐ ┌──▼───────┐
│ DR-1      │ │ BL-1     │ │ WT-1     │ │ WT-1     │  how to play
│ adapter   │ │ adapter  │ │ adapter  │ │ adapter  │
│ + engine  │ │ + engine │ │ + engine │ │ + engine │
│ + worklet │ │ + worklet│ │ + worklet│ │ + worklet│
└───────────┘ └──────────┘ └──────────┘ └──────────┘
```

- The **session document** is dumb data: fully serializable, self-contained
  (patches embedded), no references into live objects.
- The **conductor** owns all musical *decisions* — what launches when. It is
  the existing SQ-4 store (**exists**: `src/seq/store.ts` owner/queue/quant
  semantics, kept as-is) plus real boundary scheduling.
- The **device adapters** own machine *mechanics* — one class per machine
  translating the uniform clip commands into that engine's messages. Devices
  never decide timing; they execute stamped commands.

## 3. The shared timebase

All `AudioWorkletProcessor`s on one `AudioContext` read the same global
`currentFrame`. That is the master clock; there is no other.

- **Anchor**: when the conductor starts the transport it records
  `songStartFrame` (the context frame of beat zero). Every musical position
  is derived arithmetic, never an accumulating counter on the main thread:

  ```
  samplesPerBeat = sampleRate * 60 / bpm        (beat = quarter note)
  samplesPerStep = samplesPerBeat / 4           (16ths)
  barFrames      = samplesPerBeat * 4
  boundary(quant, now) = songStartFrame
      + ceil((now - songStartFrame) / q) * q    where q = barFrames or samplesPerBeat
  ```

- **Command stamping**: every transport command carries an absolute
  `atFrame`. A device holds the command as *pending* and executes it in the
  render quantum containing `atFrame`. Two devices given the same `atFrame`
  fire in the same 128-sample block by construction — the sync guarantee
  costs no cross-worklet communication.
- **Precision**: events land within one render quantum (≤ 128 samples,
  ~2.9 ms) of the ideal frame, *identically* on every device, and step
  durations count real samples so there is no drift. (This matches how the
  WT-1 note seq already ticks; DR-1/BL-1 sub-block splitting can later snap
  the first hit to the exact sample, but block-consistent is the contract.)
- **Global pause**: SQ-4's ⏸ maps to `ctx.suspend()` / `ctx.resume()`.
  Frames freeze, every pending `atFrame` and every playing clip resumes in
  phase for free. Stop-all is a real command; pause is not.
- **Tempo**: one global BPM owned by the conductor. v1: BPM is editable
  while the transport is stopped (changing it re-derives nothing because
  nothing is scheduled). Changing tempo mid-flight would rescale the anchor
  math and is deferred.

## 4. Session document schema

```ts
// v1 — persisted to localStorage 'fable.session.v1' + file import/export.
interface SessionDoc {
  v: 1;
  name: string;
  bpm: number;                 // 60..200, global
  swing: number;               // 0..1, global (each machine's own swing math)
  quant: '1 BAR' | '1/4' | 'OFF';
  tracks: TrackDoc[];          // exactly 4 in the SQ-4 layout
  scenes: SceneDoc[];          // any count; the mock ships 6
}

interface TrackDoc {
  machine: 'DR1' | 'BL1' | 'WT1';
  name: string;                // 'DRUMS', 'BASS', ...
  color: string;
  gain: number;                // 0..1 track fader (the head knob)
  patch: PatchDoc;             // the *track's* sound — clips share it (v1)
}

// Patches embed the machine's existing serialized form so a session file is
// portable on its own. The payloads are exactly what the standalone apps
// already persist (DrumKit / BassPatch / Preset), reused not redefined.
type PatchDoc =
  | { kind: 'factory'; index: number }
  | { kind: 'inline'; data: unknown };   // machine-native patch JSON

interface SceneDoc {
  name: string;
  clips: (ClipDoc | null)[];   // one slot per track, null = empty cell
  pass?: number[];             // tracks whose empty cell is pass-through
                               // (default: empty cells stop the track on
                               // scene launch, Ableton stop-button style)
}

interface ClipDoc {
  name: string;
  bars: number;                // 1..16 — pattern data length, loops
  pattern: string;             // base64 of packed pattern bytes (see §5)
}
```

Decisions worth calling out:

- **Patch per track, not per clip.** The SQ-4 mock shows one patch chip per
  track head; clips are patterns only. This avoids patch-swap clicks at
  launch boundaries and keeps clips tiny. Per-clip patches are a possible v2
  (schedule a patch apply at the boundary).
- **Clips carry their own pattern bytes**, not references to the machine's
  A–D slots. The standalone 4-pattern banks and chains remain a standalone
  feature; a clip is self-contained content the conductor uploads on demand.
- **Multi-bar clips are literal**: a 4-bar clip is 4×16 steps of data. No
  chain juggling — the mock's `8B` pads (FOG SWELL, AIR OUT) are just 128
  steps. Sizes are trivial (WT-1/BL-1: 48 B/bar; DR-1: 256 B/bar).

## 5. Clip pattern payloads

Each machine keeps its existing packed step format; a clip is `bars`
consecutive 16-step blocks of it. `patternKind` is implied by the track's
machine:

| machine | per step | per bar | semantics |
| --- | --- | --- | --- |
| `DR1` | 16 pads × 1 byte (0/1/2 = off/on/accent) | 256 B | pad hits, choke groups apply |
| `BL1` | 3 bytes (flags on/acc/slide · note 0..11 · oct+1) | 48 B | mono acid line, slides glide |
| `WT1` | 3 bytes (flags on/acc/tie · note 0..11 · oct+1) | 48 B | poly voice line, ties legato |

These reuse `src/drum/seq.ts`, `src/bass/seq.ts` and `src/noteseq.ts`
layouts verbatim (**exists**) — the editors in the standalone apps are
already editors for clip content. BL-1/WT-1 clips also carry the track root
note via the patch/params (`seq.root` for WT-1, lane root for BL-1),
unchanged.

## 6. Device protocol (worklet messages)

Hosted mode adds five messages to each worklet, with identical names and
shapes across machines. Existing standalone messages are untouched.

**In (main thread → worklet):**

| message | meaning |
| --- | --- |
| `{t:'host', on:1}` | enter hosted mode: the standalone transport (`play`/`stop`, pattern banks, chains) is ignored; the internal BPM/swing params are overridden by `tempo` |
| `{t:'tempo', bpm, swing, anchor}` | global clock: tempo, swing, and `anchor` = `songStartFrame`. Sent at transport start and never varied mid-flight (v1) |
| `{t:'clip', data, bars, atFrame}` | schedule clip start: `data` is the packed pattern bytes (transferable), loops every `bars`×16 steps. Replaces any *pending* clip (re-launch before the boundary re-targets). `atFrame ≤ currentFrame` ⇒ fires in this block (quant OFF) |
| `{t:'clipstop', atFrame}` | schedule stop at the boundary. Cancels a pending clip if one is waiting; otherwise stops the playing clip |
| `{t:'panic'}` | (**exists**) unchanged; also clears pending/playing clip in hosted mode |

**Out (worklet → main thread):**

| message | meaning |
| --- | --- |
| `{t:'clipstart', frame}` | the pending clip became the playing clip — the conductor flips queue→owner *on this ack*, so the UI turns green exactly when the audio changed |
| `{t:'clipstop', frame}` | the clip stopped (scheduled stop reached, or clip replaced by silence) |
| `{t:'pos', step, bar}` | per-step while playing — drives the cell playhead/progress in SQ-4 (throttled to step boundaries, like today's `step` messages) |

Execution rules inside every worklet (shared contract):

1. One **playing** slot and one **pending** slot. `clip` writes pending;
   at `atFrame` the pending swaps in (starting at its step 0) and
   `clipstart` is posted. Ableton-style: clips always launch from the top.
2. Step timing derives from the swap frame: the countdown starts at
   `atFrame`, counts real samples with the machine's existing swing math, so
   two devices swapped at the same frame stay in phase indefinitely.
3. **Stop semantics per machine**: DR-1 lets sounding pads ring out; BL-1
   releases its voice; WT-1 gates off its sequencer notes (and only those —
   live keyboard/MIDI notes survive).
4. A `clip` arriving while another is *playing* and none pending: the playing
   clip keeps running until `atFrame`, then swaps. No gap, no double-trigger:
   the old clip's last gate-off and the new clip's first trigger execute in
   the same block, old before new.
5. Hosted mode never touches the standalone pattern banks — a user's A–D
   patterns and chains are exactly as they left them.

Mute/solo deliberately do **not** appear in the protocol: they are per-track
`GainNode` ramps on the main thread (§7). A muted clip keeps running in
time, like the mock's `LIVE · MUTED` state.

## 7. Audio graph

Engines currently construct their own `AudioContext` and connect to
`ctx.destination`. Hosted mode parameterizes both (backwards-compatible
default keeps standalone behavior):

```ts
engine.init({ ctx?: AudioContext, output?: AudioNode })
```

```
DR-1 engine (own FX chain) ─→ trackGain[0] ─┐
BL-1 engine (own FX chain) ─→ trackGain[1] ─┼─→ masterGain → limiter → destination
WT-1 engine (own FX chain) ─→ trackGain[2] ─┤        └→ analyser (the SUM scope)
WT-1 engine (own FX chain) ─→ trackGain[3] ─┘
```

- Each engine keeps its full per-machine FX rack (that's part of its patch).
  In hosted mode its `masterGain → dcBlock → limiter` tail connects to the
  provided `output` (the track gain) instead of `destination`; SQ-4 owns the
  single final limiter so four machines can't fight four limiters.
- `trackGain` implements track fader × mute × solo × scene-mute with a
  ~15 ms ramp (effective-mute math already exists in `src/seq/model.ts`).
- Two WT-1 instances (LEAD + PADS) are two `SynthEngine` objects on the same
  context; `audioWorklet.addModule` of the same URL is idempotent and two
  `fable-wt` nodes coexist.

## 8. Adapter interface (main thread)

```ts
interface SeqDevice {
  init(ctx: AudioContext, output: AudioNode): Promise<void>;
  applyPatch(patch: PatchDoc): void;         // machine-native apply
  setTempo(bpm: number, swing: number, anchorFrame: number): void;
  scheduleClip(pattern: Uint8Array, bars: number, atFrame: number): void;
  scheduleStop(atFrame: number): void;
  panic(): void;
  onClipStart: (frame: number) => void;      // → conductor flips queue→owner
  onClipStop: (frame: number) => void;
  onPos: (step: number, bar: number) => void; // → playhead UI
}
```

One implementation per machine (`Dr1Device`, `Bl1Device`, `Wt1Device`), each
a thin wrapper over the existing engine class — no engine logic is
duplicated in the adapters.

## 9. Conductor state machine

The SQ-4 store keeps its existing shape (**exists**: `owner`, `queue`,
`quant`, mutes/solos, all tested); scheduling replaces the setTimeout beat
clock:

- `launch(t, s)`:
  `queue[t] = s` → `device[t].scheduleClip(clip.bytes, clip.bars, boundary(quant))`.
  Re-launching before the boundary just re-schedules (device pending slot
  replaces). Quant `OFF` stamps `atFrame = 0` (= now).
- `stopTrack(t)`: `queue[t] = STOP` → `scheduleStop(boundary(quant))`.
- `launchScene(s)` / `stopScene(s)` / `stopAll()`: per-track fan-out of the
  above, one shared boundary frame so the whole scene flips as one block.
  Empty cells act as Ableton-style stop buttons: `launchScene` calls
  `stopTrack` on uncovered tracks so a scene is a complete snapshot of what
  plays. Tracks in `scene.pass` are pass-through instead — the previous
  clip rides through the scene change (right-click an empty cell to toggle).
- **Owner flips on the `clipstart`/`clipstop` acks**, not on a UI timer —
  the grid shows what is audible, with a 250 ms watchdog fallback if an ack
  is lost.
- Beat dots / bar counter derive from `ctx.currentTime` against the anchor
  (rAF on the main thread), replacing the mock's setTimeout clock.
- First launch while the transport is idle starts the transport: set
  `songStartFrame = currentFrame + one block`, send `tempo` to all devices,
  then schedule.

Failure containment: a device that hasn't finished `init()` reports its
track as `LOADING` and rejects launches for that track only; the other
tracks play. All four engines are initialized from SQ-4's power-on gesture
(one user gesture unlocks the shared context).

## 10. Worked example

DROP A is live everywhere; the user taps BREAK's scene launch with quant
`1 BAR`, bar boundary at frame **F**:

```
tap        conductor: queue = {0:STOP, 1:4, 2:4, 3:4}
           dr1.scheduleStop(F)                  (BREAK has no DRUMS clip —
           bl1.scheduleClip(SUB HOLD bytes,  4, F)   its empty cell is a
           wt1a.scheduleClip(GLASS SOLO bytes, 4, F)  stop button)
           wt1b.scheduleClip(FOG SWELL bytes,  8, F)
…          cells pulse QUEUED (UI from queue state)
frame F    each worklet swaps pending→playing (DR-1 stops) in the same
           block, posts {t:'clipstart'|'clipstop', frame:F}
           conductor: owner = {1:4, 2:4, 3:4}, queue = {}
           only BREAK reads LIVE — a scene is a snapshot of what plays
```

Had DRUMS been marked pass-through in BREAK (`scenes[4].pass = [0]`, the
`≈` cell), no stop would be scheduled and FULL KIT A would ride through
the scene change, still owned by DROP A (`LIVE 1/4`).

Tap DRUMS' FULL KIT A cell again → `scheduleStop(F')` → at F' the DR-1
posts `clipstop`, its pads ring out, the footer NOW row clears for DRUMS.

## 11. Compatibility invariants

- Standalone `/app/`, `/drum/`, `/bass/` behavior is unchanged: hosted
  messages are additive, `host` mode is never entered there, engines default
  to creating their own context.
- Every worklet remains import-free; the hosted constants (message names,
  slot rules) get parity tests against a new `src/seq/protocol.ts` single
  source of truth, following the existing `worklet.parity.test.ts` pattern.
- The session doc is versioned (`v: 1`); loaders reject unknown majors.

## 12. Implementation phases

1. **Engine hosting** — `init({ctx, output})` on all three engines; no
   behavior change standalone. Small, mechanical, independently landable.
2. **Hosted transport** — the five messages + pending/playing slots in each
   worklet; `src/seq/protocol.ts` with types, base64 codecs, boundary math;
   parity + unit tests.
3. **Adapters + conductor wiring** — `SeqDevice` implementations, SQ-4
   store scheduling on the real clock, track gain graph, power-on overlay;
   a factory `SessionDoc` reproducing the mock's six scenes with real
   patterns/patches.
4. **Session editing** — clip pattern editing from SQ-4 (embed each
   machine's step editor per track), import/export, per-clip patch (v2).
