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
import { PitchEnvPanel } from './components/PitchEnvPanel';
import { SelBar } from './components/SelBar';
import { StepSeq } from './components/StepSeq';
import { useDrumKeys } from './hooks/useDrumKeys';
import { useDrumMidi } from './hooks/useDrumMidi';

export function DrumApp() {
  useDrumKeys();
  useDrumMidi();

  return (
    <>
      <DrumPowerOverlay />
      <main id="drum-rack">
        <Header />
        <div className="dr-main">
          <div className="dr-left">
            <div id="dr-pads"><PadGrid /></div>
            <div id="dr-padstrip"><PadStrip /></div>
          </div>
          <div className="dr-right">
            <div id="dr-selbar"><SelBar /></div>
            <div id="dr-oscrow">
              <OscSection osc="oscA" />
              <OscSection osc="oscB" />
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
        <div id="dr-stepseq"><StepSeq /></div>
        <div id="dr-fxrack"><FxRack /></div>
      </main>
    </>
  );
}
