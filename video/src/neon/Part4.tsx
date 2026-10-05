// NEON CHASE · Part 4 (BREAK). The drums leave, the lead comes forward, LAB
// SHIFT spirals its echoes, then a tape-echo throw carries it into DROP B.
// Take bar n = song bar 27 + n: BREAK on song bar 28, DROP B on song bar 36.
// The file runs from song bar 27 to 37.
import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { Flash, lerp } from '../components';
import { C, DISPLAY } from '../theme';
import { Caption, Scrim } from '../demo/Demo';
import { Cam, Readout, Spectrum, TAKES, TakePlayer, fadeWindow, useTakeMs, valueAt } from '../demo/Take';
import { partClock } from './shared';
import { Focus, type FocusKey, type RectRow } from './Focus';
import rects from '../../public/takes/neon-break/rects.json';

const NAME = 'neon-break' as const;
const { b, start: START, frames, frameOf } = partClock(NAME, 1, 9);
export const PART4_FRAMES = frames;
const M = TAKES[NAME].marks;

const SHIFT = { x: 960, y: 815, z: 2.05 }; // card sits right of the caption plate
const ECHO = { x: 1221, y: 450, z: 2.0 };
const CAM: Cam[] = [
  { t: START, x: 960, y: 540, z: 1.0 },
  { t: b(0.55), x: 960, y: 540, z: 1.03 },
  { t: b(0.85), x: 1000, y: 170, z: 2.0 },
  { t: M.leadUp + 100, x: 1000, y: 170, z: 2.05 },
  { t: M.leadUp + 600, ...SHIFT },
  { t: b(6.4), ...SHIFT, z: 2.15 },
  { t: b(6.9), ...ECHO },
  { t: b(8.15), ...ECHO, z: 2.1 },
  { t: M.dropB - 400, x: 420, y: 430, z: 1.8 },
  { t: b(8.95), x: 420, y: 430, z: 1.85 },
  { t: b(9.04), x: 960, y: 540, z: 1.0, card: 0.3 },
  { t: b(10), x: 960, y: 540, z: 1.04, card: 0.25, ry: -2 },
];
const FOCUS: FocusKey[] = [
  { t: START, keys: [] },
  { t: b(0.6), keys: ['head2'] },
  { t: M.leadUp + 400, keys: ['shift'] },
  { t: b(6.7), keys: ['echo'] },
  { t: M.dropB - 500, keys: ['scene3'] },
  { t: b(9.0), keys: [] },
];

export const Part4: React.FC = () => {
  const f = useCurrentFrame();
  const t = useTakeMs(START);
  const drop = frameOf(9);
  const post = f - drop;
  const show = (a: number, z: number) => f >= frameOf(a) && f < frameOf(z);
  return (
    <AbsoluteFill style={{ background: C.void }}>
      <TakePlayer name={NAME} offsetMs={START} cam={CAM} accent={C.cyan} under={<Focus rows={rects as RectRow[]} keys={FOCUS} t={t} color={C.cyan} />} shake={post >= 0 ? Math.max(0, 9 * (1 - post / 16)) : 0} />
      <Spectrum name={NAME} offsetMs={START} color={C.cyan} />
      <Scrim strength={post >= 0 ? 0.4 : 1} />
      {show(0.6, 1.75) && <Caption kicker="SQ-4 · THE BREAK" title={'DRUMS OUT.\nLEAD UP.'} sub="ONE KNOB ON THE TRACK HEAD" color={C.cyan} delay={frameOf(0.6)} out={frameOf(1.6)} />}
      {show(1.75, 6.6) && <Caption kicker="LAB · FREQUENCY SHIFTER" title={'ECHOES\nTHAT SPIRAL.'} sub="EVERY REPEAT SHIFTS IN PITCH" color={C.cyan} delay={frameOf(1.75)} out={frameOf(6.45)} />}
      <Readout label="SPIRAL" value={valueAt(NAME, 'spiral', t)} color={C.cyan} x={70} y={600} show={fadeWindow(t, M.spiral - 900, b(4.0))} />
      <Readout label="SHIFT" value={valueAt(NAME, 'hz', t)} color={C.orange} x={70} y={600} show={fadeWindow(t, b(4.15), b(6.5))} />
      {show(6.7, 8.2) && <Caption kicker="WT-1 · TAPE ECHO" title={'THROW IT.'} sub="FEEDBACK UP · DOTTED 1/8" color={C.violet} delay={frameOf(6.7)} out={frameOf(8.05)} />}
      <Readout label="FEEDBACK" value={valueAt(NAME, 'fdbk', t)} color={C.violet} x={70} y={600} show={fadeWindow(t, b(6.95), b(8.1))} />
      {show(8.2, 9.0) && <Caption kicker="SQ-4 · SCENE LAUNCH" title={'THE LAST\nDROP.'} sub="THE ECHOES RIDE IN" color={C.orange} delay={frameOf(8.2)} />}
      {post >= 0 && post < 14 && <Flash color="#ffffff" peak={0.7} dur={14} />}
      {post >= 0 && (
        <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', opacity: lerp(post, 0, 4, 0, 1) * lerp(post, 34, 46, 1, 0) }}>
          <div style={{ fontFamily: DISPLAY, fontSize: 210, letterSpacing: '0.08em', color: C.ice, textShadow: `0 0 60px ${C.orange}, 0 6px 40px #000`, transform: `scale(${1.15 - 0.15 * Math.min(1, post / 10)})` }}>DROP</div>
        </AbsoluteFill>
      )}
    </AbsoluteFill>
  );
};
