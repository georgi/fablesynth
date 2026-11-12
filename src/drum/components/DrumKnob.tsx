import { ParamKnob, type ParamKnobSize } from '../../components/ParamKnob';
import { DRUM_PARAMS } from '../params';
import { useDrumStore } from '../store';

export type DrumKnobSize = ParamKnobSize;

interface DrumKnobProps {
  paramId: string;
  size?: DrumKnobSize;
  accent?: string;
  label?: string;
}

export function DrumKnob({ paramId, ...rest }: DrumKnobProps) {
  const value = useDrumStore((s) => s.params[paramId]);
  const setParam = useDrumStore((s) => s.setParam);
  return <ParamKnob machine="DR1" paramId={paramId} def={DRUM_PARAMS[paramId]} value={value} setParam={setParam} {...rest} />;
}
