import { useEffect } from 'react';
import { AmpEnvPanel } from './components/AmpEnvPanel';
import { DrumPowerOverlay } from './components/DrumPowerOverlay';
import { FilterSection } from './components/FilterSection';
import { FxRack } from './components/FxRack';
import { Header } from './components/Header';
import { ModPanel } from './components/ModPanel';
import { PadGrid } from './components/PadGrid';
import { PadStrip } from './components/PadStrip';
import { NoiseSection } from './components/NoiseSection';
import { OscSection } from './components/OscSection';
import { SampleSection } from './components/SampleSection';
import { PitchEnvPanel } from './components/PitchEnvPanel';
import { SelBar } from './components/SelBar';
import { StepSeq } from './components/StepSeq';
import { useDrumKeys } from './hooks/useDrumKeys';
import { useDrumMidi } from './hooks/useDrumMidi';
import { drumEngine, useDrumStore } from './store';

export function DrumApp() {
  useDrumKeys();
  useDrumMidi();
  // STEP mode drops the pad strip — its per-pad knobs belong to the pad being
  // performed, and the 16-lane sequencer below edits every pad at once.
  const mode = useDrumStore((s) => s.mode);

  // exposed for debugging / automated verification
  useEffect(() => {
    (window as unknown as { __fableDr: unknown }).__fableDr = {
      engine: drumEngine,
      store: useDrumStore,
    };
  }, []);

  return (
    <>
      <DrumPowerOverlay />
      <main id="drum-rack">
        <Header />
        <div className={`dr-main${mode === 'step' ? ' fit-pads' : ''}`}>
          <div className="dr-left">
            <div id="dr-pads"><PadGrid /></div>
            {mode !== 'step' && <div id="dr-padstrip"><PadStrip /></div>}
          </div>
          <div className="dr-right">
            <div id="dr-selbar"><SelBar /></div>
            <div id="dr-oscrow">
              <OscSection osc="oscA" />
              <SampleSection />
              <NoiseSection />
            </div>
            <div id="dr-editrow">
              <PitchEnvPanel />
              <AmpEnvPanel />
              <FilterSection />
              <ModPanel />
            </div>
          </div>
        </div>
        <div id="dr-fxrack"><FxRack /></div>
        <div id="dr-stepseq"><StepSeq /></div>
      </main>
    </>
  );
}
