import { describe, expect, it } from 'vitest';
import { bootWt } from '../engine/workletHarness';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { defaultBassParams } from '../bass/params';
import { generateTables } from '../engine/wavetables';
import { compileClipArp, newClipArp } from './clipArp';
import { bytesToB64, emptyClipBytes } from './protocol';

const tables = generateTables();
function boot(machine: 'BL1' | 'WT1') {
  const h = machine === 'WT1' ? bootWt() : makeBassProcessor();
  if (machine === 'BL1') {
    h.send({ t: 'init', params: defaultBassParams() });
    h.send({ t: 'tables', list: tables.map(t => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })) });
  }
  let frame = 0;
  Object.defineProperty(globalThis, 'currentFrame', { configurable: true, get: () => frame });
  const data = emptyClipBytes(machine, 1);
  const arp = compileClipArp({ name: 'ARP', bars: 1, pattern: bytesToB64(data), arp: { ...newClipArp(machine), enabled: true } })!;
  const proc = h.proc as unknown as { gate: boolean; semiTarget: number; voices: { gate: boolean; note: number }[]; fenvT: number; clip: { arp?: typeof arp } | null };
  const gated = () => machine === 'BL1' ? proc.gate : proc.voices.some(v => v.gate);
  const pitch = () => machine === 'BL1' ? proc.semiTarget + 36 : proc.voices.find(v => v.gate)?.note;
  const events: { frame: number; step: unknown }[] = [];
  const post = h.proc.port.postMessage.bind(h.proc.port);
  h.proc.port.postMessage = (m: unknown) => { const msg = m as { t: string; step: number }; if (msg.t === 'pos') events.push({ frame, step: msg.step }); post(m); };
  const render = (blocks: number) => {
    for (let i = 0; i < blocks; i++) { h.proc.process([], [[new Float32Array(128), new Float32Array(128)]]); frame += 128; }
  };
  h.send({ t: 'host', on: true }); h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 512 });
  return { ...h, data, arp, proc, render, gated, pitch, events };
}
for (const machine of ['WT1', 'BL1'] as const) describe(`${machine} hosted arp`, () => {
  if (machine === 'BL1') it('keeps envelopes across slides and breaks the connection at a rest', () => {
    const h = boot(machine); h.arp.slides![1] = true; h.arp.hits[2] = false;
    h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 512, arp: h.arp });
    h.render(52); expect(h.pitch()).toBe(h.arp.notes[1]); expect(h.proc.fenvT).toBeGreaterThan(6000);
    h.render(47); expect(h.gated()).toBe(false);
    h.render(47); expect(h.gated()).toBe(true); expect(h.proc.fenvT).toBeLessThanOrEqual(256);
  });
  it('launches through the host, plays the clip pool and releases on stop', () => {
    const h = boot(machine); h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 512, arp: h.arp });
    h.render(4); expect(h.gated()).toBe(false);
    h.render(1); expect(h.pitch()).toBe(h.arp.notes[0]);
    h.send({ t: 'clipstop', atFrame: 0 }); h.render(1); expect(h.gated()).toBe(false);
    expect(h.proc.clip).toBeNull();
  });
  it('keeps a queued clip edit away from the live arp and restores sequence mode on launch', () => {
    const h = boot(machine); h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 0, arp: h.arp }); h.render(5);
    const next = { ...h.arp, notes: Array(16).fill(70) };
    h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 12032, arp: next });
    h.send({ t: 'clipupdate', data: h.data, bars: 1, arp: { ...next, notes: Array(16).fill(72) } });
    h.render(10); expect(h.proc.clip?.arp?.notes[0]).toBe(h.arp.notes[0]);
    h.render(80); expect(h.pitch()).toBe(72);
    h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 0 }); h.render(1);
    expect(h.proc.clip?.arp).toBeNull(); expect(h.gated()).toBe(false);
  });
  it('follows host rate and swing with bounded quantum error and no accumulating drift', () => {
    const h = boot(machine);
    h.send({ t: 'tempo', bpm: 127, swing: .2, anchor: 512 });
    h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 512, arp: { ...h.arp, rate: 1/6 } });
    h.render(1900);
    expect(h.events.length).toBeGreaterThan(60);
    const dur = 60 / 127 / 6 * 48000;
    h.events.forEach((e, i) => {
      expect(e.step).toBe(i % 16);
      const expected = 512 + i * dur + (i % 2 ? .2 * .667 * dur : 0);
      expect(Math.abs(e.frame - expected)).toBeLessThan(129);
    });
  });
  it('clears a live pool and disables the arp without touching stored pattern bytes', () => {
    const h = boot(machine); h.send({ t: 'clip', data: h.data, bars: 1, atFrame: 0, arp: h.arp }); h.render(1);
    expect(h.gated()).toBe(true);
    h.send({ t: 'clipupdate', data: h.data, bars: 1, arp: { ...h.arp, notes: Array(16).fill(-1) } });
    expect(h.gated()).toBe(false);
    h.send({ t: 'clipupdate', data: h.data, bars: 1 }); h.render(1);
    expect(h.proc.clip?.arp).toBeNull(); expect(h.gated()).toBe(false);
  });
});
