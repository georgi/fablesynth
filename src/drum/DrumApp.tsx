import { DrumPowerOverlay } from './components/DrumPowerOverlay';
import { Header } from './components/Header';
import { PadGrid } from './components/PadGrid';
import { PadStrip } from './components/PadStrip';
import { NoiseSection } from './components/NoiseSection';
import { OscSection } from './components/OscSection';
import { SelBar } from './components/SelBar';
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
            <div id="dr-editrow" />
          </div>
        </div>
        <div id="dr-stepseq" />
        <div id="dr-fxrack" />
      </main>
    </>
  );
}
