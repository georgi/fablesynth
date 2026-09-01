# Audio Engine Review — Web (AudioWorklet) and JUCE

Scope: the four DSP engines as they exist on `main` today — WT-1 (`src/engine/worklet.js`,
`juce/source/dsp/`), DR-1 (`src/drum/engine/`, `juce/source/drum/dsp/`), BL-1
(`src/bass/engine/`, `juce/source/bass/dsp/`), the shared FX chains, and the plugin
processors that drive them. The earlier `docs/juce-fidelity-review.md` covered the JUCE
side only; this review re-checks which of those findings actually landed, reviews the
web engines for the first time, and measures a few things instead of estimating them.

All numbers below were produced on this branch: the JUCE engines built with
`g++ -O2` (no JUCE), the web worklets driven through the existing vitest harnesses
(`workletHarness.ts`) on Node 22. Machine: a 2.1 GHz Xeon sandbox, so absolute
figures are pessimistic; ratios are what matter.

---

## 1. Where things stand

**The JUCE side has moved a long way since the first review.** Of its twelve findings,
ten are fixed in WT-1 and DR-1 (sample-accurate MIDI, lock-free table publication,
lookahead limiter, 4x FIR-oversampled drive, chunk-invariant smoothers, intra-chunk
ramps, Hermite delay reads, sample-rate-mapped noise/DC poles, Hermite wavetable reads,
full reset on prepare, no audio-thread `setSize`). Two remain open and are still the
biggest fidelity items on the plugin: **host automation is block-rate** (finding 5) and
**discrete switches are not crossfaded**.

**The web side has not received the same treatment.** Every fidelity fix listed above was
applied to the C++ port only. The worklets still use linear table reads, block-rate
parameter holds with no intra-block ramps, fixed per-sample smoothing constants that
change behaviour with the device sample rate, and non-deterministic `Math.random()`.
The two implementations are therefore no longer the "lockstep twins" the code comments
describe, and there is no test that would notice.

**Measured: table-read interpolation.** Same patch (PRIME, POS 0.66, one voice, filter
off), same alias metric as `engine_test.cpp` §3 but with a Blackman-Harris window and a
±12-bin harmonic mask so the window leakage stops dominating:

| MIDI note | web (linear read) | JUCE (cubic Hermite) |
|---|---|---|
| 36 | −87.1 dB | −98.5 dB |
| 48 | −73.0 dB | −92.7 dB |
| 60 | −82.1 dB | −101.0 dB |
| 72 | −90.4 dB | −100.3 dB |
| 84 | −99.2 dB | −102.0 dB |
| 96 | −99.4 dB | −100.0 dB |

Above C6 both engines sit on the float32/window floor. Below that the web engine's
interpolation images are 10–20 dB worse. Note also that the shipped alias test
(`engine_test.cpp:108-131`, Hann window, ±6 bins) reports −57 dB for **both** engines at
notes 96/103/108 — it is leakage-limited and cannot tell linear from Hermite. It should
be tightened (see §6).

**Measured: render cost** (8 held notes, 10 s of audio, 128-sample blocks, 48 kHz):

| Patch | web worklet | JUCE engine |
|---|---|---|
| heavy: 2 osc × 7 unison, LP24 + ADAA drive, F2, sub, pink noise, 2 mod routes | 297 µs/block (11.1 % of a core) | 249 µs/block (9.3 %) |
| light: 1 osc, 1 unison, LP12 | 62 µs/block (2.3 %) | 29 µs/block (1.1 %) |
| heavy + full FX chain (drive/chorus/delay/reverb/comp on) | n/a (native nodes) | 339 µs/block (12.7 %) |
| heavy, FX chain instantiated but all off | n/a | 265 µs/block (9.9 %) |

The web engine is within ~20 % of C++ on DSP-bound patches but 2× slower on light ones,
which points at per-voice overhead rather than arithmetic. A 30-line experiment that
only removed the per-block string building (pre-computed param keys, numeric env-coef
cache key, hoisted `srcs` array — nothing else) brought the light patch from 62 µs to
39 µs (−37 %) and the heavy one from 297 µs to 270 µs (−9 %), with all 27 engine tests
still passing. Details in §3.

---

## 2. WT-1 — JUCE (`juce/source/dsp/`)

What is good and should be kept: band-limited 11-level mips with the 0.475·sr guard and
crossfade (`Engine.cpp:505-513`), Hermite reads (`Engine.cpp:41-47`), TPT/Cytomic SVF with
`tan` prewarp and per-32-sample coefficient recompute on a ramped cutoff
(`Engine.cpp:694-742`), first-order ADAA on the filter drive (`Engine.cpp:669-686`),
per-voice DC blocker with an sr-mapped pole (`Engine.cpp:154-155`), 4x Kaiser half-band
drive oversampling (`Fx.cpp:174-179, 306-318`), a real lookahead limiter with a
monotonic-queue window minimum and a hard ceiling (`Fx.h:110-143`), reported latency
(`PluginProcessor.cpp:58`), `ScopedNoDenormals`, sample-split MIDI
(`PluginProcessor.cpp:303-311`), and sequencer runs split at every note-on/off sample
(`Engine.cpp:1063-1125`). The architecture is right; what follows is about polish.

### J1. Host automation is still block-rate (open finding 5) — High
`processBlock` copies every APVTS atomic once per host block (`PluginProcessor.cpp:220-223`)
and `fx.setParams` recomputes every EQ/drive/reverb coefficient from those values
(`Fx.cpp:229-295`). The engine's intra-chunk ramps (`Engine.cpp:557-619`) run across a
≤128-sample chunk from the *previous chunk's* target, so at a 1024-sample host block an
automated cutoff steps once per 21 ms and ramps for the first 2.7 ms of each step. That is
still a staircase, only with softened edges. Resonance (`k1`, `Engine.cpp:642`), drive,
detune, spread, blend, FX rates/depths and EQ gains have no ramp at all.

Fix: a per-parameter smoother between APVTS and the engine — `juce::SmoothedValue` per
continuous parameter, targets set once per block, values pulled per 128-sample chunk
(`Engine::render` already chunks). Smooth gains/pan/pos linearly, cutoff and pitch in log
domain, ~10–20 ms. Then the existing ramps become exact. 1–2 days including the FX
coefficients (recompute EQ/reverb per chunk, not per sample).

### J2. Discrete switches click — Medium
Filter type/route (`Engine.cpp:855-966`), oscillator/table selection (`Engine.cpp:465-468`),
sub shape, and LFO shape switch instantly. A route change also swaps which scratch buffer
is the output. Fix: on a switch, render both old and new configuration for one chunk and
crossfade (equal-power, 128 samples). For table changes, keep the old `EngineTable`
pointer for one chunk. Half a day per engine.

### J3. Table publication: `std::atomic_load(shared_ptr)` is not lock-free — Medium
`Engine::render` uses the free-function `std::atomic_load` on a `shared_ptr`
(`Engine.cpp:1035`). In libstdc++ and libc++ that is a hashed spinlock pool, so the audio
thread can spin while the message thread is inside `atomic_exchange` in `setTables`
(`Engine.cpp:218`). Short, but it is a lock. Separately, two `setTables` calls within one
render (e.g. import then rename) overwrite `retired_`, and the audio thread frees the
previous `TableSet` when `snap` dies (`Engine.cpp:1035`). The comment at
`Engine.cpp:299-305` acknowledges the second case. Fix: publish a raw
`std::atomic<const TableSet*>` and push retired sets onto a small message-thread-drained
ring (or a `juce::Timer` sweep) so no free ever happens on audio. Same pattern in
`DrumEngine.cpp:82-95` and the bass engine. Half a day for all three.

### J4. Oversampler cost — Medium (matters for DR-1's 16 chains)
`HalfBandFir::process` (`Fx.h:92-98`) is a generic direct-form FIR. It runs 2×47 + 4×17 taps
per channel-sample on the up path and again on the down path, ignoring that every other
tap of a half-band filter is zero and that half the interpolator inputs are zero-stuffed.
A polyphase implementation does the same job in roughly a quarter of the MACs. Measured
cost of the whole FX chain at present: ~3.4 % of a core per instance (table above) —
acceptable for one WT-1, but DR-1 instantiates this chain per pad (see §4).

### J5. Factory tables are rebuilt per plugin instance — Low
`generateTables()` runs in the processor constructor (`PluginProcessor.cpp:17-19`): 6
tables × 16 frames × 11 iFFTs of 2048 points and 8.6 MB of float data per instance, per
plugin type. Hosts that scan or instantiate many copies pay this repeatedly. Fix: a
process-wide `static std::shared_ptr<const std::vector<TablePtr>>` built once (the
tables are immutable and sample-rate independent), or JUCE's `SharedResourcePointer`.

### J6. Remaining sample-rate-dependent constants — Low
The steal fade `level -= level * 0.12` (`Engine.cpp:90`) reaches −80 dB in ~72 samples:
1.5 ms at 48 kHz, 0.75 ms at 96 kHz. Derive it from a time constant like the others.
The glide coefficient (`Engine.cpp:796`) is already sr-scaled.

### J7. SQ-4 does not report the chain latency — Low
Every hosted device chain (two `Fx`, `BassFx`, DR-1 with `enablePadFx(true)`,
`SeqProcessor.cpp:410-412`) carries the same 27 + 72 = 99 samples of drive-FIR + limiter
lookahead, so the four tracks are mutually aligned, but `SeqAudioProcessor` never calls
`setLatencySamples`, so a DAW cannot compensate the ~2 ms. One line.

### J8. User-table import bakes resampling images into the table — Medium
`sliceToFrames` (`UserTables.cpp:114-131`; identical in `src/engine/usertables.ts:148-166`)
stretches each detected cycle (typically 100–500 samples) to 2048 with **linear**
interpolation, then FFTs the result and keeps up to 1024 harmonics. Linear upsampling by
4–20× leaves the interpolation images (sinc² lobes at −20 to −40 dB) inside the kept
band, so they become permanent "harmonics" of the imported table, and the genuine
partials get the sinc² droop. Because the band-limiting FFT already exists, the fix is
cheap: FFT the cycle at (rounded) native length, place its bins into the 2048-point
spectrum, zero the rest — that is exact band-limited resampling. For non-integer periods,
refine `detectCycleLength` with parabolic interpolation around the autocorrelation peak
and resample with a windowed sinc. Half a day, both engines, and it makes imported tables
match Serum-style imports.

---

## 3. WT-1 — Web (`src/engine/worklet.js`, `synth.ts`)

### W1. Fidelity fixes that never reached the web port — High
Each of these is done in C++ and absent in the worklet:

- **Table reads are linear** (`worklet.js:795-797, 817-823`); JUCE uses Hermite. Alias
  table in §1.
- **No intra-block ramps.** Phase increments, morph fraction, unison gains, amp-mod factor
  and sub increment are held for the whole 128-sample quantum
  (`worklet.js:781-833, 1105-1128, 1198-1210`). Glide, LFO→pitch and LFO→POS staircase at
  375 Hz; the FM sidebands are audible on fast LFO rates and deep pitch routes.
- **Smoothers are per-block constants**: POS `0.35` (`worklet.js:720`), cutoff `0.5`
  (`worklet.js:853`), steal fade `0.12` per sample (`worklet.js:181`). A 96 kHz context
  (common on macOS with pro interfaces) halves every one of these times.
- **Fixed-rate one-pole coefficients**: `DC_R = 0.9998` (`worklet.js:218`) and the Kellet
  pink-noise poles (`worklet.js:1142-1149`) are literal 48 kHz values; at 44.1 kHz the DC
  corner and the pink tilt differ from the plugin.
- **Filter coefficients per block**, no cutoff ramp inside the block
  (`worklet.js:848-865`), where JUCE recomputes per 32 samples on a ramped value.

Recommendation: port `Engine.cpp`'s versions verbatim — Hermite `rdH`, the `pIncs/pGl/pGr/pFt`
ramps, `smoothCoef(n, tau·sr)`, `pow(DC_R, 48000/sr)` in the constructor, the
`cutPrev→cutTarget` ramp with 32-sample sub-blocks. Two days, and it restores the parity
premise the whole codebase is built on.

### W2. Per-voice overhead: string keys and allocation in the render loop — High (perf)
The worklet stores parameters in a string-keyed dictionary and rebuilds the key strings
every block for every voice: `pre + '.on'`, `k + 'level'`, `'mat' + s + '.src'` (16 slots
× 3 keys, `worklet.js:1055-1065`), `pre + '.sync'`, plus the envelope coefficient cache
key `a + '|' + d + '|' + r` (`worklet.js:150`) and the `srcs` array literal
(`worklet.js:1044`). Each is a heap allocation and a hash of a fresh cons-string on the
audio thread, per voice, per 2.7 ms. The `for (const k in accum) delete accum[k]`
pattern (`worklet.js:1054, 1071`) keeps those objects in dictionary mode.

Measured (see §1): removing only the string building gave −37 % on the light patch and
−9 % on the heavy one. The full fix is what JUCE already does — a `Float64Array` param
store indexed by integer ids (mirror `Params.h`), with `{t:'p'}` messages resolved to an
index once in `onMsg`. That also removes the `pm` dictionary in favour of a second
`Float64Array`. One day, mechanical.

### W3. Event timing is quantised to the render quantum — Medium
Note on/off from the UI and MIDI, sequencer steps, and sequencer note-offs all take
effect at the start of a 128-sample block (`worklet.js:1256-1277`): up to 2.9 ms of
jitter at 44.1 kHz. The internal sequencer and hosted-clip transport *know* the exact
sample (`seqToNext`, `clipToNext`, `e.remaining`), so `process()` can split the block at
those offsets exactly as `Engine::render` does (`Engine.cpp:1063-1125`) — `renderVoice`
already takes an arbitrary `n`. For live notes, send `at: audioContext.currentTime`-based
timestamps with the message and apply them at the matching offset; Web MIDI provides
`event.timeStamp`. One day for the sequencer path, which is the one that matters for
groove.

### W4. Table transfer: structured clone of the whole pool — Medium (UX)
`pushTables()` copies every table (`t.data.slice().buffer`, `synth.ts:166-172`) — 8.6 MB
of factory tables plus up to `MAX_USER_TABLES` × frames × 90 KB — through `postMessage`
on every user-table add/delete/rename, and the worklet allocates fresh `Float32Array`s
(`worklet.js:379-384`), leaving the old ones for a GC that runs on the render thread.
Fix: transfer (`postMessage(msg, [buf])`) instead of copying, and send only the changed
slot; or hold tables in a `SharedArrayBuffer` (cross-origin isolation permitting) and
send indices.

### W5. `Math.random()` in the render loop — Low
Noise, unison start phases and LFO S&H use `Math.random()` (`worklet.js:193, 289-290,
1141, 1155`). It is non-seedable, so no parity or regression test can compare
noise-bearing renders, and it is slower than an inline xorshift. JUCE already has `Rng`
(`Engine.h:29-32`); port it.

### W6. FX chain is a different algorithm on the web — Medium (fidelity + parity)
The web FX rack is a graph of native nodes (`synth.ts:190-305`) while the plugin
re-implements each stage in `Fx.cpp`. The two now differ materially:

| Stage | web | JUCE |
|---|---|---|
| Drive | `WaveShaperNode`, 2x, 513-point curve, table-interpolated | 4x Kaiser FIR, analytic `tanh` |
| Chorus / delay reads | `DelayNode` (linear interpolation in Chromium) | Catmull-Rom |
| Reverb | `ConvolverNode` with a random-noise IR, no damping, re-rendered on every SIZE change (`synth.ts:307-322`) — the buffer swap cuts the tail | Freeverb, sr-scaled |
| Compressor | `DynamicsCompressorNode` (Chromium detector, built-in pre-delay) | peak follower + spec static curve |
| Limiter | `DynamicsCompressorNode`, ratio 14, no hard ceiling | lookahead, −1 dBFS ceiling |
| Latency | 0 reported / implicit | 99 samples, reported |

Two defensible directions. (a) Keep native nodes for CPU reasons but fix the worst
web-only behaviours: pre-filter the reverb IR (frequency-dependent decay, a short early
reflection cluster) and crossfade two convolvers on SIZE changes instead of swapping the
buffer; add a hard clipper/ceiling after the limiter. (b) Port `Fx.cpp` into the worklet
(same JS as the engine, ~450 lines) so both products run one algorithm and the FX become
testable in vitest; keep the native graph only for the analysers. (b) is the one that
makes "sounds identical" true; its cost is the drive oversampler in JS, which should be
polyphase (J4) from day one.

### W7. Harness renders allocate per block — Low (tests only)
`workletHarness.render` allocates two `Float32Array(128)` per block; fine for tests, but
benchmark numbers include it.

---

## 4. DR-1 — drum machine (web `worklet-drum.js`, JUCE `DrumEngine.cpp` / `DrumFx.cpp`)

Status of the first review's drum-related items: MIDI timing, lock-free tables, 4x drive,
Hermite delay/table reads, chunk-invariant smoothers, sr-mapped noise/DC, prepare/reset
and scratch sizing are all fixed on the JUCE side (`DrumProcessor.cpp:380-410`,
`DrumEngine.cpp:82-95, 39-45, 34-36, 66, 577-578`, `DrumFx.cpp:68-73, 92-115`). The web
worklet has none of them (`worklet-drum.js:21, 317, 372-374, 456, 572, 623`).

### D1. JUCE limits each pad, not the bus — High
The web chain is `16 pads → sum → master gain → DC → limiter` (`drum-synth.ts:204-218,
296`). The JUCE port put master gain, DC block and the `LookaheadLimiter` **inside**
`DrumFx` (`DrumFx.cpp:317-326`) and then sums sixteen independently limited pads onto the
output bus (`DrumEngine.cpp:714-725`). MAIN therefore has no ceiling at all — a kick +
snare + hat can add up well past 0 dBFS — while sixteen limiters, DC biquads and makeup
stages run where one should. Move gain/DC/limiter to a per-bus stage after the sum
(`DR_NBUSES` limiters). Half a day, plus a summed-bus ceiling test.

### D2. Retriggering a sounding pad hard-cuts it — High
`trigger()` zeroes `ampLevel`, the SVF, the DC blocker, the saturator state and resets
phase (`worklet-drum.js:66-76`; `DrumEngine.cpp:48-59, 108-111`). Only *other* pads in a
choke group get the fade; the retriggered pad itself goes to zero in one sample. Any decay
longer than the step spacing clicks on every hit, on both platforms. Fix: route a
retrigger through the choke fade (2–5 ms, sr-derived) and start the new hit on a second
voice slot, or at least after the fade. Small.

### D3. Amp envelope ends with a step — Medium
Decay is `lin + (exp − lin)·curve` with `exp = e^(−4.5·td/dec)`, then `0` once
`td ≥ dec` (`worklet-drum.js:540-549`; `DrumEngine.cpp:544-547`): at the boundary the
value is `0.011·curve`, a −40 dB step per hit, made audible by the per-pad compressor
makeup and reverb. Normalise the exponential so it reaches 0:
`(e^(−4.5·td/dec) − e^(−4.5)) / (1 − e^(−4.5))`. Trivial; update golden numbers.

### D4. Sample layer — Medium
Linear interpolation, no anti-alias/anti-image filter, unbounded ratio (`worklet-drum.js:426,
437-439`; `DrumEngine.cpp:412, 424-426`): pitched-up one-shots alias hard. START/END and
the reverse stop are hard cuts (`worklet-drum.js:433-436`; `DrumEngine.cpp:420-423`). JUCE
still holds the sample-player `step` per 16-sample sub-block (`DrumEngine.cpp:412-413,
566`), so the pitch envelope staircases on the sample layer even though the oscillator
layer is ramped. Fix: Hermite read, a one-pole or half-band pre-filter when `step > 1`,
1–2 ms edge fades, and ramp `step` like the oscillator increments. One day.

### D5. Mip crossfade only covers a 0.07-octave band — Medium
`W = 0.07` (`worklet-drum.js:325-331`; `DrumEngine.cpp:290-296`) is fine for glides but a
drum pitch envelope crosses several mips within milliseconds and hard-switches between
them; the JUCE morph ramp is explicitly disabled when the frame/mip offset changes
(`DrumEngine.cpp:334`). Since the second read path already exists, blend by `frac(mipF)`
always (full trilinear). Hours. The same change would help WT-1 pitch-env routes.

### D6. Divergences that are bugs, not choices — Medium
- `DrumAudioProcessor::setChain` throws the chain contents away and stores
  `iota(0..bars−1)` (`DrumProcessor.cpp:213-219`, also `:262-265, :531-533`), while the
  engine supports arbitrary chains (`DrumEngine.cpp:130-136`) and the web plays them.
- The web exposes `pad.out` (`src/drum/params.ts:122`) but the worklet never reads it and
  every chain feeds `masterGain` (`drum-synth.ts:296`); JUCE routes to five buses
  (`DrumEngine.cpp:710, 721`). Either implement it (16 → 5 outputs on the node) or hide it.
- Web `chain` entries are unclamped (`worklet-drum.js:137`) → a bad index yields a silent
  bar; JUCE clamps (`DrumEngine.cpp:134`).

### D7. Web per-sample overhead — High (perf)
`ampEnv` runs per sample and does five `pre + 'aenv.xxx'` string concatenations plus one
`Math.exp` (`worklet-drum.js:535-549`): with 16 voices that is millions of string
allocations per second on the render thread. `padMod` allocates per pad per run
(`:276-277`), `renderSample` loops even at level 0 (`:432-445`, same in JUCE `:419-431`),
and every pad has its own `ConvolverNode` with a 0.5–5 s stereo IR, reverb on by default
(`drum-synth.ts:305-323`) — the dominant CPU cost of the web drum machine. Same fix as W2
(flat typed-array params, hoisted envelope constants with a recursive multiply for the
exponential), plus share reverbs across pads (one convolver per size bucket with per-pad
send gains).

### D8. JUCE CPU: sixteen always-on FX chains — Medium (perf)
All sixteen `DrumFx` chains run every sample regardless of activity
(`DrumEngine.cpp:714-720`) so tails ring out: 16 × (Freeverb 24 delays + compressor with
`pow`+`log10` per sample (`DrumFx.cpp:267`) + two Hermite delays + the 4x drive). Gate a
chain to bypass after its tail has been silent for N ms, make the oversampler polyphase
(J4), and move the compressor gain computer to a per-32-sample update. Roughly halves
plugin CPU.

### D9. Smaller items
`DR_CHOKE_FADE = 0.88`/sample is sr-dependent on both sides (`worklet-drum.js:623`,
`DrumEngine.cpp:634`). `prepare()` does not reset `samplesToNext_`/`hostSynced_`/clip
state, so a sample-rate change mid-play keeps a step count computed at the old rate.
`drumOneShots()` is a function-local static first touched from `render()`
(`DrumEngine.cpp:394, 735`) — one heap allocation on the audio thread on first use; touch
it in `prepare()`. 1075 seq_cst atomic loads per block (`DrumProcessor.cpp:302-304`) can
be relaxed. The `switch (ftype)` inside the SVF sample loop (`DrumEngine.cpp:504-509`)
belongs outside it.

---

## 5. BL-1 — acid bass (web `worklet-bass.js`, JUCE `BassEngine.cpp` / `BassFx.cpp`)

The JUCE side has the same fixed list as WT-1 (sample-split MIDI `BassProcessor.cpp:281-305`,
immutable table set `BassEngine.cpp:65-81`, 4x drive and lookahead limiter `BassFx.cpp:51-56,
269`, ramped increment/pan/morph/sub/cutoff `BassEngine.cpp:356-418, 432-434, 546-554`,
sr-mapped DC pole `BassEngine.cpp:56`, full reset on prepare). The web worklet has none of
them (`worklet-bass.js:30, 368, 416-429, 423-425, 530-542`). Slide (one-pole in semitones,
~60 ms, `worklet-bass.js:622-630`), last-note-priority legato with slide-back on release
(`:284-303`), and the 55 % gate with tie extension (`:339`) are all right for a 303-style
instrument and should stay.

### B1. Filter sweep zipper on the web — High
Cutoff, resonance, drive and env are computed once per render chunk and held
(`worklet-bass.js:530-542, 637-638`). During the default 180 ms filter-env sweep the cutoff
moves 0.1–0.2 octave per chunk, and with resonance up that is an audible 375 Hz zipper on
every note — the biggest single fidelity gap in the web bass. JUCE ramps between chunk
values with coefficients every 32 samples (`BassEngine.cpp:549-554`). Port it (same
work item as W1).

### B2. Accent gain steps mid-note — Medium (both)
An accented slide target sets `acc = true` on the running voice (`worklet-bass.js:266-270`,
`BassEngine.cpp:96-100`) and the amp gain `vel·(1 + accAmt·0.7)` (`worklet-bass.js:647`,
`BassEngine.cpp:631`) jumps by up to +3.5 dB at the next chunk boundary; the filter-env
peak and decay formula change discontinuously at the same time. Latch the accent at
note-on or ramp the gain over a chunk. Small.

### B3. LP24 is two identical resonant stages — Medium (BL-1 and WT-1)
"LP24" cascades two SVF stages with the same `k` (`worklet-bass.js:592-608`,
`BassEngine.cpp:575-591`; identically in WT-1 `worklet.js:944-960`, `Engine.cpp:738-740`).
The resonant peak is therefore `(1/k)²`: +3.8 dB at the default resonance and +46 dB at
`res = 1`, while `k = 2 − 1.93·res` (`worklet-bass.js:538`) bottoms out at Q ≈ 14 so the
filter never self-oscillates and most of the knob travel does little. That is neither a
4-pole ladder (one peak, −12 dB/oct passband loss with resonance — the "thin when
squelchy" 303 signature) nor a well-tapered SVF. The drive/ADAA saturator also sits
*before* the filter (`worklet-bass.js:546-567`), so nothing bounds the resonance loop; the
output limiter ends up doing that job. Cheap fix: put the resonance in one stage only (or
distribute it as √k per stage) and retaper `res` exponentially so the top of the knob
reaches near self-oscillation. Better, for BL-1 specifically: a diode-ladder or an SVF
with `tanh` in the feedback path, run at 2x, so drive lives inside the loop. Medium for the
cheap fix, large for a new filter; both twins must change together.

### B4. Sub square uses a one-sided polyBLEP — Low
Only the post-edge residual is applied (`worklet-bass.js:470-471`, `BassEngine.cpp:442-445`);
the pre-edge `(t+1)²` term is missing (WT-1's sub has both, `worklet.js:1115-1118`). Half
the correction, so half the alias suppression. Negligible below 130 Hz but wrong and a
five-line fix.

### B5. Master dynamics differ by several dB between the twins — Medium
The web output stage is a `DynamicsCompressorNode` at −6 dB, 14:1, 4 dB knee
(`bass-synth.ts:219-224`): a compressor that engages on most accents, with Chromium's
built-in makeup and no hard ceiling. JUCE applies the equivalent makeup and then a
transparent −1 dBFS brickwall (`BassFx.cpp:65-66`, `Fx.h:115-133`). Accents and resonance
peaks are louder and more dynamic in the plugin. Pick one: emulate the WebAudio curve in
JUCE ahead of the limiter, or move the web to a worklet limiter (see W6).

### B6. Real-time safety in the JUCE processor — Medium
`BassEngine::setChain` does `chain_.assign()` (`BassEngine.cpp:158-166`) and is called from
`processBlock` (`BassProcessor.cpp:264`); `chain_` starts with capacity 1, so any 2–4 bar
chain reallocates on the audio thread. `held_.push_back` (`BassEngine.cpp:125`) is on the
MIDI path. Use a fixed `std::array<int, BL_NPATTERNS>` plus count and `reserve(128)` in
`prepare`. `prepare()` also leaves `playing_`, `samplesToNext_` (in old-rate samples),
`hostSynced_` and `songPos_` untouched. Small.

### B7. Standalone clock drops the fractional step residue — Low (both)
Each fire reassigns `samplesToNext = dur − offNow + offNext` and the run is split at
`ceil(samplesToNext)` (`worklet-bass.js:340-348, 686-692`; `BassEngine.cpp:243, 700`), so
every step is `ceil(dur)` samples: ~0.01 % slow, ~35 ms over five minutes against an
external clock. The hosted SQ-4 path already schedules anchor-absolute and does not
drift; make the standalone path accumulate the residue the same way. The same pattern
exists in WT-1's `seqFire` (`worklet.js:603`, `Engine.cpp:412`).

### B8. Smaller items
The filter runs as two independent channels even when `uni = 1` or `spread = 0` (L == R),
doubling the ADAA `exp`/`log1p` cost; a mono fast path halves it. The `switch (ftype)`
lives inside the sample loop (`worklet-bass.js:583`, `BassEngine.cpp:566`). Filter-type
switches keep stale state, including the second LP24 stage. On the web, every FX edit
replaces `driveShaper.curve` and sets `drivePre.gain.value` unramped (`bass-synth.ts:266-268`)
and reverb SIZE swaps `convolver.buffer` (`bass-synth.ts:248, 296`) — both click. MIDI
velocity maps to plain level (`BassProcessor.cpp:297`); most 303 clones map velocity ≥ ~0.8
to the accent path, which the sequencer already has.

---

## 6. Test coverage

What exists is good at catching crashes and gross level errors, and weak at catching the
regressions this review is about:

- **Alias test is leakage-limited.** `engine_test.cpp` §3 (Hann, ±6 bins) reports −57 dB
  for both linear and Hermite reads. Switch to a 4-term Blackman-Harris window with a
  ±12-bin mask (the numbers in §1 came from exactly that) and lower the threshold to
  −85 dB at C4. Add the same test to the web harness — it has none.
- **No cross-engine parity test.** With the JUCE `Rng` ported to the worklet, a
  deterministic patch (noise off, phase-random off) can be rendered on both sides and
  compared per band in dB. Until then "sounds identical" is a comment, not a property.
- **No sample-rate or block-size invariance tests.** Every engine test runs at 48 kHz in
  128-sample blocks; the web harness accepts a sample rate and never varies it. Render at
  44.1/48/96 kHz and in 480/512/4096-sample host blocks and assert spectral tolerance.
- **No click detector.** A max-|Δx| check after note release, on voice steal, on drum
  retrigger and on filter-type switches would have caught D2/D3 and will guard J2.
- **No automation test.** Sweep a cutoff over one second at a 1024-sample block and
  assert no spectral line at the block rate (46.9 Hz and harmonics); this is the
  regression test for J1.
- **No summed-bus ceiling test** for DR-1 with pad FX on (D1).

---

## 7. Recommended order

1. **DR-1 bus limiter** (D1) — a correctness bug with a real clipping risk. Half a day.
2. **Retrigger/decay clicks** on DR-1 (D2, D3) — audible on every default kit. One day.
3. **Web parity port for all three worklets** (W1, B1, and the DR-1 side of W1): Hermite
   reads, ramps, sr-invariant smoothers, `Rng`. Three to four days, then the parity test
   becomes possible. B1 alone is worth doing first if time is short.
3b. **BL-1 accent latch and LP24 resonance retaper** (B2, B3 cheap fix), plus the JUCE
   real-time-safety fixes (B6). One day.
4. **Flat typed-array parameter store in both worklets** (W2, D7). One to two days;
   measured −37 % on light patches, and the drum envelope path will gain far more.
5. **Host-automation smoothing layer** in the JUCE processors (J1) and switch crossfades
   (J2). Two days. This is the last fidelity item from the first review.
6. **Table publication without locks or audio-thread frees** (J3), shared across
   engines. Half a day.
7. **Sample layer** (D4) and **full trilinear mip blend** (D5). One and a half days.
8. **User-table import resampling** (J8) — both platforms, half a day; makes imported
   tables behave like the factory ones.
9. **FX**: polyphase half-band (J4), DR-1 chain gating (D8), web reverb IR shaping and
   crossfaded IR swaps or the worklet FX port (W6). Two to four days depending on the
   route chosen for the web.
10. **Tests** from §6 alongside each item above, not after.

Items 1, 2, 5 and 6 are plugin-only; 3, 4 and 8 are where the web app has the most to
gain. Everything here keeps the current sound where it is already right; nothing changes
the wavetable pipeline, the SVF topology or the ADAA drive, which are the parts worth
protecting.
