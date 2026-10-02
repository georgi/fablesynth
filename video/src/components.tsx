import React from 'react';
import { AbsoluteFill, Img, interpolate, spring, staticFile, useCurrentFrame, useVideoConfig, Easing } from 'remotion';
import manifest from '../public/capture/manifest.json';
import { BEAT, C, DISPLAY, MONO } from './theme';

type Clip = { frames: number; times: number[]; w: number; h: number };
const clips = manifest as Record<string, Clip>;
export const clipSize = (name: string) => clips[name];

const clamp = { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' } as const;
export const ease = Easing.bezier(0.16, 1, 0.3, 1);
export const lerp = (f: number, a: number, b: number, from: number, to: number, e = ease) =>
  interpolate(f, [a, b], [from, to], { ...clamp, easing: e });

/** Beat pulse 1 → 0 decaying after every quarter note of the soundtrack. */
export const usePulse = () => {
  const f = useCurrentFrame();
  return Math.exp(-(f % BEAT) / 4);
};

/** Plays a captured UI sequence, resampled by its real capture timestamps. */
export const Capture: React.FC<{ name: string; offsetMs?: number; rate?: number; style?: React.CSSProperties }> = ({
  name,
  offsetMs = 0,
  rate = 1,
  style,
}) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const c = clips[name];
  const span = c.times[c.times.length - 1] + 30;
  const t = (offsetMs + (f / fps) * 1000 * rate) % span;
  let i = 0;
  while (i + 1 < c.frames && c.times[i + 1] <= t) i++;
  return (
    <Img
      src={staticFile(`capture/${name}/${String(i).padStart(3, '0')}.jpg`)}
      style={{ width: '100%', height: '100%', objectFit: 'cover', objectPosition: 'top left', display: 'block', ...style }}
    />
  );
};

/** A floating piece of real UI with an accent glow and 3D placement. */
export const Card: React.FC<{
  name?: string;
  still?: string;
  width: number;
  height?: number;
  accent?: string;
  x?: number;
  y?: number;
  z?: number;
  rx?: number;
  ry?: number;
  rz?: number;
  scale?: number;
  opacity?: number;
  offsetMs?: number;
  rate?: number;
  crop?: { x: number; y: number; w: number; h: number; sw: number; sh: number };
  glow?: number;
  children?: React.ReactNode;
}> = ({ children, name, still, width, height, accent = C.cyan, x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, scale = 1, opacity = 1, offsetMs, rate, crop, glow = 1 }) => {
  const pulse = usePulse();
  const size = name ? clips[name] : undefined;
  const h = height ?? (crop ? (width * crop.h) / crop.w : size ? (width * size.h) / size.w : width * 0.5625);
  const g = glow * (0.55 + 0.45 * pulse);
  let inner: React.ReactNode;
  if (crop && still) {
    const k = width / crop.w;
    inner = (
      <Img
        src={staticFile(still)}
        style={{ position: 'absolute', left: -crop.x * k, top: -crop.y * k, width: crop.sw * k, height: crop.sh * k }}
      />
    );
  } else if (still) {
    inner = <Img src={staticFile(still)} style={{ width: '100%', height: '100%', objectFit: 'cover', objectPosition: 'top' }} />;
  } else {
    inner = <Capture name={name!} offsetMs={offsetMs} rate={rate} />;
  }
  return (
    <div
      style={{
        position: 'absolute',
        left: '50%',
        top: '50%',
        width,
        height: h,
        marginLeft: -width / 2,
        marginTop: -h / 2,
        opacity,
        transform: `translate3d(${x}px, ${y}px, ${z}px) rotateX(${rx}deg) rotateY(${ry}deg) rotateZ(${rz}deg) scale(${scale})`,
        transformStyle: 'preserve-3d',
        borderRadius: 14,
        overflow: 'hidden',
        background: C.void,
        boxShadow: `0 0 0 1px ${accent}${Math.round(40 + 60 * g).toString(16)}, 0 40px 90px rgba(0,0,0,0.7), 0 0 ${50 + 40 * g}px ${accent}${Math.round(18 + 30 * g).toString(16)}`,
      }}
    >
      {inner}
      {children}
      <div
        style={{
          position: 'absolute',
          inset: 0,
          background: 'linear-gradient(115deg, rgba(255,255,255,0.07) 0%, rgba(255,255,255,0) 35%)',
          pointerEvents: 'none',
        }}
      />
    </div>
  );
};

export const Stage: React.FC<{ children: React.ReactNode; perspective?: number }> = ({ children, perspective = 2000 }) => (
  <AbsoluteFill style={{ perspective, perspectiveOrigin: '50% 45%' }}>
    <AbsoluteFill style={{ transformStyle: 'preserve-3d' }}>{children}</AbsoluteFill>
  </AbsoluteFill>
);

export const Background: React.FC<{ accent?: string; accent2?: string }> = ({ accent = C.cyan, accent2 = C.violet }) => {
  const f = useCurrentFrame();
  const pulse = usePulse();
  return (
    <AbsoluteFill style={{ background: C.void, overflow: 'hidden' }}>
      <AbsoluteFill
        style={{
          background: `radial-gradient(900px 600px at ${30 + 8 * Math.sin(f / 70)}% ${35 + 6 * Math.cos(f / 90)}%, ${accent}${Math.round(26 + 14 * pulse).toString(16)}, transparent 70%),
            radial-gradient(800px 700px at ${75 + 6 * Math.cos(f / 80)}% 70%, ${accent2}1c, transparent 70%)`,
        }}
      />
      <div
        style={{
          position: 'absolute',
          left: '-50%',
          width: '200%',
          top: '55%',
          height: '120%',
          transform: 'perspective(600px) rotateX(72deg)',
          transformOrigin: '50% 0%',
          backgroundImage: `linear-gradient(${accent}22 1px, transparent 1px), linear-gradient(90deg, ${accent}22 1px, transparent 1px)`,
          backgroundSize: '80px 80px',
          backgroundPosition: `0 ${(f * 2) % 80}px`,
          maskImage: 'linear-gradient(to bottom, transparent, black 30%, black 60%, transparent)',
          opacity: 0.55,
        }}
      />
      <AbsoluteFill style={{ background: 'radial-gradient(ellipse at center, transparent 50%, rgba(0,0,0,0.75) 100%)' }} />
    </AbsoluteFill>
  );
};

/** Wavetable terrain: stacked morphing waves, echoing the WT-1 display. */
export const Terrain: React.FC<{ width?: number; height?: number; color?: string; lines?: number; t?: number; draw?: number }> = ({
  width = 1400,
  height = 520,
  color = C.cyan,
  lines = 28,
  t,
  draw = 1,
}) => {
  const f = useCurrentFrame();
  const time = t ?? f / 30;
  const pts = 140;
  const paths: React.ReactNode[] = [];
  for (let l = lines - 1; l >= 0; l--) {
    const depth = l / (lines - 1);
    const ox = depth * width * 0.18;
    const oy = -depth * height * 0.55;
    const w = width * 0.78;
    const morph = depth * 2.2 + time * 0.6;
    let d = '';
    for (let i = 0; i <= pts; i++) {
      const u = i / pts;
      const ph = u * Math.PI * 2;
      const v =
        Math.sin(ph) * (1 - 0.4 * Math.sin(morph)) +
        0.45 * Math.sin(ph * 2 + morph) * Math.cos(morph * 0.7) +
        0.25 * Math.sin(ph * 5 + morph * 1.3) * Math.sin(morph * 0.5);
      const px = ox + u * w;
      const py = height * 0.78 + oy - v * height * 0.16;
      d += `${i ? 'L' : 'M'}${px.toFixed(1)},${py.toFixed(1)}`;
    }
    const front = l === 0;
    paths.push(
      <path
        key={l}
        d={d}
        fill="none"
        stroke={color}
        strokeWidth={front ? 4 : 1.2}
        strokeOpacity={front ? 1 : 0.12 + 0.5 * (1 - depth)}
        pathLength={1}
        strokeDasharray={1}
        strokeDashoffset={1 - Math.min(1, Math.max(0, draw * 1.6 - depth * 0.6))}
        filter={front ? 'url(#glow)' : undefined}
      />,
    );
  }
  return (
    <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} style={{ overflow: 'visible' }}>
      <defs>
        <filter id="glow" x="-20%" y="-50%" width="140%" height="200%">
          <feGaussianBlur stdDeviation="6" result="b" />
          <feMerge>
            <feMergeNode in="b" />
            <feMergeNode in="SourceGraphic" />
          </feMerge>
        </filter>
      </defs>
      {paths}
    </svg>
  );
};

export const Kicker: React.FC<{ text: string; color?: string; delay?: number; size?: number }> = ({ text, color = C.cyan, delay = 0, size = 20 }) => {
  const f = useCurrentFrame() - delay;
  const w = lerp(f, 0, 14, 0, 60);
  const chars = Math.floor(lerp(f, 4, 4 + text.length * 0.8, 0, text.length, Easing.linear));
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 16, fontFamily: MONO, fontWeight: 600, fontSize: size, letterSpacing: '0.28em', color }}>
      <div style={{ width: w, height: 2, background: color, boxShadow: `0 0 12px ${color}` }} />
      <span>
        {text.slice(0, chars)}
        <span style={{ opacity: chars < text.length ? 1 : 0 }}>▍</span>
      </span>
    </div>
  );
};

/** Michroma headline with a per-letter rise from a mask. */
export const Headline: React.FC<{
  text: string;
  size?: number;
  color?: string;
  highlight?: { from: number; color: string };
  delay?: number;
  stagger?: number;
  align?: 'left' | 'center';
  out?: number;
}> = ({ text, size = 72, color = C.ice, highlight, delay = 0, stagger = 1.1, align = 'left', out }) => {
  const f = useCurrentFrame() - delay;
  const { fps } = useVideoConfig();
  const lines = text.split('\n');
  let idx = 0;
  return (
    <div style={{ fontFamily: DISPLAY, fontSize: size, letterSpacing: '0.12em', lineHeight: 1.15, color, textAlign: align }}>
      {lines.map((line, li) => (
        <div key={li} style={{ overflow: 'hidden', paddingBottom: size * 0.08 }}>
          {line.split('').map((ch) => {
            const k = idx++;
            const s = spring({ frame: f - k * stagger, fps, config: { damping: 18, stiffness: 160 } });
            const o = out !== undefined ? lerp(f, out + k * 0.5, out + 8 + k * 0.5, 0, 1) : 0;
            const col = highlight && k >= highlight.from ? highlight.color : color;
            return (
              <span
                key={k}
                style={{
                  display: 'inline-block',
                  whiteSpace: 'pre',
                  transform: `translateY(${(1 - s) * 110 - o * 110}%)`,
                  color: col,
                  textShadow: col !== color || highlight ? `0 0 30px ${col}55` : undefined,
                }}
              >
                {ch}
              </span>
            );
          })}
        </div>
      ))}
    </div>
  );
};

export const Sub: React.FC<{ text: string; delay?: number; color?: string; size?: number }> = ({ text, delay = 0, color = C.dim, size = 22 }) => {
  const f = useCurrentFrame() - delay;
  return (
    <div
      style={{
        fontFamily: MONO,
        fontSize: size,
        letterSpacing: '0.18em',
        color,
        opacity: lerp(f, 0, 14, 0, 1),
        transform: `translateY(${lerp(f, 0, 18, 18, 0)}px)`,
      }}
    >
      {text}
    </div>
  );
};

/** Short accent flash that sells a cut on the downbeat. */
export const Flash: React.FC<{ color?: string; dur?: number; peak?: number }> = ({ color = '#ffffff', dur = 10, peak = 0.35 }) => {
  const f = useCurrentFrame();
  return <AbsoluteFill style={{ background: color, opacity: lerp(f, 0, dur, peak, 0, Easing.out(Easing.quad)), mixBlendMode: 'screen' }} />;
};

/** Thin animated scan line used as a section wipe. */
export const Wipe: React.FC<{ color: string; delay?: number }> = ({ color, delay = 0 }) => {
  const f = useCurrentFrame() - delay;
  const x = lerp(f, 0, 16, -10, 110, Easing.inOut(Easing.cubic));
  return (
    <AbsoluteFill style={{ pointerEvents: 'none' }}>
      <div
        style={{
          position: 'absolute',
          left: `${x}%`,
          top: 0,
          bottom: 0,
          width: 3,
          background: color,
          boxShadow: `0 0 40px 12px ${color}88`,
          opacity: f < 0 || f > 16 ? 0 : 1,
        }}
      />
    </AbsoluteFill>
  );
};

/** Callout: a dot on the UI, a leader line and a mono label. */
export const Callout: React.FC<{ x: number; y: number; dx: number; dy: number; label: string; color: string; delay?: number }> = ({
  x,
  y,
  dx,
  dy,
  label,
  color,
  delay = 0,
}) => {
  const f = useCurrentFrame() - delay;
  const p = lerp(f, 0, 14, 0, 1);
  const tx = x + dx * p;
  const ty = y + dy * p;
  return (
    <AbsoluteFill style={{ pointerEvents: 'none' }}>
      <svg width={1920} height={1080} style={{ position: 'absolute', inset: 0 }}>
        <circle cx={x} cy={y} r={7 * Math.min(1, f / 5)} fill={color} style={{ filter: `drop-shadow(0 0 8px ${color})` }} />
        <circle cx={x} cy={y} r={7 + 18 * ((f % 30) / 30)} fill="none" stroke={color} strokeOpacity={1 - (f % 30) / 30} />
        <line x1={x} y1={y} x2={tx} y2={ty} stroke={color} strokeWidth={1.5} />
      </svg>
      <div
        style={{
          position: 'absolute',
          left: x + dx + (dx >= 0 ? 12 : -12),
          top: y + dy - 14,
          transform: dx < 0 ? 'translateX(-100%)' : undefined,
          fontFamily: MONO,
          fontWeight: 600,
          fontSize: 18,
          letterSpacing: '0.2em',
          color: C.ice,
          background: 'rgba(6,7,11,0.82)',
          border: `1px solid ${color}66`,
          padding: '5px 12px',
          borderRadius: 6,
          opacity: lerp(f, 10, 18, 0, 1),
          whiteSpace: 'nowrap',
        }}
      >
        {label}
      </div>
    </AbsoluteFill>
  );
};

export { clamp };
