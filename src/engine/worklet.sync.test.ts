import { describe, expect, it } from 'vitest';
import { FACTORY_PRESETS } from '../presets';
import { bootWt, type WtHarness } from './workletHarness';

// Hosted (SQ-4) synced-LFO contract: the LFO phase derives from the
// conductor's shared anchor, not from the worklet's own render history, so a
// device that joins mid-song ducks on the same downbeats as everyone else.
// PUMP PAD (40) is the probe: its beat-synced saw LFO fully ducks the amp, so
// the position of the RMS minimum exposes the LFO phase.

const BEAT = 24000; // 120 BPM @ 48 kHz
const ANCHOR = 256;
const CMP_BLOCKS = Math.floor(BEAT / 128); // one full duck cycle of windows

function bootPumpPad(): WtHarness {
  const h = bootWt({
    ...FACTORY_PRESETS[40]!.params,
    // Engine-only probe: FX tails carry render history and would blur the
    // phase comparison between the two engines.
    'fx.chorus.on': 0, 'fx.reverb.on': 0,
  });
  h.send({ t: 'host', on: 1 });
  h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: ANCHOR });
  h.send({ t: 'on', n: 60, v: 1 });
  return h;
}

/** Windowed RMS (one value per 128-sample block). */
function profile(h: WtHarness, blocks: number): number[] {
  const { L, R } = h.render(blocks);
  const out: number[] = [];
  for (let b = 0; b < blocks; b++) {
    let s = 0;
    for (let i = b * 128; i < (b + 1) * 128; i++) s += L[i]! * L[i]! + R[i]! * R[i]!;
    out.push(Math.sqrt(s / 256));
  }
  return out;
}

describe('hosted synced LFO', () => {
  it('locks the synced LFO phase to the host anchor, not render history', () => {
    // Engine A plays from the anchor; complete A before booting B — the
    // harness's currentFrame global tracks the most recent harness.
    const a = bootPumpPad();
    a.setFrame(ANCHOR);
    a.render(3000); // 16 beats of history
    const pa = profile(a, CMP_BLOCKS);

    // Engine B joins 8 beats late, mid-beat (5120 = 40 blocks past the beat),
    // and renders up to the same absolute frame.
    const b = bootPumpPad();
    b.setFrame(ANCHOR + 8 * BEAT + 5120);
    b.render(1460); // lands exactly on CMP_START
    const pb = profile(b, CMP_BLOCKS);

    const peak = Math.max(...pa);
    expect(peak).toBeGreaterThan(1e-4);
    // Same absolute frames -> the duck minimum lands in the same window.
    // A free-running LFO puts B's minimum ~40 windows away.
    const argmin = (v: number[]) => v.indexOf(Math.min(...v));
    const da = argmin(pa), db = argmin(pb);
    const diff = Math.abs(da - db);
    expect(Math.min(diff, CMP_BLOCKS - diff)).toBeLessThanOrEqual(2);
  });
});
