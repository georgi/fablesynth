// Now-playing footer: stop-all + live scene chips on the left, then one
// cell per track showing its owning scene/clip and a VU meter.

import type * as React from 'react';
import { isTrackAudible, pad2, SCENES, TRACKS } from '../model';
import { useSeqStore } from '../store';

const VU_DUR = [0.9, 1.35, 0.75, 1.7];

export function FooterRow() {
  const playing = useSeqStore((s) => s.playing);
  const owner = useSeqStore((s) => s.owner);
  const trackMute = useSeqStore((s) => s.trackMute);
  const sceneMute = useSeqStore((s) => s.sceneMute);
  const solo = useSeqStore((s) => s.solo);
  const { stopAll, stopTrack } = useSeqStore.getState();

  const liveScenes = SCENES
    .map((sc, s) => ({ sc, s }))
    .filter(({ s }) => Object.values(owner).includes(s));

  return (
    <div className="sq-grid">
      <div className="sq-foot-master">
        <button className="sq-stopall" onClick={stopAll}>■ STOP ALL</button>
        <div className="sq-live-chips">
          <span className="sq-live-tag">LIVE</span>
          {liveScenes.length ? (
            liveScenes.map(({ sc, s }) => (
              <span key={s} className="sq-live-chip">
                {pad2(s + 1)} {sc.name}{sceneMute[s] ? ' ·M' : ''}
              </span>
            ))
          ) : (
            <span className="sq-live-chip">—</span>
          )}
        </div>
      </div>
      {TRACKS.map((tr, t) => {
        const o = owner[t];
        const live = o != null;
        const audible = live && isTrackAudible(t, owner, trackMute, sceneMute, solo);
        const clip = live ? SCENES[o].clips[t] : null;
        return (
          <div key={tr.name} className="sq-foot-cell" style={{ '--tc': tr.color } as React.CSSProperties}>
            <button className="sq-mini" onClick={() => stopTrack(t)} title="Stop track">■</button>
            <div className="sq-foot-now">
              <div className="sq-foot-tag">NOW</div>
              <div className={`sq-foot-owner${audible ? ' on' : ''}`}>
                {live ? `${pad2(o + 1)} ${SCENES[o].name} · ${clip ? clip.n : ''}` : '—'}
              </div>
            </div>
            <div className="sq-vu">
              <div
                className={audible && playing ? `vu${(t % 3) + 1}` : ''}
                style={{ animationDuration: `${VU_DUR[t]}s` }}
              />
            </div>
          </div>
        );
      })}
    </div>
  );
}
