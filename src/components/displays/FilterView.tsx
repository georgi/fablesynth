// Filter response view. Redraws only when its parameters or size change.

import { useEffect, useReducer, useRef } from 'react';
import { setupCanvas } from './canvas';

interface FilterViewProps {
  type: number;
  cutoff: number;
  res: number;
  on: boolean;
  accent: string;
  className?: string;
}

export function FilterView({ type, cutoff, res, on, accent, className }: FilterViewProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [resizeTick, bumpResize] = useReducer((x: number) => x + 1, 0);

  useEffect(() => {
    const onResize = () => bumpResize();
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ftype = type | 0;
    const { ctx, w, h } = setupCanvas(canvas);
    ctx.clearRect(0, 0, w, h);
    const pad = 6;
    const k = 2 - 1.93 * res;
    const fmin = 20, fmax = 20000;

    // grid
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 1;
    for (const f of [100, 1000, 10000]) {
      const x = pad + (Math.log(f / fmin) / Math.log(fmax / fmin)) * (w - pad * 2);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
    }

    ctx.beginPath();
    for (let i = 0; i <= 120; i++) {
      const f = fmin * Math.pow(fmax / fmin, i / 120);
      const wn = f / cutoff;
      const den = Math.sqrt(Math.pow(1 - wn * wn, 2) + Math.pow(k * wn, 2));
      let mag;
      switch (ftype) {
        case 0: mag = 1 / den; break;
        case 1: mag = 1 / (den * den); break;
        case 2: mag = (k * wn) / den; break;
        case 3: mag = (wn * wn) / den; break;
        default: mag = Math.abs(1 - wn * wn) / den; break;
      }
      const db = Math.max(-30, Math.min(24, 20 * Math.log10(Math.max(1e-6, mag))));
      const x = pad + (i / 120) * (w - pad * 2);
      const y = h * 0.45 - (db / 30) * h * 0.42;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = on ? accent : '#5a6275';
    ctx.lineWidth = 1.8;
    ctx.shadowColor = accent;
    ctx.shadowBlur = on ? 7 : 0;
    ctx.stroke();
    ctx.shadowBlur = 0;
  }, [type, cutoff, res, on, accent, resizeTick]);

  return <canvas ref={canvasRef} className={className} />;
}
