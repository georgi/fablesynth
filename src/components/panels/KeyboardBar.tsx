import { Knob } from '../Knob';
import { Keyboard } from '../Keyboard';
import { useStore } from '../../store';

export function KeyboardBar() {
  const octave = useStore((s) => s.octave);
  const setOctave = useStore((s) => s.setOctave);
  const playNote = useStore((s) => s.playNote);

  return (
    <footer className="kb-bar">
      <div className="kb-side">
        <div className="oct-ctl">
          <button id="oct-down" aria-label="octave down" onClick={() => setOctave(Math.max(-3, octave - 1))}>−</button>
          <span id="oct-label">{'C' + (4 + octave)}</span>
          <button id="oct-up" aria-label="octave up" onClick={() => setOctave(Math.min(3, octave + 1))}>+</button>
        </div>
        <div id="glide-knob"><Knob paramId="master.glide" size="sm" accent="n" /></div>
      </div>
      <Keyboard low={36} high={84} onNote={playNote} />
    </footer>
  );
}
