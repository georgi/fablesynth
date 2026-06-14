// Oscilloscope. Reads the time-domain analyser every frame.

import { useEffect, useRef } from 'react';
import { setupCanvas } from './canvas';

interface ScopeViewProps {
  analyser: AnalyserNode;
  accent: string;
}

export function ScopeView({ analyser, accent }: ScopeViewProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const buf = new Float32Array(analyser.fftSize);
    let raf = 0;
    const draw = (canvas: HTMLCanvasElement) => {
      const { ctx, w, h } = setupCanvas(canvas);
      analyser.getFloatTimeDomainData(buf);
      ctx.clearRect(0, 0, w, h);
      // stable trigger: find rising zero-crossing in first half
      let start = 0;
      for (let i = 1; i < buf.length / 2; i++) {
        if (buf[i - 1] <= 0 && buf[i] > 0) { start = i; break; }
      }
      const N = Math.min(900, buf.length - start);
      ctx.beginPath();
      for (let i = 0; i < N; i++) {
        const x = (i / (N - 1)) * w;
        const y = h / 2 - buf[start + i] * h * 0.46;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = accent;
      ctx.lineWidth = 1.2;
      ctx.globalAlpha = 0.95;
      ctx.stroke();
      ctx.globalAlpha = 1;
    };
    const frame = () => {
      const canvas = canvasRef.current;
      if (canvas) draw(canvas);
      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, [analyser, accent]);

  return <canvas ref={canvasRef} />;
}
