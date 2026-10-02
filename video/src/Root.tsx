import { Composition } from 'remotion';
import { Promo } from './Promo';
import { Demo, DEMO_TOTAL } from './demo/Demo';
import { FPS, TOTAL } from './theme';

export const Root = () => (
  <>
    <Composition id="FableSynthDemo" component={Demo} durationInFrames={DEMO_TOTAL} fps={FPS} width={1920} height={1080} />
    <Composition id="FableSynthPromo" component={Promo} durationInFrames={TOTAL} fps={FPS} width={1920} height={1080} />
  </>
);
