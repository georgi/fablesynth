# SQ-4 Musical Preset Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lead and pad never share a note in any factory preset; every song gets unique, style-fit procedural drums; web and native generate byte-identical presets.

**Architecture:** The web generator (`src/seq/sessionPresets.ts`) is the source of truth. It gains a register split (pad close-voiced +0…+11, lead +12…+23) and a procedural drum generator (family archetype × variation/energy mutation × scene density mask). The whole generator is then ported note-for-note to `juce/source/seq/dsp/SeqFactory.cpp`, replacing its stale random clip-pick. Parity is proven by a JSON fixture dumped from the web and compared byte-for-byte in the native host test.

**Tech Stack:** TypeScript + vitest (web), C++17 + hand-rolled CHECK harness (native), CMake/Unix Makefiles build.

**Spec:** `docs/superpowers/specs/2026-07-16-sq4-musical-preset-generator-design.md`

## Global Constraints

- Semitones are relative to seq root C3 = 0. Packed WT-1/BL-1 note: byte0 = `1 | accent<<1 | duration<<2`, byte1 = pitch class 0..11 (bit7 = BL-1 slide), byte2 = octave+1 (0..2 ⇒ −1..+1).
- DR-1 clip: one byte per pad-step, `0` off / `1` hit / `2` accent, index `(bar*16 + pad)*16 + step`. Pad map: 0 KICK · 2 SNARE · 3 CLAP · 4 RIM · 5 CH HAT · 6 OH HAT · 8/9/10 TOMS · 12/13 PERC.
- Preset index 0 (NEON TALE) stays the hand-authored `factorySession()` on both platforms; only its FOG pad clips change (−1 octave).
- The C++ in `SeqFactory.cpp` must be a note-for-note transcription of the TS — same tables, same iteration order, same names (camelCase preserved).
- No RNG anywhere: everything is a pure function of `(family, variationIndex, energy, scene)`.
- No new dependencies. Web tests: `npx vitest run src/seq/sessionPresets.test.ts` etc. Native build: `cmake --build juce/build --target sq4_engine_test sq4_host_test --parallel` (Unix Makefiles — never pass `-G Ninja`).
- Commit after each task; conventional commit messages, `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer.

---

### Task 1: NEON TALE — drop FOG_STABS and FOG_SWELL one octave (web)

The hand-authored song's only lead/pad overlaps are DROP A/B (`GLASS_HOOK`/`II` vs `FOG_STABS`) and BREAK (`GLASS_SOLO` vs `FOG_SWELL`). Dropping the two FOG pads −12 clears the shared band. The `AIR_*` pads never share a scene with a lead — leave them.

**Files:**
- Modify: `src/seq/factory.ts:162-176` (FOG_STABS, FOG_SWELL)
- Create: `src/seq/factory.test.ts`

**Interfaces:**
- Consumes: `factorySession()` from `src/seq/factory.ts`, `b64ToBytes`, `wtNoteIdx` from `src/seq/protocol.ts`.
- Produces: no API change — clip byte content only. Task 4 re-dumps `web-session.json` from this; Task 5 mirrors the same octave change in C++.

- [ ] **Step 1: Write the failing test**

Create `src/seq/factory.test.ts`:

```ts
import { describe, expect, it } from 'vitest';
import { factorySession } from './factory';
import { b64ToBytes, WT_POLY_LANES, NOTE_STRIDE } from './protocol';

/** step → set of sounding pitches (o*12+n), honouring durations, for one WT-1 clip. */
function activePitches(pattern: string, bars: number): Map<number, Set<number>> {
  const bytes = b64ToBytes(pattern);
  const active = new Map<number, Set<number>>();
  for (let step = 0; step < bars * 16; step++) {
    for (let lane = 0; lane < WT_POLY_LANES; lane++) {
      const offset = (step * WT_POLY_LANES + lane) * NOTE_STRIDE;
      if (!(bytes[offset]! & 1)) continue;
      const duration = bytes[offset]! >> 2;
      const pitch = (bytes[offset + 2]! - 1) * 12 + (bytes[offset + 1]! & 0x7f);
      for (let t = step; t < Math.min(bars * 16, step + duration); t++) {
        if (!active.has(t)) active.set(t, new Set());
        active.get(t)!.add(pitch);
      }
    }
  }
  return active;
}

describe('NEON TALE factory session registers', () => {
  it('never sounds the same pitch on lead and pad at the same step', () => {
    const session = factorySession();
    for (const scene of session.scenes) {
      const lead = scene.clips[2];
      const pad = scene.clips[3];
      if (!lead || !pad) continue;
      const leadActive = activePitches(lead.pattern, lead.bars);
      const padActive = activePitches(pad.pattern, pad.bars);
      // Compare over the least common loop length of the two clips.
      const span = (lead.bars * pad.bars * 16) / (lead.bars === pad.bars ? lead.bars : 1);
      for (let t = 0; t < span; t++) {
        const l = leadActive.get(t % (lead.bars * 16)) ?? new Set();
        const p = padActive.get(t % (pad.bars * 16)) ?? new Set();
        for (const pitch of l) {
          expect(p.has(pitch), `${scene.name} step ${t} pitch ${pitch}`).toBe(false);
        }
      }
    }
  });

  it('keeps FOG pads at or below the root octave, under every lead', () => {
    const session = factorySession();
    // DROP A / DROP B / BREAK: the scenes where a lead and pad both play.
    for (const sceneIndex of [2, 3, 4]) {
      const pad = session.scenes[sceneIndex]!.clips[3]!;
      const bytes = b64ToBytes(pad.pattern);
      for (let i = 0; i < bytes.length; i += NOTE_STRIDE) {
        if (!(bytes[i]! & 1)) continue;
        const pitch = (bytes[i + 2]! - 1) * 12 + (bytes[i + 1]! & 0x7f);
        expect(pitch, `${pad.name} note`).toBeLessThanOrEqual(0);
      }
    }
  });
});
```

Note: `WT_POLY_LANES` and `NOTE_STRIDE` are exported from `src/seq/protocol.ts` (values 8 and 3). If `NOTE_STRIDE` is not exported, use the literal 3 and `8` in the test rather than adding exports.

- [ ] **Step 2: Run the test to verify it fails**

Run: `npx vitest run src/seq/factory.test.ts`
Expected: FAIL — `keeps FOG pads at or below the root octave` fails (FOG_STABS reaches +12); the collision test fails on DROP B step 58 pitch 10 and BREAK step 8 pitch 7.

- [ ] **Step 3: Close-voice the two FOG clips into the low octave**

A plain −12 transpose is impossible: the packed octave byte floors at −1 (pitch −12), and the B♭ chord's root would land at −14. Instead, close-voice every chord tone at `pitch class − 12` — all notes land in −12…−1 (musically: inversions in the octave below the root), strictly under every NEON TALE lead (GLASS_SOLO's floor is 0).

In `src/seq/factory.ts`, change `FOG_STABS`: all roots move to `o: -1`, and the voicing lanes drop their octave bumps (each lane is just a pitch class in the low octave):

```ts
const FOG_STABS = wtClip('FOG STABS', 4, [
  // Four roots establish a C–Bb–G–F progression for the chord patch,
  // close-voiced in the low octave (−12..−1) so the stabs never reach the lead.
  { s: 0, n: 0, o: -1 }, { s: 2, n: 0, o: -1 }, { s: 7, n: 3, o: -1 }, { s: 10, n: 5, o: -1, a: true }, { s: 12, n: 5, o: -1 },
  { s: 16, n: 10, o: -1 }, { s: 18, n: 10, o: -1 }, { s: 23, n: 0, o: -1 }, { s: 26, n: 2, o: -1 }, { s: 28, n: 3, o: -1 },
  { s: 32, n: 7, o: -1 }, { s: 34, n: 7, o: -1 }, { s: 39, n: 10, o: -1 }, { s: 42, n: 2, o: -1, a: true }, { s: 44, n: 2, o: -1 },
  { s: 48, n: 5, o: -1 }, { s: 50, n: 5, o: -1 }, { s: 55, n: 0, o: -1 }, { s: 58, n: 3, o: -1 }, { s: 60, n: 0, o: -1 },
].flatMap((step) => [step, { ...step, lane: 1, n: (step.n + 3) % 12 }, { ...step, lane: 2, n: (step.n + 7) % 12 }]));
```

(The spread keeps `o: -1` on every lane; the old `step.n >= 9/5` octave bumps are removed, so the whole clip spans −12…−1.)

And `FOG_SWELL` — add a low-voiced sibling of `chordHeld` right below it and use it (do NOT touch `chordHeld` itself; the `AIR_*` clips keep their open voicings):

```ts
/** A held chord close-voiced in the octave below the root: each tone at its pitch class − 12. */
function chordHeldLow(s0: number, span: number, root: number, minor = true): NoteStep[] {
  const intervals = [0, minor ? 3 : 4, 7];
  return intervals.flatMap((interval, lane) => held(s0, span, ((root + interval) % 12 + 12) % 12, -1, lane));
}

const FOG_SWELL = wtClip('FOG SWELL', 8, [
  ...chordHeldLow(0, 32, 0),
  ...chordHeldLow(32, 32, 10, false),
  ...chordHeldLow(64, 32, 5),
  ...chordHeldLow(96, 32, 3),
]);
```

(B♭ major becomes D−F−B♭ at −10/−7/−2 — a first inversion, all within −12…−1.)

- [ ] **Step 4: Run the tests to verify they pass**

Run: `npx vitest run src/seq/factory.test.ts`
Expected: PASS (both tests).

- [ ] **Step 5: Run the full web suite to catch regressions**

Run: `npx vitest run && npx tsc --noEmit`
Expected: all green. `sessionPresets.test.ts` still passes because preset 0's tests don't inspect FOG registers.

- [ ] **Step 6: Commit**

```bash
git add src/seq/factory.ts src/seq/factory.test.ts
git commit -m "fix(seq): drop NEON TALE fog pads below the lead register"
```

---

### Task 2: Register split in the web generator

Pad close-voiced in +0…+11; lead strictly +12…+23. `voicedLeadPitch` is deleted — the band is one octave, so no octave search remains.

**Files:**
- Modify: `src/seq/sessionPresets.ts:100-107` (padProgression), `src/seq/sessionPresets.ts:141-167` (voicedLeadPitch, leadProgression)
- Modify: `src/seq/sessionPresets.test.ts` (add register-invariant test)

**Interfaces:**
- Consumes: `putNote(bytes, offset, absolute, duration, accent)` and `wtNoteIdx(bar, step, lane)` — both already in `sessionPresets.ts` / `protocol.ts`.
- Produces: `padProgression(harmony, variation): ClipDoc` with all note pitches in 0..11; `leadProgression(harmony, spec): ClipDoc` with all note pitches in 12..23. Task 3's tests and Task 5's port rely on exactly these bands.

- [ ] **Step 1: Write the failing test**

Append to `src/seq/sessionPresets.test.ts` (inside the existing `describe`):

```ts
it('voices every pad strictly below every lead note', () => {
  for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
    for (const scene of preset.session.scenes) {
      const lead = scene.clips[2];
      const pad = scene.clips[3];
      if (!lead || !pad) continue;
      const pitches = (pattern: string) => {
        const bytes = b64ToBytes(pattern);
        const out: number[] = [];
        for (let i = 0; i < bytes.length; i += 3) {
          if (bytes[i]! & 1) out.push((bytes[i + 2]! - 1) * 12 + (bytes[i + 1]! & 0x7f));
        }
        return out;
      };
      const leadPitches = pitches(lead.pattern);
      const padPitches = pitches(pad.pattern);
      expect(Math.min(...leadPitches), `${preset.name} ${scene.name} lead floor`).toBeGreaterThanOrEqual(12);
      expect(Math.max(...leadPitches), `${preset.name} ${scene.name} lead ceiling`).toBeLessThanOrEqual(23);
      expect(Math.max(...padPitches), `${preset.name} ${scene.name} pad ceiling`).toBeLessThanOrEqual(11);
      expect(Math.min(...padPitches), `${preset.name} ${scene.name} pad floor`).toBeGreaterThanOrEqual(0);
    }
  }
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `npx vitest run src/seq/sessionPresets.test.ts`
Expected: the new test FAILS (current pads reach +18, current leads dip to −12); all pre-existing tests PASS.

- [ ] **Step 3: Implement the split**

Replace `padProgression` in `src/seq/sessionPresets.ts`:

```ts
function padProgression(harmony: Harmony, variation: string): ClipDoc {
  const bytes = emptyClipBytes('WT1', 4);
  harmony.roots.forEach((root, bar) => {
    // Close-voice the triad as pitch classes inside the root octave (+0..+11):
    // the pad bed stays strictly below the +12..+23 lead band for every root.
    const chord = [root, root + (harmony.minor[bar] ? 3 : 4), root + 7];
    chord.forEach((note, lane) => putNote(bytes, wtNoteIdx(bar, 0, lane), ((note % 12) + 12) % 12, 16));
  });
  return { name: `${variation} CHORDS · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}
```

Delete `voicedLeadPitch` entirely (lines 141-149) and replace `leadProgression`'s inner loop:

```ts
function leadProgression(harmony: Harmony, spec: Spec): ClipDoc {
  const bytes = emptyClipBytes('WT1', 4);
  const tonic = harmony.roots[0]!;
  const line = LEAD_LINES[spec.variationIndex]!;
  const rhythm = LEAD_RHYTHMS[spec.family] ?? LEAD_RHYTHMS.NEON!;
  line.forEach((barNotes, bar) => {
    barNotes.forEach((relativePitch, event) => {
      // Voice the melody strictly one octave above the pad bed (+12..+23):
      // the authored lines carry the contour, so no octave search is needed.
      const pitchClass = (tonic + relativePitch) % 12;
      const [step, duration] = rhythm[event]!;
      putNote(bytes, wtNoteIdx(bar, step, 0), pitchClass + 12, duration, event === 0);
    });
  });
  return { name: `${spec.variation} MELODY · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}
```

- [ ] **Step 4: Run the web suite**

Run: `npx vitest run && npx tsc --noEmit`
Expected: all PASS. The existing `composes six-note-per-bar lead phrases` test compares pitch classes only (`& 0x7f` on the note byte), which the split preserves.

- [ ] **Step 5: Commit**

```bash
git add src/seq/sessionPresets.ts src/seq/sessionPresets.test.ts
git commit -m "feat(seq): split generated pad and lead into disjoint octave bands"
```

---

### Task 3: Procedural per-song drum generator (web)

Replace `drumClip` (shared library clip pick) with `drumProgression` — family archetype × deterministic per-song mutation × per-scene density mask. No scene references a library clip afterwards; `dr1-distant-ticks` disappears from the presets.

**Files:**
- Modify: `src/seq/sessionPresets.ts:169-190` (delete `drumClip` + the `awaitlessFactoryClips` block), `src/seq/sessionPresets.ts:206-215` (buildSession scene wiring)
- Modify: `src/seq/sessionPresets.test.ts`

**Interfaces:**
- Consumes: `dr1Idx(bar, pad, step)` from `src/seq/protocol.ts` (add to the existing import list), `Spec` (`family`, `variationIndex`, `energy`, `variation`).
- Produces: `drumProgression(spec: Spec, scene: number): ClipDoc` — 4-bar DR-1 clip, bytes `0|1|2`. Task 5 transcribes the tables and function verbatim.

- [ ] **Step 1: Write the failing tests**

Append to `src/seq/sessionPresets.test.ts`:

```ts
it('generates a unique drum pattern for every song', () => {
  // DROP A (scene 2) carries the full groove: all 24 songs must differ.
  const drops = FACTORY_SESSION_PRESETS.map((preset) => preset.session.scenes[2]!.clips[0]!.pattern);
  expect(new Set(drops).size).toBe(FACTORY_SESSION_PRESETS.length);
});

it('varies the drums across scenes within each song', () => {
  for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
    const [intro, build, dropA, dropB, brk, outro] = preset.session.scenes.map((scene) => scene.clips[0]);
    expect(brk, `${preset.name} break stays drumless`).toBeNull();
    const patterns = [intro, build, dropA, dropB, outro].map((clip) => clip!.pattern);
    expect(new Set(patterns).size, `${preset.name} scene drums`).toBe(5);
  }
});

it('ends busy scenes with a bar-4 fill', () => {
  for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
    for (const sceneIndex of [1, 2, 3]) {
      const clip = preset.session.scenes[sceneIndex]!.clips[0]!;
      const bytes = b64ToBytes(clip.pattern);
      const bar = (n: number) => bytes.slice(n * 256, (n + 1) * 256).join(',');
      expect(bar(3), `${preset.name} scene ${sceneIndex}`).not.toBe(bar(0));
    }
  }
});

it('never reuses a library clip in a generated preset', () => {
  const library = new Set(FACTORY_CLIP_LIBRARY.map((clip) => clip.pattern));
  for (const preset of FACTORY_SESSION_PRESETS.slice(1)) {
    for (const scene of preset.session.scenes) {
      const drums = scene.clips[0];
      if (drums) expect(library.has(drums.pattern), `${preset.name} ${scene.name}`).toBe(false);
    }
  }
});
```

Add to the test file's imports: `import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';`

- [ ] **Step 2: Run the tests to verify they fail**

Run: `npx vitest run src/seq/sessionPresets.test.ts`
Expected: all four new tests FAIL (shared library clips today).

- [ ] **Step 3: Implement the drum generator**

In `src/seq/sessionPresets.ts`, delete `drumClip` and the `awaitlessFactoryClips` block (including the `FACTORY_CLIP_LIBRARY` / `b64ToBytes` imports at lines 188-190 if now unused), add `dr1Idx` to the protocol import, and insert:

```ts
// ---------- procedural drums ----------
// Every song gets its own kit patterns: a per-family groove archetype,
// mutated deterministically by variation and energy, then masked per scene
// role. No preset references a shared library clip.

const DRUM = { KICK: 0, SNARE: 2, CLAP: 3, RIM: 4, CH: 5, OH: 6, TOM_LO: 8, TOM_MID: 9, TOM_HI: 10, PERC_A: 12, PERC_B: 13 } as const;

interface DrumVoice { pad: number; steps: number[]; accents: number[] }
interface DrumArchetype { kick: DrumVoice; back: DrumVoice; hat: DrumVoice; open: DrumVoice; perc: DrumVoice[] }

const DRUM_ARCHETYPES: Record<string, DrumArchetype> = {
  NEON: { // driving synthwave: four-on-floor, clap backbeat, offbeat hats
    kick: { pad: DRUM.KICK, steps: [0, 4, 8, 12], accents: [0] },
    back: { pad: DRUM.CLAP, steps: [4, 12], accents: [] },
    hat: { pad: DRUM.CH, steps: [2, 6, 10, 14], accents: [2, 10] },
    open: { pad: DRUM.OH, steps: [2, 10], accents: [] },
    perc: [{ pad: DRUM.PERC_A, steps: [15], accents: [] }],
  },
  ACID: { // warehouse: relentless floor + ghost kick, rolling 16th hats
    kick: { pad: DRUM.KICK, steps: [0, 4, 8, 12, 14], accents: [0] },
    back: { pad: DRUM.SNARE, steps: [4, 12], accents: [4, 12] },
    hat: { pad: DRUM.CH, steps: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15], accents: [2, 6, 10, 14] },
    open: { pad: DRUM.OH, steps: [7], accents: [] },
    perc: [],
  },
  AMBIENT: { // deep: minimal kick, soft rim, airy hats, lots of space
    kick: { pad: DRUM.KICK, steps: [0, 8], accents: [] },
    back: { pad: DRUM.RIM, steps: [8], accents: [] },
    hat: { pad: DRUM.CH, steps: [4, 12], accents: [] },
    open: { pad: DRUM.OH, steps: [], accents: [] },
    perc: [{ pad: DRUM.PERC_A, steps: [2], accents: [] }],
  },
  HOUSE: { // club: swung floor (global swing 0.12), open-hat offbeat "tss"
    kick: { pad: DRUM.KICK, steps: [0, 4, 8, 12], accents: [] },
    back: { pad: DRUM.CLAP, steps: [4, 12], accents: [] },
    hat: { pad: DRUM.CH, steps: [2, 6, 10, 14], accents: [] },
    open: { pad: DRUM.OH, steps: [2, 6, 10, 14], accents: [6, 14] },
    perc: [],
  },
  'LO-FI': { // dusty boom-bap: broken kick, laid-back snare, swung hats
    kick: { pad: DRUM.KICK, steps: [0, 7, 10], accents: [0] },
    back: { pad: DRUM.SNARE, steps: [4, 12], accents: [] },
    hat: { pad: DRUM.CH, steps: [0, 3, 6, 8, 11, 14], accents: [] },
    open: { pad: DRUM.OH, steps: [14], accents: [] },
    perc: [{ pad: DRUM.PERC_B, steps: [6], accents: [] }],
  },
  CINEMATIC: { // epic half-time: sparse kick, big snare on 3, tom colour
    kick: { pad: DRUM.KICK, steps: [0, 10], accents: [0] },
    back: { pad: DRUM.SNARE, steps: [8], accents: [8] },
    hat: { pad: DRUM.CH, steps: [], accents: [] },
    open: { pad: DRUM.OH, steps: [], accents: [] },
    perc: [{ pad: DRUM.TOM_LO, steps: [13], accents: [] }],
  },
};

// Family-flavoured bar-4 fills: [pad, step, accent].
const DRUM_FILLS: Record<string, Array<[number, number, boolean]>> = {
  NEON: [[DRUM.TOM_HI, 10, false], [DRUM.TOM_MID, 12, false], [DRUM.TOM_LO, 14, true]],
  ACID: [[DRUM.SNARE, 13, false], [DRUM.SNARE, 14, false], [DRUM.SNARE, 15, true]],
  AMBIENT: [[DRUM.OH, 12, false], [DRUM.PERC_A, 14, false]],
  HOUSE: [[DRUM.CLAP, 13, false], [DRUM.CLAP, 15, true]],
  'LO-FI': [[DRUM.PERC_B, 13, false], [DRUM.PERC_B, 15, false]],
  CINEMATIC: [[DRUM.TOM_HI, 8, false], [DRUM.TOM_MID, 10, false], [DRUM.TOM_LO, 12, true], [DRUM.TOM_LO, 14, true]],
};

// One ghost hit per variation (index 0 adds none): [pad, step].
const DRUM_GHOSTS: Array<[number, number] | null> = [null, [DRUM.KICK, 14], [DRUM.SNARE, 11], [DRUM.PERC_B, 3]];

function drumProgression(spec: Spec, scene: number): ClipDoc {
  const family = DRUM_ARCHETYPES[spec.family] ?? DRUM_ARCHETYPES.NEON!;
  const bytes = new Uint8Array(4 * 256);
  const hit = (bar: number, pad: number, step: number, accent = false) => { bytes[dr1Idx(bar, pad, step)] = accent ? 2 : 1; };
  const intro = scene === 0, build = scene === 1, outro = scene === 5;

  // Energy scales hat density; variation (plus one extra for DROP B) rotates
  // which hat steps carry the accents, so sibling songs land differently.
  const hatSteps = spec.energy >= 4 ? Array.from({ length: 16 }, (_, s) => s)
    : spec.energy <= 2 ? family.hat.steps.filter((_, i) => i % 2 === 0)
      : family.hat.steps;
  const shift = spec.variationIndex + (scene === 3 ? 1 : 0);
  const hatAccents = family.hat.accents.map((_, k) => (hatSteps.length ? hatSteps[(k * 2 + shift) % hatSteps.length]! : -1));
  const ghost = DRUM_GHOSTS[spec.variationIndex] ?? null;

  for (let bar = 0; bar < 4; bar++) {
    const lastBar = bar === 3;
    const kickSteps = intro || outro ? family.kick.steps.filter((step) => step % 8 === 0) : family.kick.steps;
    for (const step of kickSteps) hit(bar, family.kick.pad, step, family.kick.accents.includes(step));
    if (!intro && !outro) {
      for (const step of family.back.steps) hit(bar, family.back.pad, step, family.back.accents.includes(step));
      if (!build && ghost) hit(bar, ghost[0], ghost[1]);
    }
    if (!outro) {
      // Intros thin the hats and stagger the picks by variation, so each
      // song opens with its own sparse figure instead of a shared "ticks" loop.
      const steps = intro ? family.hat.steps.filter((_, i) => (i + spec.variationIndex) % 2 === 0) : hatSteps;
      for (const step of steps) hit(bar, family.hat.pad, step, !intro && hatAccents.includes(step));
      if (!intro) for (const step of family.open.steps) hit(bar, family.open.pad, step, family.open.accents.includes(step));
    }
    if (!intro) for (const voice of family.perc) for (const step of voice.steps) hit(bar, voice.pad, step, voice.accents.includes(step));
    if ((intro || outro) && lastBar && ghost && spec.variationIndex >= 2) hit(bar, ghost[0], ghost[1]);
    if (build && lastBar) for (const step of [12, 13, 14, 15]) hit(bar, DRUM.PERC_A, step, step === 15);
    if (!intro && !build && !outro && (lastBar || (scene === 3 && bar === 1))) {
      for (const [pad, step, accent] of DRUM_FILLS[spec.family] ?? []) hit(bar, pad, step, accent);
    }
  }
  const role = intro ? 'INTRO' : build ? 'BUILD' : outro ? 'TAIL' : scene === 3 ? 'DRIVE II' : 'DRIVE';
  return { name: `${spec.variation} ${role} · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}
```

In `buildSession`, replace `const drums = drumClip(spec, s);` with `const drums = drumProgression(spec, s);` (the scene wiring itself is unchanged — BREAK already gets `null`).

- [ ] **Step 4: Run the web suite**

Run: `npx vitest run && npx tsc --noEmit`
Expected: all PASS. If the DROP-A uniqueness test fails, print the colliding pair and adjust `DRUM_GHOSTS`/rotation — but the archetype/mutation matrix as specified yields distinct patterns for all 24 (6 archetypes × per-variation ghosts+rotation × per-energy hat density).

- [ ] **Step 5: Commit**

```bash
git add src/seq/sessionPresets.ts src/seq/sessionPresets.test.ts
git commit -m "feat(seq): generate unique per-song drum patterns per scene"
```

---

### Task 4: Parity fixtures — dump the web output for the native tests

**Files:**
- Create: `scripts/dump-session-presets.ts`
- Modify: `juce/test/fixtures/web-session.json` (regenerate — Task 1 changed FOG clips)
- Create: `juce/test/fixtures/web-session-presets.json`

**Interfaces:**
- Consumes: `FACTORY_SESSION_PRESETS` from `src/seq/sessionPresets.ts`.
- Produces: `web-session-presets.json` — a JSON array of 24 `{ name, family, variation, energy, session }` objects, where `session` is the exact `SessionDoc` shape (`pattern` fields are base64). Task 5's host test reads it.

- [ ] **Step 1: Write the dump script**

Create `scripts/dump-session-presets.ts`:

```ts
// Dumps all 24 web factory session presets as JSON — the JUCE side's
// cross-platform fixture (juce/test/fixtures/web-session-presets.json), so
// sq4_host_test can prove the two generators agree byte-for-byte. Regenerate:
//   npx tsx scripts/dump-session-presets.ts > juce/test/fixtures/web-session-presets.json
import { FACTORY_SESSION_PRESETS } from '../src/seq/sessionPresets';

console.log(JSON.stringify(FACTORY_SESSION_PRESETS.map(({ name, family, variation, energy, session }) => ({ name, family, variation, energy, session }))));
```

- [ ] **Step 2: Regenerate both fixtures**

```bash
npx tsx scripts/dump-session.ts > juce/test/fixtures/web-session.json
npx tsx scripts/dump-session-presets.ts > juce/test/fixtures/web-session-presets.json
```

Verify: `python3 -c "import json; d=json.load(open('juce/test/fixtures/web-session-presets.json')); print(len(d), d[0]['name'], d[1]['name'])"` → `24 NEON TALE NEON CHASE`.

- [ ] **Step 3: Commit**

```bash
git add scripts/dump-session-presets.ts juce/test/fixtures/web-session.json juce/test/fixtures/web-session-presets.json
git commit -m "test(seq): dump web preset fixture for native parity"
```

---

### Task 5: Port the generator to native (SeqFactory.cpp)

Replace the stale random clip-pick in `factorySessionLibrary()` with a note-for-note transcription of the web generator. Also mirror Task 1's FOG octave drop, and align NEON CHASE's lead program with the web (native ships `{13, 2, 4, 11}`, web ships `{13, 2, 14, 11}` — web is the source of truth).

**Files:**
- Modify: `juce/source/seq/dsp/SeqFactory.cpp` (fogStabs, fogSwell, chordHeld, factorySessionLibrary + new generator functions)
- Modify: `juce/test/sq4_host_test.cpp:390` (the `neonChase` expected programs array `{ 13, 2, 4, 11 }` → `{ 13, 2, 14, 11 }`)

**Interfaces:**
- Consumes: `sqEmptyClip`, `sqDr1Idx`, `sqNoteIdx`, `sqWtNoteIdx` from `SeqProtocol.h`; `SessionPreset`, `SessionData`, `ClipData` from `SeqFactory.h`/`SeqModel.h`; the existing `factorySession()` and `calibratedGain` logic.
- Produces: `factorySessionLibrary()` whose 24 presets match `web-session-presets.json` byte-for-byte: same names/family/variation/energy/programs/gains/bpm/swing, and per scene×track identical clip bytes, names, and bars.

- [ ] **Step 1: Mirror the FOG close-voicing**

In `SeqFactory.cpp`, `fogStabs()` roots: change every root to octave −1 (positional field 3 is `o`), and strip the octave bumps out of `fogVoicing` to match Task 1's TS:

```cpp
// FOG STABS voicing (web factory.ts FOG_STABS flatMap): each root expands to a
// close-voiced 3-note chord across lanes 0..2 — root, +3, +7 as pitch classes
// in the root's octave, so the stabs stay in the −12..−1 band under the lead.
std::vector<NoteStep> fogVoicing(std::vector<NoteStep> roots) {
    std::vector<NoteStep> out;
    for (auto step : roots) {
        out.push_back(step);                                  // lane 0 (root)
        NoteStep v1 = step; v1.lane = 1; v1.n = (step.n + 3) % 12;
        out.push_back(v1);
        NoteStep v2 = step; v2.lane = 2; v2.n = (step.n + 7) % 12;
        out.push_back(v2);
    }
    return out;
}

ClipData fogStabs() {
    static const std::vector<NoteStep> roots = {
        { 0, 0, -1 }, { 2, 0, -1 }, { 7, 3, -1 }, { 10, 5, -1, true }, { 12, 5, -1 },
        { 16, 10, -1 }, { 18, 10, -1 }, { 23, 0, -1 }, { 26, 2, -1 }, { 28, 3, -1 },
        { 32, 7, -1 }, { 34, 7, -1 }, { 39, 10, -1 }, { 42, 2, -1, true }, { 44, 2, -1 },
        { 48, 5, -1 }, { 50, 5, -1 }, { 55, 0, -1 }, { 58, 3, -1 }, { 60, 0, -1 },
    };
    return wtClip("FOG STABS", 4, fogVoicing(roots));
}
```

`fogSwell()` — add `chordHeldLow` next to `chordHeld` (leave `chordHeld` untouched; the `AIR_*` clips keep their open voicings):

```cpp
// A held chord close-voiced in the octave below the root (web chordHeldLow):
// each tone at its pitch class − 12.
std::vector<NoteStep> chordHeldLow(int s0, int span, int root, bool minor = true) {
    std::vector<NoteStep> out;
    const int intervals[3] { 0, minor ? 3 : 4, 7 };
    for (int lane = 0; lane < 3; ++lane)
        append(out, held(s0, span, ((root + intervals[lane]) % 12 + 12) % 12, -1, lane));
    return out;
}

ClipData fogSwell() {
    std::vector<NoteStep> steps;
    append(steps, chordHeldLow(0, 32, 0));
    append(steps, chordHeldLow(32, 32, 10, false));
    append(steps, chordHeldLow(64, 32, 5));
    append(steps, chordHeldLow(96, 32, 3));
    return wtClip("FOG SWELL", 8, steps);
}
```

- [ ] **Step 2: Transcribe the generator**

In the anonymous namespace of `SeqFactory.cpp`, add (a straight transcription of `sessionPresets.ts` — keep the TS names):

```cpp
// ---------- procedural session generator (port of src/seq/sessionPresets.ts) ----------

struct Harmony { std::array<int, 4> roots; std::array<bool, 4> minor; };

struct PresetSpec {
    const char* name; const char* family; const char* variation;
    int energy; std::vector<std::string> tags;
    std::array<int, 4> programs; int variationIndex;
};

Harmony harmonyFor(const PresetSpec& spec) {
    const auto tonic = [&]() -> int {
        const std::string f = spec.family;
        if (f == "NEON") return 0;
        if (f == "ACID") return 2;
        if (f == "AMBIENT") return 9;
        if (f == "HOUSE") return 5;
        if (f == "LO-FI") return 7;
        return 4; // CINEMATIC
    }();
    static const Harmony plans[4] = {
        { { 0, 8, 3, 10 }, { true, false, false, false } }, // i–VI–III–VII
        { { 0, 5, 8, 7 },  { true, true, false, false } },  // i–iv–VI–V
        { { 0, 3, 10, 5 }, { true, false, false, true } },  // i–III–VII–iv
        { { 0, 7, 5, 10 }, { true, false, true, false } },  // i–V–iv–VII
    };
    Harmony h = plans[spec.variationIndex];
    for (auto& root : h.roots) root = (root + tonic) % 12;
    return h;
}

void putNote(std::vector<uint8_t>& bytes, int offset, int absolute, int duration = 1, bool accent = false) {
    const int pitchClass = ((absolute % 12) + 12) % 12;
    bytes[(size_t)offset] = (uint8_t)(1 | (accent ? 2 : 0) | (std::min(63, std::max(1, duration)) << 2));
    bytes[(size_t)offset + 1] = (uint8_t)pitchClass;
    bytes[(size_t)offset + 2] = (uint8_t)std::max(0, std::min(2, (absolute - pitchClass) / 12 + 1));
}

ClipData bassProgression(const Harmony& harmony, const std::string& variation) {
    ClipData clip { variation + " ROOTS · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
    for (int bar = 0; bar < 4; ++bar) {
        // One low root, then one fifth: deliberate space for the drums and pad.
        putNote(clip.bytes, sqNoteIdx(bar, 0), harmony.roots[(size_t)bar] - 12, 8, true);
        putNote(clip.bytes, sqNoteIdx(bar, 8), harmony.roots[(size_t)bar] - 5, 8);
    }
    return clip;
}

ClipData padProgression(const Harmony& harmony, const std::string& variation) {
    ClipData clip { variation + " CHORDS · 4 BAR", 4, sqEmptyClip(Machine::WT1, 4) };
    for (int bar = 0; bar < 4; ++bar) {
        // Close-voice the triad as pitch classes inside the root octave (+0..+11):
        // the pad bed stays strictly below the +12..+23 lead band for every root.
        const int root = harmony.roots[(size_t)bar];
        const int chord[3] { root, root + (harmony.minor[(size_t)bar] ? 3 : 4), root + 7 };
        for (int lane = 0; lane < 3; ++lane)
            putNote(clip.bytes, sqWtNoteIdx(bar, 0, lane), ((chord[lane] % 12) + 12) % 12, 16);
    }
    return clip;
}
```

Transcribe `LEAD_LINES` and `LEAD_RHYTHMS` from `src/seq/sessionPresets.ts:113-139` verbatim:

```cpp
// Authored pitch-class melodies for the four progressions in harmonyFor().
// (see src/seq/sessionPresets.ts LEAD_LINES for the voice-leading notes)
constexpr int LEAD_LINES[4][4][6] = {
    { { 7, 3, 5, 7, 10, 7 }, { 8, 0, 10, 0, 3, 0 }, { 10, 7, 5, 7, 10, 2 }, { 5, 2, 3, 2, 10, 0 } },
    { { 3, 7, 10, 7, 5, 3 }, { 5, 8, 0, 8, 7, 5 }, { 3, 0, 8, 0, 3, 0 }, { 2, 7, 11, 2, 11, 7 } },
    { { 0, 3, 7, 7, 10, 3 }, { 7, 10, 2, 3, 2, 10 }, { 10, 2, 5, 2, 0, 10 }, { 8, 5, 0, 8, 7, 0 } },
    { { 7, 10, 0, 3, 2, 0 }, { 7, 11, 2, 7, 5, 2 }, { 5, 8, 0, 0, 3, 8 }, { 5, 2, 10, 2, 0, 7 } },
};

struct LeadRhythm { int step; int duration; };
const std::array<LeadRhythm, 6>& leadRhythmFor(const std::string& family) {
    static const std::map<std::string, std::array<LeadRhythm, 6>> rhythms = {
        { "NEON",      { { { 0, 3 }, { 3, 3 }, { 6, 2 }, { 8, 3 }, { 11, 3 }, { 14, 2 } } } },
        { "ACID",      { { { 0, 2 }, { 2, 3 }, { 5, 3 }, { 8, 2 }, { 10, 4 }, { 14, 2 } } } },
        { "AMBIENT",   { { { 0, 4 }, { 4, 4 }, { 8, 2 }, { 10, 2 }, { 12, 2 }, { 14, 2 } } } },
        { "HOUSE",     { { { 0, 3 }, { 3, 3 }, { 6, 3 }, { 9, 2 }, { 11, 3 }, { 14, 2 } } } },
        { "LO-FI",     { { { 0, 4 }, { 4, 3 }, { 7, 2 }, { 9, 3 }, { 12, 3 }, { 15, 1 } } } },
        { "CINEMATIC", { { { 0, 4 }, { 4, 3 }, { 7, 1 }, { 8, 4 }, { 12, 2 }, { 14, 2 } } } },
    };
    const auto it = rhythms.find(family);
    return it != rhythms.end() ? it->second : rhythms.at("NEON");
}

ClipData leadProgression(const Harmony& harmony, const PresetSpec& spec) {
    ClipData clip { std::string(spec.variation) + " MELODY · 4 BAR", 4, sqEmptyClip(Machine::WT1, 4) };
    const int tonic = harmony.roots[0];
    const auto& rhythm = leadRhythmFor(spec.family);
    for (int bar = 0; bar < 4; ++bar) {
        for (int event = 0; event < 6; ++event) {
            // Voice the melody strictly one octave above the pad bed (+12..+23).
            const int pitchClass = (tonic + LEAD_LINES[spec.variationIndex][bar][event]) % 12;
            putNote(clip.bytes, sqWtNoteIdx(bar, rhythm[(size_t)event].step, 0),
                    pitchClass + 12, rhythm[(size_t)event].duration, event == 0);
        }
    }
    return clip;
}
```

Transcribe the drum generator (mirror of Task 3's TS — same tables, same order):

```cpp
namespace drum {
constexpr int KICK = 0, SNARE = 2, CLAP = 3, RIM = 4, CH = 5, OH = 6,
              TOM_LO = 8, TOM_MID = 9, TOM_HI = 10, PERC_A = 12, PERC_B = 13;
}

struct DrumVoice { int pad; std::vector<int> steps; std::vector<int> accents; };
struct DrumArchetype { DrumVoice kick, back, hat, open; std::vector<DrumVoice> perc; };
struct DrumFillHit { int pad; int step; bool accent; };

const DrumArchetype& drumArchetypeFor(const std::string& family) {
    using namespace drum;
    static const std::map<std::string, DrumArchetype> archetypes = {
        { "NEON", { { KICK, { 0, 4, 8, 12 }, { 0 } }, { CLAP, { 4, 12 }, {} },
                    { CH, { 2, 6, 10, 14 }, { 2, 10 } }, { OH, { 2, 10 }, {} },
                    { { PERC_A, { 15 }, {} } } } },
        { "ACID", { { KICK, { 0, 4, 8, 12, 14 }, { 0 } }, { SNARE, { 4, 12 }, { 4, 12 } },
                    { CH, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 2, 6, 10, 14 } },
                    { OH, { 7 }, {} }, {} } },
        { "AMBIENT", { { KICK, { 0, 8 }, {} }, { RIM, { 8 }, {} },
                       { CH, { 4, 12 }, {} }, { OH, {}, {} },
                       { { PERC_A, { 2 }, {} } } } },
        { "HOUSE", { { KICK, { 0, 4, 8, 12 }, {} }, { CLAP, { 4, 12 }, {} },
                     { CH, { 2, 6, 10, 14 }, {} }, { OH, { 2, 6, 10, 14 }, { 6, 14 } }, {} } },
        { "LO-FI", { { KICK, { 0, 7, 10 }, { 0 } }, { SNARE, { 4, 12 }, {} },
                     { CH, { 0, 3, 6, 8, 11, 14 }, {} }, { OH, { 14 }, {} },
                     { { PERC_B, { 6 }, {} } } } },
        { "CINEMATIC", { { KICK, { 0, 10 }, { 0 } }, { SNARE, { 8 }, { 8 } },
                         { CH, {}, {} }, { OH, {}, {} },
                         { { TOM_LO, { 13 }, {} } } } },
    };
    const auto it = archetypes.find(family);
    return it != archetypes.end() ? it->second : archetypes.at("NEON");
}

const std::vector<DrumFillHit>& drumFillFor(const std::string& family) {
    using namespace drum;
    static const std::map<std::string, std::vector<DrumFillHit>> fills = {
        { "NEON", { { TOM_HI, 10, false }, { TOM_MID, 12, false }, { TOM_LO, 14, true } } },
        { "ACID", { { SNARE, 13, false }, { SNARE, 14, false }, { SNARE, 15, true } } },
        { "AMBIENT", { { OH, 12, false }, { PERC_A, 14, false } } },
        { "HOUSE", { { CLAP, 13, false }, { CLAP, 15, true } } },
        { "LO-FI", { { PERC_B, 13, false }, { PERC_B, 15, false } } },
        { "CINEMATIC", { { TOM_HI, 8, false }, { TOM_MID, 10, false }, { TOM_LO, 12, true }, { TOM_LO, 14, true } } },
    };
    const auto it = fills.find(family);
    return it != fills.end() ? it->second : fills.at("NEON");
}

ClipData drumProgression(const PresetSpec& spec, int scene) {
    using namespace drum;
    const auto& family = drumArchetypeFor(spec.family);
    ClipData clip;
    clip.bars = 4;
    clip.bytes.assign(4 * 256, 0);
    const auto hit = [&](int bar, int pad, int step, bool accent = false) {
        clip.bytes[(size_t)sqDr1Idx(bar, pad, step)] = (uint8_t)(accent ? 2 : 1);
    };
    const auto contains = [](const std::vector<int>& v, int x) {
        return std::find(v.begin(), v.end(), x) != v.end();
    };
    const bool intro = scene == 0, build = scene == 1, outro = scene == 5;

    std::vector<int> hatSteps;
    if (spec.energy >= 4) for (int s = 0; s < 16; ++s) hatSteps.push_back(s);
    else if (spec.energy <= 2) {
        for (size_t i = 0; i < family.hat.steps.size(); i += 2) hatSteps.push_back(family.hat.steps[i]);
    } else hatSteps = family.hat.steps;
    const int shift = spec.variationIndex + (scene == 3 ? 1 : 0);
    std::vector<int> hatAccents;
    for (size_t k = 0; k < family.hat.accents.size(); ++k)
        hatAccents.push_back(hatSteps.empty() ? -1 : hatSteps[((size_t)((int)k * 2 + shift)) % hatSteps.size()]);
    static const std::array<std::pair<int, int>, 4> ghosts { { { -1, -1 }, { KICK, 14 }, { SNARE, 11 }, { PERC_B, 3 } } };
    const auto ghost = ghosts[(size_t)spec.variationIndex];

    for (int bar = 0; bar < 4; ++bar) {
        const bool lastBar = bar == 3;
        std::vector<int> kickSteps;
        for (int s : family.kick.steps) if (!(intro || outro) || s % 8 == 0) kickSteps.push_back(s);
        for (int s : kickSteps) hit(bar, family.kick.pad, s, contains(family.kick.accents, s));
        if (!intro && !outro) {
            for (int s : family.back.steps) hit(bar, family.back.pad, s, contains(family.back.accents, s));
            if (!build && ghost.first >= 0) hit(bar, ghost.first, ghost.second);
        }
        if (!outro) {
            std::vector<int> steps;
            if (intro) {
                for (size_t i = 0; i < family.hat.steps.size(); ++i)
                    if (((int)i + spec.variationIndex) % 2 == 0) steps.push_back(family.hat.steps[i]);
            } else steps = hatSteps;
            for (int s : steps) hit(bar, family.hat.pad, s, !intro && contains(hatAccents, s));
            if (!intro) for (int s : family.open.steps) hit(bar, family.open.pad, s, contains(family.open.accents, s));
        }
        if (!intro) for (const auto& voice : family.perc) for (int s : voice.steps) hit(bar, voice.pad, s, contains(voice.accents, s));
        if ((intro || outro) && lastBar && ghost.first >= 0 && spec.variationIndex >= 2) hit(bar, ghost.first, ghost.second);
        if (build && lastBar) for (int s : { 12, 13, 14, 15 }) hit(bar, PERC_A, s, s == 15);
        if (!intro && !build && !outro && (lastBar || (scene == 3 && bar == 1)))
            for (const auto& f : drumFillFor(spec.family)) hit(bar, f.pad, f.step, f.accent);
    }
    const char* role = intro ? "INTRO" : build ? "BUILD" : outro ? "TAIL" : scene == 3 ? "DRIVE II" : "DRIVE";
    clip.name = std::string(spec.variation) + " " + role + " · 4 BAR";
    return clip;
}
```

- [ ] **Step 3: Rewrite factorySessionLibrary**

Keep the existing `make` lambda's **signature and its 24 call rows exactly as they are** (`SeqFactory.cpp:388-424` — name, family, variation, energy, tags, programs, variationIndex), with one data fix: the NEON CHASE row's programs `{ 13, 2, 4, 11 }` become `{ 13, 2, 14, 11 }` (the web — `sessionPresets.ts:24` — is the source of truth). Keep the `calibratedGain` lambda verbatim.

Inside `make`, after the gains loop, delete the whole clip-selection section (the `wanted` family string, the `candidates` loops, and the `pick` modulo — `SeqFactory.cpp:356-382`) and replace it with the generator, building a local `PresetSpec` from the lambda's parameters:

```cpp
            // One harmonic world per song (port of sessionPresets.ts buildSession).
            const PresetSpec spec { name, family, variation, energy, preset.tags,
                                    programs, variationIndex };
            const Harmony harmony = harmonyFor(spec);
            const ClipData bass = bassProgression(harmony, variation);
            const ClipData lead = leadProgression(harmony, spec);
            const ClipData pads = padProgression(harmony, variation);
            for (size_t s = 0; s < preset.session.scenes.size(); ++s) {
                auto& sceneData = preset.session.scenes[s];
                const ClipData drums = drumProgression(spec, (int)s);
                // Arrange density intentionally; the tonal parts keep one
                // progression whenever they enter, so every scene is one song.
                const std::array<const ClipData*, 4> picks =
                    s == 0 ? std::array<const ClipData*, 4> { &drums, nullptr, nullptr, &pads }
                    : s == 1 ? std::array<const ClipData*, 4> { &drums, &bass, nullptr, &pads }
                    : s == 4 ? std::array<const ClipData*, 4> { nullptr, &bass, &lead, &pads }
                    : s == 5 ? std::array<const ClipData*, 4> { &drums, nullptr, nullptr, &pads }
                    : std::array<const ClipData*, 4> { &drums, &bass, &lead, &pads };
                for (size_t t = 0; t < 4; ++t) {
                    sceneData.hasClip[t] = picks[t] != nullptr;
                    sceneData.clips[t] = picks[t] ? *picks[t] : ClipData {};
                }
            }
            return preset;
```

(`bassProgression`/`padProgression` take `const std::string&` — pass the lambda's `const char* variation` and it converts. The trailing `// Preserve the hand-authored ... library.front().session = factorySession();` line stays.) The BREAK scene (s == 4) now ships **no drum clip** — the old code filled all four tracks, so `hasClip` totals change; that's intended and matches the web.

Also update the file's header comment (lines 1-2) — it claims a "note-for-note transcription so both builds ship the same NEON TALE factory session"; extend it to say the session *library* generator is a transcription of `src/seq/sessionPresets.ts`.

**Gotcha:** the scene mask lambda captures `drums` per scene but `bass`/`lead`/`pads` once — mirror the TS exactly (TS builds `drums` inside the scene loop, tonal clips outside).

- [ ] **Step 4: Update the host-test expectation**

`juce/test/sq4_host_test.cpp` — the `neonChase` array: `const std::array<int, 4> neonChase { 13, 2, 4, 11 };` → `{ 13, 2, 14, 11 }`.

- [ ] **Step 5: Build and run the native tests**

```bash
cmake --build juce/build --target sq4_engine_test --parallel && juce/build/sq4_engine_test
```
Expected: builds clean; all existing CHECKs pass (parity is proven in Task 6).

- [ ] **Step 6: Commit**

```bash
git add juce/source/seq/dsp/SeqFactory.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(seq): port musical session generator to native"
```

---

### Task 6: Native invariants + byte-for-byte parity test

**Files:**
- Modify: `juce/test/sq4_engine_test.cpp` (register + drum-uniqueness checks)
- Modify: `juce/test/sq4_host_test.cpp` (preset parity vs `web-session-presets.json`)

**Interfaces:**
- Consumes: `factorySessionLibrary()`, the fixture from Task 4, and the existing web-session parity pattern at `sq4_host_test.cpp:795-810` (juce::File / juce::JSON usage to copy).
- Produces: green `sq4_engine_test` + `sq4_host_test` proving the port.

- [ ] **Step 1: Add invariant checks to sq4_engine_test.cpp**

Add a new test function and call it from `main()`:

```cpp
static void testSessionLibraryMusicality() {
    using namespace fable;
    const auto& library = factorySessionLibrary();
    CHECK(library.size() == 24);

    // Register split: every generated pad sits strictly below every lead note.
    for (size_t p = 1; p < library.size(); ++p) {
        for (const auto& scene : library[p].session.scenes) {
            if (!scene.hasClip[2] || !scene.hasClip[3]) continue;
            const auto pitches = [](const ClipData& clip) {
                std::vector<int> out;
                for (size_t i = 0; i + 2 < clip.bytes.size(); i += SQ_NOTE_STRIDE)
                    if (clip.bytes[i] & 1)
                        out.push_back(((int)clip.bytes[i + 2] - 1) * 12 + (clip.bytes[i + 1] & 0x7f));
                return out;
            };
            const auto lead = pitches(scene.clips[2]);
            const auto pad = pitches(scene.clips[3]);
            CHECK(!lead.empty() && !pad.empty());
            CHECK(*std::min_element(lead.begin(), lead.end()) >= 12);
            CHECK(*std::max_element(lead.begin(), lead.end()) <= 23);
            CHECK(*std::max_element(pad.begin(), pad.end()) <= 11);
            CHECK(*std::min_element(pad.begin(), pad.end()) >= 0);
        }
    }

    // Unique drums: all 24 DROP-A patterns differ; scenes differ within a song.
    std::set<std::vector<uint8_t>> dropDrums;
    for (const auto& preset : library)
        dropDrums.insert(preset.session.scenes[2].clips[0].bytes);
    CHECK(dropDrums.size() == library.size());
    for (size_t p = 1; p < library.size(); ++p) {
        const auto& scenes = library[p].session.scenes;
        CHECK(!scenes[4].hasClip[0]); // BREAK stays drumless
        std::set<std::vector<uint8_t>> perScene;
        for (size_t s : { (size_t)0, (size_t)1, (size_t)2, (size_t)3, (size_t)5 })
            perScene.insert(scenes[s].clips[0].bytes);
        CHECK(perScene.size() == 5);
    }
}
```

(Add `#include <set>` and `#include <vector>` to the includes if missing.)

- [ ] **Step 2: Build + run — expect failures only if the port diverged**

```bash
cmake --build juce/build --target sq4_engine_test --parallel && juce/build/sq4_engine_test
```
Expected: PASS. Any FAIL here means the C++ transcription differs from the plan — fix `SeqFactory.cpp`, not the test.

- [ ] **Step 3: Add the preset parity check to sq4_host_test.cpp**

Next to the existing `web-session.json` block (`sq4_host_test.cpp:795`), load `web-session-presets.json` the same way (`juce::File(__FILE__).getSiblingFile("fixtures")...` fallback included) and compare all 24 presets:

```cpp
{
    juce::File fixture = juce::File::getCurrentWorkingDirectory()
                             .getChildFile("test/fixtures/web-session-presets.json");
    if (!fixture.existsAsFile())
        fixture = juce::File(__FILE__).getSiblingFile("fixtures").getChildFile("web-session-presets.json");
    check(fixture.existsAsFile(), "web-session-presets.json fixture is present");
    const auto parsed = juce::JSON::parse(fixture.loadFileAsString());
    const auto* webPresets = parsed.getArray();
    const auto& native = fable::factorySessionLibrary();
    check(webPresets != nullptr && webPresets->size() == (int)native.size(),
          "fixture carries all 24 presets");
    bool metaMatches = true, clipsMatch = true;
    for (int p = 0; webPresets != nullptr && p < webPresets->size(); ++p) {
        const auto& web = (*webPresets)[p];
        const auto& mine = native[(size_t)p];
        const auto webSession = web.getProperty("session", {});
        if (web.getProperty("name", "").toString() != juce::String(mine.name)
            || web.getProperty("family", "").toString() != juce::String(mine.family)
            || (int)web.getProperty("energy", -1) != mine.energy
            || std::abs((double)webSession.getProperty("bpm", 0.0) - mine.session.bpm) > 1e-9
            || std::abs((double)webSession.getProperty("swing", -1.0) - mine.session.swing) > 1e-9)
            metaMatches = false;
        const auto* scenes = webSession.getProperty("scenes", {}).getArray();
        if (scenes == nullptr || scenes->size() != (int)mine.session.scenes.size()) { clipsMatch = false; continue; }
        for (int s = 0; s < scenes->size(); ++s) {
            const auto* clips = (*scenes)[s].getProperty("clips", {}).getArray();
            for (int t = 0; clips != nullptr && t < clips->size(); ++t) {
                const auto& webClip = (*clips)[t];
                const bool webHas = !webClip.isVoid() && webClip.isObject();
                if (webHas != mine.session.scenes[(size_t)s].hasClip[(size_t)t]) { clipsMatch = false; continue; }
                if (!webHas) continue;
                juce::MemoryOutputStream decoded;
                juce::Base64::convertFromBase64(decoded, webClip.getProperty("pattern", "").toString());
                const auto& nativeBytes = mine.session.scenes[(size_t)s].clips[(size_t)t].bytes;
                if (decoded.getDataSize() != nativeBytes.size()
                    || std::memcmp(decoded.getData(), nativeBytes.data(), nativeBytes.size()) != 0)
                    clipsMatch = false;
                if (webClip.getProperty("name", "").toString()
                        != juce::String(mine.session.scenes[(size_t)s].clips[(size_t)t].name))
                    clipsMatch = false;
            }
        }
    }
    check(metaMatches, "session-preset metadata matches the web generator");
    check(clipsMatch, "all 24 preset sessions match the web generator byte-for-byte");
}
```

(JSON `null` clips arrive as void variants — the `isVoid()`/`isObject()` guard covers both. Add `#include <cstring>` if missing.)

- [ ] **Step 4: Build + run everything**

```bash
cmake --build juce/build --target sq4_engine_test sq4_host_test --parallel
juce/build/sq4_engine_test
juce/build/sq4_host_test_artefacts/Release/sq4_host_test || juce/build/sq4_host_test
```
(Use whichever host-test path exists — check `juce/build/` for the artefact layout; the plugin host tests live under `*_artefacts/Release/`.)
Expected: 0 failures on both. A byte mismatch means the C++ and TS generators diverged — diff the first mismatching scene/track and fix `SeqFactory.cpp` (never the fixture).

- [ ] **Step 5: Full verification sweep**

```bash
npx vitest run && npx tsc --noEmit
cmake --build juce/build --parallel
juce/build/sq4_engine_test && juce/build/engine_test && juce/build/drum_engine_test && juce/build/bass_engine_test
```
Expected: everything green.

- [ ] **Step 6: Commit**

```bash
git add juce/test/sq4_engine_test.cpp juce/test/sq4_host_test.cpp
git commit -m "test(seq): prove native/web preset parity and register split"
```
