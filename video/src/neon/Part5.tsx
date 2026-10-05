// NEON CHASE · Part 5 (DROP B + OUTRO). The whole session view: the spotlight
// walks the four tracks on DROP B, OUTRO is launched for bar 44, and STOP ALL
// ends the track on bar 48 under the end card.
// Take bar n = song bar 35 + n; the file runs from song bar 35 to 49.
import React from 'react';
import { AbsoluteFill, Sequence, spring, useCurrentFrame, useVideoConfig } from 'remotion';
import { Headline, Sub, Terrain, lerp } from '../components';
import { C, DISPLAY, MONO } from '../theme';
import { Caption, PLATE, Scrim } from '../demo/Demo';
import { Cam, Spectrum, TAKES, TakePlayer, useTakeMs } from '../demo/Take';
import { partClock } from './shared';
import { Focus, type FocusKey, type RectRow } from './Focus';
import { DropHit } from './DropHit';
import rects from '../../public/takes/neon-final/rects.json';

const NAME = 'neon-final' as const;
const { b, start: START, frames, frameOf } = partClock(NAME, 1, 13);
export const PART5_FRAMES = frames;
const M = TAKES[NAME].marks;

const COLS: [string, string, string][] = [
  ['DR-1', 'UZU KIT', C.amber],
  ['BL-1', 'ACID BASS', C.green],
  ['WT-1', 'CRYSTAL LEAD', C.orange],
  ['WT-1', 'PUMP PAD', C.violet],
];
const colX = [621, 923, 1224, 1526];
// The column walk starts once the DROP word has faded: 0.8 bar per track.
const WALK = 1.8, STEP = 0.8;
const colAt = (n: number) => WALK + n * STEP;
const CAM: Cam[] = [
  { t: START, x: 960, y: 400, z: 1.0 },
  { t: b(0.97), x: 960, y: 400, z: 1.02 },
  ...colX.flatMap((x, i) => [{ t: b(colAt(i) + 0.1), x, y: 330, z: 1.55 }, { t: b(colAt(i) + STEP - 0.05), x, y: 330, z: 1.6 }]),
  { t: b(5.2), x: 960, y: 420, z: 1.0, card: 0.25 },
  { t: b(7.8), x: 960, y: 420, z: 1.05, card: 0.2, ry: -2 },
  { t: M.outro - 450, x: 420, y: 560, z: 1.8 },
  { t: b(8.95), x: 420, y: 560, z: 1.85 },
  { t: b(9.2), x: 960, y: 420, z: 1.0, card: 0.4 },
  { t: b(14), x: 960, y: 420, z: 1.06, card: 0.45, ry: -3 },
];
const FOCUS: FocusKey[] = [
  { t: START, keys: [] },
  ...colX.map((_, i) => ({ t: b(colAt(i)), keys: [`col${i}`] })),
  { t: b(5.0), keys: [] },
  { t: M.outro - 600, keys: ['scene5'] },
  { t: b(9.0), keys: [] },
];

const EndCard: React.FC<{ of: number }> = ({ of }) => {
  const { fps } = useVideoConfig();
  const beat = (60 / 132) * fps;
  return (
    <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center' }}>
      <div style={{ position: 'absolute', top: 120, left: 260, opacity: 0.55 }}>
        <Terrain width={1400} height={520} draw={Math.min(1, of / 30)} t={of / 30 + 3} />
      </div>
      <div style={{ marginTop: -120 }}>
        <Headline text="FABLESYNTH" size={140} highlight={{ from: 5, color: C.cyan }} align="center" stagger={0.7} />
      </div>
      <div style={{ display: 'flex', gap: 26, marginTop: 40 }}>
        {[['WT-1', 'WAVETABLE', C.cyan], ['DR-1', 'DRUMS', C.amber], ['BL-1', 'ACID BASS', C.green], ['SQ-4', 'SESSIONS', C.orange]].map(([id, name, col], i) => {
          const sp = spring({ frame: of - Math.round(beat * (2 + i)), fps, config: { damping: 14, stiffness: 160 } });
          return (
            <div key={id} style={{ opacity: sp, transform: `translateY(${(1 - sp) * 40}px)`, border: `1px solid ${col}88`, background: `${col}14`, boxShadow: `0 0 30px ${col}33`, borderRadius: 10, padding: '14px 24px', minWidth: 200, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6 }}>
              <div style={{ fontFamily: DISPLAY, fontSize: 32, color: col, letterSpacing: '0.12em' }}>{id}</div>
              <div style={{ fontFamily: MONO, fontSize: 15, fontWeight: 600, letterSpacing: '0.3em', color: C.ice }}>{name}</div>
            </div>
          );
        })}
      </div>
      <div style={{ marginTop: 40 }}>
        <Sequence from={Math.round(beat * 7)} layout="none">
          <Sub text="VST3 · AU · STANDALONE · IN YOUR BROWSER" color={C.ice} size={26} />
        </Sequence>
      </div>
    </AbsoluteFill>
  );
};

export const Part5: React.FC = () => {
  const f = useCurrentFrame();
  const t = useTakeMs(START);
  const post = f - frameOf(1);
  const end = frameOf(9);
  const of = f - end;
  const dim = of >= 0 ? Math.min(1, of / 18) : 0;
  const fadeOut = lerp(f, frameOf(13.1), frameOf(14), 1, 0);
  const col = Math.floor((t - b(WALK)) / (b(STEP) - b(0)));
  return (
    <AbsoluteFill style={{ background: C.void, opacity: fadeOut }}>
      <AbsoluteFill style={{ filter: dim ? `blur(${dim * 12}px) brightness(${1 - dim * 0.6})` : undefined }}>
        <TakePlayer name={NAME} offsetMs={START} cam={CAM} accent={C.orange} under={<Focus rows={rects as RectRow[]} keys={FOCUS} t={t} color={col >= 0 && col < 4 ? COLS[col][2] : C.orange} />} shake={post >= 0 ? Math.max(0, 9 * (1 - post / 16)) : 0} />
        <Spectrum name={NAME} offsetMs={START} color={C.orange} opacity={0.9 - dim * 0.4} />
        {of < 0 && <Scrim />}
        {col >= 0 && col < 4 && (
          <div key={col} style={{ position: 'absolute', left: 70, top: 56, ...PLATE }}>
            <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color: COLS[col][2] }}>{COLS[col][0]}</div>
            <div style={{ fontFamily: DISPLAY, fontSize: 84, color: C.ice, textShadow: `0 0 40px ${COLS[col][2]}` }}>{COLS[col][1]}</div>
          </div>
        )}
        {f >= frameOf(5.1) && f < frameOf(8.2) && <Caption kicker="SQ-4 · NEON CHASE" title={'FOUR DEVICES.\nONE SESSION.'} sub="EVERYTHING YOU SAW, PLAYING TOGETHER" color={C.orange} delay={frameOf(5.1)} out={frameOf(8.05)} />}
        {f >= frameOf(8.2) && f < end && <Caption kicker="SQ-4 · SCENE LAUNCH" title={'BRING IT\nHOME.'} sub="OUTRO ON THE NEXT BAR" color={C.orange} delay={frameOf(8.2)} />}
      </AbsoluteFill>
      <DropHit post={post} color={C.orange} />
      {of >= 0 && <EndCard of={of} />}
    </AbsoluteFill>
  );
};
