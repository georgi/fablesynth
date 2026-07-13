// Hosted device view (focus mode): attaches the focused track's running
// engine to that machine's standalone store, keeps the target clip and the
// track patch in sync with the session doc, and renders the device panels.

import { useEffect, useRef } from 'react';
import type * as React from 'react';
import { EnvPanel as BassEnvPanel } from '../../bass/components/EnvPanel';
import { BassFxRack } from '../../bass/components/BassFxRack';
import { FilterSection as BassFilterSection } from '../../bass/components/FilterSection';
import { KeysPanel } from '../../bass/components/KeysPanel';
import { AccentPanel, LfoPanel as BassLfoPanel } from '../../bass/components/LfoPanel';
import { OscSection as BassOscSection, SubSection } from '../../bass/components/OscSection';
import { PitchSeq } from '../../bass/components/PitchSeq';
import { makeEmptyPatterns as bassEmpty } from '../../bass/seq';
import { useBassStore } from '../../bass/store';
import { AmpEnvPanel } from '../../drum/components/AmpEnvPanel';
import { FilterSection } from '../../drum/components/FilterSection';
import { FxRack } from '../../drum/components/FxRack';
import { ModPanel } from '../../drum/components/ModPanel';
import { NoiseSection } from '../../drum/components/NoiseSection';
import { OscSection } from '../../drum/components/OscSection';
import { PadGrid } from '../../drum/components/PadGrid';
import { PadStrip } from '../../drum/components/PadStrip';
import { PitchEnvPanel } from '../../drum/components/PitchEnvPanel';
import { SelBar } from '../../drum/components/SelBar';
import { StepSeq } from '../../drum/components/StepSeq';
import { makeEmptyPatterns as drumEmpty } from '../../drum/seq';
import { useDrumStore } from '../../drum/store';
import { makeEmptyPatterns as wtEmpty } from '../../noteseq';
import { EnvPanel } from '../../components/panels/EnvPanel';
import { FilterPanel } from '../../components/panels/FilterPanel';
import { FxPanel } from '../../components/panels/FxPanel';
import { KeyboardBar } from '../../components/panels/KeyboardBar';
import { LfoPanel } from '../../components/panels/LfoPanel';
import { MatrixPanel } from '../../components/panels/MatrixPanel';
import { OscPanel } from '../../components/panels/OscPanel';
import { SeqPanel } from '../../components/panels/SeqPanel';
import { UtilPanel } from '../../components/panels/UtilPanel';
import { useStore as useWtStore } from '../../store';
import { clipToPatterns, patternsToClip } from '../hostBridge';
import { HOSTED_MAX_BARS, type MachineId } from '../protocol';
import { clipPattern, useSeqStore } from '../store';
import { HostedClipBar } from './HostedClipBar';

// One uniform handle per machine over the three (differently-typed) stores.
interface HostedStore {
  attach: (engine: unknown) => void;
  getPatterns: () => Uint8Array;
  setPatterns: (p: Uint8Array) => void;
  getParams: () => Record<string, number>;
  setPos: (step: number, bar: number, playing: boolean) => void;
  subscribe: (fn: () => void) => () => void;
  empty: () => Uint8Array;
}

const HOSTS: Record<MachineId, HostedStore> = {
  DR1: {
    attach: (e) => useDrumStore.getState().attachHosted(e as never),
    getPatterns: () => useDrumStore.getState().patterns,
    setPatterns: (p) => useDrumStore.setState({ patterns: p, editPattern: 0 }),
    getParams: () => useDrumStore.getState().params,
    setPos: (step, bar, playing) => useDrumStore.setState({ curStep: step, curPat: bar, playing }),
    subscribe: (fn) => useDrumStore.subscribe(fn),
    empty: drumEmpty,
  },
  BL1: {
    attach: (e) => useBassStore.getState().attachHosted(e as never),
    getPatterns: () => useBassStore.getState().patterns,
    setPatterns: (p) => useBassStore.setState({ patterns: p, editPattern: 0 }),
    getParams: () => useBassStore.getState().params,
    setPos: (step, bar, playing) => useBassStore.setState({ curStep: step, curPat: bar, playing }),
    subscribe: (fn) => useBassStore.subscribe(fn),
    empty: bassEmpty,
  },
  WT1: {
    attach: (e) => useWtStore.getState().attachHosted(e as never),
    getPatterns: () => useWtStore.getState().patterns,
    setPatterns: (p) => useWtStore.setState({ patterns: p, editPattern: 0 }),
    getParams: () => useWtStore.getState().params,
    setPos: (step, bar, playing) => useWtStore.setState({ curStep: step, curPat: bar, seqPlaying: playing }),
    subscribe: (fn) => useWtStore.subscribe(fn),
    empty: wtEmpty,
  },
};

export function DeviceView() {
  const focus = useSeqStore((s) => s.focus);
  const session = useSeqStore((s) => s.session);
  const rig = useSeqStore((s) => s.rig);
  const clipLoadRevision = useSeqStore((s) => s.clipLoadRevision);

  const track = focus ? session.tracks[focus.track] : null;
  const clip = focus ? session.scenes[focus.scene]?.clips[focus.track] : null;
  const machine = track?.machine ?? null;
  const host = machine ? HOSTS[machine] : null;
  const editable = !!clip && clip.bars <= HOSTED_MAX_BARS;

  // Guards the pattern-subscription against echoing our own writes.
  const syncing = useRef(false);

  // 1. Attach the rig engine whenever the focused track changes.
  useEffect(() => {
    if (!focus || !rig || !host) return;
    const engine = rig.devices[focus.track].engine;
    if (engine) host.attach(engine);
  }, [focus?.track, rig, host]);

  // 2. Load the target clip's bytes into the device store on target change.
  useEffect(() => {
    if (!focus || !host || !machine) return;
    syncing.current = true;
    const bytes = clipPattern(session, focus.scene, focus.track);
    host.setPatterns(bytes ? clipToPatterns(bytes, host.empty()) : host.empty());
    syncing.current = false;
    // Intentionally NOT keyed on the pattern string: our own write-backs must
    // not reload (they'd reset editPattern); createClip flips `!!clip`.
  }, [focus?.scene, focus?.track, !!clip, host, machine, clipLoadRevision]);

  // 3. Write pattern edits back: device store → session doc (+ live hot-swap).
  useEffect(() => {
    if (!focus || !host || !machine || !editable) return;
    let prev = host.getPatterns();
    return host.subscribe(() => {
      const next = host.getPatterns();
      if (next === prev || syncing.current) { prev = next; return; }
      prev = next;
      const st = useSeqStore.getState();
      const cur = st.session.scenes[focus.scene]?.clips[focus.track];
      if (!cur) return;
      st.updateClipBytes(focus.scene, focus.track, patternsToClip(machine, next, cur.bars), cur.bars);
    });
  }, [focus?.scene, focus?.track, host, machine, editable]);

  // 4. Snapshot patch edits (debounced) into the track's inline patch doc.
  //    Cleanup flushes a pending timer rather than discarding it, so tweaks
  //    made <400ms before Esc/track-switch aren't lost.
  useEffect(() => {
    if (!focus || !host) return;
    let prev = host.getParams();
    let timer: ReturnType<typeof setTimeout> | undefined;
    const write = () => {
      useSeqStore.getState().setTrackPatch(focus.track, {
        kind: 'inline',
        data: { params: { ...host.getParams() } },
      });
    };
    const unsub = host.subscribe(() => {
      const next = host.getParams();
      if (next === prev) return;
      prev = next;
      clearTimeout(timer);
      timer = setTimeout(() => {
        timer = undefined;
        write();
      }, 400);
    });
    return () => {
      if (timer !== undefined) {
        clearTimeout(timer);
        write();
      }
      unsub();
    };
  }, [focus?.track, host]);

  // 5. Mirror the conductor's per-track playhead into the device step LEDs —
  //    only while the focused clip is the one actually sounding.
  const owner = useSeqStore((s) => s.owner);
  const pos = useSeqStore((s) => focus ? s.pos[focus.track] : null);
  const playing = useSeqStore((s) => s.playing);
  useEffect(() => {
    if (!focus || !host) return;
    const live = owner[focus.track] === focus.scene && playing;
    if (live && pos) host.setPos(pos.step, pos.bar, true);
    else host.setPos(-1, 0, false);
  }, [focus, host, owner, pos, playing]);

  if (!focus || !track || !machine) return null;

  return (
    <section className="sq-device" style={{ '--tc': track.color } as React.CSSProperties}>
      <HostedClipBar machine={machine} />
      <div className="sq-device-body">
        {machine === 'DR1' && <DrumPanels />}
        {machine === 'BL1' && <BassPanels />}
        {machine === 'WT1' && <WtPanels />}
      </div>
    </section>
  );
}

function DrumPanels() {
  return (
    <div id="drum-rack" className="sq-hosted-rack">
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
      <div id="dr-fxrack"><FxRack /></div>
      <div id="dr-stepseq"><StepSeq /></div>
    </div>
  );
}

function BassPanels() {
  return (
    <div id="bass-rack" className="sq-hosted-rack">
      <div id="bl-editrow">
        <BassOscSection />
        <SubSection />
        <BassFilterSection />
        <BassEnvPanel />
      </div>
      <div id="bl-modrow">
        <BassLfoPanel />
        <AccentPanel />
        <KeysPanel />
      </div>
      <div id="bl-seq"><PitchSeq /></div>
      <div id="bl-fxrack"><BassFxRack /></div>
    </div>
  );
}

function WtPanels() {
  return (
    <div id="rack" className="sq-hosted-rack">
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
        <SeqPanel />
      </div>
      <KeyboardBar />
    </div>
  );
}
