import { useEffect } from 'react';
import { FooterRow } from './components/FooterRow';
import { Header } from './components/Header';
import { SceneRow } from './components/SceneRow';
import { SqPowerOverlay } from './components/SqPowerOverlay';
import { TrackHeads } from './components/TrackHeads';
import { useSeqStore } from './store';

export function SeqApp() {
  const session = useSeqStore((s) => s.session);
  const powered = useSeqStore((s) => s.powered);
  const quant = useSeqStore((s) => s.quant);

  // UI clock: beat dots / bar counter derive from the shared context-frame
  // timebase (ctx.suspend freezes it, so pause is free).
  useEffect(() => {
    if (!powered) return;
    let raf = 0;
    const loop = () => {
      raf = requestAnimationFrame(loop);
      useSeqStore.getState().tick();
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, [powered]);

  // exposed for debugging / automated verification
  useEffect(() => {
    (window as unknown as { __fableSq: unknown }).__fableSq = { store: useSeqStore };
  }, []);

  return (
    <>
      <SqPowerOverlay />
      <main id="sq-rack">
        <Header />
        <TrackHeads />
        {session.scenes.map((_, s) => (
          <SceneRow key={s} s={s} />
        ))}
        <FooterRow />
        <div className="sq-hint">
          TAP CLIP TO LAUNCH · TAP AGAIN TO STOP · LAUNCHES QUANTIZE TO {quant} · SCENES LAYER — LATEST CLIP WINS EACH TRACK
        </div>
      </main>
    </>
  );
}
