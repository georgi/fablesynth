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
