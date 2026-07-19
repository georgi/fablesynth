# SQ-4 device view: SEQ / EDIT mode toggle (web)

Date: 2026-07-19
Status: approved
Scope: web app only (`src/seq`). JUCE port follows on a later branch.

## Problem

In SQ-4 focus mode the hosted device view shows the sequencer/keys panels and
the advanced panels (FX, Modulation, Filters, Envelopes, LFOs) at the same
time. They should be mutually exclusive: the user toggles between a
sequencer/keys view and an advanced-panels view.

## Design

### State

- New field on the seq store (`src/seq/store.ts`): `deviceMode: 'seq' | 'edit'`,
  default `'seq'`.
- New action `setDeviceMode(mode)`.
- Whenever the focused track changes (any action that sets `focus` to a
  different track, or from null), `deviceMode` resets to `'seq'` —
  focusing a track always starts in the performance-first view.
- Not persisted.

### Toggle control

- Segmented two-way control (SEQ | EDIT) rendered in `HostedClipBar`
  (`src/seq/components/HostedClipBar.tsx`), styled like the existing compact
  header controls.

### Panel grouping in `DeviceView.tsx`

Osc/source panels are always visible in both modes. DR-1's pad grid is always
visible (it doubles as pad selection for per-pad editing).

| Machine | Always | SEQ mode | EDIT mode |
|---|---|---|---|
| DR-1 | PadGrid, PadStrip, SelBar, OscSection/SampleSection/NoiseSection | StepSeq | PitchEnvPanel, AmpEnvPanel, FilterSection, ModPanel, FxRack |
| BL-1 | OscSection, SubSection | PitchSeq, KeysPanel | FilterSection, EnvPanel, LfoPanel, AccentPanel, BassFxRack |
| WT-1 | OscPanel A, OscPanel B, UtilPanel | SeqPanel, KeyboardBar | FilterPanel, EnvPanel (amp), EnvPanel (mod), LfoPanel, MatrixPanel, FxPanel |

Judgment call: BL-1's AccentPanel goes to EDIT mode (it is a modulation
control), even though accent amount interacts with sequencer accent steps.

Hidden panels are unmounted (conditional render), not CSS-hidden — no live
subscriptions from invisible panels.

### Layout

Each machine's rack grid in `src/seq/seq.css` gets a per-mode variant (a
`data-mode` attribute or modifier class on the rack root) so the visible group
fills the space the hidden group vacates. The device window size does not
change between modes — no resize jump when toggling.

### Testing

- Store test: `deviceMode` defaults to `'seq'`, `setDeviceMode` works, mode
  resets to `'seq'` on focus-track change.
- Component-level check that toggling swaps the rendered groups.
- Visual check via the headless `/verify` flow.
