import { useEffect } from 'react';
import { DeviceView } from './components/DeviceView';
import { FooterRow } from './components/FooterRow';
import { Header } from './components/Header';
import { SceneRail } from './components/SceneRail';
import { SceneRow } from './components/SceneRow';
import { SqPowerOverlay } from './components/SqPowerOverlay';
import { TrackHeads } from './components/TrackHeads';
import { useSeqStore } from './store';

export function SeqApp() {
  const session = useSeqStore((s) => s.session);
  const powered = useSeqStore((s) => s.powered);
  const quant = useSeqStore((s) => s.quant);
  const focus = useSeqStore((s) => s.focus);

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

  // focus-mode keys: Esc exits, 1–4 switch devices, ↑/↓ move the scene rail
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const st = useSeqStore.getState();
      if (!st.focus) return;
      const el = e.target as HTMLElement | null;
      if (el?.closest('input, textarea, select, [role="slider"]')) return;
      if (e.key === 'Escape') st.exitFocus();
      else if (e.key >= '1' && e.key <= String(st.session.tracks.length)) st.enterFocus(Number(e.key) - 1);
      else if (e.key === 'ArrowUp') { e.preventDefault(); st.focusScene(st.focus.scene - 1); }
      else if (e.key === 'ArrowDown') { e.preventDefault(); st.focusScene(st.focus.scene + 1); }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  return (
    <>
      <SqPowerOverlay />
      <main id="sq-rack" className={focus ? 'focused' : ''}>
        <Header />
        <TrackHeads />
        {focus ? (
          <div className="sq-focus" key={`f${focus.track}`}>
            <div className="sq-strip">
              <SceneRail />
              <div className="sq-strip-row"><SceneRow s={focus.scene} /></div>
            </div>
            <DeviceView />
          </div>
        ) : (
          session.scenes.map((_, s) => <SceneRow key={s} s={s} />)
        )}
        <FooterRow />
        <div className="sq-hint">
          {focus
            ? 'MINI STRIP STAYS LIVE — TAP CELLS TO LAUNCH · ✎ RETARGETS THE EDITOR · ESC BACK TO SESSION'
            : `TAP CLIP TO LAUNCH · TAP AGAIN TO STOP · LAUNCHES QUANTIZE TO ${quant} · RIGHT-CLICK EMPTY CELL TO TOGGLE PASS-THROUGH · CLICK A TRACK NAME TO OPEN ITS DEVICE`}
        </div>
      </main>
    </>
  );
}
