import { engine, useStore } from '../store';
import { PARAM_DEFS, type ParamDef, type ParamValues } from '../params';
import type { AgentHost, AgentParameter, AgentSnapshot } from './webAgent';

function parameter(def: ParamDef, values: ParamValues): AgentParameter {
  const choices = def.type === 'bool' ? ['OFF', 'ON'] : def.options;
  const min = def.type === 'bool' || def.type === 'enum' ? 0 : def.min ?? 0;
  const max = def.type === 'bool' ? 1 : def.type === 'enum' ? Math.max(0, (def.options?.length ?? 1) - 1) : def.max ?? 1;
  return {
    id: def.id,
    name: def.label ?? def.id,
    unit: '',
    min,
    max,
    value: values[def.id],
    step: def.type || def.curve === 'int' ? 1 : 0,
    choices,
  };
}

function outputMeasurement(analyser: AnalyserNode | null, reason: string): Record<string, unknown> {
  if (!analyser) return { available: false, reason };
  const samples = new Float32Array(analyser.fftSize);
  analyser.getFloatTimeDomainData(samples);
  let sum = 0;
  let peak = 0;
  for (const sample of samples) {
    if (!Number.isFinite(sample)) return { available: false, reason: 'Output analyser returned a non-finite sample.' };
    sum += sample * sample;
    peak = Math.max(peak, Math.abs(sample));
  }
  const rms = Math.sqrt(sum / Math.max(1, samples.length));
  const dbfs = (value: number) => value > 0 ? 20 * Math.log10(value) : -Infinity;
  return {
    available: true,
    source: 'post-synth output analyser',
    sampleRate: analyser.context.sampleRate,
    frames: samples.length,
    rms,
    peak,
    rmsDbfs: dbfs(rms),
    peakDbfs: dbfs(peak),
  };
}

function revision(values: ParamValues): string {
  return JSON.stringify(values);
}

export function makeWtAgentHost(): AgentHost {
  return makeSingleParamAgentHost({
    plugin: 'FableSynth WT-1 (web)',
    definitions: PARAM_DEFS,
    getValues: () => useStore.getState().params,
    outputAnalyser: () => engine.ready ? engine.scopeAnalyser : null,
    unavailableReason: 'Power on WT-1 to measure output.',
    applyValues: next => {
      const state = useStore.getState();
      engine.panic(state.hosted);
      engine.params = next;
      useStore.setState({ params: next, dirty: true });
      engine.applyAllParams();
    },
  });
}

export interface SingleParamAgentHostOptions {
  plugin: string;
  definitions: ParamDef[];
  getValues(): ParamValues;
  outputAnalyser(): AnalyserNode | null;
  unavailableReason: string;
  applyValues(next: ParamValues): void;
}

export function makeSingleParamAgentHost(options: SingleParamAgentHostOptions): AgentHost {
  return {
    snapshot(): AgentSnapshot {
      const values = options.getValues();
      const output = outputMeasurement(options.outputAnalyser(), options.unavailableReason);
      return {
        plugin: options.plugin,
        parameters: options.definitions.map(def => parameter(def, values)),
        audio: output,
        meters: { available: Boolean(output.available), output, fx: { available: false, reason: 'WT-1 web FX telemetry is not exposed to the agent yet.' } },
      };
    },
    revision: () => revision(options.getValues()),
    apply(changes, before) {
      const current = options.getValues();
      const snapshot = new Map(before.parameters.map(item => [item.id, item.value]));
      const next = { ...current };
      for (const change of changes) {
        if (snapshot.get(change.id) !== change.before || current[change.id] !== change.before)
          throw new Error(`Parameter changed while the proposal was waiting: ${change.id}`);
        next[change.id] = change.after;
      }
      // One complete replacement keeps each worklet and its UI in sync without
      // a partially-applied sequence of individual knob writes.
      options.applyValues(next);
    },
  };
}
