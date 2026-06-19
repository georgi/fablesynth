import { useEffect } from 'react';
import { PowerOverlay } from './components/PowerOverlay';
import { WavetableEditor } from './components/WavetableEditor';
import { TopBar } from './components/panels/TopBar';
import { OscPanel } from './components/panels/OscPanel';
import { UtilPanel } from './components/panels/UtilPanel';
import { FilterPanel } from './components/panels/FilterPanel';
import { EnvPanel } from './components/panels/EnvPanel';
import { LfoPanel } from './components/panels/LfoPanel';
import { MatrixPanel } from './components/panels/MatrixPanel';
import { FxPanel } from './components/panels/FxPanel';
import { KeyboardBar } from './components/panels/KeyboardBar';
import { useComputerKeys } from './hooks/useComputerKeys';
import { useMidi } from './hooks/useMidi';
import { engine, useStore } from './store';

export function App() {
  useComputerKeys();
  useMidi();

  // exposed for debugging / automated verification
  useEffect(() => {
    (window as unknown as { __fable: unknown }).__fable = {
      engine,
      applyPreset: useStore.getState().applyPreset,
    };
  }, []);

  return (
    <>
      <PowerOverlay />
      <WavetableEditor />
      <main id="rack">
        <TopBar />
        <div className="panels">
          <OscPanel prefix="oscA" accentKey="a" title="OSC A" gridArea="oscA" />
          <OscPanel prefix="oscB" accentKey="b" title="OSC B" gridArea="oscB" />
          <UtilPanel />
          <FilterPanel />
          <EnvPanel id="env1" title="AMP ENV" gridArea="env1" viewAccent="#e8edf7" knobAccent="n" />
          <EnvPanel id="env2" title="MOD ENV" gridArea="env2" viewAccent="#b18cff" knobAccent="f" modSource={3} />
          <LfoPanel />
          <MatrixPanel />
          <FxPanel />
        </div>
        <KeyboardBar />
      </main>
    </>
  );
}
