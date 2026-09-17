import type { ReactNode } from 'react';
import { ArpModeSwitch, ArpPanel } from '../../components/panels/ArpPanel';
import { newClipArp } from '../clipArp';
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
  const { launch, stopTrack, updateClipArp, setSwing } = useSeqStore.getState();
  if (!focus || !clip?.arp?.enabled) return <>{children}</>;
  const a = (clip.arp ?? newClipArp(machine)).settings;
  return <ArpPanel key={`${focus.scene}:${focus.track}`} hosted bass={machine === 'BL1'} a={a} keys={[]} playing={playing} queued={queued} current={current}
    bpm={bpm} swing={swing} setParam={(_, value) => setSwing(value)}
    play={() => launch(focus.track, focus.scene)} stop={() => stopTrack(focus.track)} clear={() => {}}
    update={patch => updateClipArp(focus.scene, focus.track, { settings: { ...a, ...patch, input: 'stored', latch: false } })}
    setMode={enabled => updateClipArp(focus.scene, focus.track, { enabled })} />;
}
