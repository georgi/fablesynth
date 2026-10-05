// NEON CHASE · Part 1 (INTRO). One live SQ-4 take: the pad opens from a sine
// over bars 0–7, the drums drop in on bar 8, BUILD is queued for bar 12. The
// file starts one bar before bar 0 and ends one bar after bar 12, as handles.
import React from 'react';
import { AbsoluteFill, spring, useCurrentFrame, useVideoConfig } from 'remotion';
import { Flash, Headline, Sub, Terrain, lerp } from '../components';
import { C } from '../theme';
import { Caption, Scrim } from '../demo/Demo';
import { Cam, Readout, Spectrum, TakePlayer, fadeWindow, useTakeMs, valueAt } from '../demo/Take';
import { partClock } from './shared';
import { Focus, type FocusKey, type RectRow } from './Focus';
import rects from '../../public/takes/neon-intro/rects.json';

const NAME = 'neon-intro' as const;
// Take bar 0 = song bar 0; the file runs from bar -1 to bar 13 (BUILD handle).
const { b, start: START, frames, frameOf } = partClock(NAME, 0, 12);
export const PART1_FRAMES = frames;
const FOCUS: FocusKey[] = [
  { t: START, keys: ['oscA'] },
  { t: b(7.0), keys: ['head0'] },
  { t: b(8.0), keys: [] },
  { t: b(10.85), keys: ['scene1'] },
  { t: b(12.05), keys: [] },
];

const CAM: Cam[] = [
  { t: START, x: 760, y: 290, z: 2.7 },
  { t: b(0.4), x: 740, y: 285, z: 2.45 },
  { t: b(6.6), x: 760, y: 280, z: 2.75 },
  { t: b(7.4), x: 700, y: 170, z: 2.1 },
  { t: b(7.95), x: 700, y: 170, z: 2.15 },
  { t: b(8.05), x: 960, y: 540, z: 1.0, card: 0.35 },
  { t: b(10.6), x: 960, y: 540, z: 1.04, card: 0.3, ry: -2 },
  { t: b(11.3), x: 420, y: 300, z: 1.8 },
  { t: b(11.95), x: 420, y: 300, z: 1.85 },
  { t: b(12.15), x: 960, y: 540, z: 1.0 },
  { t: b(13), x: 960, y: 540, z: 1.03 },
];

export const Part1: React.FC = () => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = useTakeMs(START);
  const at = frameOf;
  const drop = at(8);
  const lf = f - drop;
  const logo = lf >= 0 ? spring({ frame: lf, fps, config: { damping: 14, stiffness: 120 } }) * lerp(f, at(9.4), at(9.9), 1, 0) : 0;
  const fadeIn = lerp(f, 0, 12, 0, 1);
  return (
    <AbsoluteFill style={{ background: C.void, opacity: fadeIn }}>
      <AbsoluteFill style={{ filter: logo > 0.01 ? `blur(${logo * 10}px) brightness(${1 - logo * 0.55})` : undefined }}>
        <TakePlayer name={NAME} offsetMs={START} cam={CAM} accent={C.violet} under={<Focus rows={rects as RectRow[]} keys={FOCUS} t={t} color={C.violet} />} />
        <Spectrum name={NAME} offsetMs={START} color={C.violet} />
        <Scrim strength={f < drop ? 1 : 0.6} />
        {f < drop && <Caption kicker="WT-1 · PUMP PAD" title={'START\nFROM A SINE.'} sub="ONE FADER OPENS THE WAVETABLE" color={C.violet} delay={at(0.3)} out={at(7.2)} />}
        <Readout label="WAVETABLE POS" value={valueAt(NAME, 'pos', t)} color={C.violet} x={1850} y={600} align="right" show={fadeWindow(t, b(1.0), b(7.2))} />
        {f >= at(10.9) && <Caption kicker="SQ-4 · SESSION" title={'QUEUE\nTHE BUILD.'} sub="LANDS ON THE NEXT BAR" color={C.green} delay={at(10.9)} out={at(12.1)} />}
      </AbsoluteFill>
      {lf >= 0 && lf < 12 && <Flash color={C.violet} peak={0.45} dur={12} />}
      {logo > 0.01 && (
        <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', opacity: logo, transform: `scale(${0.85 + 0.15 * logo})` }}>
          <div style={{ position: 'absolute', top: 210, left: 260, opacity: 0.6 }}>
            <Terrain width={1400} height={520} draw={Math.min(1, lf / 20)} />
          </div>
          <Headline text="FABLESYNTH" size={140} highlight={{ from: 5, color: C.cyan }} align="center" stagger={0.7} />
          <div style={{ marginTop: 26 }}>
            <Sub text="NEON CHASE · PLAYED LIVE" color={C.ice} size={30} delay={8} />
          </div>
        </AbsoluteFill>
      )}
    </AbsoluteFill>
  );
};
