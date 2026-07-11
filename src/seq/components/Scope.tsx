// Master "SUM" oscilloscope in the top bar. One colored trace per audible
// track; the phase accumulator freezes while the clock is paused so the
// display stops with the transport. Drawn imperatively at rAF rate — the
// component itself never re-renders.

import { useEffect, useRef } from 'react';
import { isTrackAudible, TRACKS } from '../model';
import { useSeqStore } from '../store';

const FREQS = [5.2, 2.1, 3.6, 1.2];

export function Scope() {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const cv = canvasRef.current;
    if (!cv) return;
    let raf = 0;
    let phase = 0;
    let last = performance.now();

    const draw = (now: number) => {
      raf = requestAnimationFrame(draw);
      const st = useSeqStore.getState();
      if (st.playing) phase += now - last;
      last = now;

      const w = cv.clientWidth, h = cv.clientHeight;
      if (!w || !h) return;
      if (cv.width !== w * 2) { cv.width = w * 2; cv.height = h * 2; }
      const x = cv.getContext('2d');
      if (!x) return;
      x.setTransform(2, 0, 0, 2, 0, 0);
      x.clearRect(0, 0, w, h);
      x.strokeStyle = 'rgba(255,255,255,0.07)';
      x.lineWidth = 1;
      x.beginPath();
      x.moveTo(0, h / 2);
      x.lineTo(w, h / 2);
      x.stroke();

      const ph = phase * 0.001;
      const master = 0.3 + 0.7 * st.masterVol;
      TRACKS.forEach((tr, t) => {
        if (!isTrackAudible(t, st.owner, st.trackMute, st.sceneMute, st.solo)) return;
        const amp = h * 0.3 * (0.3 + 0.7 * st.trackVol[t]) * master;
        x.strokeStyle = tr.color;
        x.globalAlpha = 0.8;
        x.lineWidth = 1.4;
        x.beginPath();
        for (let i = 0; i <= w; i += 2) {
          const y = h / 2
            + Math.sin((i / w) * Math.PI * 2 * FREQS[t] + ph * (2 + t * 0.7))
              * amp * (0.5 + 0.5 * Math.sin(ph * 1.3 + t * 2));
          if (i === 0) x.moveTo(i, y);
          else x.lineTo(i, y);
        }
        x.stroke();
      });
      x.globalAlpha = 1;
    };

    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, []);

  return (
    <div className="sq-scope">
      <canvas ref={canvasRef} />
      <span className="sq-scope-tag">SUM</span>
    </div>
  );
}
