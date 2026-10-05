import { describe, expect, it, vi } from 'vitest';
import { automationFocus, registerAutomationFocus } from './automationFocus';

describe('control automation focus', () => {
  it('routes to the current machine editor and cleans up on unmount', () => {
    const wt = { canShow: () => true, show: vi.fn() };
    const drum = { canShow: () => false, show: vi.fn() };
    const removeWt = registerAutomationFocus('WT1', wt);
    const removeDrum = registerAutomationFocus('DR1', drum);
    automationFocus('WT1')?.show('filter.cutoff');
    expect(wt.show).toHaveBeenCalledWith('filter.cutoff');
    expect(drum.show).not.toHaveBeenCalled();
    expect(automationFocus('DR1')?.canShow('pad1.flt.cut')).toBe(false);
    removeWt();
    removeDrum();
    expect(automationFocus('WT1')).toBeUndefined();
  });

  it('does not unregister a replacement editor during old-editor cleanup', () => {
    const first = { canShow: () => true, show: vi.fn() };
    const second = { canShow: () => true, show: vi.fn() };
    const removeFirst = registerAutomationFocus('BL1', first);
    const removeSecond = registerAutomationFocus('BL1', second);
    removeFirst();
    expect(automationFocus('BL1')).toBe(second);
    removeSecond();
    expect(automationFocus('BL1')).toBeUndefined();
  });
});
