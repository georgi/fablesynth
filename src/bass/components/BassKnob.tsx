import { ParamKnob, type ParamKnobSize } from '../../components/ParamKnob';
import { BASS_PARAMS } from '../params';
import { useBassStore } from '../store';

export type BassKnobSize = ParamKnobSize;

interface BassKnobProps {
  paramId: string;
  size?: BassKnobSize;
  accent?: string;
  label?: string;
}

export function BassKnob({ paramId, ...rest }: BassKnobProps) {
  const value = useBassStore((s) => s.params[paramId]);
  const setParam = useBassStore((s) => s.setParam);
  return <ParamKnob machine="BL1" paramId={paramId} def={BASS_PARAMS[paramId]} value={value} setParam={setParam} {...rest} />;
}
