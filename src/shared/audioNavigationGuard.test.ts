import { describe, expect, it } from 'vitest';
import { guardAudioNavigation } from './audioNavigationGuard';

class Context extends EventTarget {
  state: AudioContextState = 'suspended';
  setState(state: AudioContextState) {
    this.state = state;
    this.dispatchEvent(new Event('statechange'));
  }
}

function leaving(target: EventTarget): boolean {
  return !target.dispatchEvent(new Event('beforeunload', { cancelable: true }));
}

describe('audio navigation warning', () => {
  it('warns only while an initialized context is running', () => {
    const target = new EventTarget();
    const ctx = new Context();
    guardAudioNavigation(ctx as unknown as AudioContext, target as Window);
    expect(leaving(target)).toBe(false);
    ctx.setState('running');
    expect(leaving(target)).toBe(true);
    ctx.setState('suspended');
    expect(leaving(target)).toBe(false);
    ctx.setState('running');
    expect(leaving(target)).toBe(true);
    ctx.setState('closed');
    expect(leaving(target)).toBe(false);
  });

  it('keeps protection while another context runs and accepts shared contexts', () => {
    const target = new EventTarget();
    const contexts = [new Context(), new Context()];
    for (const ctx of contexts) {
      ctx.setState('running');
      guardAudioNavigation(ctx as unknown as AudioContext, target as Window);
      guardAudioNavigation(ctx as unknown as AudioContext, target as Window);
    }
    expect(leaving(target)).toBe(true);
    contexts[0].setState('closed');
    expect(leaving(target)).toBe(true);
    contexts[1].setState('closed');
    expect(leaving(target)).toBe(false);
  });
});
