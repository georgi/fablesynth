# Shared web FX chain

DR-1, WT-1, and BL-1 share OTT → COMP → DRIVE → CHORUS → DELAY → REVERB on
web, including their hosted editors inside SQ-4. WT-1 retains its EQ before
OTT. DR-1 applies the chain per pad; the other instruments apply it to their
output. Newly added OTT stages and BL-1 COMP default to bypass.

- DEPTH blends the processed signal (35% by default).
- TIME scales band detector attack and release times from 1% to 1000%
  (100% is the reference). Gain and control smoothing remain 15 ms.
- UPWARD brings quiet detail forward; DOWNWARD controls peak compression.
  Both reach 200%. Above 100%, the exaggerated curves can overcompress,
  making louder input produce quieter output.
- AUTO GAIN replaces OUTPUT. It compares the stereo input and processed RMS
  energy and smoothly compensates level before the depth blend. COMP uses the
  same automatic compensation instead of its former MAKEUP knob.

This is an original OTT-style compressor, not a replica of Xfer OTT. Three
complementary bands split at 120 Hz and 2.5 kHz, with stereo-linked detectors,
4:1 upward and 10:1 downward compression at 100%. Upward gain is capped at
24 dB at 100% and 48 dB at 200%, fading out near silence. Controls and bypass
are smoothed; depth zero is dry. Auto-gain uses a 300 ms energy average and
150 ms gain smoothing to preserve transient dynamics. Correction is bounded
to -48/+24 dB and holds during silence. It approximates the input RMS level,
not perceived loudness or peak level. The existing bus limiter remains active.

All three instruments protect module boundaries at -1 dBFS sample peak: the FX
input, each insert output, and delay feedback writes. DR-1 additionally protects
summed reverb sends and the combined dry/reverb bus input. Stereo-linked gain reduction catches overloads immediately
and releases over 80 ms without adding latency. Signals below the ceiling pass
unchanged when no earlier overload is still releasing. Intentional DRIVE/filter
saturation remains available. This is sample-peak protection, not true-peak
limiting; the final bus lookahead limiter remains in place.

`src/engine/ott-worklet.js` contains the reusable DSP components. Load it with
`audioWorklet.addModule()` before an instrument module, then construct
`new globalThis.FableOttCompressor(sampleRate)`. It exposes
`setParams(on, depth, time, up, down)`, `process(L, R, n)` (in-place),
and `reset()`. Processing allocates no buffers and adds no lookahead latency.
`processSample(l, r)` exposes output through `.l` and `.r` for sample loops.
`FableCompressor` provides the same processing methods with
`setParams(on, thresholdDb)`. `FableAutoGain` and `FablePeakGuard` supply the
shared automatic level compensation and boundary protection.

Web presets, pad patches, kits, and session exports retain the new settings.
Native DSP does not yet implement OTT or BL-1 COMP; the native song generator
strips these bypassed parameters and rejects enabled unsupported stages.
Existing generated native presets remain unchanged. Legacy
`fx.ott.gain` and `fx.comp.gain` values are retained in saved patches for
compatibility but no longer affect web audio. Native COMP still uses its
existing makeup behavior and stage ordering; automatic gain and the aligned
chain currently apply only on web.
