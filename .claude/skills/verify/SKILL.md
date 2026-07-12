---
name: verify
description: Build/launch/drive recipe for verifying FableSynth web surfaces (SQ-4, DR-1, BL-1, WT-1) headlessly
---

# Verifying FableSynth web surfaces

## Launch

```bash
npx vite --port 5199 &          # serves /seq/ /drum/ /bass/ /app/
```

Headless Chrome must allow autoplay or the audio clock never runs and
clip acks never arrive (chromectl's `start` can't pass the flag — launch
Chrome directly, then use chromectl for eval/screenshot on the same port):

```bash
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  --headless=new --remote-debugging-port=9223 --remote-allow-origins='*' \
  --user-data-dir=<scratch>/chrome-profile --no-first-run \
  --autoplay-policy=no-user-gesture-required --window-size=2000,1100 &
```

## Drive (SQ-4 at /seq/)

- `localStorage.clear(); location.reload()` first — sessions persist under
  `fable.session.v1` and stale state leaks across runs.
- Power on: `document.getElementById('power-on').click()`, wait ~4s for the
  four engines to boot.
- Debug handle: `window.__fableSq.store.getState()` → `owner`, `queue`,
  `session`. (`__fableDr` / `__fable` on the other surfaces.)
- Scene launch buttons: `document.querySelectorAll('.sq-scene-launch')[s]`.
- Clip cells of scene s: `document.querySelectorAll('.sq-grid')[s+1]
  .querySelectorAll('.sq-cell')` (grid 0 is the track-heads row).
- Quant is 1 BAR at 122 BPM → wait ~3s after a launch for the owner flip
  (owner changes only on real worklet acks, so it proves audio ran).

## Drive (focus mode)

Device-focus mode hosts the full DR-1/BL-1/WT-1 UIs inside `/seq/` — one
scene row + scene rail + device panel, wired live to the SQ-4 conductor.

- Enter/exit/switch via the store (no DOM click needed to focus):
  `window.__fableSq.store.getState().enterFocus(trackIndex)` — 0=DR-1,
  1=BL-1, 2 and 3=WT-1. `exitFocus()` returns to the full grid.
  `focus` on the state is `{track, scene}` or `null`.
- Owning-scene proof: after a scene launch and ~3s wait, `owner` (an object
  keyed by track index → owning scene) is non-empty — that's the audio
  worklet ack, not just UI state.
- DR-1 hosted step cells: `document.querySelectorAll('#dr-stepseq .step')`
  (plain `.step` buttons in a `.step-row`, *not* `.dr-step`). Clicking one
  hot-swaps the live clip's pattern bytes in place — `owner` stays
  unchanged and the bar counter keeps advancing, proving playback wasn't
  interrupted. Read the pattern before/after at
  `session.scenes[s].clips[trackIndex].pattern` (base64; decode to diff
  bytes).
- Patch snapshot: there's no "kit" selector inside the hosted panel (the
  DR-1 kit bar lives in the standalone `Header`, which isn't hosted) — use
  the hosted patch stepper instead:
  `document.querySelector('.dr-patchbar button[aria-label="next patch"]').click()`.
  That calls `stepPatch` → `applyPatchByValue` → a `setParam` per param,
  which the bridge debounces (~400ms) into
  `session.tracks[trackIndex].patch = {kind:'inline', data:{params}}`. Wait
  ~700ms then assert `patch.kind === 'inline'`.
- Reload persistence: `localStorage['fable.session.v1']` is written live
  (no extra save step) — `location.reload()`, power on, and the edited
  pattern/patch are already back in `session` on next read.
- Screenshot each hosted device to eyeball the panel:
  `chromectl screenshot --id <target> -o <file>` right after `enterFocus`;
  give React a beat (a few hundred ms) before capturing.

## Gotchas

- Store reads immediately after dispatching a DOM event show the new state,
  but className/textContent read stale until React re-renders — re-query in
  a second eval.
