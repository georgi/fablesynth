import { beforeEach, describe, expect, it } from 'vitest';
import { loadArp, newArp } from '../arp';
import { useBassStore } from './store';
import { useStore } from '../store';

describe('BL-1 arp state', () => {
  beforeEach(() => useBassStore.setState({ hosted: false, arpMode: false, playing: false,
    arp: { ...newArp(), notes: [36, 39, 43], slides: Array(16).fill(false) }, heldSemis: [], arpKeys: [] }));
  it('collects root-relative keys as MIDI notes, latches, replaces and clears them', () => {
    const s = useBassStore.getState(); s.setArpMode(true); s.updateArp({ input: 'keys', latch: true });
    s.noteOn(0, .8); s.noteOn(7, .8); s.noteOff(0); s.noteOff(7);
    expect(useBassStore.getState().arpKeys).toEqual([36, 43]);
    s.noteOn(3, .8); expect(useBassStore.getState().arpKeys).toEqual([39]);
    s.noteOff(3); s.clearArpKeys(); expect(useBassStore.getState().arpKeys).toEqual([]);
  });
  it('retains physically held keys when starting and releases unlatched keys', () => {
    const s = useBassStore.getState(); s.setArpMode(true); s.updateArp({ input: 'keys' });
    s.noteOn(0, .8); s.play(); expect(useBassStore.getState().heldSemis).toEqual([0]);
    s.noteOff(0); expect(useBassStore.getState().arpKeys).toEqual([]);
  });
  it('preserves the written bass sequence and WT-1 state across arp edits and mode changes', () => {
    const s = useBassStore.getState(); const notes = s.patterns.slice(); const wt = useStore.getState();
    s.setArpMode(true); s.updateArp({ notes: [38, 41, 45], octaves: 2 }); s.setArpMode(false);
    expect(useBassStore.getState().patterns).toEqual(notes);
    expect(useStore.getState()).toBe(wt);
  });
  it('uses independent storage and restores slides for bass only', () => {
    const data = new Map<string, string>();
    const old = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
    Object.defineProperty(globalThis, 'localStorage', { configurable: true, value: { getItem: (k: string) => data.get(k), setItem: (k: string, v: string) => data.set(k, v) } });
    try {
      const slides = Array(16).fill(false); slides[3] = true;
      useBassStore.getState().updateArp({ slides });
      expect(data.has('bl1-arp-v1')).toBe(true); expect(data.has('wt1-arp-v1')).toBe(false);
      expect(loadArp('bl1-arp-v1', { ...newArp(), slides: Array(16).fill(false) }).slides?.[3]).toBe(true);
      expect(loadArp().notes).toEqual(newArp().notes);
    } finally {
      if (old) Object.defineProperty(globalThis, 'localStorage', old);
      else Reflect.deleteProperty(globalThis, 'localStorage');
    }
  });
});
