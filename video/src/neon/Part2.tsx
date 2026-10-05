// NEON CHASE · Part 2 (BUILD → DROP A). The acid bass enters on song bar 12,
// two painted risers (FILTER CUT, ENV MOD) open it across bars 16–19, the
// drums drop out for bar 19, and DROP A lands on bar 20.
// Take bar n = song bar 11 + n; the file runs from song bar 11 to 21.
import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { Flash, lerp } from '../components';
import { C, DISPLAY, MONO } from '../theme';
import { Caption, PLATE, Scrim } from '../demo/Demo';
import { Cam, Spectrum, TAKES, TakePlayer, fadeWindow, useTakeMs } from '../demo/Take';
import { partClock } from './shared';
import { Focus, type FocusKey, type RectRow } from './Focus';
import rects from '../../public/takes/neon-build/rects.json';

const NAME = 'neon-build' as const;
const { b, start: START, frames, frameOf } = partClock(NAME, 1, 9);
export const PART2_FRAMES = frames;
const M = TAKES[NAME].marks;
const FOCUS: FocusKey[] = [
  { t: START, keys: ['scene1'] },
  { t: b(0.95), keys: ['blSeq'] },
  { t: M['lane-cut'] - 100, keys: ['auto'] },
  { t: b(5.5), keys: ['blSeq', 'auto'] },
  { t: M.dropQueued - 300, keys: ['scene2'] },
  { t: b(7.75), keys: ['head0'] },
  { t: b(9.0), keys: [] },
];

const LANE = { x: 1075, y: 930, z: 1.7 };
const CAM: Cam[] = [
  { t: START, x: 420, y: 300, z: 1.75 },
  { t: b(0.65), x: 420, y: 300, z: 1.8 },
  { t: b(0.98), x: 1000, y: 480, z: 1.12 },
  { t: b(1.15), x: 1000, y: 500, z: 1.15 },
  { t: M['lane-cut'] + 200, ...LANE },
  { t: M['drawn-env'] + 150, ...LANE, z: 1.75 },
  { t: M['drawn-env'] + 650, x: 1000, y: 620, z: 1.08 },
  { t: b(6.8), x: 1000, y: 600, z: 1.12 },
  { t: M.dropQueued - 150, x: 420, y: 330, z: 1.8 },
  { t: M.dropQueued + 250, x: 420, y: 330, z: 1.8 },
  { t: b(7.85), x: 700, y: 170, z: 2.1 },
  { t: b(8.9), x: 700, y: 175, z: 2.35 },
  { t: b(9.04), x: 960, y: 540, z: 1.0, card: 0.3 },
  { t: b(10), x: 960, y: 540, z: 1.04, card: 0.25, ry: -2 },
];

/** Pill naming the lane being painted, pinned above the automation editor. */
const LaneTag: React.FC<{ t: number }> = ({ t }) => {
  const lanes: [string, number, number, string][] = [
    ['FILTER CUT', M['draw-cut'], M['drawn-cut'], C.green],
    ['ENV MOD', M['draw-env'], M['drawn-env'], C.cyan],
  ];
  return (
    <div style={{ position: 'absolute', right: 70, top: 56, display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 12, ...PLATE, opacity: fadeWindow(t, M['draw-cut'] - 200, b(5.6)) }}>
      <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color: C.green }}>PAINTING</div>
      {lanes.map(([label, from, to, col]) => {
        const on = t >= from;
        const u = Math.min(1, Math.max(0, (t - from) / (to - from)));
        return (
          <div key={label} style={{ opacity: on ? 1 : 0.25, fontFamily: DISPLAY, fontSize: 54, color: C.ice, textShadow: on ? `0 0 34px ${col}` : undefined }}>
            {label}
            <div style={{ height: 4, marginTop: 6, background: `${col}33` }}>
              <div style={{ height: 4, width: `${u * 100}%`, background: col, boxShadow: `0 0 12px ${col}` }} />
            </div>
          </div>
        );
      })}
    </div>
  );
};

export const Part2: React.FC = () => {
  const f = useCurrentFrame();
  const t = useTakeMs(START);
  const drop = frameOf(9);
  const post = f - drop;
  const air = t >= M.drumsOut && t < b(9);
  return (
    <AbsoluteFill style={{ background: C.void }}>
      <TakePlayer name={NAME} offsetMs={START} cam={CAM} accent={C.green} under={<Focus rows={rects as RectRow[]} keys={FOCUS} t={t} color={C.green} />} shake={post >= 0 ? Math.max(0, 9 * (1 - post / 16)) : 0} />
      <Spectrum name={NAME} offsetMs={START} color={C.green} opacity={1 - fadeWindow(t, M['lane-cut'], b(5.4), 300)} />
      <Scrim strength={post >= 0 ? 0.4 : 1} />
      {f < frameOf(1.1) && <Caption kicker="SQ-4 · SCENE LAUNCH" title={'BRING IN\nTHE ACID.'} sub="BL-1 · NEON SQUELCH" color={C.green} delay={frameOf(0.05)} out={frameOf(1.0)} />}
      {f >= frameOf(1.2) && f < frameOf(5.6) && <Caption kicker="SQ-4 · CLIP AUTOMATION" title={'PAINT\nTHE RISE.'} sub="TWO CURVES · ONE 4-BAR CLIP" color={C.green} delay={frameOf(1.2)} out={frameOf(5.5)} />}
      <LaneTag t={t} />
      {f >= frameOf(5.6) && f < frameOf(7.9) && <Caption kicker="THE BUILD · BARS 16–19" title={'DARK → OPEN.'} sub="THE BASS FOLLOWS YOUR CURVES" color={C.cyan} delay={frameOf(5.6)} out={frameOf(7.8)} />}
      {air && <Caption kicker="DRUMS OUT" title={'ONE BAR\nOF AIR.'} color={C.orange} delay={frameOf(7.95)} />}
      {post >= 0 && post < 14 && <Flash color="#ffffff" peak={0.7} dur={14} />}
      {post >= 0 && (
        <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', opacity: lerp(post, 0, 4, 0, 1) * lerp(post, 34, 46, 1, 0) }}>
          <div style={{ fontFamily: DISPLAY, fontSize: 210, letterSpacing: '0.08em', color: C.ice, textShadow: `0 0 60px ${C.green}, 0 6px 40px #000`, transform: `scale(${1.15 - 0.15 * Math.min(1, post / 10)})` }}>DROP</div>
        </AbsoluteFill>
      )}
    </AbsoluteFill>
  );
};
