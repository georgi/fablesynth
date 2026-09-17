import { useBassStore } from '../store';
import { ArpModeSwitch as ModeSwitch, ArpPanel } from '../../components/panels/ArpPanel';

export function BassArpModeSwitch() {
  const mode = useBassStore(s => s.arpMode);
  const setMode = useBassStore(s => s.setArpMode);
  return <ModeSwitch mode={mode} setMode={setMode} />;
}

export function BassArpPanel() {
  const a = useBassStore(s => s.arp);
  const keys = useBassStore(s => s.arpKeys);
  const update = useBassStore(s => s.updateArp);
  const playing = useBassStore(s => s.playing);
  const current = useBassStore(s => s.curStep);
  const play = useBassStore(s => s.play);
  const stop = useBassStore(s => s.stop);
  const clear = useBassStore(s => s.clearArpKeys);
  const bpm = useBassStore(s => s.params['seq.bpm']);
  const swing = useBassStore(s => s.params['master.swing']);
  const setParam = useBassStore(s => s.setParam);
  const setMode = useBassStore(s => s.setArpMode);
  return <ArpPanel bass {...{ a, keys, update, playing, current, play, stop, clear, bpm, swing, setParam, setMode }} />;
}
