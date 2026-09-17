import { useStore } from '../../store';
import { ArpModeSwitch as ModeSwitch, ArpPanel as Panel } from './ArpPanel';

export function ArpModeSwitch() {
  const mode = useStore(s => s.arpMode);
  const setMode = useStore(s => s.setArpMode);
  return <ModeSwitch mode={mode} setMode={setMode} />;
}

export function ArpPanel() {
  const a = useStore(s => s.arp);
  const keys = useStore(s => s.arpKeys);
  const update = useStore(s => s.updateArp);
  const playing = useStore(s => s.seqPlaying);
  const current = useStore(s => s.curStep);
  const play = useStore(s => s.seqPlay);
  const stop = useStore(s => s.seqStop);
  const clear = useStore(s => s.clearArpKeys);
  const bpm = useStore(s => s.params['seq.bpm']);
  const swing = useStore(s => s.params['seq.swing']);
  const setParam = useStore(s => s.setParam);
  const setMode = useStore(s => s.setArpMode);
  return <Panel {...{ a, keys, update, playing, current, play, stop, clear, bpm, swing, setParam, setMode }} />;
}
