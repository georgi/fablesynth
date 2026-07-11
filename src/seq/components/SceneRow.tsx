// One scene row: the scene card (launch / mute / stop, per-track dots and a
// live status line) followed by one clip cell per track.

import type * as React from 'react';
import {
  BAR_SEC, isTrackAudible, pad2, SCENES, STOP, stepsFor, TRACKS,
} from '../model';
import { useSeqStore } from '../store';

function ClipCell({ s, t }: { s: number; t: number }) {
  const tr = TRACKS[t];
  const clip = SCENES[s].clips[t];
  const playing = useSeqStore((st) => st.playing);
  const live = useSeqStore((st) => st.owner[t] === s);
  const queued = useSeqStore(
    (st) => st.queue[t] === s || (st.queue[t] === STOP && st.owner[t] === s),
  );
  const muted = useSeqStore(
    (st) => st.owner[t] === s && !isTrackAudible(t, st.owner, st.trackMute, st.sceneMute, st.solo),
  );
  const { setClip, stopTrack } = useSeqStore.getState();

  const style = { '--tc': tr.color } as React.CSSProperties;

  if (!clip) {
    return (
      <button className="sq-cell sq-cell-empty" style={style} onClick={() => stopTrack(t)} title="Stop track">
        <span>■</span>
      </button>
    );
  }

  const cls = ['sq-cell'];
  if (live) cls.push('live');
  if (muted) cls.push('muted');
  const aps = playing ? 'running' : 'paused';

  return (
    <button
      className={cls.join(' ')}
      style={style}
      onClick={() => (live ? stopTrack(t) : setClip(t, s))}
      title={live ? 'Stop clip' : 'Launch clip'}
    >
      <div className="sq-cell-body">
        <div className="sq-cell-head">
          {live ? (
            <span className="sq-eq" style={{ animationPlayState: aps }}>
              <i style={{ animationPlayState: aps }} />
              <i style={{ animationPlayState: aps }} />
              <i style={{ animationPlayState: aps }} />
            </span>
          ) : (
            <span className="sq-cell-idle">▶</span>
          )}
          <span className="sq-cell-name">{clip.n}</span>
          <span className="sq-cell-len">{clip.b}B</span>
        </div>
        <div className="sq-steps">
          {stepsFor(t, s).map((sp, i) => (
            <span key={i} className={sp.on ? 'on' : ''} style={{ height: `${sp.h}px` }} />
          ))}
        </div>
        <div className="sq-cell-progress">
          {live && (
            <div
              style={{ animationDuration: `${clip.b * BAR_SEC}s`, animationPlayState: aps }}
            />
          )}
        </div>
      </div>
      {queued && <div className="sq-cell-queued" />}
      {muted && <span className="sq-cell-mutedtag">MUTED</span>}
    </button>
  );
}

export function SceneRow({ s }: { s: number }) {
  const sc = SCENES[s];
  const clipTracks = sc.clips.map((c, t) => (c ? t : -1)).filter((t) => t >= 0);

  const owner = useSeqStore((st) => st.owner);
  const trackMute = useSeqStore((st) => st.trackMute);
  const sceneMute = useSeqStore((st) => st.sceneMute);
  const solo = useSeqStore((st) => st.solo);
  const queued = useSeqStore((st) => Object.values(st.queue).includes(s));
  const { launchScene, stopScene, toggleSceneMute } = useSeqStore.getState();

  const owned = clipTracks.filter((t) => owner[t] === s);
  const muted = !!sceneMute[s];
  const liveAny = owned.length > 0;
  const full = liveAny && owned.length === clipTracks.length;

  let status: string, statusCls: string;
  if (muted && liveAny) { status = 'LIVE · MUTED'; statusCls = 'warn'; }
  else if (muted) { status = 'MUTED'; statusCls = 'warn'; }
  else if (queued) { status = 'QUEUED'; statusCls = 'lit'; }
  else if (full) { status = 'LIVE'; statusCls = 'live'; }
  else if (liveAny) { status = `LIVE ${owned.length}/${clipTracks.length}`; statusCls = 'live'; }
  else { status = 'READY'; statusCls = ''; }

  const hot = liveAny && !muted;

  return (
    <div className="sq-grid">
      <div className={`sq-scene-card${hot ? ' live' : ''}`}>
        <div className="sq-scene-top">
          <button
            className={`sq-scene-launch${hot ? ' live' : ''}${queued ? ' queued' : ''}`}
            onClick={() => launchScene(s)}
            title="Launch scene"
          >
            ▶
          </button>
          <div className="sq-scene-id">
            <div className="sq-scene-name-row">
              <span className="sq-scene-num">{pad2(s + 1)}</span>
              <span className="sq-scene-name">{sc.name}</span>
            </div>
            <div className={`sq-scene-status ${statusCls}`}>{status}</div>
          </div>
          <button
            className={`sq-mini sq-mute${muted ? ' on' : ''}`}
            onClick={() => toggleSceneMute(s)}
            title="Mute scene"
          >
            M
          </button>
          <button className="sq-mini" onClick={() => stopScene(s)} title="Stop scene">■</button>
        </div>
        <div className="sq-scene-dots">
          {TRACKS.map((tr, t) => {
            const has = !!sc.clips[t];
            const on = has && owner[t] === s
              && isTrackAudible(t, owner, trackMute, sceneMute, solo);
            return (
              <span
                key={t}
                className={`sq-dot${on ? ' on' : ''}`}
                style={{
                  background: !has ? '#1a1f2a' : on ? tr.color : `${tr.color}44`,
                  boxShadow: on ? `0 0 8px ${tr.color}` : 'none',
                }}
              />
            );
          })}
          <span className="sq-clip-count">
            {clipTracks.length} {clipTracks.length === 1 ? 'CLIP' : 'CLIPS'}
          </span>
        </div>
      </div>
      {TRACKS.map((_, t) => (
        <ClipCell key={t} s={s} t={t} />
      ))}
    </div>
  );
}
