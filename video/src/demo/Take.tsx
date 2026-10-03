// Plays one recorded performance: the 4K screencast behind a virtual camera,
// the real cursor path, value readouts and a spectrum computed from the very
// audio this take produced.
import React from 'react';
import { AbsoluteFill, Audio, Easing, Img, interpolate, staticFile, useCurrentFrame, useVideoConfig } from 'remotion';
import { C, DISPLAY, MONO } from '../theme';

import wtMorph from '../../public/takes/wt-morph/take.json';
import blSweep from '../../public/takes/bl-sweep/take.json';
import wtMod from '../../public/takes/wt-mod/take.json';
import wtFx from '../../public/takes/wt-fx/take.json';
import drBuild from '../../public/takes/dr-build/take.json';
import sqAuto from '../../public/takes/sq-auto/take.json';
import sqLaunch from '../../public/takes/sq-launch/take.json';
import wtShift from '../../public/takes/wt-shift/take.json';
import wtGlitch from '../../public/takes/wt-glitch/take.json';
import vMorph from '../../public/takes/wt-morph/values.json';
import vSweep from '../../public/takes/bl-sweep/values.json';
import vMod from '../../public/takes/wt-mod/values.json';
import vFx from '../../public/takes/wt-fx/values.json';
import vShift from '../../public/takes/wt-shift/values.json';
import vGlitch from '../../public/takes/wt-glitch/values.json';
import sMorph from '../../public/takes/wt-morph/spec.json';
import sSweep from '../../public/takes/bl-sweep/spec.json';
import sMod from '../../public/takes/wt-mod/spec.json';
import sFx from '../../public/takes/wt-fx/spec.json';
import sDr from '../../public/takes/dr-build/spec.json';
import sAuto from '../../public/takes/sq-auto/spec.json';
import sLaunch from '../../public/takes/sq-launch/spec.json';
import sShift from '../../public/takes/wt-shift/spec.json';
import sGlitch from '../../public/takes/wt-glitch/spec.json';

type Pt = { t: number; x: number; y: number; d: number };
type TakeMeta = { dur: number; times: number[]; cursor: Pt[]; marks: Record<string, number> };
type Spec = { fps: number; bands: number[][]; rms: number[] };
export type TakeName = 'wt-morph' | 'bl-sweep' | 'wt-mod' | 'wt-fx' | 'dr-build' | 'sq-auto' | 'sq-launch' | 'wt-shift' | 'wt-glitch';

export const TAKES: Record<TakeName, TakeMeta> = {
  'wt-morph': wtMorph as TakeMeta,
  'bl-sweep': blSweep as TakeMeta,
  'wt-mod': wtMod as TakeMeta,
  'wt-fx': wtFx as TakeMeta,
  'dr-build': drBuild as TakeMeta,
  'sq-auto': sqAuto as TakeMeta,
  'sq-launch': sqLaunch as TakeMeta,
  'wt-shift': wtShift as TakeMeta,
  'wt-glitch': wtGlitch as TakeMeta,
};
export const VALUES: Partial<Record<TakeName, Record<string, string | number | null>[]>> = {
  'wt-morph': vMorph,
  'bl-sweep': vSweep,
  'wt-mod': vMod,
  'wt-fx': vFx,
  'wt-shift': vShift,
  'wt-glitch': vGlitch,
};
const SPECS: Record<TakeName, Spec> = {
  'wt-morph': sMorph, 'bl-sweep': sSweep, 'wt-mod': sMod, 'wt-fx': sFx, 'dr-build': sDr, 'sq-auto': sAuto, 'sq-launch': sLaunch, 'wt-shift': sShift, 'wt-glitch': sGlitch,
};

/** Take time (ms) at the current frame of a scene that starts at `offsetMs`. */
export const useTakeMs = (offsetMs: number) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  return offsetMs + (f / fps) * 1000;
};

const lastIndexAtOrBefore = (arr: { t: number }[] | number[], t: number) => {
  let lo = 0, hi = arr.length - 1;
  const at = (i: number) => (typeof arr[i] === 'number' ? (arr[i] as number) : (arr[i] as { t: number }).t);
  if (at(0) > t) return 0;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (at(mid) <= t) lo = mid; else hi = mid - 1;
  }
  return lo;
};

export const cursorAt = (name: TakeName, t: number) => {
  const c = TAKES[name].cursor;
  if (!c.length) return { x: 960, y: 1200, d: 0 };
  const i = lastIndexAtOrBefore(c, t);
  const a = c[i], b = c[Math.min(c.length - 1, i + 1)];
  const u = b.t > a.t ? Math.min(1, Math.max(0, (t - a.t) / (b.t - a.t))) : 0;
  return { x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u, d: a.d };
};

export const valueAt = (name: TakeName, key: string, t: number) => {
  const v = VALUES[name];
  if (!v?.length) return null;
  return v[lastIndexAtOrBefore(v as { t: number }[], t)][key] as string | null;
};

/** Camera keyframe in take time: centre (x, y) in CSS px, zoom z, and a "card" amount (floating device look). */
export type Cam = { t: number; x: number; y: number; z: number; card?: number; ry?: number };
const camAt = (keys: Cam[], t: number) => {
  if (t <= keys[0].t) return { card: 0, ry: 0, ...keys[0] };
  for (let i = 0; i < keys.length - 1; i++) {
    const a = keys[i], b = keys[i + 1];
    if (t <= b.t) {
      const u = Easing.inOut(Easing.cubic)((t - a.t) / (b.t - a.t));
      const L = (p: number, q: number) => p + (q - p) * u;
      return { t, x: L(a.x, b.x), y: L(a.y, b.y), z: L(a.z, b.z), card: L(a.card ?? 0, b.card ?? 0), ry: L(a.ry ?? 0, b.ry ?? 0) };
    }
  }
  const k = keys[keys.length - 1];
  return { card: 0, ry: 0, ...k };
};

const Pointer: React.FC<{ down: boolean; color: string; scale: number }> = ({ down, color, scale }) => (
  <svg width={34} height={40} viewBox="0 0 34 40" style={{ transform: `scale(${scale * (down ? 0.88 : 1)})`, transformOrigin: '3px 3px', overflow: 'visible', filter: `drop-shadow(0 4px 10px rgba(0,0,0,0.7)) drop-shadow(0 0 ${down ? 14 : 6}px ${color})` }}>
    <path d="M3 3 L3 31 L10.5 24 L15.5 36 L20.5 34 L15.5 22.5 L26 22.5 Z" fill="#f4f7fc" stroke="#06070b" strokeWidth={2.2} strokeLinejoin="round" />
  </svg>
);

/** Ripple rings for every press in the take near the current time. */
const Presses: React.FC<{ name: TakeName; t: number; color: string; inv: number }> = ({ name, t, color, inv }) => {
  const c = TAKES[name].cursor;
  const rings: React.ReactNode[] = [];
  for (let i = 1; i < c.length; i++) {
    if (c[i].d && !c[i - 1].d) {
      const age = t - c[i].t;
      if (age >= 0 && age < 650) {
        const u = age / 650;
        rings.push(
          <div
            key={i}
            style={{
              position: 'absolute',
              left: c[i].x,
              top: c[i].y,
              width: 0,
              height: 0,
            }}
          >
            <div
              style={{
                position: 'absolute',
                left: -(10 + 60 * u) * inv,
                top: -(10 + 60 * u) * inv,
                width: (20 + 120 * u) * inv,
                height: (20 + 120 * u) * inv,
                borderRadius: '50%',
                border: `${3 * inv}px solid ${color}`,
                opacity: 1 - u,
                boxShadow: `0 0 ${20 * inv}px ${color}`,
              }}
            />
          </div>,
        );
      }
    }
  }
  return <>{rings}</>;
};

export const TakePlayer: React.FC<{
  name: TakeName;
  offsetMs: number;
  cam: Cam[];
  accent: string;
  audio?: boolean;
  audioFadeOutFrames?: number;
  hideCursor?: boolean;
  shake?: number;
  children?: React.ReactNode;
}> = ({ name, offsetMs, cam, accent, audio = true, hideCursor, shake = 0, children }) => {
  const { fps } = useVideoConfig();
  const t = useTakeMs(offsetMs);
  const meta = TAKES[name];
  const idx = lastIndexAtOrBefore(meta.times, t);
  const k = camAt(cam, t);
  let z = k.z;
  let cx = k.x, cy = k.y;
  if (z >= 1) {
    cx = Math.min(1920 - 960 / z, Math.max(960 / z, cx));
    cy = Math.min(1080 - 540 / z, Math.max(540 / z, cy));
  }
  const card = k.card ?? 0;
  const cardScale = 1 - 0.16 * card;
  const cur = cursorAt(name, t);
  const sx = Math.sin(t / 23) * shake, sy = Math.cos(t / 31) * shake;
  return (
    <AbsoluteFill style={{ perspective: 2200 }}>
      {audio && <Audio src={staticFile(`takes/${name}/norm.wav`)} trimBefore={Math.round((offsetMs / 1000) * fps)} />}
      <AbsoluteFill
        style={{
          transform: `translate(${sx}px, ${sy}px) scale(${cardScale}) rotateX(${7 * card}deg) rotateY(${(k.ry ?? 0) - 9 * card}deg)`,
          borderRadius: 18 * card,
          overflow: 'hidden',
          boxShadow: card > 0.01 ? `0 0 0 1px ${accent}66, 0 50px 120px rgba(0,0,0,0.75), 0 0 90px ${accent}33` : undefined,
          background: C.void,
        }}
      >
        <div
          style={{
            position: 'absolute',
            left: 0,
            top: 0,
            width: 1920,
            height: 1080,
            transformOrigin: '0 0',
            transform: `translate(${960 - cx * z}px, ${540 - cy * z}px) scale(${z})`,
          }}
        >
          <Img src={staticFile(`takes/${name}/${String(idx).padStart(4, '0')}.jpg`)} style={{ width: 1920, height: 1080, display: 'block' }} />
          <Presses name={name} t={t} color={accent} inv={1 / z} />
          {!hideCursor && (
            <div style={{ position: 'absolute', left: cur.x - 3 / z, top: cur.y - 3 / z }}>
              <Pointer down={!!cur.d} color={accent} scale={1.15 / z} />
            </div>
          )}
          {children}
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};

/** Spectrum strip from the take's own audio. */
export const Spectrum: React.FC<{ name: TakeName; offsetMs: number; color: string; height?: number; opacity?: number }> = ({
  name,
  offsetMs,
  color,
  height = 150,
  opacity = 0.9,
}) => {
  const t = useTakeMs(offsetMs);
  const s = SPECS[name];
  const i = Math.max(0, Math.min(s.bands.length - 1, Math.round((t / 1000) * s.fps)));
  const bands = s.bands[i];
  const w = 1920 / bands.length;
  return (
    <AbsoluteFill style={{ pointerEvents: 'none' }}>
      <div
        style={{
          position: 'absolute',
          left: 0,
          right: 0,
          bottom: 0,
          height: height + 60,
          background: 'linear-gradient(to top, rgba(6,7,11,0.92), rgba(6,7,11,0))',
        }}
      />
      <svg width={1920} height={height} style={{ position: 'absolute', left: 0, bottom: 0, opacity }}>
        <defs>
          <linearGradient id={`sg-${name}`} x1="0" y1="1" x2="0" y2="0">
            <stop offset="0" stopColor={color} stopOpacity={0.15} />
            <stop offset="1" stopColor={color} stopOpacity={1} />
          </linearGradient>
        </defs>
        {bands.map((v, k) => {
          const h = Math.max(2, (v / 100) ** 1.6 * height);
          return <rect key={k} x={k * w + 3} y={height - h} width={w - 6} height={h} rx={2} fill={`url(#sg-${name})`} />;
        })}
      </svg>
    </AbsoluteFill>
  );
};

export const rmsAt = (name: TakeName, t: number) => {
  const s = SPECS[name];
  const i = Math.max(0, Math.min(s.rms.length - 1, Math.round((t / 1000) * s.fps)));
  return s.rms[i];
};

/** Big live readout of a knob value, e.g. CUT 70 Hz -> 12.45 kHz. */
export const Readout: React.FC<{ label: string; value: string | null; color: string; x: number; y: number; show: number; align?: 'left' | 'right' }> = ({
  label,
  value,
  color,
  x,
  y,
  show,
  align = 'left',
}) => (
  <div
    style={{
      position: 'absolute',
      left: align === 'left' ? x : undefined,
      right: align === 'right' ? 1920 - x : undefined,
      top: y,
      opacity: show,
      transform: `translateY(${(1 - show) * 30}px)`,
      textAlign: align,
      pointerEvents: 'none',
      padding: '20px 30px',
      borderRadius: 16,
      background: 'rgba(6,7,11,0.8)',
      backdropFilter: 'blur(14px)',
      border: '1px solid rgba(255,255,255,0.07)',
      boxShadow: '0 20px 60px rgba(0,0,0,0.5)',
    }}
  >
    <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color }}>{label}</div>
    <div
      style={{
        fontFamily: DISPLAY,
        fontSize: 84,
        letterSpacing: '0.04em',
        color: C.ice,
        textShadow: `0 0 40px ${color}88, 0 4px 30px rgba(0,0,0,0.9)`,
        lineHeight: 1.05,
        fontVariantNumeric: 'tabular-nums',
      }}
    >
      {value ?? '—'}
    </div>
  </div>
);

export const fadeWindow = (t: number, a: number, b: number, ramp = 220) =>
  interpolate(t, [a - ramp, a, b, b + ramp], [0, 1, 1, 0], { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' });
