import { useStore } from '../../store';
import { ArpPanel as Panel } from './ArpPanel';

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
  const bars = useStore(s => s.chain.length);
  const setBars = useStore(s => s.setSequenceLength);
  return <Panel {...{ a, keys, update, playing, current, play, stop, clear, bpm, swing, setParam, bars, setBars }} />;
}
