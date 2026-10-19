import { describe, expect, it, vi } from 'vitest';
import { NoteLengthHandle } from './NoteLengthHandle';

vi.mock('react', () => ({ useRef: (current?: unknown) => ({ current }) }));

describe('NoteLengthHandle undo gestures', () => {
  it('shares one token during a drag and ends it on release, cancel, or lost capture', () => {
    const onChange = vi.fn();
    const tree = NoteLengthHandle({ prefix: 'ns', absoluteStep: 0, totalSteps: 16, duration: 1, onChange });
    const handle = tree.props.children.props;
    let captured = false;
    const event = (clientX: number) => ({
      clientX, pointerId: 1, preventDefault() {}, stopPropagation() {},
      currentTarget: {
        hasPointerCapture: () => captured,
        setPointerCapture: () => { captured = true; },
        releasePointerCapture: () => { captured = false; },
        closest: () => ({ getBoundingClientRect: () => ({ left: 0, width: 155 }) }),
      },
    });
    handle.onPointerDown(event(0));
    handle.onPointerMove(event(12));
    handle.onPointerMove(event(22));
    handle.onPointerUp(event(32));
    expect(onChange.mock.calls.map(c => c[0])).toEqual([2, 3, 4]);
    const first = onChange.mock.calls[0][1];
    expect(typeof first).toBe('symbol');
    expect(onChange.mock.calls.every(c => c[1] === first)).toBe(true);
    handle.onPointerMove(event(42));
    expect(onChange).toHaveBeenCalledTimes(3);

    for (const end of ['onPointerCancel', 'onLostPointerCapture']) {
      handle.onPointerDown(event(0));
      handle.onPointerMove(event(22));
      const token = onChange.mock.lastCall![1];
      expect(token).not.toBe(first);
      handle[end]();
      captured = false;
      handle.onPointerDown(event(0));
      handle.onPointerMove(event(32));
      expect(onChange.mock.lastCall![1]).not.toBe(token);
      handle.onPointerUp(event(32));
    }
    handle.onKeyDown({ key: 'ArrowRight', preventDefault() {} });
    expect(onChange.mock.lastCall).toEqual([2]);
  });
});
