import { afterEach, describe, expect, it, vi } from 'vitest';
import { WebAgent, type AgentHost } from './webAgent';

const host: AgentHost = {
  snapshot: () => ({
    plugin: 'WT-1 test',
    parameters: [{ id: 'master.volume', name: 'OUTPUT', unit: '', min: 0, max: 1, value: 0.75, step: 0 }],
    audio: { available: false },
    meters: { available: false },
  }),
  revision: () => 'stable',
  apply: vi.fn(),
};

function event(delta: Record<string, unknown>) {
  return `data: ${JSON.stringify({ choices: [{ delta }] })}\n\ndata: [DONE]\n\n`;
}

afterEach(() => vi.unstubAllGlobals());

describe('WebAgent', () => {
  it('stages a constrained tool proposal and applies it only after Apply', async () => {
    const fetch = vi.fn()
      .mockResolvedValueOnce(new Response(event({
        tool_calls: [{ index: 0, id: 'tool_1', type: 'function', function: { name: 'execute_js', arguments: '{"code":"host.proposeParameters({\\\"master.volume\\\":0.5}); return {ok:true};"}' } }],
      })))
      .mockResolvedValueOnce(new Response(event({ content: 'I staged the output change for review.' })));
    vi.stubGlobal('fetch', fetch);
    const apply = vi.fn();
    const agent = new WebAgent({ ...host, apply }, { apiKey: 'test', model: 'openrouter/auto' });

    await agent.submit('Lower the output.');
    expect(agent.getState().canApply).toBe(true);
    expect(agent.getState().proposed).toEqual([{ id: 'master.volume', before: 0.75, after: 0.5 }]);
    expect(apply).not.toHaveBeenCalled();

    await agent.apply();
    expect(apply).toHaveBeenCalledOnce();
    expect(agent.getState().turns[0].status).toBe('applied');
  });

  it('reports unsupported browser-tool expressions as a tool result without executing them', async () => {
    const fetch = vi.fn()
      .mockResolvedValueOnce(new Response(event({
        tool_calls: [{ index: 0, id: 'tool_1', type: 'function', function: { name: 'execute_js', arguments: '{"code":"return globalThis;"}' } }],
      })))
      .mockResolvedValueOnce(new Response(event({ content: 'The restricted tool runtime rejected that request.' })));
    vi.stubGlobal('fetch', fetch);
    const agent = new WebAgent(host, { apiKey: 'test', model: 'openrouter/auto' });

    await agent.submit('Inspect the host.');
    expect(agent.getState().turns[0].activityLog).toContain('Unsupported expression: globalThis');
    expect(agent.getState().turns[0].status).toBe('complete');
  });
});
