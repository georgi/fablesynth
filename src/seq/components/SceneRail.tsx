// Focus-mode scene rail: one chip per scene (number + live dot), the focused
// chip lit. Keyboard: ↑/↓ move it (handled in SeqApp).

import { pad2 } from '../model';
import { STEPS_PER_BAR } from '../protocol';
import { useSeqStore } from '../store';

export function SceneRail() {
  const scenes = useSeqStore((s) => s.session.scenes);
  const focus = useSeqStore((s) => s.focus)!;
  const owner = useSeqStore((s) => s.owner);
  const queue = useSeqStore((s) => s.queue);
  const pos = useSeqStore((s) => s.pos[focus.track]);
  const { focusScene, launchScene } = useSeqStore.getState();

  // Progress through the focused track's playing clip, from the device's own
  // pos messages (one per step) rather than a free-running animation, so the
  // fill can't drift from what is actually sounding. CSS eases between steps.
  const playingScene = owner[focus.track];
  const playingClip = playingScene === undefined ? null : scenes[playingScene]?.clips[focus.track];
  const progress = pos && playingClip
    ? Math.min(1, (pos.bar * STEPS_PER_BAR + pos.step + 1) / (playingClip.bars * STEPS_PER_BAR))
    : 0;

  return (
    <div className="sq-rail">
      {scenes.map((sc, s) => {
        const live = Object.values(owner).includes(s);
        const queued = Object.values(queue).includes(s);
        return (
          <div key={s} className={`sq-rail-chip${focus.scene === s ? ' active' : ''}`}>
            <button
              className={`sq-rail-launch${live ? ' live' : ''}${queued ? ' queued' : ''}`}
              onClick={() => launchScene(s)}
              title={`Launch ${sc.name}`}
            >
              ▶
            </button>
            <button
              className="sq-rail-target"
              onClick={() => focusScene(s)}
              title={sc.name}
            >
              {pad2(s + 1)}
              <span className={`sq-rail-dot${live ? ' live' : ''}${queued ? ' queued' : ''}`} />
              {playingScene === s && (
                <span className="sq-rail-progress" style={{ transform: `scaleX(${progress})` }} />
              )}
            </button>
          </div>
        );
      })}
    </div>
  );
}
