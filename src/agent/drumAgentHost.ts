import { DRUM_PARAM_DEFS } from '../drum/params';
import { FACTORY_KITS } from '../drum/kits';
import { drumEngine, useDrumStore } from '../drum/store';
import { presetReferences } from './presetReferences';
import { makeSingleParamAgentHost } from './wtAgentHost';

export function makeDrumAgentHost() {
  return makeSingleParamAgentHost({
    plugin: 'FableSynth DR-1 (web)',
    definitions: DRUM_PARAM_DEFS,
    getValues: () => useDrumStore.getState().params,
    outputAnalyser: () => drumEngine.ready ? drumEngine.scopeAnalyser : null,
    unavailableReason: 'Power on DR-1 to measure output.',
    presetReferences: () => {
      const state = useDrumStore.getState();
      return presetReferences('DR-1', FACTORY_KITS.map(kit => kit.name), state.kitValue,
        state.userKits.map(kit => kit.name));
    },
    applyValues: next => {
      const state = useDrumStore.getState();
      drumEngine.panic(state.hosted);
      drumEngine.params = next;
      useDrumStore.setState({ params: next, kitDirty: true });
      drumEngine.applyAllParams();
    },
  });
}
