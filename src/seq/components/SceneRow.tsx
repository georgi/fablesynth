// One scene row: the scene card (launch / mute / stop, per-track dots and a
// live status line) followed by one clip cell per track. Cells preview the
// clip's real pattern bytes; live cells sweep at the clip's actual length.

import type * as React from 'react';
import { isTrackAudible, pad2, previewSteps } from '../model';
import { barSeconds } from '../protocol';
import { clipPattern, useSeqStore } from '../store';

function ClipCell({ s, t }: { s: number; t: number }) {
  const session = useSeqStore((st) => st.session);
  const playing = useSeqStore((st) => st.playing);
  const live = useSeqStore((st) => st.owner[t] === s);
  const queued = useSeqStore((st) => st.queue[t] === s || (st.queue[t] === -1 && st.owner[t] === s));
  const muted = useSeqStore(
    (st) => st.owner[t] === s && !isTrackAudible(t, st.owner, st.trackMute, st.sceneMute, st.solo),
  );
  const { launch, stopTrack } = useSeqStore.getState();

  const tr = session.tracks[t];
  const clip = session.scenes[s]?.clips[t];
  const style = { '--tc': tr.color } as React.CSSProperties;

  // Ableton semantics: an empty cell is a stop button (fires on scene
  // launch too); right-click removes it — pass-through lets the previous
  // clip keep playing across the scene change.
  if (!clip) {
    const pass = !!session.scenes[s]?.pass?.includes(t);
    const { togglePassThrough } = useSeqStore.getState();
    return (
      <button
        className={`sq-cell sq-cell-empty${pass ? ' pass' : ''}`}
        style={style}
        onClick={() => { if (!pass) stopTrack(t); }}
        onContextMenu={(e) => { e.preventDefault(); togglePassThrough(s, t); }}
        title={pass
          ? 'Pass-through — previous clip rides through this scene · right-click to restore stop'
          : 'Stop button — stops this track on scene launch · right-click for pass-through'}
      >
        <span>{pass ? '≈' : '■'}</span>
      </button>
    );
  }

  const bytes = clipPattern(session, s, t);
  const steps = bytes ? previewSteps(tr.machine, bytes, clip.bars) : [];

  const cls = ['sq-cell'];
  if (live) cls.push('live');
  if (muted) cls.push('muted');
  const aps = playing ? 'running' : 'paused';

  return (
    <button
      className={cls.join(' ')}
      style={style}
      onClick={() => (live ? stopTrack(t) : launch(t, s))}
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
          <span className="sq-cell-name">{clip.name}</span>
          <span className="sq-cell-len">{clip.bars}B</span>
        </div>
        <div className="sq-steps">
          {steps.map((sp, i) => (
            <span key={i} className={sp.on ? 'on' : ''} style={{ height: `${sp.h}px` }} />
          ))}
        </div>
        <div className="sq-cell-progress">
          {live && (
            <div
              style={{
                animationDuration: `${clip.bars * barSeconds(session.bpm)}s`,
                animationPlayState: aps,
              }}
            />
          )}
        </div>
      </div>
      <span
        className="sq-cell-editbtn"
        role="button"
        tabIndex={0}
        title="Edit clip in its device"
        onClick={(e) => { e.stopPropagation(); useSeqStore.getState().enterFocus(t, s); }}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            if (e.key === ' ') e.preventDefault();
            e.stopPropagation();
            useSeqStore.getState().enterFocus(t, s);
          }
        }}
      >
        ✎
      </span>
      {queued && <div className="sq-cell-queued" />}
      {muted && <span className="sq-cell-mutedtag">MUTED</span>}
    </button>
  );
}

export function SceneRow({ s }: { s: number }) {
  const session = useSeqStore((st) => st.session);
  const owner = useSeqStore((st) => st.owner);
  const trackMute = useSeqStore((st) => st.trackMute);
  const sceneMute = useSeqStore((st) => st.sceneMute);
  const solo = useSeqStore((st) => st.solo);
  const queued = useSeqStore((st) => Object.values(st.queue).includes(s));
  const { launchScene, stopScene, toggleSceneMute } = useSeqStore.getState();

  const sc = session.scenes[s];
  const clipTracks = sc.clips.map((c, t) => (c ? t : -1)).filter((t) => t >= 0);
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
          {session.tracks.map((tr, t) => {
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
      {session.tracks.map((_, t) => (
        <ClipCell key={t} s={s} t={t} />
      ))}
    </div>
  );
}
