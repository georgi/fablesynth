import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { PointerEvent as ReactPointerEvent } from 'react';
import { useSeqNoteDraw } from './useSeqNoteDraw';

const hooks = vi.hoisted(() => ({ value: null as unknown, cleanups: [] as (() => void)[] }));
vi.mock('react', () => ({
  useRef: (current: unknown) => ({ current }),
  useState: (initial: unknown) => {
    hooks.value = initial;
    return [initial, (value: unknown) => { hooks.value = value; }];
  },
  useEffect: (effect: () => () => void) => { hooks.cleanups.push(effect()); },
}));

beforeEach(() => {
  vi.stubGlobal('window', new EventTarget());
  vi.useFakeTimers();
});
afterEach(() => {
  hooks.cleanups.splice(0).forEach(cleanup => cleanup());
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

function dispatch(type: string, props: Record<string, unknown> = {}) {
  window.dispatchEvent(Object.assign(new Event(type), { pointerId: 1, clientX: 105, ...props }));
}
function start(draw: ReturnType<typeof useSeqNoteDraw>, step = 2, total = 32) {
  dispatch('pointerdown');
  draw.startNoteDraw({
    button: 0, pointerId: 1, preventDefault: vi.fn(),
    currentTarget: { getBoundingClientRect: () => ({ left: 100, width: 20 }) },
  } as unknown as ReactPointerEvent<HTMLElement>, step, 5, total);
}

it('previews one growing note at the original pitch and commits the release position once', () => {
  const commit = vi.fn();
  const draw = useSeqNoteDraw(commit);
  start(draw);
  dispatch('pointermove', { clientX: 205, clientY: 999 });
  expect(hooks.value).toEqual({ absoluteStep: 2, note: 5, duration: 5 });
  expect(commit).not.toHaveBeenCalled();
  dispatch('pointerup', { clientX: 255 });
  expect(commit).toHaveBeenCalledExactlyOnceWith({ absoluteStep: 2, note: 5, duration: 7 });
  expect(hooks.value).toBeNull();
  expect(draw.consumeDrawClick()).toBe(true);
  expect(draw.consumeDrawClick()).toBe(false);
});

it('extends across bars, shrinks when dragging back, and clamps to the timeline', () => {
  const commit = vi.fn();
  const draw = useSeqNoteDraw(commit);
  start(draw, 14);
  dispatch('pointermove', { clientX: 205 });
  expect(hooks.value).toMatchObject({ duration: 5 });
  dispatch('pointermove', { clientX: 9999 });
  expect(hooks.value).toMatchObject({ duration: 18 });
  dispatch('pointermove', { clientX: 130 });
  expect(hooks.value).toMatchObject({ duration: 2 });
  dispatch('pointerup', { clientX: -100 });
  expect(commit).toHaveBeenCalledExactlyOnceWith({ absoluteStep: 14, note: 5, duration: 1 });
});

it.each(['Escape', 'pointercancel', 'blur', 'unmount'])('cancels drawing on %s without changing notes', (reason) => {
  const commit = vi.fn();
  const draw = useSeqNoteDraw(commit);
  start(draw);
  dispatch('pointermove', { clientX: 205 });
  if (reason === 'Escape') dispatch('keydown', { key: 'Escape' });
  else if (reason === 'unmount') hooks.cleanups.splice(0).forEach(cleanup => cleanup());
  else dispatch(reason);
  vi.runAllTimers();
  dispatch('pointerup', { clientX: 205 });
  expect(commit).not.toHaveBeenCalled();
  if (reason !== 'unmount') expect(draw.consumeDrawClick()).toBe(true);
});

it('ignores another pointer and keeps a plain click one step long', () => {
  const commit = vi.fn();
  const draw = useSeqNoteDraw(commit);
  start(draw);
  dispatch('pointermove', { pointerId: 2, clientX: 205 });
  dispatch('pointerup', { pointerId: 2, clientX: 205 });
  expect(commit).not.toHaveBeenCalled();
  dispatch('pointerup');
  expect(commit).toHaveBeenCalledExactlyOnceWith({ absoluteStep: 2, note: 5, duration: 1 });
});
