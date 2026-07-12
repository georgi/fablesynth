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
