import type { ReactNode } from 'react';
import type { ArpState } from '../../arp';
import { ArpModeSwitch, ArpPanel } from '../../components/panels/ArpPanel';
import { clipChordBars, linkedClipTracks, newClipArp } from '../clipArp';
import type { MachineId } from '../protocol';
import { useSeqStore } from '../store';

export function HostedArpModeSwitch() {
  const focus = useSeqStore(s => s.focus);
  const clip = useSeqStore(s => focus ? s.session.scenes[focus.scene]?.clips[focus.track] : null);
  const update = useSeqStore(s => s.updateClipArp);
  if (!focus || !clip) return null;
  return <ArpModeSwitch mode={!!clip.arp?.enabled} setMode={enabled => update(focus.scene, focus.track, { enabled })} />;
}

export function HostedArpEditor({ machine, children }: { machine: MachineId; children: ReactNode }) {
  const focus = useSeqStore(s => s.focus);
  const clip = useSeqStore(s => focus ? s.session.scenes[focus.scene]?.clips[focus.track] : null);
  const playing = useSeqStore(s => !!focus && s.playing && s.owner[focus.track] === focus.scene);
  const queued = useSeqStore(s => !!focus && s.queue[focus.track] === focus.scene);
  const current = useSeqStore(s => playing && focus ? s.pos[focus.track]?.step ?? -1 : -1);
  const bpm = useSeqStore(s => s.session.bpm);
  const swing = useSeqStore(s => s.swing);
  const peers = useSeqStore(s => focus ? linkedClipTracks(s.session, focus.scene, focus.track).length : 0);
  const { launch, stopTrack, updateClipArp, setSwing } = useSeqStore.getState();
  if (!focus || !clip?.arp?.enabled) return <>{children}</>;
  const a = (clip.arp ?? newClipArp(machine)).settings;
  // Fresh settings per edit: one gesture can send several edits (a progression sets chords, then bars).
  const edit = (patch: Partial<ArpState>) => {
    const now = useSeqStore.getState().session.scenes[focus.scene]?.clips[focus.track]?.arp?.settings ?? a;
    updateClipArp(focus.scene, focus.track, { settings: { ...now, ...patch, input: patch.input === 'keys' ? 'stored' : patch.input ?? now.input, latch: false } });
  };
  return <ArpPanel key={`${focus.scene}:${focus.track}`} hosted bass={machine === 'BL1'} a={a} keys={[]} playing={playing} queued={queued} current={current}
    bpm={bpm} swing={swing} setParam={(_, value) => setSwing(value)}
    play={() => launch(focus.track, focus.scene)} stop={() => stopTrack(focus.track)} clear={() => {}}
    bars={clipChordBars(a)} setBars={chordBars => edit({ chordBars })}
    chordLink={{ on: !!a.chordLink, peers, toggle: () => edit({ chordLink: !a.chordLink }) }}
    update={edit}
    setMode={enabled => updateClipArp(focus.scene, focus.track, { enabled })} />;
}
