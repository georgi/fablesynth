import { useEffect } from 'react';
import { BassFxRack } from './components/BassFxRack';
import { BassPowerOverlay } from './components/BassPowerOverlay';
import { EnvPanel } from './components/EnvPanel';
import { FilterSection } from './components/FilterSection';
import { Header } from './components/Header';
import { KeysPanel } from './components/KeysPanel';
import { AccentPanel, LfoPanel } from './components/LfoPanel';
import { OscSection, SubSection } from './components/OscSection';
import { PitchSeq } from './components/PitchSeq';
import { useBassKeys } from './hooks/useBassKeys';
import { useBassMidi } from './hooks/useBassMidi';
import { bassEngine, useBassStore } from './store';

export function BassApp() {
  useBassKeys();
  useBassMidi();

  // exposed for debugging / automated verification
  useEffect(() => {
    (window as unknown as { __fableBl: unknown }).__fableBl = {
      engine: bassEngine,
      store: useBassStore,
    };
  }, []);

  return (
    <>
      <BassPowerOverlay />
      <main id="bass-rack">
        <Header />
        <div id="bl-editrow">
          <OscSection />
          <SubSection />
          <FilterSection />
          <EnvPanel />
        </div>
        <div id="bl-modrow">
          <LfoPanel />
          <AccentPanel />
        </div>
        <div id="bl-seq"><PitchSeq /></div>
        <div id="bl-fxrack"><BassFxRack /></div>
        {/* Keyboard last, where a synth's keys belong. */}
        <div id="bl-keysrow"><KeysPanel /></div>
      </main>
    </>
  );
}
