// Hosted device view (focus mode): attaches the focused track's running
// engine to that machine's standalone store, keeps the target clip and the
// track patch in sync with the session doc, and renders the device panels.

import { useEffect, useRef } from 'react';
import type * as React from 'react';
import { makeEmptyPatterns as bassEmpty } from '../../bass/seq';
import { useBassStore } from '../../bass/store';
import { makeEmptyPatterns as drumEmpty } from '../../drum/seq';
import { useDrumStore } from '../../drum/store';
import { makeEmptyPatterns as wtEmpty } from '../../noteseq';
import { useStore as useWtStore } from '../../store';
import { clipToPatterns, patternsToClip } from '../hostBridge';
import { HOSTED_MAX_BARS, type MachineId } from '../protocol';
import { clipPattern, useSeqStore } from '../store';

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
  }, [focus?.scene, focus?.track, !!clip, host, machine]);

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
  useEffect(() => {
    if (!focus || !host) return;
    let prev = host.getParams();
    let timer: ReturnType<typeof setTimeout> | undefined;
    const unsub = host.subscribe(() => {
      const next = host.getParams();
      if (next === prev) return;
      prev = next;
      clearTimeout(timer);
      timer = setTimeout(() => {
        useSeqStore.getState().setTrackPatch(focus.track, {
          kind: 'inline',
          data: { params: { ...host.getParams() } },
        });
      }, 400);
    });
    return () => { clearTimeout(timer); unsub(); };
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
      <div className="sq-device-body" />
    </section>
  );
}
