import { Composition } from 'remotion';
import { Promo } from './Promo';
import { Demo, DEMO_TOTAL } from './demo/Demo';
import { FPS, TOTAL } from './theme';
import { PART1_FRAMES, Part1 } from './neon/Part1';
import { PART2_FRAMES, Part2 } from './neon/Part2';
import { PART3_FRAMES, Part3 } from './neon/Part3';
import { PART4_FRAMES, Part4 } from './neon/Part4';
import { PART5_FRAMES, Part5 } from './neon/Part5';

export const Root = () => (
  <>
    <Composition id="FableSynthDemo" component={Demo} durationInFrames={DEMO_TOTAL} fps={FPS} width={1920} height={1080} />
    <Composition id="FableSynthPromo" component={Promo} durationInFrames={TOTAL} fps={FPS} width={1920} height={1080} />
    <Composition id="NeonPart1" component={Part1} durationInFrames={PART1_FRAMES} fps={30} width={1920} height={1080} />
    <Composition id="NeonPart2" component={Part2} durationInFrames={PART2_FRAMES} fps={30} width={1920} height={1080} />
    <Composition id="NeonPart3" component={Part3} durationInFrames={PART3_FRAMES} fps={30} width={1920} height={1080} />
    <Composition id="NeonPart4" component={Part4} durationInFrames={PART4_FRAMES} fps={30} width={1920} height={1080} />
    <Composition id="NeonPart5" component={Part5} durationInFrames={PART5_FRAMES} fps={30} width={1920} height={1080} />
  </>
);
