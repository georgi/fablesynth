import { BASS_PARAM_DEFS } from '../bass/params';
import { FACTORY_PATCHES } from '../bass/patches';
import { bassEngine, useBassStore } from '../bass/store';
import { presetReferences } from './presetReferences';
import { makeSingleParamAgentHost } from './wtAgentHost';

export function makeBassAgentHost() {
  return makeSingleParamAgentHost({
    plugin: 'FableSynth BL-1 (web)',
    definitions: BASS_PARAM_DEFS,
    getValues: () => useBassStore.getState().params,
    outputAnalyser: () => bassEngine.ready ? bassEngine.scopeAnalyser : null,
    unavailableReason: 'Power on BL-1 to measure output.',
    presetReferences: () => {
      const state = useBassStore.getState();
      return presetReferences('BL-1', FACTORY_PATCHES.map(patch => patch.name), state.patchValue,
        state.userPatches.map(patch => patch.name));
    },
    applyValues: next => {
      const state = useBassStore.getState();
      bassEngine.panic(state.hosted);
      bassEngine.params = next;
      useBassStore.setState({ params: next, dirty: true });
      bassEngine.applyAllParams();
    },
  });
}
