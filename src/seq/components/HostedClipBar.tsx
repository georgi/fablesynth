// Focus-mode clip toolbar: name, bar selector (device editPattern = bar),
// bars stepper, edit-lock banner, and CREATE CLIP for empty targets.

import { useBassStore } from '../../bass/store';
import { useDrumStore } from '../../drum/store';
import { useStore as useWtStore } from '../../store';
import { useState } from 'react';
import { SequenceLengthControl } from '../../components/SequenceLengthControl';
import { patternsToClip } from '../hostBridge';
import { HOSTED_MAX_BARS, type MachineId } from '../protocol';
import { clipPattern, useSeqStore } from '../store';
import { factoryPatchNames, patchBaseIndex, stepFactoryPatchIndex } from '../devices';
import { ClipLibraryBrowser } from './ClipLibraryBrowser';

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
  const [libraryOpen, setLibraryOpen] = useState(false);
  const focus = useSeqStore((s) => s.focus)!;
  const clip = useSeqStore((s) => s.session.scenes[focus.scene]?.clips[focus.track]);
  const { createClip } = useSeqStore.getState();
  const patchSelect = <HostedPatchSelect machine={machine} />;
  const modeToggle = <DeviceModeToggle />;

  if (!clip) {
    return (
      <>
        <div className="sq-clipbar">
          {patchSelect}
          {modeToggle}
          <button className="sq-clipbar-create" onClick={() => createClip(focus.scene, focus.track)}>＋ CREATE CLIP</button>
          <button className="sq-clipbar-library" onClick={() => setLibraryOpen(true)}>▦ CLIP LIBRARY</button>
          <span className="sq-clipbar-hint">EMPTY SLOT — LOAD A CLIP OR CREATE ONE</span>
        </div>
        {libraryOpen && <ClipLibraryBrowser machine={machine} onClose={() => setLibraryOpen(false)} />}
      </>
    );
  }

  if (clip.bars > HOSTED_MAX_BARS) {
    return (
      <>
        <div className="sq-clipbar">
          {patchSelect}
          {modeToggle}
          <span className="sq-clipbar-name">{clip.name}</span>
          <button className="sq-clipbar-library" onClick={() => setLibraryOpen(true)}>▦ CLIP LIBRARY</button>
          <span className="sq-clipbar-lock">CLIP IS {clip.bars} BARS — EDITING CAPS AT {HOSTED_MAX_BARS} (PLAYBACK UNAFFECTED)</span>
        </div>
        {libraryOpen && <ClipLibraryBrowser machine={machine} onClose={() => setLibraryOpen(false)} />}
      </>
    );
  }

  return (
    <>
      <div className="sq-clipbar">
        {patchSelect}
        {modeToggle}
        <span className="sq-clipbar-name">{clip.name}</span>
        <button className="sq-clipbar-library" onClick={() => setLibraryOpen(true)}>▦ CLIP LIBRARY</button>
      </div>
      {libraryOpen && <ClipLibraryBrowser machine={machine} onClose={() => setLibraryOpen(false)} />}
    </>
  );
}

// Bar selector + clip length. Lives in the hosted device's own sequencer
// panel head (passed down as `headerExtra`), where the standalone devices put
// the same control — not in the clip toolbar above it.
export function HostedLengthControl({ machine }: { machine: MachineId }) {
  const focus = useSeqStore((s) => s.focus)!;
  const clip = useSeqStore((s) => s.session.scenes[focus.scene]?.clips[focus.track]);
  const currentBar = useSeqStore((s) => (
    s.playing && s.owner[focus.track] === focus.scene ? s.pos[focus.track]?.bar ?? null : null
  ));
  const bars = BAR_STORE[machine];
  const editBar = bars.use();
  if (!clip || clip.bars > HOSTED_MAX_BARS) return null;

  const setBars = (n: number) => {
    const next = Math.max(1, Math.min(HOSTED_MAX_BARS, n));
    if (next === clip.bars) return;
    const base = clipPattern(useSeqStore.getState().session, focus.scene, focus.track) ?? undefined;
    useSeqStore.getState().updateClipBytes(
      focus.scene, focus.track, patternsToClip(machine, bars.patterns(), next, base), next,
    );
    if (editBar >= next) bars.set(next - 1);
  };

  return (
    <SequenceLengthControl
      editBar={editBar}
      length={clip.bars}
      playingBar={currentBar}
      onEditBar={bars.set}
      onLengthChange={setBars}
    />
  );
}

function HostedPatchSelect({ machine }: { machine: MachineId }) {
  const focus = useSeqStore((s) => s.focus)!;
  const patch = useSeqStore((s) => s.session.tracks[focus.track].patch);
  const names = factoryPatchNames(machine);
  // The selector tracks the factory origin even after edits; a `*` marks the
  // patch as dirty rather than dropping the whole selection to CUSTOM.
  const current = patchBaseIndex(patch);
  const dirty = patch.kind === 'inline' && current >= 0;
  const load = (index: number) => useSeqStore.getState().loadTrackFactoryPatch(focus.track, index);
  const step = (delta: number) => load(stepFactoryPatchIndex(machine, current, delta));

  return (
    <div className="sq-device-patch" aria-label={`${machine} patch selection`}>
      <span>PATCH</span>
      <button type="button" onClick={() => step(-1)} aria-label="Previous factory patch">◂</button>
      <select value={current} onChange={(event) => load(Number(event.target.value))} aria-label="Factory patch">
        {current < 0 && <option value={-1}>CUSTOM</option>}
        {names.map((name, index) => <option key={`${index}:${name}`} value={index}>{name}</option>)}
      </select>
      {dirty && <span className="sq-device-patch-dirty" title="Patch has unsaved edits" aria-label="edited">*</span>}
      <button type="button" onClick={() => step(1)} aria-label="Next factory patch">▸</button>
    </div>
  );
}

function DeviceModeToggle() {
  const mode = useSeqStore((s) => s.deviceMode);
  const setDeviceMode = useSeqStore.getState().setDeviceMode;
  return (
    <div className="sq-mode-toggle" role="group" aria-label="Device view mode">
      <button type="button" className={mode === 'seq' ? 'on' : ''} aria-pressed={mode === 'seq'} onClick={() => setDeviceMode('seq')}>SEQ</button>
      <button type="button" className={mode === 'edit' ? 'on' : ''} aria-pressed={mode === 'edit'} onClick={() => setDeviceMode('edit')}>EDIT</button>
    </div>
  );
}
