// Track header row: the SCENES label card plus one card per track with a
// live LED, machine chip, mute/solo and a volume knob.

import type * as React from 'react';
import { isTrackAudible, TRACKS } from '../model';
import { useSeqStore } from '../store';
import { SeqKnob } from './SeqKnob';

export function TrackHeads() {
  const owner = useSeqStore((s) => s.owner);
  const playing = useSeqStore((s) => s.playing);
  const trackMute = useSeqStore((s) => s.trackMute);
  const sceneMute = useSeqStore((s) => s.sceneMute);
  const solo = useSeqStore((s) => s.solo);
  const trackVol = useSeqStore((s) => s.trackVol);
  const { toggleTrackMute, toggleSolo, setTrackVol } = useSeqStore.getState();

  return (
    <div className="sq-grid sq-heads">
      <div className="sq-scenes-card">
        <div className="sq-scenes-title">SCENES</div>
        <div className="sq-scenes-sub">STACK FREELY · LATEST CLIP OWNS THE TRACK</div>
      </div>
      {TRACKS.map((tr, t) => {
        const audible = playing && isTrackAudible(t, owner, trackMute, sceneMute, solo);
        return (
          <div key={tr.name} className="sq-track-head" style={{ '--tc': tr.color } as React.CSSProperties}>
            <span className={`sq-led${audible ? ' on' : ''}`} />
            <div className="sq-track-id">
              <div className="sq-track-name-row">
                <span className="sq-track-name">{tr.name}</span>
                <span className="sq-machine-chip">{tr.machine}</span>
              </div>
              <div className="sq-track-patch">{tr.patch}</div>
            </div>
            <button
              className={`sq-mini sq-mute${trackMute[t] ? ' on' : ''}`}
              onClick={() => toggleTrackMute(t)}
              title="Mute track"
            >
              M
            </button>
            <button
              className={`sq-mini sq-solo${solo[t] ? ' on' : ''}`}
              onClick={() => toggleSolo(t)}
              title="Solo track"
            >
              S
            </button>
            <SeqKnob value={trackVol[t]} onChange={(v) => setTrackVol(t, v)} label="VOL" size="xs" defaultValue={tr.vol} />
          </div>
        );
      })}
    </div>
  );
}
