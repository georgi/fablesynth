import { useEffect, useRef, useState } from 'react';
import { WebAgent, listOpenRouterModels, type AgentHost, type AgentState } from './webAgent';

const emptyState: AgentState = { busy: false, model: 'openrouter/auto', activity: '', turns: [], error: '', canApply: false, proposed: [] };

export function WebAgentPanel({ host, plugin, onClose }: { host: AgentHost; plugin: string; onClose: () => void }) {
  const agent = useRef<WebAgent | null>(null);
  const configuration = useRef('');
  const [apiKey, setApiKey] = useState('');
  const [model, setModel] = useState('openrouter/auto');
  const [models, setModels] = useState<string[]>(['openrouter/auto']);
  const [prompt, setPrompt] = useState('');
  const [state, setState] = useState(emptyState);
  const [modelError, setModelError] = useState('');

  useEffect(() => {
    const controller = new AbortController();
    listOpenRouterModels('', undefined, controller.signal)
      .then(items => setModels(['openrouter/auto', ...items.map(item => item.id).filter(id => id !== 'openrouter/auto')]))
      .catch(error => { if (!controller.signal.aborted) setModelError(error instanceof Error ? error.message : String(error)); });
    return () => { controller.abort(); agent.current?.cancel(); };
  }, []);

  const getAgent = () => {
    const next = `${model}\n${apiKey}`;
    if (!agent.current || configuration.current !== next) {
      agent.current?.cancel();
      configuration.current = next;
      const created = new WebAgent(host, { apiKey, model });
      created.subscribe(setState);
      agent.current = created;
    }
    return agent.current;
  };

  const send = async () => {
    if (!apiKey.trim()) { setState(current => ({ ...current, error: 'Enter an OpenRouter API key for this browser session.' })); return; }
    if (!prompt.trim()) return;
    const text = prompt.trim();
    setPrompt('');
    try { await getAgent().submit(text); }
    catch (error) { setState(current => ({ ...current, error: error instanceof Error ? error.message : String(error) })); }
  };

  const apply = async () => {
    try { await agent.current?.apply(); }
    catch (error) { setState(current => ({ ...current, error: error instanceof Error ? error.message : String(error) })); }
  };

  return (
    <div className="web-agent-backdrop" role="dialog" aria-modal="true" aria-label="Fable agent">
      <section className="web-agent-panel">
        <header className="web-agent-header"><strong>FABLE AGENT</strong><button className="pb-btn" onClick={onClose}>CLOSE</button></header>
        <div className="web-agent-settings">
          <label>MODEL<select value={model} onChange={event => setModel(event.target.value)} disabled={state.busy}>
            {models.map(id => <option key={id} value={id}>{id}</option>)}
          </select></label>
          <label>OPENROUTER API KEY<input value={apiKey} onChange={event => setApiKey(event.target.value)} type="password" autoComplete="off" placeholder="Session only — never stored" disabled={state.busy} /></label>
        </div>
        {modelError && <p className="web-agent-note">Live model list unavailable: {modelError}. You can still use the default model.</p>}
        <div className="web-agent-chat" aria-live="polite">
          {!state.turns.length && <article className="web-agent-bubble assistant"><b>FABLE AGENT</b><p>Ask for a {plugin} parameter change or an output measurement. Changes remain staged until you apply them.</p></article>}
          {state.turns.map((turn, index) => <div key={index} className="web-agent-turn">
            <article className="web-agent-bubble user"><b>YOU</b><p>{turn.prompt}</p></article>
            {turn.activityLog && <details className="web-agent-activity" open={turn.status === 'running'}><summary>TOOL ACTIVITY — complete model-visible log</summary><pre>{turn.activityLog}</pre></details>}
            {turn.assistant && <article className="web-agent-bubble assistant"><b>FABLE AGENT</b><p>{turn.assistant}</p></article>}
            {turn.error && <article className="web-agent-bubble error"><b>REQUEST ERROR</b><p>{turn.error}</p></article>}
            {turn.changes.length > 0 && <article className="web-agent-bubble proposal"><b>PROPOSED CHANGES ({turn.status.toUpperCase()})</b><ul>{turn.changes.map(change => <li key={change.id}>{change.id}: {change.before} → {change.after}</li>)}</ul></article>}
          </div>)}
        </div>
        <textarea className="web-agent-composer" value={prompt} onChange={event => setPrompt(event.target.value)} placeholder="Ask for a sound change or follow-up…" disabled={state.busy} />
        <footer className="web-agent-footer"><span>{state.error || state.activity || 'Ready for a follow-up'}</span><div>
          {state.busy ? <button className="pb-btn" onClick={() => agent.current?.cancel()}>CANCEL</button> : <button className="pb-btn" onClick={send}>SEND</button>}
          <button className="pb-btn" onClick={() => { agent.current?.newConversation(); setState(emptyState); }} disabled={state.busy}>NEW CONVERSATION</button>
          <button className="pb-btn web-agent-apply" onClick={apply} disabled={!state.canApply}>APPLY CHANGES</button>
        </div></footer>
      </section>
    </div>
  );
}
