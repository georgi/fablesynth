// NEON CHASE · Part 3 (DROP A). DR-1 in focus with the UZU kit: a crash on the
// one and a tom fill into bar 4 of the loop are written live. The fill plays on
// song bars 23 and 27, the crash on 24; BREAK is queued for bar 28.
// Take bar n = song bar 19 + n; the file runs from song bar 19 to 29.
import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { C, DISPLAY, MONO } from '../theme';
import { Caption, PLATE, PressLabels, Scrim, camLerp } from '../demo/Demo';
import { Cam, Spectrum, TAKES, TakePlayer, fadeWindow, useTakeMs } from '../demo/Take';
import { partClock } from './shared';
import { Focus, type FocusKey, type RectRow } from './Focus';
import { DropHit } from './DropHit';
import rects from '../../public/takes/neon-dropa/rects.json';

const NAME = 'neon-dropa' as const;
const { b, start: START, frames, frameOf } = partClock(NAME, 1, 9);
export const PART3_FRAMES = frames;
const M = TAKES[NAME].marks;

const CAM: Cam[] = [
  { t: START, x: 960, y: 540, z: 1.0 },
  { t: b(1.0), x: 960, y: 540, z: 1.04 },
  { t: M.crash - 500, x: 820, y: 640, z: 2.0 },
  { t: M.crash + 150, x: 820, y: 640, z: 2.05 },
  { t: M.tab4 - 250, x: 820, y: 560, z: 2.0 },
  { t: M.tab4 + 300, x: 1300, y: 640, z: 1.9 },
  { t: M.fill, x: 1420, y: 680, z: 2.0 },
  { t: b(3.6), x: 1100, y: 760, z: 1.3 },
  { t: b(5.4), x: 1100, y: 760, z: 1.34 },
  { t: b(7.4), x: 1050, y: 640, z: 1.1 },
  { t: M.break - 500, x: 420, y: 480, z: 1.8 },
  { t: M.break + 300, x: 420, y: 480, z: 1.85 },
  { t: b(9.0), x: 960, y: 540, z: 1.0 },
  { t: b(10), x: 960, y: 540, z: 1.03 },
];
const FOCUS: FocusKey[] = [
  { t: START, keys: [] },
  { t: b(1.15), keys: ['drSeq'] },
  { t: M.break - 600, keys: ['scene4'] },
  { t: b(9.0), keys: [] },
];
const LABELS = ['CRASH · BAR 1', 'BAR 4', 'TOM HI', 'TOM MD', 'TOM LO', 'TOM LO'];

/** Callout when the written parts play: the fill on the loop's bar 4, the crash on its one. */
const Hits: React.FC<{ t: number }> = ({ t }) => {
  const items: [string, number, number, string][] = [
    ['FILL', b(4.75), b(5.0), C.amber],
    ['CRASH', b(5.0), b(5.6), C.cyan],
  ];
  return (
    <>
      {items.map(([label, from, to, col]) => {
        const o = fadeWindow(t, from, to, 120);
        if (o <= 0) return null;
        return (
          <div key={label} style={{ position: 'absolute', right: 70, top: 56, opacity: o, textAlign: 'right', ...PLATE }}>
            <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color: col }}>{label === 'FILL' ? 'BAR 23' : 'BAR 24'}</div>
            <div style={{ fontFamily: DISPLAY, fontSize: 96, color: C.ice, textShadow: `0 0 40px ${col}` }}>{label}</div>
          </div>
        );
      })}
    </>
  );
};

export const Part3: React.FC = () => {
  const f = useCurrentFrame();
  const t = useTakeMs(START);
  return (
    <AbsoluteFill style={{ background: C.void }}>
      <TakePlayer name={NAME} offsetMs={START} cam={CAM} accent={C.amber} under={<Focus rows={rects as RectRow[]} keys={FOCUS} t={t} color={C.amber} />} />
      <PressLabels name={NAME} offsetMs={START} labels={LABELS} color={C.amber} cam={camLerp(CAM)} />
      <Spectrum name={NAME} offsetMs={START} color={C.amber} />
      <Scrim />
      {f >= frameOf(1.15) && f < frameOf(3.6) && <Caption kicker="DR-1 · UZU KIT" title={'WRITE\nTHE FILL.'} sub="LIVE, WHILE THE DROP RUNS" color={C.amber} delay={frameOf(1.15)} out={frameOf(3.4)} />}
      {f >= frameOf(3.6) && f < frameOf(5.8) && <Caption kicker="DROP A · LOOP BAR 4" title={'HEAR IT\nCOME ROUND.'} color={C.amber} delay={frameOf(3.6)} out={frameOf(5.6)} />}
      <Hits t={t} />
      <DropHit post={f - frameOf(1)} color={C.amber} />
      {f >= frameOf(7.5) && f < frameOf(9.1) && <Caption kicker="SQ-4 · SCENE LAUNCH" title={'INTO\nTHE BREAK.'} sub="DRUMS OUT ON THE NEXT BAR" color={C.cyan} delay={frameOf(7.5)} out={frameOf(8.95)} />}
    </AbsoluteFill>
  );
};
