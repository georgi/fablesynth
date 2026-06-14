// Web MIDI input — notes + pitch bend (Chrome/Edge).

import { useEffect } from 'react';
import { useStore } from '../store';

export function useMidi() {
  const playNote = useStore((s) => s.playNote);
  const bend = useStore((s) => s.bend);
  const setMidiActive = useStore((s) => s.setMidiActive);

  useEffect(() => {
    if (!navigator.requestMIDIAccess) return;
    let access: MIDIAccess | null = null;
    navigator.requestMIDIAccess().then((acc) => {
      access = acc;
      const update = () => {
        let any = false;
        for (const input of acc.inputs.values()) {
          any = true;
          input.onmidimessage = (e: MIDIMessageEvent) => {
            const data = e.data;
            if (!data) return;
            const [st, d1, d2] = data;
            const cmd = st & 0xf0;
            if (cmd === 0x90 && d2 > 0) { playNote(d1, d2 / 127); }
            else if (cmd === 0x80 || (cmd === 0x90 && d2 === 0)) { playNote(d1, 0); }
            else if (cmd === 0xe0) { bend((((d2 << 7) | d1) - 8192) / 8192 * 2); }
          };
        }
        setMidiActive(any);
      };
      update();
      acc.onstatechange = update;
    }).catch(() => { /* ignore */ });

    return () => {
      if (access) {
        for (const input of access.inputs.values()) input.onmidimessage = null;
        access.onstatechange = null;
      }
    };
  }, [playNote, bend, setMidiActive]);
}
