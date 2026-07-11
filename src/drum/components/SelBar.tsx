import { patchOptions } from '../patches';
import { useDrumStore } from '../store';

export function SelBar() {
  const sel = useDrumStore((s) => s.sel);
  const padName = useDrumStore((s) => s.padNames[sel]);
  const patchValue = useDrumStore((s) => s.patchValue);
  const userPatches = useDrumStore((s) => s.userPatches);
  const stepPatch = useDrumStore((s) => s.stepPatch);
  const savePatch = useDrumStore((s) => s.savePatch);

  const currentPatch = patchOptions(userPatches).find((option) => option.value === patchValue)?.name ?? '—';
  const onSave = () => {
    const name = (window.prompt('Patch name') || '').trim().toUpperCase();
    if (!name) return;
    savePatch(name);
  };

  return (
    <div className="dr-selbar">
      <span className="dr-led dr-led-a" aria-hidden="true" />
      <span className="dr-mini-head">PAD {String(sel + 1).padStart(2, '0')}</span>
      <span className="dr-sel-name">{padName}</span>
      <div className="dr-patchbar" aria-label="patch selection">
        <span className="dr-mini-head">PATCH</span>
        <button className="pb-btn" aria-label="previous patch" onClick={() => stepPatch(-1)}>◂</button>
        <span className="dr-patchname">{currentPatch}</span>
        <button className="pb-btn" aria-label="next patch" onClick={() => stepPatch(1)}>▸</button>
        <button className="pb-btn pb-save" onClick={onSave}>SAVE</button>
      </div>
    </div>
  );
}
