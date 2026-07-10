import { useDrumStore } from '../store';

export function SelBar() {
  const sel = useDrumStore((s) => s.sel);
  const padName = useDrumStore((s) => s.padNames[sel]);

  return (
    <div className="dr-selbar">
      <span className="dr-led dr-led-a" aria-hidden="true" />
      <span className="dr-mini-head">PAD {String(sel + 1).padStart(2, '0')}</span>
      <span className="dr-sel-name">{padName}</span>
      <span className="dr-sel-hint">MOD ENV ▸ POS · THE MORPH AXIS</span>
    </div>
  );
}
