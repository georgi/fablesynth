import { useEffect } from 'react';
import { FooterRow } from './components/FooterRow';
import { Header } from './components/Header';
import { SceneRow } from './components/SceneRow';
import { TrackHeads } from './components/TrackHeads';
import { BEAT_MS, SCENES } from './model';
import { useSeqStore } from './store';

export function SeqApp() {
  const quant = useSeqStore((s) => s.quant);

  // The beat clock. It keeps ticking while paused (onBeat no-ops), so
  // resuming play continues on the grid instead of drifting.
  useEffect(() => {
    let t: ReturnType<typeof setTimeout>;
    const loop = () => {
      t = setTimeout(() => {
        useSeqStore.getState().onBeat();
        loop();
      }, BEAT_MS);
    };
    loop();
    return () => clearTimeout(t);
  }, []);

  // exposed for debugging / automated verification
  useEffect(() => {
    (window as unknown as { __fableSq: unknown }).__fableSq = { store: useSeqStore };
  }, []);

  return (
    <main id="sq-rack">
      <Header />
      <TrackHeads />
      {SCENES.map((_, s) => (
        <SceneRow key={s} s={s} />
      ))}
      <FooterRow />
      <div className="sq-hint">
        TAP CLIP TO LAUNCH · TAP AGAIN TO STOP · LAUNCHES QUANTIZE TO {quant} · SCENES LAYER — LATEST CLIP WINS EACH TRACK
      </div>
    </main>
  );
}
