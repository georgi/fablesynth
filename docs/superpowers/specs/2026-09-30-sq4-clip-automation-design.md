# SQ-4 Clip Automation Design

Status: web implementation  
Scope: Web SQ-4 hosted DR-1, BL-1, and WT-1. Native parity follows after the web UI settles.

## Summary

Add drawable automation lanes to every SQ-4 clip. A lane steers one continuous
device parameter with breakpoints, straight ramps, bent slopes, and stepped holds.
The lanes live in the clip, travel with the clip, and play in the device worklet
on the same sample clock as the notes.

Automation is a peer of the modulators, not a replacement. The modulators move
parameters per voice around the knob; an automation lane writes the knob value
itself over the clip timeline. The two combine: the matrix modulates around the
automated value.

## Layout

The panel sits below the device panels in SEQ and EDIT modes.

```text
AUTOMATION  [DRAW|LINE|POINT]  [1/16|FREE]            [+ LANE] [LEARN]
─────────────────────────────────────────────────────────────────────────
● F1 CUTOFF  1.20 kHz │ ╱‾‾‾╲___╱‾‾‾╲___ ┆ ╱‾‾‾╲___ (ghost repeats)       │
● A POS      62%      │ ▁▂▃▅▆▇█▇▆▅▃▂▁                                    │
──── selected lane ─────────────────────────────────────────────────────
TARGET [F1 CUTOFF ▾] [LEARN]  TIME [CLIP|GRID|FIT|PAD]  STEPS [- 12 +]  [ON] [✕]
┌────────────────────────────────────────────────────────────────────────┐
│ large editable curve, step columns, bar lines, playhead, value readout │
└────────────────────────────────────────────────────────────────────────┘
```

- Compact rows show every lane as a miniature. Click a row to select it.
- The selected lane opens a tall editor with its settings above it.
- The time axis always shows the clip. The lane cycle is solid; later cycles
  are ghosted, so a polymetric lane is visible against the clip at a glance.
- Each lane has its own colour. The value readout uses the parameter's units.
- An empty lane shows the stored knob value as a dashed baseline.

## Tools

- **DRAW:** drag to paint one held value per grid cell (a p-lock bar graph).
- **LINE:** drag from start to end to write a straight ramp; it replaces the
  points inside that span.
- **POINT:** click to add a point, drag a point to move it, Alt-drag a segment
  to bend it into a slope, double-click a point to remove it.
- **SNAP 1/16** quantizes time to steps; **FREE** uses quarter-step resolution.
- Shift constrains a point drag to its time.

## Time and POLY

Each lane owns a timebase, like a DR-1 POLY lane.

| Mode | Cycle |
| --- | --- |
| CLIP | Follows the clip length. Default. |
| GRID | Repeats every N sixteenths (1–64), independent of the clip. |
| FIT | Divides 1 or 2 bars into N equal slots (1–64), straight. |
| PAD | DR-1 only: follows the POLY settings of the target pad's lane (GRID/FIT, steps, rotation). |

PAD makes a pad lane's automation stay in phase with its polymetric hits. If
the pad lane is not POLY, PAD falls back to the clip length. Changing the mode
keeps the points; points outside the current cycle are kept but do not play.

## Data model

`ClipDoc.automation?: AutoLane[]`, at most eight lanes.

```ts
interface AutoLane {
  target: string;          // device param id, e.g. 'filter.cutoff', 'pad3.fx.eq.hiGain'
  enabled: boolean;
  time: { mode: 'clip' } | { mode: 'grid'; steps: number }
      | { mode: 'fit'; steps: number; cycleBeats: 4 | 8 } | { mode: 'pad' };
  points: { t: number; v: number; c?: number; hold?: boolean }[];
}
```

- `t` is lane time in steps (quarter-step resolution); `v` is the normalized
  value 0–1 in the parameter's own curve (log for cutoff).
- `c` (−1…1) bends the segment that leaves the point; `hold` keeps `v` until the
  next point. The last point wraps to the first point at the cycle end.
- Only continuous parameters (lin/log, no enum or bool) are targets. Tempo and
  swing are excluded.

## Playback

The store compiles every enabled lane with points into a table of absolute
values (16 samples per step, mapped through the parameter's curve) plus the
cycle geometry. It sends `{t:'auto', lanes}` after every clip launch or update.
The worklet attaches it to the pending or active clip.

Each render quantum the worklet computes the lane phase from the host anchor,
interpolates the table, and writes the parameter. It remembers the stored value
of every automated parameter; a knob edit changes the remembered value. When the
clip stops or the lane disappears, the stored value returns.

DR-1 voices read pad parameters at the trigger, so pad automation acts per hit.
Pad and group FX parameters move continuously.

## Learn

LEARN arms the selected lane. The next continuous knob the user moves on the
focused device becomes the target. A second click cancels.

## Out of scope for this pass

- Knob rings that show the live automated value.
- Native JUCE parity and the authored-song exporter (both ignore the field).
- Recording automation from live knob movement.
