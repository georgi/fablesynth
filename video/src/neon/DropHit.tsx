// The flash and "DROP" word on a drop downbeat. Parts that start on a drop show
// it on their first song bar, so it survives a cut on that bar.
import React from 'react';
import { AbsoluteFill } from 'remotion';
import { Flash, lerp } from '../components';
import { C, DISPLAY } from '../theme';

export const DropHit: React.FC<{ post: number; color: string }> = ({ post, color }) => {
  if (post < 0) return null;
  return (
    <>
      {post < 14 && <Flash color="#ffffff" peak={0.7} dur={14} />}
      <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', opacity: lerp(post, 0, 4, 0, 1) * lerp(post, 34, 46, 1, 0) }}>
        <div style={{ fontFamily: DISPLAY, fontSize: 210, letterSpacing: '0.08em', color: C.ice, textShadow: `0 0 60px ${color}, 0 6px 40px #000`, transform: `scale(${1.15 - 0.15 * Math.min(1, post / 10)})` }}>DROP</div>
      </AbsoluteFill>
    </>
  );
};
