import { BASS_PARAM_DEFS } from '../bass/params';
import { bassEngine, useBassStore } from '../bass/store';
import { makeSingleParamAgentHost } from './wtAgentHost';

export function makeBassAgentHost() {
  return makeSingleParamAgentHost({
    plugin: 'FableSynth BL-1 (web)',
    definitions: BASS_PARAM_DEFS,
    getValues: () => useBassStore.getState().params,
    outputAnalyser: () => bassEngine.ready ? bassEngine.scopeAnalyser : null,
    unavailableReason: 'Power on BL-1 to measure output.',
    applyValues: next => {
      const state = useBassStore.getState();
      bassEngine.panic(state.hosted);
      bassEngine.params = next;
      useBassStore.setState({ params: next, dirty: true });
      bassEngine.applyAllParams();
    },
  });
}
