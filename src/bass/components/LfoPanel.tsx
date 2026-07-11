// LFO panel: bar-locked LFO → cutoff. Waveform display animates in sync with
// the sequencer tempo while playing.

import { useEffect, useRef } from 'react';
import { setupCanvas } from '../../components/displays/canvas';
import { LFO_DIV_F } from '../../params';
import { useBassStore } from '../store';
import { BassKnob } from './BassKnob';
import { BassStepper } from './BassStepper';

function shapeValue(shape: number, phase: number): number {
  const p = phase - Math.floor(phase);
  switch (shape) {
    case 1: return 1 - 4 * Math.abs(p - 0.5);
    case 2: return 1 - 2 * p;
    case 3: return p < 0.5 ? 1 : -1;
    case 4: return Math.sin(Math.floor(phase * 4) * 999.9) ; // pseudo s&h preview
    default: return Math.sin(p * 2 * Math.PI);
  }
}

function LfoView() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const shape = useBassStore((s) => s.params['lfo.shape']);
  const rate = useBassStore((s) => s.params['lfo.rate']);
  const bpm = useBassStore((s) => s.params['seq.bpm']);
  const playing = useBassStore((s) => s.playing);
  const propsRef = useRef({ shape, rate, bpm, playing });
  propsRef.current = { shape, rate, bpm, playing };

  useEffect(() => {
    let raf = 0;
    const t0 = performance.now();
    const draw = (canvas: HTMLCanvasElement) => {
      const { ctx, w, h } = setupCanvas(canvas);
      ctx.clearRect(0, 0, w, h);
      const p = propsRef.current;
      const cpb = LFO_DIV_F[p.rate | 0] || 2;
      const ph0 = p.playing
        ? (((performance.now() - t0) / 1000) * (p.bpm / 60) * cpb) % 1
        : 0;
      ctx.beginPath();
      for (let i = 0; i <= 100; i++) {
        const q = i / 100;
        const y = h / 2 - shapeValue(p.shape | 0, q * 2 - ph0) * h * 0.34;
        const x = q * w;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = '#4dff9e';
      ctx.lineWidth = 1.3;
      ctx.globalAlpha = 0.9;
      ctx.shadowColor = '#4dff9e';
      ctx.shadowBlur = 5;
      ctx.stroke();
      ctx.shadowBlur = 0;
      ctx.globalAlpha = 1;
    };
    const frame = () => {
      const canvas = canvasRef.current;
      if (canvas) draw(canvas);
      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, []);

  return <canvas ref={canvasRef} className="bl-lfo-view" />;
}

export function LfoPanel() {
  return (
    <section className="panel bl-lfo-section">
      <div className="panel-head">
        <h2>LFO</h2>
        <span className="bl-head-note">→ CUTOFF · BAR-LOCKED</span>
      </div>
      <div className="bl-lfo-body">
        <div className="bl-lfo-left">
          <LfoView />
          <div className="bl-lfo-steppers">
            <BassStepper paramId="lfo.rate" accent="a" />
            <BassStepper paramId="lfo.shape" accent="n" />
          </div>
        </div>
        <BassKnob paramId="lfo.depth" size="md" accent="a" />
      </div>
    </section>
  );
}

export function AccentPanel() {
  return (
    <section className="panel bl-acc-section" data-accent="a">
      <div className="panel-head">
        <span className="bl-led bl-led-a" aria-hidden="true" />
        <h2>ACCENT · SLIDE</h2>
      </div>
      <div className="knob-row bl-acc-knobs">
        <BassKnob paramId="acc.amt" size="lg" accent="a" />
        <BassKnob paramId="slide.time" size="md" accent="n" />
      </div>
      <div className="bl-acc-hint">ONE KNOB · LEVEL + ENV + DECAY</div>
    </section>
  );
}
