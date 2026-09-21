import { BASS_PARAM_DEFS } from '../bass/params';
import { FACTORY_PATCHES, patchToState } from '../bass/patches';
import { DRUM_PARAM_DEFS } from '../drum/params';
import { FACTORY_KITS, kitToState } from '../drum/kits';
import { PARAM_DEFS, type ParamDef } from '../params';
import { FACTORY_PRESETS, resolvePresetMods } from '../presets';
import { isTrackOpen } from '../seq/model';
import { saveSession, type MachineId, type PatchDoc, type SessionDoc } from '../seq/protocol';
import { embedSessionPatches } from '../seq/sessionExport';
import { FACTORY_SESSION_PRESETS } from '../seq/sessionPresets';
import { useSeqStore } from '../seq/store';
import { soundDesignReferences } from './presetReferences';
import type { AgentHost, AgentParameter, AgentSnapshot } from './webAgent';

type Values = Record<string, number>;

type PresetReference = {
  id: string;
  name: string;
  source: 'factory' | 'authored';
  family: string;
  variation: string;
  bpm: number;
  swing: number;
  energy: number;
  tags: string[];
  tracks: string[];
  note?: string;
};

const authoredNotes: Record<string, string> = {
  'TIDAL MEMORY': 'D minor, 118 BPM dub techno: soft chord stabs, deep bass, spacious dotted-eighth echoes.',
  'PHASE RUNNER': 'F minor, 126 BPM dub techno: driving bass hook, dark chord echoes, low wooden response.',
};

// Intentionally compact. The agent gets musical identity and instrumentation,
// not whole clips or patch dumps, so this remains a reference catalog rather
// than a hidden preset-loading API.
const presetReferences: readonly PresetReference[] = FACTORY_SESSION_PRESETS.map((preset, index) => ({
  id: `session.${index}`,
  name: preset.name,
  source: preset.tags.includes('authored') ? 'authored' : 'factory',
  family: preset.family,
  variation: preset.variation,
  bpm: preset.session.bpm,
  swing: preset.session.swing,
  energy: preset.energy,
  tags: [...preset.tags],
  tracks: preset.session.tracks.map(track => `${track.machine} · ${track.name}`),
  note: authoredNotes[preset.name],
}));

function presetCatalog(currentSession: string): Record<string, unknown> {
  return {
    presetCatalog: {
      readOnly: true,
      description: 'Compact SQ-4 session references for musical orientation. Filter entries before returning them.',
      currentSession,
      entries: presetReferences,
    },
    ...soundDesignReferences('SQ-4'),
  };
}

const defsFor = (machine: MachineId): ParamDef[] =>
  machine === 'DR1' ? DRUM_PARAM_DEFS : machine === 'BL1' ? BASS_PARAM_DEFS : PARAM_DEFS;

function paramsFor(machine: MachineId, patch: PatchDoc): Values {
  const index = patch.kind === 'factory' ? patch.index : patch.base ?? 0;
  const factory = machine === 'DR1'
    ? kitToState(FACTORY_KITS[index] ?? FACTORY_KITS[0]).params
    : machine === 'BL1'
      ? patchToState(FACTORY_PATCHES[index] ?? FACTORY_PATCHES[0]).params
      : resolvePresetMods((FACTORY_PRESETS[index] ?? FACTORY_PRESETS[0]).params, (FACTORY_PRESETS[index] ?? FACTORY_PRESETS[0]).mods);
  const inline = patch.kind === 'inline' ? (patch.data as { params?: Values }).params : undefined;
  return { ...factory, ...inline };
}

function parameter(id: string, name: string, def: ParamDef, value: number): AgentParameter {
  const choices = def.type === 'bool' ? ['OFF', 'ON'] : def.options;
  const min = def.type === 'bool' || def.type === 'enum' ? 0 : def.min ?? 0;
  const max = def.type === 'bool' ? 1 : def.type === 'enum' ? Math.max(0, (def.options?.length ?? 1) - 1) : def.max ?? 1;
  return { id, name, unit: '', min, max, value, step: def.type || def.curve === 'int' ? 1 : 0, choices };
}

function measurement(analyser: AnalyserNode | null, reason: string): Record<string, unknown> {
  if (!analyser) return { available: false, reason };
  const samples = new Float32Array(analyser.fftSize);
  analyser.getFloatTimeDomainData(samples);
  let sum = 0;
  let peak = 0;
  for (const sample of samples) { sum += sample * sample; peak = Math.max(peak, Math.abs(sample)); }
  const rms = Math.sqrt(sum / Math.max(1, samples.length));
  const dbfs = (value: number) => value > 0 ? 20 * Math.log10(value) : -Infinity;
  return { available: true, source: 'post-fader track analyser', sampleRate: analyser.context.sampleRate, frames: samples.length, rms, peak, rmsDbfs: dbfs(rms), peakDbfs: dbfs(peak) };
}

function snapshot(): AgentSnapshot {
  const state = useSeqStore.getState();
  const parameters: AgentParameter[] = [
    { id: 'session.bpm', name: 'Session · BPM', unit: 'BPM', min: 60, max: 200, value: state.session.bpm, step: 1 },
    { id: 'session.swing', name: 'Session · Swing', unit: '', min: 0, max: 1, value: state.swing, step: 0 },
    { id: 'master.volume', name: 'Master · Volume', unit: '', min: 0, max: 1, value: state.masterVol, step: 0 },
  ];
  const trackMeters: Record<string, unknown> = {};
  state.session.tracks.forEach((track, trackIndex) => {
    parameters.push({ id: `track.${trackIndex}.volume`, name: `${track.name} · Track volume`, unit: '', min: 0, max: 1, value: state.trackVol[trackIndex], step: 0 });
    const engineParams = state.rig?.devices[trackIndex]?.engine?.params;
    const values = engineParams ? { ...engineParams } : paramsFor(track.machine, track.patch);
    defsFor(track.machine).forEach(def => parameters.push(parameter(
      `track.${trackIndex}.param.${def.id}`,
      `${track.name} · ${def.label ?? def.id}`,
      def,
      values[def.id],
    )));
    trackMeters[`track.${trackIndex}`] = measurement(state.rig?.trackAnalysers?.[trackIndex] ?? null, 'Power on SQ-4 and play a clip to measure this track.');
  });
  const available = Object.values(trackMeters).some((item) => (item as { available?: boolean }).available);
  return {
    plugin: 'FableSynth SQ-4 (web)', parameters,
    audio: { available, tracks: trackMeters },
    meters: { available, tracks: trackMeters, master: { available: false, reason: 'SQ-4 exposes post-fader track analysers; master telemetry is not exposed yet.' } },
    references: presetCatalog(state.session.name),
  };
}

function currentValues(): Map<string, number> {
  return new Map(snapshot().parameters.map((item) => [item.id, item.value]));
}

const gainCurve = (value: number) => value * value * 1.4;

export function makeSqAgentHost(): AgentHost {
  return {
    snapshot,
    revision: () => JSON.stringify(snapshot().parameters.map((item) => [item.id, item.value])),
    apply(changes, before) {
      const state = useSeqStore.getState();
      const prior = new Map(before.parameters.map((item) => [item.id, item.value]));
      const current = currentValues();
      for (const change of changes) {
        if (prior.get(change.id) !== change.before || current.get(change.id) !== change.before)
          throw new Error(`Parameter changed while the proposal was waiting: ${change.id}`);
      }

      let bpm = state.session.bpm;
      let swing = state.swing;
      let masterVol = state.masterVol;
      const trackVol = state.trackVol.slice();
      const trackParams = state.session.tracks.map((track, trackIndex) => {
        const engineParams = state.rig?.devices[trackIndex]?.engine?.params;
        return engineParams ? { ...engineParams } : paramsFor(track.machine, track.patch);
      });
      for (const change of changes) {
        if (change.id === 'session.bpm') bpm = change.after;
        else if (change.id === 'session.swing') swing = change.after;
        else if (change.id === 'master.volume') masterVol = change.after;
        else {
          const match = change.id.match(/^track\.(\d+)\.(?:param\.(.+)|volume)$/);
          if (!match) throw new Error(`Unsupported SQ-4 parameter: ${change.id}`);
          const trackIndex = Number(match[1]);
          if (!state.session.tracks[trackIndex]) throw new Error(`Unknown SQ-4 track: ${trackIndex}`);
          if (match[2]) trackParams[trackIndex][match[2]] = change.after;
          else trackVol[trackIndex] = change.after;
        }
      }
      const tracks = state.session.tracks.map((track, trackIndex) => ({
        ...track,
        gain: trackVol[trackIndex],
        patch: { kind: 'inline' as const, data: { params: trackParams[trackIndex] }, base: track.patch.kind === 'factory' ? track.patch.index : track.patch.base },
      }));
      const session: SessionDoc = { ...state.session, bpm, swing, tracks };
      // Commit one store update, then mirror the complete transaction to the
      // live rig. No partial sequence of knob writes becomes visible to the user.
      useSeqStore.setState({ session, swing, trackVol, masterVol });
      if (state.rig) {
        state.rig.devices.forEach((device, trackIndex) => device.applyPatch(tracks[trackIndex].patch));
        state.rig.sendTempo(bpm, swing, state.anchor);
        state.rig.setMasterGain(gainCurve(masterVol));
        tracks.forEach((_, trackIndex) => state.rig!.setTrackGain(trackIndex,
          isTrackOpen(trackIndex, state.owner, state.trackMute, state.sceneMute, state.solo) ? gainCurve(trackVol[trackIndex]) : 0,
        ));
      }
      saveSession(embedSessionPatches({ ...session, quant: state.quant, swing }));
    },
  };
}
