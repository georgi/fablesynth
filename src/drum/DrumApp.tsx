import { DrumPowerOverlay } from './components/DrumPowerOverlay';
import { Header } from './components/Header';

export function DrumApp() {
  return (
    <>
      <DrumPowerOverlay />
      <main id="drum-rack">
        <Header />
        <div className="dr-main">
          <div className="dr-left">
            <div id="dr-pads" />
            <div id="dr-padstrip" />
          </div>
          <div className="dr-right">
            <div id="dr-selbar" />
            <div id="dr-oscrow" />
            <div id="dr-editrow" />
          </div>
        </div>
        <div id="dr-stepseq" />
        <div id="dr-fxrack" />
      </main>
    </>
  );
}
