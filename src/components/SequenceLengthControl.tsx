import { MAX_SEQUENCE_BARS, MIN_SEQUENCE_BARS } from '../sequenceLength';

interface SequenceLengthControlProps {
  editBar: number;
  length: number;
  playingBar?: number | null;
  onEditBar: (bar: number) => void;
  onLengthChange: (length: number) => void;
}

export function SequenceLengthControl({
  editBar,
  length,
  playingBar = null,
  onEditBar,
  onLengthChange,
}: SequenceLengthControlProps) {
  const barCount = Math.min(MAX_SEQUENCE_BARS, Math.max(MIN_SEQUENCE_BARS, length));

  return (
    <div className="seq-bars-control">
      <span className="seq-bars-label">BAR</span>
      <div className="seq-bars" role="group" aria-label="Bar to edit">
        {Array.from({ length: MAX_SEQUENCE_BARS }, (_, index) => (
          <button
            className={`seq-bar${editBar === index ? ' active' : ''}${index < barCount ? ' included' : ''}${barCount > 1 && playingBar === index ? ' playing' : ''}`}
            type="button"
            aria-label={`Edit bar ${index + 1}${index < barCount ? ', included in sequence' : ''}${barCount > 1 && playingBar === index ? ', currently playing' : ''}`}
            aria-pressed={editBar === index}
            key={index}
            onClick={() => onEditBar(index)}
          >
            {index + 1}
          </button>
        ))}
      </div>
      <span className="seq-bars-label seq-length-label">LENGTH</span>
      <div className="seq-length" role="group" aria-label="Sequence length">
        <button
          type="button"
          aria-label="Shorten sequence"
          disabled={barCount === MIN_SEQUENCE_BARS}
          onClick={() => onLengthChange(barCount - 1)}
        >
          −
        </button>
        <output aria-live="polite">{barCount} {barCount === 1 ? 'BAR' : 'BARS'}</output>
        <button
          type="button"
          aria-label="Lengthen sequence"
          disabled={barCount === MAX_SEQUENCE_BARS}
          onClick={() => onLengthChange(barCount + 1)}
        >
          +
        </button>
      </div>
    </div>
  );
}
