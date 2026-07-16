// Onboarding tour: step model invariants, persistence guards (node env has
// no localStorage) and the store's tour state machine.

import { beforeEach, describe, expect, it } from 'vitest';
import { isTourSeen, markTourSeen, nextTourStep, TOUR_STEPS } from './onboarding';
import { resetSeqStore, useSeqStore } from './store';

describe('tour steps', () => {
  it('covers the four core gestures in order', () => {
    expect(TOUR_STEPS.map((s) => s.id)).toEqual(['play', 'scenes', 'devices', 'songs']);
  });

  it('anchors every step to a class selector rendered by the session grid', () => {
    for (const s of TOUR_STEPS) {
      expect(s.target).toMatch(/^\.sq-[a-z-]+$/);
      expect(s.title.length).toBeGreaterThan(0);
      expect(s.body.length).toBeGreaterThan(0);
    }
  });

  it('steps forward, clamps backward and signals the end with null', () => {
    expect(nextTourStep(0, 1)).toBe(1);
    expect(nextTourStep(1, -1)).toBe(0);
    expect(nextTourStep(0, -1)).toBe(0);
    expect(nextTourStep(TOUR_STEPS.length - 1, 1)).toBeNull();
  });
});

describe('tour persistence without localStorage', () => {
  it('treats a node environment as never-seen and swallows writes', () => {
    expect(typeof localStorage).toBe('undefined');
    expect(isTourSeen()).toBe(false);
    expect(() => markTourSeen()).not.toThrow();
  });
});

describe('store tour state', () => {
  beforeEach(() => resetSeqStore());

  it('is hidden initially and opens at step 0 on startTour', () => {
    expect(useSeqStore.getState().tour).toBeNull();
    useSeqStore.getState().startTour();
    expect(useSeqStore.getState().tour).toBe(0);
  });

  it('exits focus mode when the tour starts, so its anchors are on screen', () => {
    useSeqStore.getState().enterFocus(1);
    expect(useSeqStore.getState().focus).not.toBeNull();
    useSeqStore.getState().startTour();
    expect(useSeqStore.getState().focus).toBeNull();
  });

  it('advances through the steps and ends past the last one', () => {
    const st = useSeqStore.getState();
    st.startTour();
    for (let i = 1; i < TOUR_STEPS.length; i++) {
      useSeqStore.getState().advanceTour(1);
      expect(useSeqStore.getState().tour).toBe(i);
    }
    useSeqStore.getState().advanceTour(1);
    expect(useSeqStore.getState().tour).toBeNull();
  });

  it('clamps BACK at the first step and ignores advance when closed', () => {
    useSeqStore.getState().startTour();
    useSeqStore.getState().advanceTour(-1);
    expect(useSeqStore.getState().tour).toBe(0);
    useSeqStore.getState().endTour();
    useSeqStore.getState().advanceTour(1);
    expect(useSeqStore.getState().tour).toBeNull();
  });
});
