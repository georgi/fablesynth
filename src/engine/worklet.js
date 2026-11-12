// Melodic POLY web prototype; mirrored in shared/sequenceRhythm.ts.
function readSequenceRhythm(r) {
  if (!r || r.v !== 1 || typeof r.enabled !== 'boolean' || !['grid', 'fit'].includes(r.mode)
    || !Number.isInteger(r.steps) || r.steps < 1 || r.steps > 16
    || !Number.isInteger(r.rotation) || r.rotation < 0 || r.rotation >= r.steps
    || ![4, 8].includes(r.cycleBeats)) return null;
  return { v: 1, enabled: r.enabled, mode: r.mode, steps: r.steps, rotation: r.rotation, cycleBeats: r.cycleBeats };
}
// FableSynth DSP core — runs in the AudioWorklet thread. Self-contained (no imports).
// Protocol (port messages in):
//   {t:'init', params:{id:value,...}}
//   {t:'tables', list:[{frames,mips,size,buf:ArrayBuffer}]}   full pool
//   {t:'table', i, frames, mips, size, buf}   one slot (buf transferred)
//   {t:'tablecount', n}                       resize the slot list
//   {t:'p', k, v}                    single param change (incl. mat{n}.src/.dst/.amt)
//   {t:'on', n, v} {t:'off', n}      note events (n=midi note, v=0..1)
//   {t:'bend', s}                    pitch bend in semitones
//   {t:'panic'}
//   {t:'pats', data:Uint8Array} {t:'chain', list} {t:'play'} {t:'stop'}   note sequencer
//   {t:'host',on} {t:'tempo',bpm,swing,anchor} {t:'clip',data,bars,atFrame}
//   {t:'clipstop',atFrame}                     hosted clip transport (SQ-4)
//   {t:'auto',lanes}                           clip automation tables (SQ-4)
//
// Modulation is a fixed pool of 16 slots (mat1..mat16), each {src,dst,amt}, read
// straight from `this.p` — no separate routing message. The per-destination
// scaling matches the VST exactly, so the two engines sound identical.
// Out: {t:'viz', a, b, n}            modulated wt positions + active voice count
//      {t:'mod', d}                  live per-destination route sums for the UI's
//                                    knob indicators: d[MOD_DESTS index] = sum x
//                                    (Float32Array, viz cadence, only while a
//                                    routed voice sounds; d=null once when idle)
//      {t:'step', s, pat}            per step while the sequencer plays
//      {t:'clipstart', frame} {t:'clipstop', frame} {t:'pos', step, bar}   hosted

const NVOICES = 8;
function readClipArp(a) {
  // 16 notes, or a chord-track cycle (a multiple of 16) whose rhythm lanes repeat.
  if (!a || !Array.isArray(a.notes) || a.notes.length < 16 || a.notes.length > 1024 || a.notes.length % 16 ||
      !Array.isArray(a.hits) || !Array.isArray(a.accents) ||
      !Number.isFinite(a.rate) || !Number.isFinite(a.gate)) return null;
  return { notes: a.notes.map(n => Number.isInteger(n) && n >= 0 && n <= 127 ? n : -1),
    hits: a.hits.slice(0, 16), accents: a.accents.slice(0, 16), ...readCompStack(a),
    rate: Math.max(.125, Math.min(1, a.rate)), gate: Math.max(.05, Math.min(.95, a.gate)) };
}
// Comp style (src/comp.ts): a voicing per step and its length in steps; a
// length of 0 marks a step a tie holds. Malformed stacks fall back to `notes`.
function readCompStack(a) {
  const n = a.notes.length;
  if (!Array.isArray(a.stack) || !Array.isArray(a.lens) || a.stack.length !== n || a.lens.length !== n) return {};
  const stack = a.stack.map(s => Array.isArray(s) ? s.filter(x => Number.isInteger(x) && x >= 0 && x <= 127).slice(0, 8) : []);
  const lens = a.lens.map(l => Number.isInteger(l) && l >= 0 && l <= n ? l : 1);
  return { stack, lens, strum: Number.isFinite(a.strum) ? Math.max(0, Math.min(60, a.strum)) : 0 };
}
// Scale mode (src/scales.ts scaleMap): up to 64 sixteenths, each a remap table
// of 12 semitone offsets by pitch class. The map loops; one table = a static scale.
function readScaleMap(m) {
  if (!Array.isArray(m) || !m.length || m.length > 64) return null;
  const ok = m.every(t => Array.isArray(t) && t.length === 12 && t.every(x => Number.isInteger(x) && x > -12 && x < 12));
  return ok ? m.map(t => t.slice()) : null;
}
function scaleNote(map, pos, note) {
  if (!map) return note;
  const t = map[((Math.floor(pos) % map.length) + map.length) % map.length];
  return Math.max(0, Math.min(127, note + t[note % 12]));
}
const MAXUNI = 16;
// Fixed modulation pool: mat1..mat16, each {src,dst,amt}. Mirrors MOD_MATRIX_SIZE
// in the params/slot helpers and the VST's MOD_MATRIX_SIZE.
const MOD_MATRIX_SIZE = 16;

// Note sequencer constants — hand-copied from noteseq.ts (this module is
// import-free); the parity test asserts they still match.
const SEQ_STEPS = 16;
const SEQ_NPATTERNS = 4;
const SEQ_STRIDE = 3;
const WT_POLY_LANES = 8;
const SEQ_ACCENT_VEL = 1.0;
const SEQ_PLAIN_VEL = 0.72;
const SEQ_SWING_MAX = 0.667;
// Shared DSP primitives (ott-worklet.js, loaded first).
const {
  ADAA_FADE_WIDTH, DRIVE_OS_RAMP_SEC, lcosh, adaaInput, makeDriveOs, resetDriveOs, copyDriveOs,
  driveOsStep, rdH, subSquareTable, subSquareMip, subSquareRead, besselI0, LfoClock, Biquad,
  DelayLine, LookaheadLimiter, FvComb, FvAllpass, PlateReverb, lfoShapeValue, mixGate,
} = globalThis.FableDsp;

// Generic modulation log-curve depth: a full route swings a Log param ×2^(x·D),
// i.e. ±D octaves. D=5 reproduces the legacy CUTOFF scaling exactly. Mirrors the
// engine's `std::pow(2, x*5)` and the design contract (D=5).
const MOD_LOG_D = 5;

// Canonical dst index -> target. Mirrors dstTarget() in params.ts / Params.h
// index-for-index. Per-param dests hold the modulated paramId; the three globals
// and "none" hold a sentinel handled directly below. Globals keep their legacy
// math (pitch ±12 semis, amp gain, pan add); per-param dests fold into pm via the
// Lin/Log curve rule. Index 0 (none) is intentionally absent (skipped by src/dst
// guard). Keep in sync with MOD_DESTS.
const DST_PITCH = '\0pitch', DST_AMP = '\0amp', DST_PAN = '\0pan';
const DST_TARGET = [
  null,            // 0 — (unused; guarded out)
  'oscA.pos',      // 1  A POS
  'oscB.pos',      // 2  B POS
  'filter.cutoff', // 3  F1 CUT
  DST_PITCH,       // 4  PITCH (global)
  DST_AMP,         // 5  AMP   (global)
  DST_PAN,         // 6  PAN   (global)
  'oscA.level',    // 7  A LVL
  'oscB.level',    // 8  B LVL
  'filter2.cutoff',// 9  F2 CUT
  'filter2.res',   // 10 F2 RES
  'oscA.detune',   // 11 A DETUNE
  'oscA.spread',   // 12 A SPREAD
  'oscA.pan',      // 13 A PAN
  'oscB.detune',   // 14 B DETUNE
  'oscB.spread',   // 15 B SPREAD
  'oscB.pan',      // 16 B PAN
  'filter.res',    // 17 F1 RES
  'filter.drive',  // 18 F1 DRIVE
  'filter.env',    // 19 F1 ENV
  'filter.key',    // 20 F1 KEY
  'filter2.drive', // 21 F2 DRIVE
  'filter2.env',   // 22 F2 ENV
  'filter2.key',   // 23 F2 KEY
  'sub.level',     // 24 SUB LVL
  'noise.level',   // 25 NOISE LVL
  'oscA.blend',    // 26 A BLEND
  'oscB.blend',    // 27 B BLEND
  'oscA.warpamt',  // 28 A WARP
  'oscB.warpamt',  // 29 B WARP
  'lfo1.rate',     // 30 LFO1 RATE
  'lfo2.rate',     // 31 LFO2 RATE
  'lfo3.rate',     // 32 LFO3 RATE
  'lfo4.rate',     // 33 LFO4 RATE
  'env1.a',        // 34 AMP ATK
  'env1.d',        // 35 AMP DEC
  'env1.r',        // 36 AMP REL
  'env2.a',        // 37 MENV ATK
  'env2.d',        // 38 MENV DEC
  'env2.r',        // 39 MENV REL
  'env3.a',        // 40 MENV2 ATK
  'env3.d',        // 41 MENV2 DEC
  'env3.r',        // 42 MENV2 REL
  'fx.drive.amt',  // 43 DRIVE AMT
  'fx.drive.mix',  // 44 DRIVE MIX
  'fx.chorus.mix', // 45 CHORUS MIX
  'fx.delay.fb',   // 46 DELAY FDBK
  'fx.delay.mix',  // 47 DELAY MIX
  'fx.reverb.mix', // 48 REVERB MIX
  'mat1.amt',      // 49 MOD 1 AMT
  'mat2.amt',      // 50 MOD 2 AMT
  'mat3.amt',      // 51 MOD 3 AMT
  'mat4.amt',      // 52 MOD 4 AMT
  'mat5.amt',      // 53 MOD 5 AMT
  'mat6.amt',      // 54 MOD 6 AMT
  'mat7.amt',      // 55 MOD 7 AMT
  'mat8.amt',      // 56 MOD 8 AMT
  'mat9.amt',      // 57 MOD 9 AMT
  'mat10.amt',     // 58 MOD 10 AMT
  'mat11.amt',     // 59 MOD 11 AMT
  'mat12.amt',     // 60 MOD 12 AMT
  'mat13.amt',     // 61 MOD 13 AMT
  'mat14.amt',     // 62 MOD 14 AMT
  'mat15.amt',     // 63 MOD 15 AMT
  'mat16.amt',     // 64 MOD 16 AMT
];

// Per-param curve + range for every modulatable target, mirroring PARAM_DEFS in
// params.ts (curve + min/max). Used to fold a route sum x into the modulated value:
//   Lin: pm = p + x·(hi−lo)   (width-1 lin reproduces POS/LEVEL/RES exactly)
//   Log: pm = p · 2^(x·D)     (D=5 reproduces CUTOFF exactly)
// Kept local because the worklet runs in the render thread with no imports.
const MOD_PARAM_INFO = {
  'oscA.pos':       { curve: 'lin', lo: 0, hi: 1 },
  'oscB.pos':       { curve: 'lin', lo: 0, hi: 1 },
  'oscA.level':     { curve: 'lin', lo: 0, hi: 1 },
  'oscB.level':     { curve: 'lin', lo: 0, hi: 1 },
  'oscA.detune':    { curve: 'lin', lo: 0, hi: 1 },
  'oscB.detune':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscB.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.blend':     { curve: 'lin', lo: 0, hi: 1 },
  'oscB.blend':     { curve: 'lin', lo: 0, hi: 1 },
  'oscA.pan':       { curve: 'lin', lo: -1, hi: 1 },
  'oscB.pan':       { curve: 'lin', lo: -1, hi: 1 },
  'filter.cutoff':  { curve: 'log', lo: 20, hi: 20000 },
  'filter2.cutoff': { curve: 'log', lo: 20, hi: 20000 },
  'filter.res':     { curve: 'lin', lo: 0, hi: 1 },
  'filter2.res':    { curve: 'lin', lo: 0, hi: 1 },
  'filter.drive':   { curve: 'lin', lo: 0, hi: 1 },
  'filter2.drive':  { curve: 'lin', lo: 0, hi: 1 },
  'filter.env':     { curve: 'lin', lo: -1, hi: 1 },
  'filter2.env':    { curve: 'lin', lo: -1, hi: 1 },
  'filter.key':     { curve: 'lin', lo: 0, hi: 1 },
  'filter2.key':    { curve: 'lin', lo: 0, hi: 1 },
  'sub.level':      { curve: 'lin', lo: 0, hi: 1 },
  'noise.level':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.warpamt':   { curve: 'lin', lo: 0, hi: 1 },
  'oscB.warpamt':   { curve: 'lin', lo: 0, hi: 1 },
  'lfo1.rate':      { curve: 'log', lo: 0.02, hi: 30 },
  'lfo2.rate':      { curve: 'log', lo: 0.02, hi: 30 },
  'lfo3.rate':      { curve: 'log', lo: 0.02, hi: 30 },
  'lfo4.rate':      { curve: 'log', lo: 0.02, hi: 30 },
  'env1.a':         { curve: 'log', lo: 0.001, hi: 8 },
  'env1.d':         { curve: 'log', lo: 0.005, hi: 10 },
  'env1.r':         { curve: 'log', lo: 0.005, hi: 12 },
  'env2.a':         { curve: 'log', lo: 0.001, hi: 8 },
  'env2.d':         { curve: 'log', lo: 0.005, hi: 10 },
  'env2.r':         { curve: 'log', lo: 0.005, hi: 12 },
  'env3.a':         { curve: 'log', lo: 0.001, hi: 8 },
  'env3.d':         { curve: 'log', lo: 0.005, hi: 10 },
  'env3.r':         { curve: 'log', lo: 0.005, hi: 12 },
  'fx.drive.amt':   { curve: 'lin', lo: 0, hi: 1 },
  'fx.drive.mix':   { curve: 'lin', lo: 0, hi: 1 },
  'fx.chorus.mix':  { curve: 'lin', lo: 0, hi: 1 },
  'fx.delay.fb':    { curve: 'lin', lo: 0, hi: 0.92 },
  'fx.delay.mix':   { curve: 'lin', lo: 0, hi: 1 },
  'fx.reverb.mix':  { curve: 'lin', lo: 0, hi: 1 },
  'mat1.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat2.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat3.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat4.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat5.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat6.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat7.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat8.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat9.amt':       { curve: 'lin', lo: -1, hi: 1 },
  'mat10.amt':      { curve: 'lin', lo: -1, hi: 1 },
  'mat11.amt':      { curve: 'lin', lo: -1, hi: 1 },
  'mat12.amt':      { curve: 'lin', lo: -1, hi: 1 },
  'mat13.amt':      { curve: 'lin', lo: -1, hi: 1 },
  'mat14.amt':      { curve: 'lin', lo: -1, hi: 1 },
  'mat15.amt':      { curve: 'lin', lo: -1, hi: 1 },
  'mat16.amt':      { curve: 'lin', lo: -1, hi: 1 },
};
// ---------- flat parameter store (mirrors juce/source/dsp/Params.h) ----------
// The render thread must never build a string key (finding W2): every parameter
// lives in a Float64Array indexed by an integer id, and `{t:'p'}` messages are
// resolved to that index once, in onMsg. PARAM_IDS mirrors PARAM_DEFS in
// params.ts index-for-index; the parity test asserts the two lists still match.
const PARAM_IDS = [];
const addOsc = (pre) => PARAM_IDS.push(
  pre + '.on', pre + '.table', pre + '.pos', pre + '.oct', pre + '.semi', pre + '.fine',
  pre + '.unison', pre + '.detune', pre + '.spread', pre + '.blend', pre + '.level', pre + '.pan');
const addFilter = (pre) => PARAM_IDS.push(
  pre + '.on', pre + '.type', pre + '.cutoff', pre + '.res', pre + '.drive', pre + '.env', pre + '.key');
const addLfo = (pre) => PARAM_IDS.push(
  pre + '.shape', pre + '.rate', pre + '.sync', pre + '.syncrate', pre + '.rise', pre + '.phase', pre + '.retrig');
addOsc('oscA'); addOsc('oscB');
PARAM_IDS.push('sub.on', 'sub.shape', 'sub.oct', 'sub.level');
PARAM_IDS.push('noise.on', 'noise.type', 'noise.level');
addFilter('filter');
PARAM_IDS.push('filter.route');
addFilter('filter2');
PARAM_IDS.push('env1.a', 'env1.d', 'env1.s', 'env1.r');
PARAM_IDS.push('env2.a', 'env2.d', 'env2.s', 'env2.r');
addLfo('lfo1'); addLfo('lfo2');
for (let i = 1; i <= MOD_MATRIX_SIZE; i++) PARAM_IDS.push('mat' + i + '.src', 'mat' + i + '.dst', 'mat' + i + '.amt');
PARAM_IDS.push('fx.eq.on', 'fx.eq.low', 'fx.eq.mid', 'fx.eq.mfreq', 'fx.eq.high');
PARAM_IDS.push('fx.drive.on', 'fx.drive.amt', 'fx.drive.mix');
PARAM_IDS.push('fx.chorus.on', 'fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix');
PARAM_IDS.push('fx.delay.on', 'fx.delay.time', 'fx.delay.fb', 'fx.delay.mix');
PARAM_IDS.push('fx.reverb.on', 'fx.reverb.size', 'fx.reverb.mix');
PARAM_IDS.push('fx.comp.on', 'fx.comp.thr', 'fx.comp.gain');
PARAM_IDS.push('master.volume', 'master.glide', 'master.mono');
PARAM_IDS.push('seq.bpm', 'seq.swing', 'seq.root');
PARAM_IDS.push('fx.ott.on', 'fx.ott.depth', 'fx.ott.time', 'fx.ott.up', 'fx.ott.down');
PARAM_IDS.push('fx.eq.lfreq', 'fx.eq.m2freq', 'fx.eq.hfreq', 'fx.eq.mid2');
for (const band of ['l', 'm', 'm2', 'h']) PARAM_IDS.push(`fx.eq.${band}q`, `fx.eq.${band}type`, `fx.eq.${band}on`);
PARAM_IDS.push('fx.delay.tone', 'fx.delay.sat', 'fx.delay.wow', 'fx.delay.flutter', 'fx.delay.width', 'fx.delay.mode', 'fx.delay.sync', 'fx.delay.div');

PARAM_IDS.push('fx.comp.att', 'fx.comp.rel', 'fx.comp.ratio', 'fx.drive.tone', 'fx.drive.type');
// LAB page (lab-worklet.js), in params.ts order.
const LAB_FIELDS = {
  crush: ['on', 'bits', 'rate', 'chaos', 'mix'], reso: ['on', 'note', 'chord', 'decay', 'mix'],
  shift: ['on', 'hz', 'fb', 'spread', 'mix'], spray: ['on', 'pitch', 'density', 'scatter', 'mix'],
  glitch: ['on', 'div', 'chance', 'drift', 'mix'],
};
for (const [stage, fields] of Object.entries(LAB_FIELDS)) for (const f of fields) PARAM_IDS.push(`fx.${stage}.${f}`);
// Per-route polarity (params.ts MOD_POLARITY), appended after LAB.
for (let i = 1; i <= MOD_MATRIX_SIZE; i++) PARAM_IDS.push('mat' + i + '.pol');
// Serum-parity modulators (params.ts), appended after polarity.
addLfo('lfo3'); addLfo('lfo4');
PARAM_IDS.push('env3.a', 'env3.d', 'env3.s', 'env3.r');
PARAM_IDS.push('macro1', 'macro2', 'macro3', 'macro4', 'perf.modwheel', 'perf.aftertouch');
for (let i = 1; i <= MOD_MATRIX_SIZE; i++) PARAM_IDS.push('mat' + i + '.aux');
for (let n = 1; n <= 3; n++) PARAM_IDS.push(`env${n}.model`, `env${n}.h`, `env${n}.ac`, `env${n}.dc`, `env${n}.rc`);
PARAM_IDS.push('master.legato');
PARAM_IDS.push('fx.comp.sc', 'fx.comp.schp');
// Oscillator warp and the reverb algorithm (params.ts), appended.
PARAM_IDS.push('oscA.warp', 'oscA.warpamt', 'oscB.warp', 'oscB.warpamt');
PARAM_IDS.push('fx.reverb.type', 'fx.reverb.damp');
// Response curve per route (params.ts routeCurve).
for (let i = 1; i <= MOD_MATRIX_SIZE; i++) PARAM_IDS.push('mat' + i + '.crv');
// Drawn LFO shapes (params.ts): one-shot, point count, then x/y/c per point.
const LFO_MAX_POINTS = 16;
for (let n = 1; n <= 4; n++) {
  PARAM_IDS.push(`lfo${n}.oneshot`, `lfo${n}.pn`);
  for (let k = 1; k <= LFO_MAX_POINTS; k++) PARAM_IDS.push(`lfo${n}.p${k}.x`, `lfo${n}.p${k}.y`, `lfo${n}.p${k}.c`);
}
const NUM_PARAMS = PARAM_IDS.length;
const PID = Object.create(null);
for (let i = 0; i < NUM_PARAMS; i++) PID[PARAM_IDS[i]] = i;
// Settings object keys (LabSettings in LabFx.h) for each LAB parameter index.
const LAB_KEYS = Object.entries(LAB_FIELDS).flatMap(([stage, fields]) =>
  fields.map(f => [PID[`fx.${stage}.${f}`], stage + f[0].toUpperCase() + f.slice(1), f === 'on']));
const EQ_BANDS = [
  ['low', 'lfreq', 'l'], ['mid', 'mfreq', 'm'],
  ['mid2', 'm2freq', 'm2'], ['high', 'hfreq', 'h'],
].map(([gain, freq, pre]) => [gain, freq, `${pre}q`, `${pre}type`, `${pre}on`].map(key => PID[`fx.eq.${key}`]));

// Offsets inside the repeated osc / filter / LFO / mat groups above.
const O_ON = 0, O_TABLE = 1, O_POS = 2, O_OCT = 3, O_SEMI = 4, O_FINE = 5,
      O_UNI = 6, O_DET = 7, O_SPR = 8, O_BLEND = 9, O_LEVEL = 10, O_PAN = 11;
const F_ON = 0, F_TYPE = 1, F_CUT = 2, F_RES = 3, F_DRIVE = 4, F_ENV = 5, F_KEY = 6;
const L_SHAPE = 0, L_RATE = 1, L_SYNC = 2, L_SYNCRATE = 3, L_RISE = 4, L_PHASE = 5, L_RETRIG = 6;
const M_SRC = 0, M_DST = 1, M_AMT = 2, M_STRIDE = 3;

const OSCA = PID['oscA.on'], OSCB = PID['oscB.on'];
// Warp block per oscillator: mode at +0, amount at +1.
const WARPA = PID['oscA.warp'], WARPB = PID['oscB.warp'];
// Warp modes (params.ts WARP_MODES). Modes from WARP_FM up read the other
// oscillator as a modulator. Mirrors the WarpMode enum in Engine.h.
const WARP_SYNC = 1, WARP_SYNCWIN = 2, WARP_BEND = 3, WARP_ASYM = 4, WARP_PWM = 5,
      WARP_MIRROR = 6, WARP_QUANT = 7, WARP_FM = 8, WARP_AM = 9, WARP_RM = 10;
// FM depth at full amount, in carrier cycles (2 cycles = 4*pi radians).
const FM_MAX_CYCLES = 2;
// Level trim that keeps the Hann-windowed sync near the plain oscillator's RMS.
const SYNCWIN_GAIN = 1.63;
// Hard sync: crossfade length at the reset (samples), the mip headroom that
// keeps the crossfade's sidebands below Nyquist, and the slave pitch ceiling
// in cycles per sample.
const SYNC_FADE = 4, SYNC_HEADROOM = 1.5, SYNC_MAX_CPS = 0.25;
// The warped read can run faster than the oscillator, so the mip is chosen for
// the fastest read. This caps the factor (4 octaves of table band-limit).
const WARP_MIP_MAX = 16;
const FLT1 = PID['filter.on'], FLT2 = PID['filter2.on'];
const F1_CUT = FLT1 + F_CUT, F2_CUT = FLT2 + F_CUT;
const FILTER_ROUTE = PID['filter.route'];
const LFO1 = PID['lfo1.shape'], LFO2 = PID['lfo2.shape'], LFO3 = PID['lfo3.shape'], LFO4 = PID['lfo4.shape'];
// Drawn-shape block per LFO: one-shot at +0, point count at +1, points from +2.
const LFOX1 = PID['lfo1.oneshot'], LFOX2 = PID['lfo2.oneshot'], LFOX3 = PID['lfo3.oneshot'], LFOX4 = PID['lfo4.oneshot'];
const LFO_CUSTOM = 5;
// A one-shot LFO stops just before the end of its cycle and holds that value.
const LFO_ONESHOT_END = 1 - 1e-9;
const MAT1 = PID['mat1.src'], MATPOL1 = PID['mat1.pol'], MATAUX1 = PID['mat1.aux'], MATCRV1 = PID['mat1.crv'];
const MACRO1 = PID['macro1'], PERF_MW = PID['perf.modwheel'], PERF_AT = PID['perf.aftertouch'];
// Native source polarity per MOD_SOURCES index (params.ts SOURCE_BIPOLAR).
const SOURCE_BIPOLAR = [false, true, true, false, false, true, true, true, false, false, false, false, false, false, false, false];
const NUM_SOURCES = SOURCE_BIPOLAR.length;
const SUB_ON = PID['sub.on'], SUB_SHAPE = PID['sub.shape'], SUB_OCT = PID['sub.oct'], SUB_LEVEL = PID['sub.level'];
const NOISE_ON = PID['noise.on'], NOISE_TYPE = PID['noise.type'], NOISE_LEVEL = PID['noise.level'];
const ENV1_A = PID['env1.a'], ENV2_A = PID['env2.a'], ENV3_A = PID['env3.a'];
const ENVX1 = PID['env1.model'], ENVX2 = PID['env2.model'], ENVX3 = PID['env3.model'];
const MASTER_GLIDE = PID['master.glide'], MASTER_MONO = PID['master.mono'], MASTER_LEGATO = PID['master.legato'];
const SEQ_BPM = PID['seq.bpm'], SEQ_SWING = PID['seq.swing'], SEQ_ROOT = PID['seq.root'];
// FX + master param indices (the FX chain moved into the worklet, finding W6).
const FXEQ_ON = PID['fx.eq.on'];
const FXDRIVE_ON = PID['fx.drive.on'], FXDRIVE_AMT = PID['fx.drive.amt'], FXDRIVE_MIX = PID['fx.drive.mix'];
const FXCHORUS_ON = PID['fx.chorus.on'], FXCHORUS_RATE = PID['fx.chorus.rate'],
      FXCHORUS_DEPTH = PID['fx.chorus.depth'], FXCHORUS_MIX = PID['fx.chorus.mix'];
const FXDELAY_ON = PID['fx.delay.on'], FXDELAY_TIME = PID['fx.delay.time'],
      FXDELAY_FB = PID['fx.delay.fb'], FXDELAY_MIX = PID['fx.delay.mix'];
const FXDELAY_TONE = PID['fx.delay.tone'], FXDELAY_SAT = PID['fx.delay.sat'],
      FXDELAY_WOW = PID['fx.delay.wow'], FXDELAY_FLUTTER = PID['fx.delay.flutter'],
      FXDELAY_WIDTH = PID['fx.delay.width'], FXDELAY_MODE = PID['fx.delay.mode'],
      FXDELAY_SYNC = PID['fx.delay.sync'], FXDELAY_DIV = PID['fx.delay.div'];
const ECHO_DIVISIONS = [1, 1.5, 0.5, 0.75, 1 / 3, 0.25];
const FXREVERB_ON = PID['fx.reverb.on'], FXREVERB_SIZE = PID['fx.reverb.size'], FXREVERB_MIX = PID['fx.reverb.mix'];
const FXREVERB_TYPE = PID['fx.reverb.type'], FXREVERB_DAMP = PID['fx.reverb.damp'];
// PLATE wet trim, so a type switch keeps the wet level. At SIZE 0.8 the tank
// is 2.6 dB louder than the ROOM network under a held sound and 4.6 dB louder
// in the tail; the trim sits between the two. Mirrors Fx.cpp.
const PLATE_GAIN = 0.66;
const FXCOMP_ON = PID['fx.comp.on'], FXCOMP_THR = PID['fx.comp.thr'], FXCOMP_GAIN = PID['fx.comp.gain'];
const FXOTT_ON = PID['fx.ott.on'], FXOTT_DEPTH = PID['fx.ott.depth'], FXOTT_TIME = PID['fx.ott.time'],
      FXOTT_UP = PID['fx.ott.up'], FXOTT_DOWN = PID['fx.ott.down'];
const MASTER_VOLUME = PID['master.volume'];

// dst index -> param index, or one of these sentinels for the three globals.
const D_NONE = -1, D_PITCH = -2, D_AMP = -3, D_PAN = -4;
const DST_PIDX = new Int32Array(DST_TARGET.length).fill(D_NONE);
// param index -> MOD_DESTS index (0 = not a destination). Feeds the live-mod
// telemetry snapshot; globals and slot 0 have no owning knob and stay 0.
const PIDX_DST = new Int32Array(NUM_PARAMS);
for (let i = 1; i < DST_TARGET.length; i++) {
  const t = DST_TARGET[i];
  if (t === DST_PITCH) DST_PIDX[i] = D_PITCH;
  else if (t === DST_AMP) DST_PIDX[i] = D_AMP;
  else if (t === DST_PAN) DST_PIDX[i] = D_PAN;
  else if (typeof t === 'string' && PID[t] !== undefined) { DST_PIDX[i] = PID[t]; PIDX_DST[PID[t]] = i; }
}
// param index -> mod curve (0 none, 1 lin, 2 log) + Lin span (hi-lo) + range.
const MOD_CURVE = new Uint8Array(NUM_PARAMS);
const MOD_SPAN = new Float64Array(NUM_PARAMS);
const MOD_LO = new Float64Array(NUM_PARAMS), MOD_HI = new Float64Array(NUM_PARAMS);
for (const id in MOD_PARAM_INFO) {
  const inf = MOD_PARAM_INFO[id], i = PID[id];
  MOD_CURVE[i] = inf.curve === 'log' ? 2 : 1;
  MOD_SPAN[i] = inf.hi - inf.lo;
  MOD_LO[i] = inf.lo; MOD_HI[i] = inf.hi;
}
// dst index -> route (0-based) whose amount it modulates ("MOD n AMT"), or -1.
const DST_AMT_SLOT = new Int8Array(DST_TARGET.length).fill(-1);
for (let i = 1; i < DST_TARGET.length; i++) {
  const m = /^mat(\d+)\.amt$/.exec(DST_TARGET[i] || '');
  if (m) DST_AMT_SLOT[i] = +m[1] - 1;
}
// Destinations outside the voice: the FX controls and the LFO rates of the
// free-running LFOs. They follow the newest held note (the viz voice).
const FX_MOD_PIDS = [FXDRIVE_AMT, FXDRIVE_MIX, FXCHORUS_MIX, FXDELAY_FB, FXDELAY_MIX, FXREVERB_MIX];
const LFO_RATE_PIDS = [LFO1 + L_RATE, LFO2 + L_RATE, LFO3 + L_RATE, LFO4 + L_RATE];
const GLOBAL_DST = new Uint8Array(NUM_PARAMS);
for (const pid of FX_MOD_PIDS) GLOBAL_DST[pid] = 1;
for (const pid of LFO_RATE_PIDS) GLOBAL_DST[pid] = 1;
const clampTo = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

// LP24 resonance taper (finding B3). The cascade used to run two stages at the
// SAME damping k, so the peak at fc was (1/k)^2 — two coincident resonances —
// and with k = 2 - 1.93*res bottoming out at 0.071 the filter only reached
// Q ~= 14 and never rang. The resonance now lives in stage 1 alone and stage 2
// stays critically damped:
//   resT = res + 0.0035*res^4
//   k1   = max(LP24_K_MIN, 0.5*(2 - 1.93*resT)^2),  k2 = 2
// 1/(k1*k2) reproduces the old (1/k)^2 magnitude at fc to within 1.3 dB across
// the whole knob and to within 0.03 dB up to res 0.9, so presets keep their
// timbre, while the single resonant stage's Q climbs from 14 to ~470 at the top
// — a filter that sings instead of merely peaking. Identical to Engine.cpp.
const LP24_K_MIN = 0.002; // Q = 500 damping floor
// Declick slew on the amp env (s). Rounds the corners of a fast attack,
// release or steal fade without audibly softening the transient.
const AMP_SLEW_SEC = 0.0003;
// Velocity gain slew (s). A mono retrigger with a new velocity steps the
// gain of a sounding voice; 4 ms removes that click. A note from silence
// (or after a steal fade) starts at its own gain.
const VEL_SLEW_SEC = 0.004;

// Longest tuned-comb delay. Match native's sample-rate-aware capacity so the
// 20 Hz lower cutoff remains representable at 96 kHz and above.
const COMB_MAX = Math.max(4096, Math.ceil(sampleRate / 20) + 2);

// LFO note-division factors (cycles per beat, beat = quarter note). Index maps
// to params.ts LFO_DIVS.
const LFO_DIV_F = [0.25, 0.5, 1, 2 / 3, 1.5, 2, 4 / 3, 3, 4, 6, 8];

// Vowel formants (A-E-I-O-U), reused from the VOX wavetable voicing. The VOWEL
// filter is a 3-band bandpass bank tuned to these, morphed by the CUTOFF knob.
const VOWELS = [
  [730, 1090, 2440],
  [530, 1840, 2480],
  [390, 1990, 2550],
  [570, 840, 2410],
  [440, 1020, 2240],
];
const F_AMPS = [1, 0.55, 0.32];

// Sample-rate-invariant smoothing (finding W1). A coefficient is derived from a
// time constant instead of being a fixed per-block/per-sample number, so the
// smoothing TIME is the same at 44.1, 48 and 96 kHz. The taus below are chosen
// to reproduce the legacy constants exactly at the 48 kHz reference:
//   POS   0.35 per 128 samples, CUT 0.5 per 128 samples.
// Mirrors Engine.cpp's smoothCoef/POS_TAU/CUT_TAU.
function smoothCoef(n, tauSr) { return 1 - Math.exp(-n / tauSr); }
const POS_TAU = 128 / (48000 * 0.4307829160924542); // -ln(0.65)
const CUT_TAU = 128 / (48000 * Math.LN2);
// Voice-steal fade length to -80 dB (finding J6); mirrors STEAL_FADE_SEC in
// Engine.cpp: ln(1e-4) = -9.2103 over the fade, sample-rate invariant.
const STEAL_FADE_SEC = 0.004;
const STEAL_C = 1 - Math.exp(-9.210340371976184 / (STEAL_FADE_SEC * sampleRate));

// Fast deterministic RNG (xorshift32) — replaces Math.random() for noise, unison
// start phases and S&H (finding W5). Mirrors `Rng` in Engine.h, so a seeded
// render is reproducible and comparable across the two engines.
class Rng {
  constructor(seed) { this.s = (seed >>> 0) || 0x9e3779b9; }
  next() {
    let s = this.s;
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    this.s = s;
    return (s >>> 8) * (1 / 16777216);
  }
}

// CURVE envelope segment shapes (Engine.cpp envCurveExp/envRise/envFall):
// c = 0.5 is linear and the exponent doubles every 12 % away from it.
function envCurveExp(c) { return Math.pow(2, Math.abs(c - 0.5) * 100 / 12); }
function envRise(x, c, p) { return c <= 0.5 ? Math.pow(x, p) : 1 - Math.pow(1 - x, p); }
function envFall(x, c, p) { return c >= 0.5 ? Math.pow(1 - x, p) : 1 - Math.pow(x, p); }

// Minimum mod-envelope attack on a mono retrigger (Env.softAttack).
const RETRIGGER_SOFT_SEC = 0.004;

class Env {
  constructor() {
    this.state = 0; this.level = 0; this.s = 0.8;
    this.ca = 0.01; this.cd = 0.001; this.cr = 0.001;
    this.caSoft = 0; this.iaSoft = 0; // per-trigger attack override, see softAttack
    // CURVE model: per-sample phase steps, curves, and segment start levels.
    this.model = 0; this.x = 0; this.from = 0;
    this.ia = 1; this.ih = 1; this.id = 1; this.ir = 1; this.hold = 0;
    this.ac = NaN; this.dc = NaN; this.rc = NaN; this.pa = 1; this.pd = 1; this.pr = 1; // NaN: compute on first set
    // Numeric cache key (finding W2): building `a+'|'+d+'|'+r` allocated a
    // cons-string per voice per block on the render thread.
    this._a = NaN; this._d = NaN; this._r = NaN;
  }
  // decay/release use tau = t/4.5 so the audible tail roughly matches the label
  set(a, d, s, r) {
    this.s = s;
    if (a !== this._a || d !== this._d || r !== this._r) {
      this._a = a; this._d = d; this._r = r;
      this.ca = 1 - Math.exp(-1 / (Math.max(0.0008, a) * sampleRate));
      this.cd = 1 - Math.exp(-1 / (Math.max(0.002, d / 4.5) * sampleRate));
      this.cr = 1 - Math.exp(-1 / (Math.max(0.002, r / 4.5) * sampleRate));
    }
  }
  // CURVE model settings; call after set() with the same times.
  setModel(model, a, d, r, hold, ac, dc, rc) {
    this.model = model | 0;
    if (!this.model) return;
    this.ia = 1 / Math.max(1, a * sampleRate); this.id = 1 / Math.max(1, d * sampleRate);
    this.ir = 1 / Math.max(1, r * sampleRate); this.hold = hold; this.ih = 1 / Math.max(1, hold * sampleRate);
    if (ac !== this.ac) { this.ac = ac; this.pa = envCurveExp(ac); }
    if (dc !== this.dc) { this.dc = dc; this.pd = envCurveExp(dc); }
    if (rc !== this.rc) { this.rc = rc; this.pr = envCurveExp(rc); }
  }
  trigger() { this.state = 1; this.x = 0; this.from = this.level; this.caSoft = 0; this.iaSoft = 0; }
  // Slows this attack to at least `sec` (mono retrigger under a sounding
  // voice): a 1 ms filter-envelope restart on a loud voice ticks, while a
  // fresh note's attack is masked by its amp envelope. Cleared when the
  // attack ends or on the next trigger.
  softAttack(sec) {
    if (this.state !== 1) return;
    this.caSoft = Math.min(this.ca, 1 - Math.exp(-1 / (sec * sampleRate)));
    this.iaSoft = Math.min(this.ia, 1 / Math.max(1, sec * sampleRate));
  }
  release() { if (this.state !== 0) { this.state = 4; this.x = 0; this.from = this.level; } }
  kill() { this.state = 0; this.level = 0; }
  // CURVE: 1 attack, 6 hold, 2 decay, 3 sustain, 4 release, 5 steal fade.
  processCurve() {
    switch (this.state) {
      case 1:
        this.x += this.iaSoft || this.ia;
        if (this.x >= 1) { this.level = 1; this.x = 0; this.iaSoft = 0; this.state = this.hold > 0 ? 6 : 2; }
        else this.level = this.from + (1 - this.from) * envRise(this.x, this.ac, this.pa);
        break;
      case 6:
        this.x += this.ih; this.level = 1;
        if (this.x >= 1) { this.x = 0; this.state = 2; }
        break;
      case 2:
        this.x += this.id;
        if (this.x >= 1) { this.level = this.s; this.state = 3; }
        else this.level = this.s + (1 - this.s) * envFall(this.x, this.dc, this.pd);
        break;
      case 3: this.level = this.s; break;
      case 4:
        this.x += this.ir;
        if (this.x >= 1) { this.level = 0; this.state = 0; }
        else this.level = this.from * envFall(this.x, this.rc, this.pr);
        break;
      case 5:
        this.level -= this.level * STEAL_C;
        if (this.level < 1e-4) { this.level = 0; this.state = 0; }
        break;
    }
    return this.level;
  }
  process() {
    if (this.model) return this.processCurve();
    switch (this.state) {
      case 1: {
        this.level += (1.45 - this.level) * (this.caSoft || this.ca);
        if (this.level >= 1) { this.level = 1; this.caSoft = 0; this.state = 2; }
        break;
      }
      case 2: {
        this.level += (this.s - this.level) * this.cd;
        if (this.level - this.s < 0.0005) this.state = 3;
        break;
      }
      case 3: this.level = this.s; break;
      case 4: {
        this.level -= this.level * this.cr;
        if (this.level < 1e-4) { this.level = 0; this.state = 0; }
        break;
      }
      case 5: {
        // steal fade: STEAL_FADE_SEC to silence at any sample rate, then the
        // voice is free for its pending note
        this.level -= this.level * STEAL_C;
        if (this.level < 1e-4) { this.level = 0; this.state = 0; }
        break;
      }
    }
    return this.level;
  }
  processBlock(n) { for (let i = 0; i < n; i++) this.process(); return this.level; }
}

class LFO {
  constructor() { this.clock = new LfoClock(); this.phase = 0; this.hold = 0; this.elapsed = 0; }
  reset(rng) { this.clock.reset(); this.phase = 0; this.hold = rng.next() * 2 - 1; this.elapsed = 0; }
  // Read the shape at a wrapped phase offset (for the start-phase control).
  // `store` and `x` locate the LFO's drawn shape: the flat parameter store
  // and the index of its one-shot param (count at +1, points from +2).
  valueOff(shape, off, store, x) {
    let p = this.phase + off; p -= Math.floor(p);
    switch (shape | 0) {
      case 0: return Math.sin(2 * Math.PI * p);
      case 1: return 1 - 4 * Math.abs(p - 0.5);
      case 2: return 1 - 2 * p;
      case 3: return p < 0.5 ? 1 : -1;
      case LFO_CUSTOM: return lfoShapeValue(store, x + 2, Math.min(LFO_MAX_POINTS, store[x + 1] | 0), p);
      default: return this.hold;
    }
  }
  // Fade-in gain, per-voice, keyed off note-on (samples since reset).
  riseGain(riseSec) { return riseSec <= 0 ? 1 : Math.min(1, this.elapsed / (riseSec * sampleRate)); }
  // `oneshot` runs a single cycle from the note start, then holds its end.
  advance(rate, n, rng, oneshot) {
    this.elapsed += n;
    // NaN guard: a non-finite rate (e.g. params not yet initialised when the
    // free-running global LFO advances) would latch phase to a sticky NaN.
    const d = (rate * n) / sampleRate;
    if (oneshot) { if (Number.isFinite(d)) this.phase = Math.min(LFO_ONESHOT_END, this.phase + d); return; }
    if (Number.isFinite(d)) this.phase += d;
    if (this.phase >= 1) { this.phase -= Math.floor(this.phase); this.hold = rng.next() * 2 - 1; }
  }
}

// Per-oscillator runtime state inside a voice.
// DC_R and the Kellet pink-noise poles below are 48 kHz reference values; the
// processor remaps them to the context rate in its constructor (finding W1), so
// the DC corner and the pink tilt are the same filter at any sample rate.
const DC_R = 0.9998; // ~3.5 Hz highpass — removes DC without touching bass
const PINK_P = [0.99886, 0.99332, 0.969, 0.8665, 0.55, 0.7616];
const PINK_G = [0.0555179, 0.0750759, 0.153852, 0.3104856, 0.5329522, 0.016898];

// Oscillator phase normally advances by less than one table length per sample,
// but defensive wrapping keeps a malformed/modulated increment from turning the
// table index into an out-of-range read. Mirrors Engine.cpp's wrapOscPhase.
function wrapOscPhase(phase, size) {
  if (!Number.isFinite(phase)) return 0;
  if (phase < 0 || phase >= size) {
    phase = phase % size;
    if (phase < 0) phase += size;
  }
  return phase;
}

const SUBSQ_MIP = { off: 0, offFine: 0, blend: 0 };

// Per-voice runtime state for one filter (both the persistent DSP state and the
// block-rate coefficients). Kept allocation-free so coef updates never trigger GC.
function makeFilterState() {
  return {
    svf: new Float64Array(8),   // SVF: 2 stages x 2 ch x (ic1, ic2)
    fmt: new Float64Array(12),  // formant: 2 ch x 3 bands x (s1, s2)
    combL: new Float32Array(COMB_MAX),
    combR: new Float32Array(COMB_MAX),
    combW: 0,
    cutSm: 0,
    osL: makeDriveOs(), osR: makeDriveOs(), adaaMix: 0, // drive oversampler + smoothed enable mix
    ftype: 0, twoPole: false,
    k1: 0,                      // SVF damping, stage 1 (a1..a3 ramp per sub-block)
    k2: 0,                      // SVF damping, stage 2 (LP24 only; = k1 elsewhere)
    cutTarget: 0, cutPrev: 0,   // runFilter ramps cutPrev -> cutTarget
    combLen: 1, combLenPrev: 0, combFb: 0, // comb coefs
    fc: new Float64Array(9),    // formant biquad coefs: 3 bands x (b0, a1, a2)
    famp: new Float64Array(3),
  };
}

function resetFilterState(fs) {
  fs.svf.fill(0); fs.fmt.fill(0);
  fs.combL.fill(0); fs.combR.fill(0); fs.combW = 0;
  fs.cutSm = 0; resetDriveOs(fs.osL); resetDriveOs(fs.osR); fs.adaaMix = 0;
  fs.cutPrev = 0; fs.combLenPrev = 0;
}

function makeOscState() {
  return {
    phases: new Float64Array(MAXUNI),
    incs: new Float64Array(MAXUNI),
    gl: new Float32Array(MAXUNI),
    gr: new Float32Array(MAXUNI),
    uni: 1, off0: 0, off1: 0, off0b: 0, off1b: 0, mipBlend: 0,
    ft: 0, gain: 0, mask: 0, size: 0, data: null, posSm: -1,
    // Previous chunk's targets — renderOsc ramps to this chunk's across n
    // samples so block-rate modulation has no staircase (finding W1).
    pIncs: new Float64Array(MAXUNI),
    pGl: new Float64Array(MAXUNI),
    pGr: new Float64Array(MAXUNI),
    pFt: 0, pOff0: -1, pUni: 0, havePrev: false,
    // Warp: mode, map parameter and mip factor for this chunk, and the
    // previous chunk's values for the per-sample ramp.
    warp: 0, warpW: 0, wf: 1, pWarp: 0, pWarpW: 0, pWf: 1,
    // Modulator role (the other oscillator's FM/AM/RM): a clean center voice
    // with its own phase. cinc is the undetuned increment; modOk marks the
    // table state as valid for this chunk.
    cinc: 0, pCinc: -1, mph: 0, modOk: false,
  };
}

class Voice {
  constructor() {
    this.note = 60; this.vel = 1; this.gate = false; this.age = 0;
    this.pitch = 60; this.velGain = 0;
    this.pending = null; // {n, vel, start, age} queued behind a steal fade (Env state 5)
    this.ampEnv = new Env(); this.modEnv = new Env(); this.modEnv2 = new Env();
    this.lfo1 = new LFO(); this.lfo2 = new LFO(); this.lfo3 = new LFO(); this.lfo4 = new LFO();
    this.rand = 0; // RANDOM source: one unipolar draw per note
    this.oA = makeOscState(); this.oB = makeOscState();
    this.subPhase = 0; this.subIncPrev = -1; this.ampFacPrev = -1;
    this.pb = [0, 0, 0, 0, 0, 0, 0]; // pink noise filter state
    this.f1 = makeFilterState(); this.f2 = makeFilterState();
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
    this.ampSm = 0; // declick-slewed amp env level (AMP_SLEW_SEC)
    this.velSm = -1; // slewed velGain (VEL_SLEW_SEC); -1 = note from silence
  }
  // A voice sounds until its slewed amp level is silent too.
  get active() { return this.ampEnv.state !== 0 || this.ampSm > 1e-4; }

  // `modRng` feeds LFO 3/4 and RANDOM. It is separate from `rng` so the
  // appended modulators leave every existing patch's random draws unchanged.
  noteOn(note, vel, startPitch, age, rng, phaseRandA, phaseRandB, modRng) {
    this.note = note; this.vel = vel; this.gate = true; this.age = age;
    this.pitch = startPitch;
    this.velGain = 0.25 + 0.75 * vel * vel; this.velSm = -1;
    this.ampEnv.trigger(); this.modEnv.trigger(); this.modEnv2.trigger();
    this.lfo1.reset(rng); this.lfo2.reset(rng);
    this.lfo3.reset(modRng); this.lfo4.reset(modRng); this.rand = modRng.next();
    for (let i = 0; i < MAXUNI; i++) {
      // Start phase is in SAMPLES (all tables are 2048 wide): scale the random
      // draw to a full cycle so unison voices (and osc A vs B) decorrelate.
      this.oA.phases[i] = phaseRandA ? rng.next() * 2048 : 0;
      this.oB.phases[i] = phaseRandB ? rng.next() * 2048 : 0;
    }
    this.oA.mph = this.oB.mph = 0; this.oA.pCinc = this.oB.pCinc = -1;
    this.oA.posSm = -1; this.oB.posSm = -1;
    // A fresh note has no previous chunk to ramp from.
    this.oA.havePrev = false; this.oB.havePrev = false;
    this.subPhase = 0; this.subIncPrev = -1; this.ampFacPrev = -1;
    resetFilterState(this.f1); resetFilterState(this.f2);
    this.dcxL = this.dcxR = this.dcyL = this.dcyR = 0;
  }
  // Mono note change without legato (Engine.cpp Voice::retrigger): restart the
  // envelopes, per-note LFOs and RANDOM; keep oscillator phase and filter state.
  retrigger(note, vel, rng, modRng) {
    this.note = note; this.vel = vel; this.velGain = 0.25 + 0.75 * vel * vel;
    this.ampEnv.trigger(); this.modEnv.trigger(); this.modEnv2.trigger();
    // The voice is sounding: restart the mod envelopes over RETRIGGER_SOFT_SEC.
    this.modEnv.softAttack(RETRIGGER_SOFT_SEC); this.modEnv2.softAttack(RETRIGGER_SOFT_SEC);
    this.lfo1.reset(rng); this.lfo2.reset(rng);
    this.lfo3.reset(modRng); this.lfo4.reset(modRng); this.rand = modRng.next();
  }
  noteOff() { this.gate = false; this.ampEnv.release(); this.modEnv.release(); this.modEnv2.release(); }
  kill() { this.gate = false; this.pending = null; this.ampEnv.kill(); this.modEnv.kill(); this.modEnv2.kill(); this.ampSm = 0; this.velSm = -1; }
}

// ---------- FX chain (finding W6) ----------
// A line-for-line port of the shared FX primitives into the worklet. The web
// chain runs EQ -> OTT -> compressor -> drive -> chorus -> ping-pong delay ->
// reverb -> master gain -> DC block -> lookahead limiter. The web build used to
// assemble this from native WebAudio nodes (WaveShaper / Delay / Convolver /
// DynamicsCompressor), which differed from the tested worklet path. synth.ts
// now keeps the native graph only for the scope and spectrum analysers.

// Safety-limiter static curve: threshold -8 dB, ratio 14 — the WebAudio
// DynamicsCompressor settings the old master limiter used. Only its spec makeup
// gain survives here; the ceiling is the lookahead limiter's hard -1 dBFS.
const LIM_THR = 0.398, LIM_RATIO = 14.0;
// One-pole smoother toward a target (setTargetAtTime equivalent).
class Smooth {
  constructor() { this.cur = 0; this.target = 0; this.coef = 0.01; }
  setTime(tau) { this.coef = 1 - Math.exp(-1 / (tau * sampleRate)); }
  next() { this.cur += (this.target - this.cur) * this.coef; return this.cur; }
  snap(v) { this.cur = this.target = v; }
}

// Hoisted out of the per-sample chorus LFO. Exactly the double 2*Math.PI.
const TWO_PI = 6.283185307179586;

// Odd-length Kaiser-windowed half-band FIR (cutoff = rate/4), stored POLYPHASE
// (finding J4). Every tap an even distance from the centre is structurally zero
// and the centre tap is exactly 0.5, so the direct form spends most of its
// multiplies on zeros; on top of that the interpolator's input is half
// zero-stuffed samples and the decimator throws away half its outputs. Split
// into phases, one branch is therefore a BARE DELAY (the lone centre tap) and
// the other a dense FIR of ceil(taps/2) taps — about a quarter of the direct
// form's MACs, 86 per base sample through the 4x drive path instead of 324.
// The dense loop is unrolled by 4. Same decomposition as HalfBandFir in
// juce/source/dsp/Fx.cpp and HalfBand in src/bass/engine/worklet-bass.js.
//
// One instance is driven in ONE mode: interpolate() or decimate(), never both —
// each keeps its own history.
class HalfBandFir {
  constructor(taps, beta) {
    const h = new Float64Array(taps);
    const M = taps - 1, ib = besselI0(beta);
    for (let i = 0; i < taps; i++) {
      const m = i - M * 0.5;
      const sinc = m === 0 ? 0.5 : Math.sin(Math.PI * 0.5 * m) / (Math.PI * m);
      const t = 2 * m / M;
      h[i] = sinc * besselI0(beta * Math.sqrt(Math.max(0, 1 - t * t))) / ib;
    }
    // sin(PI*m/2) for an even integer m evaluates to ~1e-16, not 0. Forcing
    // those taps to zero makes the polyphase split exact.
    const c = (taps - 1) >> 1;
    for (let i = 0; i < taps; i++) if (i !== c && ((i - c) % 2) === 0) h[i] = 0;
    this.h = h;

    const ne = (taps + 1) >> 1, no = taps >> 1;
    this.cEven = (c & 1) === 0; // which phase the centre tap lands in
    const branch = new Float64Array(this.cEven ? no : ne);
    for (let j = 0; j < branch.length; j++) branch[j] = h[this.cEven ? 2 * j + 1 : 2 * j];
    this.g = branch;        // the dense phase
    this.dly = c >> 1;      // the delay phase's single tap, in phase samples
    this.np = Math.max(ne, no + 1);
    // Mirror-written histories: writing each sample at p and p+np lets a branch
    // read a contiguous window with no wrap test.
    this.hx = new Float64Array(2 * this.np);
    this.he = new Float64Array(2 * this.np);
    this.ho = new Float64Array(2 * this.np);
    this.px = 0; this.pd = 0;
    this.y0 = 0; this.y1 = 0;
  }

  reset() {
    this.hx.fill(0); this.he.fill(0); this.ho.fill(0);
    this.px = 0; this.pd = 0; this.y0 = 0; this.y1 = 0;
  }

  // Adopt another instance's history. Used by the mono fast path, where the
  // right channel was skipped because its input was identical to the left's:
  // had it run, its state would be exactly this.
  copyFrom(o) {
    this.hx.set(o.hx); this.he.set(o.he); this.ho.set(o.ho);
    this.px = o.px; this.pd = o.pd;
  }

  // Block forms of the two entry points. A block at a time rather than a
  // sample at a time, so each filter's taps and pointers land in locals once
  // per block instead of once per sample — measured at 23 % of the whole drive
  // stage. Same arithmetic and the same state evolution, sample for sample.

  // 2x interpolate: n base-rate samples in, 2n upsampled samples out.
  // Equivalent to the direct form fed 2*x then 0 for each input.
  interpBlock(x, out, n) {
    const np = this.np, hx = this.hx, g = this.g, nb = g.length;
    const dly = this.dly, cEven = this.cEven;
    let p = this.px;
    for (let i = 0; i < n; i++) {
      if (--p < 0) p = np - 1;
      const v = x[i];
      hx[p] = v; hx[p + np] = v;
      let a = 0, j = 0;
      for (; j + 3 < nb; j += 4) {
        a += g[j] * hx[p + j] + g[j + 1] * hx[p + j + 1]
           + g[j + 2] * hx[p + j + 2] + g[j + 3] * hx[p + j + 3];
      }
      for (; j < nb; j++) a += g[j] * hx[p + j];
      a += a;                     // the zero-stuff gain of 2
      const d = hx[p + dly];      // delay branch: 2 * 0.5 * x[n - dly]
      const o = i + i;
      if (cEven) { out[o] = d; out[o + 1] = a; } else { out[o] = a; out[o + 1] = d; }
    }
    this.px = p;
  }

  // 2x decimate: 2n high-rate samples in, n outputs at the kept (first) phase.
  // Equivalent to the direct form fed both samples, keeping the first result.
  decimBlock(x, out, n) {
    const np = this.np, he = this.he, ho = this.ho, g = this.g, nb = g.length;
    const dly = this.dly, cEven = this.cEven;
    let p = this.pd;
    for (let i = 0; i < n; i++) {
      if (--p < 0) p = np - 1;
      const o = i + i;
      const x0 = x[o], x1 = x[o + 1];
      he[p] = x0; he[p + np] = x0;
      ho[p] = x1; ho[p + np] = x1;
      // The dense branch reads the phase the centre tap does not.
      const b = cEven ? ho : he;
      const q = cEven ? p + 1 : p;
      let a = 0, j = 0;
      for (; j + 3 < nb; j += 4) {
        a += g[j] * b[q + j] + g[j + 1] * b[q + j + 1]
           + g[j + 2] * b[q + j + 2] + g[j + 3] * b[q + j + 3];
      }
      for (; j < nb; j++) a += g[j] * b[q + j];
      out[i] = a + 0.5 * (cEven ? he[p + dly] : ho[p + dly + 1]);
    }
    this.pd = p;
  }

}

// Oversampler and drive scratch. Module-level and grown on demand: the render
// thread must never allocate. OS_X4 holds 4x the block length.
let OS_IN = new Float64Array(128), OS_X2 = new Float64Array(256);
let OS_X4 = new Float64Array(512), OS_Y2 = new Float64Array(256);
let FX_EQL = new Float64Array(128), FX_EQR = new Float64Array(128);
let FX_DRYL = new Float64Array(128), FX_DRYR = new Float64Array(128);
let FX_WETL = new Float64Array(128), FX_WETR = new Float64Array(128);
function stereoEnergy(L, R, n) {
  let energy = 0;
  for (let i = 0; i < n; i++) energy += 0.5 * (L[i] * L[i] + R[i] * R[i]);
  return energy;
}
function fxScratch(n) {
  if (OS_IN.length >= n) return;
  OS_IN = new Float64Array(n); OS_X2 = new Float64Array(2 * n);
  OS_X4 = new Float64Array(4 * n); OS_Y2 = new Float64Array(2 * n);
  FX_EQL = new Float64Array(n); FX_EQR = new Float64Array(n);
  FX_DRYL = new Float64Array(n); FX_DRYR = new Float64Array(n);
  FX_WETL = new Float64Array(n); FX_WETR = new Float64Array(n);
}

// 4x drive oversampler stages: 63-tap first half-band (2x), 17-tap second (4x).
// Total up+shape+down group delay is an exact integer in base samples.
const HB1_TAPS = 63, HB2_TAPS = 17;
const DRIVE_LATENCY = ((HB1_TAPS - 1) / 2 + (HB2_TAPS - 1) / 4) | 0; // 35

// Classic Freeverb tuning, scaled to the device sample rate. The web build used
// a ConvolverNode fed a random-noise impulse that was RE-RENDERED on every SIZE
// change, and the buffer swap cut the tail dead; SIZE here only moves the comb
// feedback and damping, so the tail bends instead of dropping out.
const FV_COMB_TUNE = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
const FV_AP_TUNE = [556, 441, 341, 225];
const FV_SPREAD = 23;

class Fx {
  constructor() {
    this.keyChannel = -1; this.keyIn = null; // COMP side-chain, see setParams
    const scale = sampleRate / 44100;
    this.headroom = {};
    for (const stage of ['input', 'eq', 'ott', 'comp', 'drive', 'lab', 'chorus', 'delay', 'reverb']) {
      this.headroom[stage] = new globalThis.FablePeakGuard(sampleRate);
    }
    this.delayFeedbackGuard = new globalThis.FablePeakGuard(sampleRate);
    // LAB stages between drive and chorus (lab-worklet.js, port of LabFx.h).
    this.lab = new globalThis.FableLabFx(sampleRate);
    this.labSettings = { ...this.lab.settings };
    this.combL = []; this.combR = []; this.apL = []; this.apR = [];
    for (let i = 0; i < 8; i++) {
      this.combL.push(new FvComb((FV_COMB_TUNE[i] * scale) | 0));
      this.combR.push(new FvComb(((FV_COMB_TUNE[i] + FV_SPREAD) * scale) | 0));
    }
    for (let i = 0; i < 4; i++) {
      this.apL.push(new FvAllpass((FV_AP_TUNE[i] * scale) | 0));
      this.apR.push(new FvAllpass(((FV_AP_TUNE[i] + FV_SPREAD) * scale) | 0));
    }
    // PLATE algorithm (fx.reverb.type 1); only the selected network runs.
    this.plate = new PlateReverb(sampleRate);
    this.verbType = 0;

    this.eq = new globalThis.FableParametricEq(sampleRate);
    // Kept as an inspection surface for the EQ response tests.
    this.eqBands = this.eq.bands.map((band) => [band.l, band.r]);

    this.driveK = 1; this.drivePre = 1; this.driveNorm = 1;
    this.driveWet = new Smooth(); this.driveDry = new Smooth();
    this.up1L = new HalfBandFir(HB1_TAPS, 6); this.up2L = new HalfBandFir(HB2_TAPS, 6);
    this.dn2L = new HalfBandFir(HB2_TAPS, 6); this.dn1L = new HalfBandFir(HB1_TAPS, 6);
    this.up1R = new HalfBandFir(HB1_TAPS, 6); this.up2R = new HalfBandFir(HB2_TAPS, 6);
    this.dn2R = new HalfBandFir(HB2_TAPS, 6); this.dn1R = new HalfBandFir(HB1_TAPS, 6);
    this.dryL = new DelayLine(DRIVE_LATENCY + 4); this.dryR = new DelayLine(DRIVE_LATENCY + 4);
    this.driveSilenced = false;
    this.monoRun = false;
    this.driveColorL = new globalThis.FableDriveColor(sampleRate);
    this.driveColorR = new globalThis.FableDriveColor(sampleRate);
    this.comp = new globalThis.FableCompressor(sampleRate);
    this.ott = new globalThis.FableOttCompressor(sampleRate);
    // Opt-in metering: no sample taps or messages for hidden SQ-4 devices.
    this.metering = false;
    this.meterEnergy = new Float64Array(4);
    this.meterSamples = 0;
    this.meterReduction = 0;

    this.chPhase = 0; this.chRate = 0.6; this.chDepth = 0.5;
    this.chWet = new Smooth(); this.chDry = new Smooth();
    this.chDl1 = new DelayLine((0.05 * sampleRate) | 0);
    this.chDl2 = new DelayLine((0.05 * sampleRate) | 0);
    this.chorusOff = false; this.chorusGated = false;

    this.dlTime = new Smooth(); this.dlFb = new Smooth();
    this.dlWet = new Smooth(); this.dlDry = new Smooth();
    this.dlL = new DelayLine(((2 * sampleRate) | 0) + 4);
    this.dlR = new DelayLine(((2 * sampleRate) | 0) + 4);
    this.dlDamp = new Biquad();
    this.dlDampR = new Biquad();
    this.dlHpL = new Biquad(); this.dlHpR = new Biquad();
    this.dlHpL.highpass(45, 0.707, sampleRate); this.dlHpR.highpass(45, 0.707, sampleRate);
    this.dlTone = 4500; this.dlToneTarget = 4500;
    this.dlSat = new Smooth(); this.dlWow = new Smooth(); this.dlFlutter = new Smooth();
    this.dlWidth = new Smooth(); this.dlMode = new Smooth();
    for (const s of [this.dlSat, this.dlWow, this.dlFlutter, this.dlWidth, this.dlMode]) s.setTime(0.03);
    this.dlWidth.snap(1);
    this.tapeClock = 0; this.dlDriftL = 0; this.dlDriftR = 0; this.delayInitialized = false;
    this.echoMetering = false; this.echoSamples = 0; this.echoEnergy = new Float64Array(3);
    this.delayOff = false; this.delayGated = false;

    this.verbWet = new Smooth(); this.verbDry = new Smooth();
    this.roomSize = 0.84; this.verbOff = false; this.verbGated = false;
    this.reverbMetering = false; this.reverbSamples = 0; this.reverbEnergy = new Float64Array(3);

    this.masterGain = new Smooth();
    this.dcL = new Biquad(); this.dcR = new Biquad();

    for (const s of [this.driveWet, this.driveDry, this.chWet, this.chDry, this.dlFb,
      this.dlWet, this.dlDry, this.verbWet, this.verbDry, this.masterGain]) s.setTime(0.02);
    this.dlTime.setTime(0.08);
    this.driveDry.snap(1); this.chDry.snap(1); this.dlDry.snap(1); this.verbDry.snap(1);

    this.dcL.highpass(8, 0.707, sampleRate); this.dcR.highpass(8, 0.707, sampleRate);
    this.dlDamp.lowpass(4500, 0.707, sampleRate);
    this.dlDampR.lowpass(4500, 0.707, sampleRate);

    // WebAudio's DynamicsCompressor applies a spec-defined makeup gain
    // ((1/c(1))^0.6, c = the static curve at 0 dBFS). The old web master limiter
    // WAS that node, so its ~4.5 dB makeup stays ahead of the lookahead limiter
    // or every patch drops in level.
    const c1 = Math.pow(1 / LIM_THR, 1 / LIM_RATIO - 1);
    this.lim = new LookaheadLimiter(sampleRate, Math.pow(1 / c1, 0.6));
  }

  latencySamples() { return DRIVE_LATENCY + this.lim.la; }

  reset() {
    this.chDl1.reset(); this.chDl2.reset(); this.dlL.reset(); this.dlR.reset();
    this.dryL.reset(); this.dryR.reset();
    for (const c of this.combL) c.reset();
    for (const c of this.combR) c.reset();
    for (const a of this.apL) a.reset();
    for (const a of this.apR) a.reset();
    this.plate.reset();
    this.dcL.reset(); this.dcR.reset(); this.dlDamp.reset();
    this.dlDampR.reset(); this.dlHpL.reset(); this.dlHpR.reset();
    this.tapeClock = 0; this.dlDriftL = this.dlDriftR = 0; this.delayInitialized = false;
    this.echoSamples = 0; this.echoEnergy.fill(0);
    this.reverbSamples = 0; this.reverbEnergy.fill(0);
    this.eq.reset();
    this.up1L.reset(); this.up2L.reset(); this.dn2L.reset(); this.dn1L.reset();
    this.up1R.reset(); this.up2R.reset(); this.dn2R.reset(); this.dn1R.reset();
    for (const guard of Object.values(this.headroom)) guard.reset();
    this.delayFeedbackGuard.reset(); this.driveColorL.reset(); this.driveColorR.reset(); this.comp.reset(); this.ott.reset();
    this.meterEnergy.fill(0); this.meterSamples = 0; this.meterReduction = 0;
    this.lim.reset();
    this.chPhase = 0;
    this.monoRun = false;
    this.driveSilenced = this.chorusGated = this.delayGated = this.verbGated = false;
    this.lab.reset();
  }

  // Clear both reverb networks.
  resetVerb() {
    for (const c of this.combL) c.reset();
    for (const c of this.combR) c.reset();
    for (const a of this.apL) a.reset();
    for (const a of this.apR) a.reset();
    this.plate.reset();
  }

  setParams(p, bpm = 120) {
    this.eq.setParams((key) => p[PID[key]]);
    const lab = this.labSettings;
    for (const [i, key, flag] of LAB_KEYS) lab[key] = flag ? p[i] > 0.5 : (key === 'resoChord' || key === 'glitchDiv' ? Math.round(p[i]) : p[i]);
    this.lab.setParams(lab, bpm);

    const amt = p[FXDRIVE_AMT];
    this.drivePre = 1 + amt * 2;
    this.driveK = 1 + amt * 12;
    this.driveNorm = 1 / (this.drivePre * Math.tanh(this.driveK));
    // No separate OFF flag: mixGate gives wet 0 for OFF and for MIX 0 alike, so
    // `driveWet.target === 0` covers both and drives the one silence gate below.
    const dOn = p[FXDRIVE_ON] > 0.5;
    this.driveWet.target = mixGate(dOn, p[FXDRIVE_MIX], true);
    this.driveDry.target = mixGate(dOn, p[FXDRIVE_MIX], false);

    this.chRate = p[FXCHORUS_RATE];
    this.chDepth = p[FXCHORUS_DEPTH];
    const cOn = p[FXCHORUS_ON] > 0.5;
    this.chorusOff = !cOn;
    this.chWet.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8, true);
    this.chDry.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8, false);

    const division = ECHO_DIVISIONS[Math.max(0, Math.min(5, p[FXDELAY_DIV] | 0))];
    this.dlTime.target = Math.max(0.02, Math.min(1.5, p[FXDELAY_SYNC] > 0.5 ? 60 / Math.max(1, bpm) * division : p[FXDELAY_TIME]));
    if (!this.delayInitialized) { this.dlTime.snap(this.dlTime.target); this.delayInitialized = true; }
    this.dlFb.target = Math.max(0, Math.min(0.92, p[FXDELAY_FB]));
    this.dlToneTarget = Math.max(400, Math.min(12000, p[FXDELAY_TONE] || 4500));
    this.dlSat.target = Math.max(0, Math.min(1, p[FXDELAY_SAT]));
    this.dlWow.target = Math.max(0, Math.min(1, p[FXDELAY_WOW]));
    this.dlFlutter.target = Math.max(0, Math.min(1, p[FXDELAY_FLUTTER]));
    this.dlWidth.target = Math.max(0, Math.min(1, p[FXDELAY_WIDTH]));
    this.dlMode.target = p[FXDELAY_MODE] > 0.5 ? 1 : 0;
    const delOn = p[FXDELAY_ON] > 0.5;
    this.delayOff = !delOn;
    this.dlWet.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85, true);
    this.dlDry.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85, false);

    // SIZE maps to roomsize/decay — a longer, brighter tail with size. No buffer
    // is rebuilt, so a SIZE sweep never cuts the tail.
    const size = p[FXREVERB_SIZE];
    this.roomSize = 0.7 + size * 0.28;
    const damp = 0.4 - size * 0.2;
    for (let i = 0; i < 8; i++) {
      this.combL[i].feedback = this.combR[i].feedback = this.roomSize;
      this.combL[i].damp1 = this.combR[i].damp1 = damp;
      this.combL[i].damp2 = this.combR[i].damp2 = 1 - damp;
    }
    // PLATE reads SIZE as the same decay time and adds DAMP. A type change
    // starts the selected network from silence, so no stale tail comes back.
    const verbType = p[FXREVERB_TYPE] > 0.5 ? 1 : 0;
    if (verbType !== this.verbType) {
      this.verbType = verbType;
      this.resetVerb();
    }
    this.plate.setParams(size, p[FXREVERB_DAMP]);
    const rOn = p[FXREVERB_ON] > 0.5;
    this.verbOff = !rOn;
    this.verbWet.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9, true);
    this.verbDry.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9, false);

    // fx.comp.gain remains serialized for native compatibility only.
    this.driveColorL.setParams(p[PID['fx.drive.type']], p[PID['fx.drive.tone']]);
    this.driveColorR.setParams(p[PID['fx.drive.type']], p[PID['fx.drive.tone']]);
    this.comp.setParams(p[FXCOMP_ON] > 0.5, p[FXCOMP_THR], p[PID['fx.comp.att']], p[PID['fx.comp.rel']], p[PID['fx.comp.ratio']]);
    // Side-chain (src/engine/sidechain.ts): 1 KICK → key channel 4, TRACK n → n-1.
    const sc = Math.round(p[PID['fx.comp.sc']] || 0);
    this.keyChannel = sc === 1 ? 4 : sc >= 2 ? sc - 2 : -1;
    this.comp.setKey(this.keyChannel >= 0, p[PID['fx.comp.schp']]);
    this.ott.setParams(p[FXOTT_ON] > 0.5, p[FXOTT_DEPTH], p[FXOTT_TIME], p[FXOTT_UP], p[FXOTT_DOWN]);

    const vol = p[MASTER_VOLUME];
    this.masterGain.target = vol * vol * 1.6;
  }

  // One channel of a block through the 4x oversampled shaper: interpolate to
  // 2x then 4x, shape all 4n samples in one flat loop, then decimate back.
  // Stage at a time rather than six filter calls per sample; the decimators
  // keep the phase aligned with the integer DRIVE_LATENCY group delay.
  driveBlock(u1, u2, d2, d1, out, n) {
    u1.interpBlock(OS_IN, OS_X2, n);
    u2.interpBlock(OS_X2, OS_X4, 2 * n);
    // tanh is bounded — no pre-clamp (a hard clamp is its own nonsmooth
    // nonlinearity). The old web path was a 513-point WaveShaper table read at
    // 2x with linear interpolation between entries.
    const color = u1 === this.up1L ? this.driveColorL : this.driveColorR;
    const K = this.driveK, norm = this.driveNorm, m = 4 * n;
    for (let i = 0; i < m; i++) OS_X4[i] = color.shape(OS_X4[i], K, norm);
    d2.decimBlock(OS_X4, OS_Y2, 2 * n);
    d1.decimBlock(OS_Y2, out, n);
  }

  // Resync the right-hand drive state from the left. The mono fast path skips
  // the right channel while its input is identical to the left's, so had those
  // filters run, their state would be exactly this.
  syncRight() {
    this.driveColorR.copyShapeFrom(this.driveColorL);
    this.up1R.copyFrom(this.up1L); this.up2R.copyFrom(this.up2L);
    this.dn2R.copyFrom(this.dn2L); this.dn1R.copyFrom(this.dn1L);
    this.dryR.copyFrom(this.dryL);
    this.monoRun = false;
  }

  process(L, R, n, offset = 0) {
    // Gate only when OFF; mix == 0 while ON must keep state accumulation alive.
    // The drive's wet gain being zero — MIX 0 while ON, as well as OFF — means
    // the oversampler's output is multiplied by zero, so it is not run at all.
    // The plugin still runs it whenever the stage is ON; the only difference is
    // that here the FIR history restarts from zero when MIX leaves 0, under a
    // wet gain below 1e-6 that then ramps in over 20 ms. Every other stage has
    // a tail or a feedback loop that MUST keep accumulating at MIX 0, so this
    // shortcut applies to the drive alone.
    const driveSilent = this.driveWet.target === 0 && Math.abs(this.driveWet.cur) < 1e-6;
    const chorusGate = this.chorusOff && this.chWet.target === 0 && Math.abs(this.chWet.cur) < 1e-6;
    const delayGate = this.delayOff && this.dlWet.target === 0 && Math.abs(this.dlWet.cur) < 1e-6;
    const verbGate = this.verbOff && this.verbWet.target === 0 && Math.abs(this.verbWet.cur) < 1e-6;

    if (driveSilent && !this.driveSilenced) {
      // Both targets are exact here: mixGate gives wet 0 / dry 1 for OFF and
      // for MIX 0 alike, so snapping is snapping to the target and the skipped
      // path is exactly `1 * dry`, not `(1 - eps) * dry`.
      this.driveWet.snap(0); this.driveDry.snap(1);
      this.driveColorL.reset(); this.driveColorR.reset();
      this.up1L.reset(); this.up2L.reset(); this.dn2L.reset(); this.dn1L.reset();
      this.up1R.reset(); this.up2R.reset(); this.dn2R.reset(); this.dn1R.reset();
    }
    this.driveSilenced = driveSilent;
    if (chorusGate && !this.chorusGated) { this.chWet.snap(0); this.chDry.snap(1); this.chDl1.reset(); this.chDl2.reset(); }
    if (delayGate && !this.delayGated) {
      this.dlWet.snap(0); this.dlDry.snap(1); this.dlL.reset(); this.dlR.reset();
      this.dlDamp.reset(); this.dlDampR.reset(); this.dlHpL.reset(); this.dlHpR.reset();
      this.dlDriftL = this.dlDriftR = 0;
    }
    if (!delayGate) {
      this.dlTone += (this.dlToneTarget - this.dlTone) * (1 - Math.exp(-n / (0.03 * sampleRate)));
      this.dlDamp.lowpass(this.dlTone, 0.707, sampleRate); this.dlDampR.lowpass(this.dlTone, 0.707, sampleRate);
    }
    if (verbGate && !this.verbGated) {
      this.verbWet.snap(0); this.verbDry.snap(1);
      this.resetVerb();
    }
    this.chorusGated = chorusGate;
    this.delayGated = delayGate; this.verbGated = verbGate;

    const combL = this.combL, combR = this.combR, apL = this.apL, apR = this.apR;
    fxScratch(n);
    this.headroom.input.process(L, R, n);

    // ---- four-band EQ (first FX; shared smoothing/type-crossfade contract) ----
    // Into double scratch, not back into L/R: the whole chain stays in double
    // precision until the final write, as the per-sample loop it replaces did.
    for (let i = 0; i < n; i++) { FX_EQL[i]=L[offset + i]; FX_EQR[i]=R[offset + i]; }
    this.eq.process(FX_EQL, FX_EQR, n);
    this.headroom.eq.process(FX_EQL, FX_EQR, n);

    // ---- OTT -> compressor (automatic level matching) ----
    if (this.metering) this.meterEnergy[0] += stereoEnergy(FX_EQL, FX_EQR, n);
    this.ott.process(FX_EQL, FX_EQR, n);
    if (this.metering) this.meterEnergy[1] += stereoEnergy(FX_EQL, FX_EQR, n);
    this.headroom.ott.process(FX_EQL, FX_EQR, n);
    if (this.metering) this.meterEnergy[2] += stereoEnergy(FX_EQL, FX_EQR, n);
    const key = this.keyChannel >= 0 ? this.keyIn?.[this.keyChannel] : null;
    this.comp.process(FX_EQL, FX_EQR, n, key ? key.subarray(offset, offset + n) : null);
    if (this.metering) {
      this.meterEnergy[3] += stereoEnergy(FX_EQL, FX_EQR, n);
      this.meterSamples += n;
      if (this.comp.wetTarget && this.comp.env > 1e-7)
        this.meterReduction = Math.max(this.meterReduction, -20 * Math.log10(Math.max(1e-9, this.comp.gain)));
    }
    this.headroom.comp.process(FX_EQL, FX_EQR, n);

    // ---- drive (4x oversampled tanh waveshaper), a block at a time ----
    // The dry/bypass path always runs through a DRIVE_LATENCY delay so the
    // dry/wet mix stays time-aligned with the shaper's FIR group delay and the
    // chain latency is constant whether drive is active or gated.
    //
    // Mono fast path: while L and R carry identical samples the drive runs once
    // and the right-hand filters are left alone; they are resynced from the left
    // the moment the channels differ, which costs nothing because their state
    // would have been exactly the left's. WT-1 sits here whenever unison is 1
    // and SPREAD and PAN are 0.
    let mono = true;
    for (let i = 0; i < n; i++) if (FX_EQL[i] !== FX_EQR[i]) { mono = false; break; }
    if (!mono && this.monoRun) this.syncRight();
    this.monoRun = mono;

    this.dryL.blockRead(FX_EQL, FX_DRYL, n, DRIVE_LATENCY + 1);
    if (!mono) this.dryR.blockRead(FX_EQR, FX_DRYR, n, DRIVE_LATENCY + 1);
    if (driveSilent) {
      for (let i = 0; i < n; i++) FX_EQL[i] = FX_DRYL[i];
      if (mono) { for (let i = 0; i < n; i++) FX_EQR[i] = FX_DRYL[i]; }
      else { for (let i = 0; i < n; i++) FX_EQR[i] = FX_DRYR[i]; }
    } else {
      const pre = this.drivePre;
      for (let i = 0; i < n; i++) OS_IN[i] = pre * FX_EQL[i];
      this.driveBlock(this.up1L, this.up2L, this.dn2L, this.dn1L, FX_WETL, n);
      if (!mono) {
        for (let i = 0; i < n; i++) OS_IN[i] = pre * FX_EQR[i];
        this.driveBlock(this.up1R, this.up2R, this.dn2R, this.dn1R, FX_WETR, n);
      }
      for (let i = 0; i < n; i++) {
        const wet = this.driveWet.next(), dry = this.driveDry.next();
        const dl = dry * FX_DRYL[i] + wet * this.driveColorL.processTone(FX_WETL[i]);
        FX_EQL[i] = dl;
        FX_EQR[i] = dry * (mono ? FX_DRYL[i] : FX_DRYR[i]) + wet * this.driveColorR.processTone(mono ? FX_WETL[i] : FX_WETR[i]);
      }
    }
    this.headroom.drive.process(FX_EQL, FX_EQR, n);

    const lab = this.lab, labActive = lab.active();
    for (let i = 0; i < n; i++) {
      let l = FX_EQL[i], r = FX_EQR[i];
      const guard = (stage) => {
        const gg = this.headroom[stage].gainFor(l, r);
        l *= gg; r *= gg;
      };

      // ---- LAB: crush -> reso -> shift -> spray -> glitch ----
      if (labActive) {
        lab.processSample(Math.fround(l), Math.fround(r));
        l = lab.out[0]; r = lab.out[1];
        guard('lab');
      }

      // ---- chorus (two modulated taps, stereo) ----
      if (!chorusGate) {
        this.chPhase += this.chRate / sampleRate;
        if (this.chPhase >= 1) this.chPhase -= 1;
        const lfo = Math.sin(TWO_PI * this.chPhase);
        const depth = 0.0008 + this.chDepth * 0.0045;
        const mono = 0.5 * (l + r);
        this.chDl1.write(mono); this.chDl2.write(mono);
        const c1 = this.chDl1.readHermite((0.012 + depth * lfo) * sampleRate);
        const c2 = this.chDl2.readHermite((0.017 - depth * 0.8 * lfo) * sampleRate);
        const wet = this.chWet.next(), dry = this.chDry.next();
        l = dry * l + wet * c1;
        r = dry * r + wet * c2;
      }
      guard('chorus');

      // ---- stereo tape echo: smoothly moving heads, lossy record path ----
      if (this.echoMetering) this.echoEnergy[0] += 0.5 * (l * l + r * r);
      if (!delayGate) {
        const dt = this.dlTime.next();
        const fb = this.dlFb.next();
        const wow = this.dlWow.next() * 0.0025, flutter = this.dlFlutter.next() * 0.00022;
        const t = this.tapeClock / sampleRate;
        // Non-matching rates keep the two heads from cycling in lockstep.
        this.dlDriftL = wow * (0.72 * Math.sin(TWO_PI * 0.23 * t) + 0.28 * Math.sin(TWO_PI * 0.37 * t))
          + flutter * Math.sin(TWO_PI * 6.7 * t);
        this.dlDriftR = wow * (0.72 * Math.sin(TWO_PI * 0.23 * t + 1.8) + 0.28 * Math.sin(TWO_PI * 0.41 * t + 0.7))
          + flutter * Math.sin(TWO_PI * 8.3 * t + 1.1);
        this.tapeClock++;
        const dL = this.dlL.readHermite(Math.max(0.005, dt + this.dlDriftL) * sampleRate);
        const dR = this.dlR.readHermite(Math.max(0.005, dt + this.dlDriftR) * sampleRate);
        const mono = 0.5 * (l + r);
        const mode = this.dlMode.next(), saturation = this.dlSat.next(), k = 1 + saturation * 4;
        const recordL = mono + mode * (l - mono) + fb * (dR + mode * (dL - dR));
        const recordR = mode * r + fb * (dL + mode * (dR - dL));
        // Unity small-signal slope; saturation cannot turn the feedback into
        // a gain > 1 loop. Both channels lose highs/lows on every pass.
        const feedbackL = this.dlDamp.process(this.dlHpL.process(recordL + saturation * (Math.tanh(k * recordL) / k - recordL)));
        const feedbackR = this.dlDampR.process(this.dlHpR.process(recordR + saturation * (Math.tanh(k * recordR) / k - recordR)));
        const feedbackGain = this.delayFeedbackGuard.gainFor(feedbackL, feedbackR);
        this.dlL.write(feedbackL * feedbackGain);
        this.dlR.write(feedbackR * feedbackGain);
        const wet = this.dlWet.next(), dry = this.dlDry.next();
        const mid = 0.5 * (dL + dR), side = 0.5 * (dL - dR) * this.dlWidth.next();
        const echoL = wet * (mid + side), echoR = wet * (mid - side);
        if (this.echoMetering) { this.echoEnergy[1] += echoL * echoL; this.echoEnergy[2] += echoR * echoR; }
        l = dry * l + echoL;
        r = dry * r + echoR;
      }
      if (this.echoMetering) this.echoSamples++;
      guard('delay');

      // ---- reverb: ROOM (Freeverb) or PLATE ----
      if (!verbGate) {
        let outL = 0, outR = 0;
        if (this.verbType) {
          const plate = this.plate;
          plate.process((l + r) * 0.5);
          outL = plate.outL * PLATE_GAIN; outR = plate.outR * PLATE_GAIN;
        } else {
          const input = (l + r) * 0.015; // fixed input gain (Freeverb convention)
          for (let c = 0; c < 8; c++) { outL += combL[c].process(input); outR += combR[c].process(input); }
          for (let a = 0; a < 4; a++) { outL = apL[a].process(outL); outR = apR[a].process(outR); }
        }
        const wet = this.verbWet.next(), dry = this.verbDry.next();
        if (this.reverbMetering) {
          const wetL = wet * outL, wetR = wet * outR;
          this.reverbEnergy[0] += wetL * wetL;
          this.reverbEnergy[1] += wetR * wetR;
          this.reverbEnergy[2] += wetL * wetR;
        }
        l = dry * l + wet * outL;
        r = dry * r + wet * outR;
      }
      if (this.reverbMetering) this.reverbSamples++;
      guard('reverb');

      // ---- master gain ----
      const g = this.masterGain.next();
      l *= g; r *= g;

      // ---- DC block ----
      l = this.dcL.process(l);
      r = this.dcR.process(r);

      // ---- lookahead safety limiter (makeup inside, -1 dBFS ceiling) ----
      this.lim.process(l, r);
      L[offset + i] = this.lim.outL; R[offset + i] = this.lim.outR;
    }
  }
}

class FableProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    subSquareTable(); // build the sub-square table now, not on the audio thread
    // Flat parameter store (finding W2): p[index], never p['some.string'].
    this.p = new Float64Array(NUM_PARAMS);
    this.rng = new Rng(0x9e3779b9);
    this.modRng = new Rng(0x85ebca6b); // LFO 3/4 + RANDOM (Engine.h modRng_)
    // Sample-rate-mapped one-pole coefficients (finding W1): the same analog
    // pole at any rate is p' = p^(48k/sr); the pink filter's input gains are
    // rescaled to hold each stage's low-frequency gain, so the pink spectrum
    // matches the 48 kHz reference. Mirrors Engine::prepare.
    const rr = 48000 / sampleRate;
    this.dcR = Math.pow(DC_R, rr);
    this.pinkP = new Float64Array(6);
    this.pinkG = new Float64Array(6);
    for (let i = 0; i < 6; i++) {
      this.pinkP[i] = Math.pow(PINK_P[i], rr);
      this.pinkG[i] = i === 5
        ? PINK_G[5] * (1 + this.pinkP[5]) / (1 + PINK_P[5])
        : PINK_G[i] * (1 - this.pinkP[i]) / (1 - PINK_P[i]);
    }
    this.tables = [];
    this.voices = [];
    for (let i = 0; i < NVOICES; i++) this.voices.push(new Voice());
    this.bend = 0;
    this.bpm = 120;
    // Virtual transport: beats (quarter notes) since audio start. Standalone has
    // no host transport, so synced LFOs phase-lock to this clock (downbeat = t0).
    // Accumulated (not samples*bpm) so a tempo change doesn't rescale the past.
    this.transportBeats = 0;
    this.gLfo1 = new LFO(); this.gLfo1.reset(this.rng);
    this.gLfo2 = new LFO(); this.gLfo2.reset(this.rng);
    this.gLfo3 = new LFO(); this.gLfo3.reset(this.modRng);
    this.gLfo4 = new LFO(); this.gLfo4.reset(this.modRng);
    this.lastPitch = 60;
    this.held = []; // press-order stack of held note numbers, newest last, tracked mode-independently
    this.clock = 0;
    // ---- note sequencer state ----
    this.seqPlaying = false;
    this.seqAuto = [];
    this.seqAnchor = 0;
    this.arp = null;
    this.seqScale = null; // standalone scale map (message 'scale')
    this.seqStep = -1;
    this.sequenceRhythm = null; this.polyK = 0;
    this.seqPats = new Uint8Array(SEQ_NPATTERNS * SEQ_STEPS * SEQ_STRIDE);
    for (let i = 2; i < this.seqPats.length; i += SEQ_STRIDE) this.seqPats[i] = 1; // oct byte: 1 = oct 0
    this.seqChain = [0];
    this.seqChainPos = 0;
    this.seqToNext = 0; // samples until the next step fires
    this.seqOffQueue = []; // { note, remaining } per sounding seq note (poly)
    this.seqOnQueue = []; // { note, vel, remaining, off } strummed comp notes still to enter
    this.seqLastNote = -1; // most recent fired note (-1 = none), for viz
    // ---- hosted clip transport (SQ-4) ----
    this.hosted = false;
    this.hostBpm = 120;
    this.hostSwing = 0;
    this.hostAnchor = 0; // songStartFrame — the shared timebase's beat zero
    this.clip = null; // { data: Uint8Array, bars }
    this.clipPend = null; // { data, bars, at } — waiting for its atFrame
    this.clipStopAt = -1;
    this.clipStep = -1; // absolute step within the clip (0 .. bars*16-1)
    this.clipToNext = 0;
    // Absolute frame at the start of the chunk being rendered. `currentFrame`
    // only advances per host block; the split loop (finding W3) needs the
    // chunk's own position for clip scheduling.
    this.frameNow = 0;
    this.autoHeld = new Map(); // param index -> stored value while automated
    this.autoLanes = null;
    this.vizCount = 0;
    this.tmpL = new Float32Array(128);
    this.tmpR = new Float32Array(128);
    // FM/AM/RM modulator signals: the one osc A reads (from B) and vice versa.
    this.modForA = new Float32Array(128);
    this.modForB = new Float32Array(128);
    this.noMod = new Float32Array(128); // silence, for a carrier without a modulator
    // filter routing scratch: osc-B split source + each filter's output
    this.bL = new Float32Array(128);
    this.bR = new Float32Array(128);
    this.f1L = new Float32Array(128);
    this.f1R = new Float32Array(128);
    this.f2L = new Float32Array(128);
    this.f2R = new Float32Array(128);
    // Per-voice modulation scratch (reused per render — no per-call allocation).
    // _modAccum[i]: route sum for param i. _pm: a full copy of `p` with the
    // modulated params overwritten, so every read is a plain array index.
    this._modAccum = new Float64Array(NUM_PARAMS);
    this._pm = new Float64Array(NUM_PARAMS);
    this._srcs = new Float64Array(NUM_SOURCES);
    // evalRoutes results and scratch: global sums, the aux scale of the route
    // in hand, and the amount offsets from "MOD n AMT" routes.
    this._mPitch = 0; this._mAmp = 0; this._mPan = 0; this._routeAux = 1;
    this._amtMod = new Float64Array(MOD_MATRIX_SIZE);
    // updateGlobalMods: the viz voice's sources, the FX parameter copy with
    // the FX routes applied, and the rate factors of the free-running LFOs.
    this._vizSrcs = new Float64Array(NUM_SOURCES);
    this._fxp = new Float64Array(NUM_PARAMS);
    this.fxParams = null;
    this.gRate = new Float64Array(4).fill(1);
    // Live-mod telemetry: the viz voice's route sums per MOD_DESTS index,
    // re-snapshotted every block, sent at the viz cadence. modIdleSent starts
    // true — the UI's default state is already "idle", no terminator needed.
    this.modViz = new Float32Array(DST_TARGET.length);
    this.modVizAny = false;
    this.modIdleSent = true;
    // The FX chain now runs here rather than as native WebAudio nodes
    // (finding W6): same algorithm as juce/source/dsp/Fx.cpp, testable offline.
    this.fx = new Fx();
    this.fxLatency = this.fx.latencySamples();
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(d) {
    switch (d.t) {
      case 'reverb':
        this.fx.reverbMetering = !!d.on;
        this.fx.reverbEnergy.fill(0); this.fx.reverbSamples = 0;
        break;
      case 'echo':
        this.fx.echoMetering = !!d.on;
        this.fx.echoEnergy.fill(0); this.fx.echoSamples = 0;
        break;
      case 'dynamics':
        this.fx.metering = !!d.on;
        this.fx.meterEnergy.fill(0); this.fx.meterSamples = 0; this.fx.meterReduction = 0;
        break;
      // Non-finite param values are dropped at this single choke point: a NaN
      // that reached `p` would latch into phases / env levels and stick there.
      case 'init':
        for (const k in d.params) {
          const i = PID[k], v = d.params[k];
          if (i !== undefined && Number.isFinite(v)) this.p[i] = v;
        }
        this.bpm = Math.min(1000, Math.max(1, (this.hosted ? this.hostBpm : this.p[SEQ_BPM]) || 120));
        // The FX drive FIR + limiter lookahead add a fixed latency the main
        // thread needs for scope/transport alignment; the plugin reports the
        // same number through setLatencySamples.
        this.port.postMessage({ t: 'latency', n: this.fxLatency });
        break;
      case 'p': {
        // The string id is resolved to its index here, once per message —
        // never on the render thread (finding W2).
        const i = PID[d.k];
        if (i !== undefined && Number.isFinite(d.v)) {
          // An automated param keeps the knob edit as its stored value.
          if (this.autoHeld.has(i)) this.autoHeld.set(i, d.v);
          else this.p[i] = d.v;
          // The web build has no host transport: while the sequencer is the
          // tempo authority, synced LFOs follow it.
          if (i === SEQ_BPM && !this.hosted) this.bpm = Math.min(1000, Math.max(1, d.v));
        }
        break;
      }
      case 'tables':
        this.tables = d.list.map((x) => this.makeTable(x));
        break;
      // Incremental table publication (finding W4): the pool is 8.6 MB+, so a
      // user-table edit sends only the changed slot, with its buffer
      // transferred rather than structured-clone-copied.
      case 'table': {
        const i = d.i | 0;
        if (i >= 0 && i < 256) this.tables[i] = this.makeTable(d);
        break;
      }
      case 'tablecount':
        this.tables.length = Math.max(0, Math.min(256, d.n | 0));
        break;
      case 'on': this.noteOn(d.n, d.v); break;
      case 'off': this.noteOff(d.n); break;
      case 'bend': this.bend = d.s; break;
      case 'bpm': this.bpm = d.v > 1 ? Math.min(d.v, 1000) : 120; break;
      case 'pats': this.seqPats = new Uint8Array(d.data); break;
      case 'sequenceRhythm': {
        if (this.hosted) break;
        const r = readSequenceRhythm(d.rhythm);
        if (d.rhythm != null && !r) break;
        const old = this.sequenceRhythm;
        this.sequenceRhythm = r;
        if (!!old?.enabled !== !!r?.enabled || (r?.enabled && (old?.mode !== r.mode || old?.cycleBeats !== r.cycleBeats || (r.mode === 'fit' && old?.steps !== r.steps)))) {
          this.seqGateOff();
          if (r?.enabled) {
            const dur = this.polyDuration(r, this.p[SEQ_BPM] || 120);
            const sw = Math.min(1, Math.max(0, this.p[SEQ_SWING] || 0));
            this.polyK = this.polyPhase(r, Math.max(0, currentFrame - this.seqAnchor), dur, sw, Math.ceil);
            this.seqToNext = this.polyFrame(r, this.polyK, dur, sw) - Math.max(0, currentFrame - this.seqAnchor);
          } else {
            const grid = { mode: 'grid' }, dur = this.polyDuration(grid, this.p[SEQ_BPM] || 120);
            const elapsed = Math.max(0, currentFrame - this.seqAnchor), sw = this.p[SEQ_SWING] || 0;
            const k = this.polyPhase(grid, elapsed, dur, sw, Math.ceil);
            this.seqStep = k % 16 - 1;
            this.seqChainPos = Math.floor(k / 16) % this.seqChain.length;
            this.seqToNext = this.polyFrame(grid, k, dur, sw) - elapsed;
          }
        }
        break;
      }
      case 'arp': {
        if (this.hosted) break;
        const a = d.config;
        // 16 notes, or a chord-track cycle (a multiple of 16) whose rhythm
        // lanes repeat every 16 steps.
        if (a && (!Array.isArray(a.notes) || a.notes.length < 16 || a.notes.length > 1024 || a.notes.length % 16 ||
          !Array.isArray(a.hits) || !Array.isArray(a.accents) ||
          !Number.isFinite(a.rate) || !Number.isFinite(a.gate))) break;
        const changedMode = !!a !== !!this.arp;
        this.arp = a ? { notes: a.notes.map(n => Number.isInteger(n) && n >= 0 && n <= 127 ? n : -1),
          hits: a.hits.slice(0, 16), accents: a.accents.slice(0, 16), ...readCompStack(a),
          rate: Math.max(.125, Math.min(1, a.rate)), gate: Math.max(.05, Math.min(.95, a.gate)) } : null;
        if (changedMode || (a && !a.notes.some(n => n >= 0))) this.seqGateOff();
        if (changedMode) { this.seqStep = -1; this.seqChainPos = 0; this.seqToNext = 0; }
        break;
      }
      case 'scale': this.seqScale = readScaleMap(d.map); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          this.seqChain = d.list.map((x) => x | 0);
          this.seqChainPos = Math.min(this.seqChainPos, this.seqChain.length - 1);
        }
        break;
      case 'play':
        if (this.hosted) break; // conductor owns the transport
        this.seqGateOff(); // restarting must not orphan an old gate
        this.seqPlaying = true;
        this.polyK = 0;
        this.seqAnchor = currentFrame;
        this.port.postMessage({ t: 'anchor', frame: this.seqAnchor });
        this.seqStep = -1;
        this.seqChainPos = 0;
        this.seqToNext = 0;
        break;
      case 'stop':
        this.seqPlaying = false;
        this.seqStep = -1;
        this.seqGateOff();
        break;
      case 'panic':
        for (const v of this.voices) v.kill();
        this.seqOffQueue.length = 0;
        this.seqOnQueue.length = 0;
        this.held.length = 0;
        this.seqLastNote = -1;
        if (!d.preserveTransport) {
          this.clip = null;
          this.clipPend = null;
          this.clipStopAt = -1;
          this.clipStep = -1;
        }
        this.fx.reset();
        break;
      case 'host': this.hosted = !!d.on; break;
      case 'tempo':
        if (Number.isFinite(d.bpm)) { this.hostBpm = d.bpm; this.bpm = Math.min(1000, Math.max(1, d.bpm)); }
        if (Number.isFinite(d.swing)) this.hostSwing = d.swing;
        const anchorMoved = Number.isFinite(d.anchor) && d.anchor !== this.hostAnchor;
        if (Number.isFinite(d.anchor)) this.hostAnchor = d.anchor;
        if (anchorMoved) { this.frameNow = currentFrame; this.polyClipArm(); } else this.polyClipRetime();
        break;
      case 'clip':
        this.clipPend = { data: new Uint8Array(d.data), bars: Math.max(1, d.bars | 0), at: +d.atFrame || 0, arp: readClipArp(d.arp), sequenceRhythm: readSequenceRhythm(d.sequenceRhythm), scale: readScaleMap(d.scale) };
        this.clipStopAt = -1; // a new launch supersedes a pending stop
        break;
      case 'seqauto': this.seqAuto = this.autoRead(d.lanes); break;
      case 'auto': {
        // Follows its clip/clipupdate message: attach to the pending launch,
        // else to the playing clip.
        const lanes = this.autoRead(d.lanes);
        if (this.clipPend) this.clipPend.auto = lanes;
        else if (this.clip) this.clip.auto = lanes;
        break;
      }
      case 'clipstop':
        this.clipPend = null; // a stop cancels a pending launch
        this.clipStopAt = +d.atFrame || 0;
        break;
      case 'clipupdate': {
        // Hosted hot-swap (SQ-4): replace pattern bytes in place. Position is
        // derived arithmetic, so a live swap never moves the playhead.
        const data = new Uint8Array(d.data);
        const bars = Math.max(1, d.bars | 0);
        const arp = readClipArp(d.arp);
        const sequenceRhythm = readSequenceRhythm(d.sequenceRhythm), scale = readScaleMap(d.scale);
        if (this.clipPend) {
          this.clipPend = { data, bars, at: this.clipPend.at, arp, sequenceRhythm, scale, auto: this.clipPend.auto };
        } else if (this.clip) {
          const resized = bars !== this.clip.bars || this.clip.arp?.notes.length !== arp?.notes.length;
          const oldRhythm = this.clip.sequenceRhythm;
          const rephase = this.clip.arp?.rate !== arp?.rate || !!oldRhythm?.enabled !== !!sequenceRhythm?.enabled
            || (sequenceRhythm?.enabled && (oldRhythm?.mode !== sequenceRhythm.mode || oldRhythm?.cycleBeats !== sequenceRhythm.cycleBeats
              || (sequenceRhythm.mode === 'fit' && oldRhythm?.steps !== sequenceRhythm.steps)));
          this.clip = { data, bars, arp, sequenceRhythm, scale, auto: this.clip.auto };
          if (rephase) { this.seqGateOff(); this.clipStep = this.clipPhase(Math.round) - 1; this.clipToNext = 0; this.polyClipArm(); }
          else if (arp && !arp.notes.some((n, i) => n >= 0 && arp.hits[i % 16])) this.seqGateOff();
          // Re-derive the phase only on a bar-count change (plain modulo can
          // land a grown clip half a cycle off). Same-length edits — every
          // sequencer click — are a pure data swap: touching the phase inside
          // a swing/quantization window would skip a step and desync devices.
          if (resized && !rephase && this.clipStep >= 0) this.clipStep = this.clipPhase(Math.floor);
        }
        break;
      }
    }
  }

  // Wrap a published table description. `buf` is either transferred (single
  // slot) or structured-cloned (full list); either way the Float32Array is a
  // view onto it, never a copy.
  makeTable(x) {
    return {
      frames: x.frames, mips: x.mips, size: x.size, mask: x.size - 1,
      data: new Float32Array(x.buf),
    };
  }

  // ---------- note sequencer ----------
  seqRead(pat, s) {
    const o = (pat * SEQ_STEPS + s) * SEQ_STRIDE;
    const flags = this.seqPats[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      duration: Math.max(1, Math.min(63, (flags >> 2) & 0x3f)),
      semi: Math.min(11, this.seqPats[o + 1]) + 12 * (Math.min(2, this.seqPats[o + 2]) - 1),
    };
  }

  // Gate off every sounding sequencer note (stop / clip swap / clip stop).
  seqGateOff() {
    for (const e of this.seqOffQueue) this.noteOff(e.note);
    this.seqOffQueue.length = 0;
    this.seqOnQueue.length = 0;
    this.seqLastNote = -1;
  }

  // Schedule an independent note-off for `note` at `remaining` samples. A
  // retriggered pitch supersedes any pending off for it (one voice per note),
  // so overlapping DIFFERENT notes ring concurrently while a repeated pitch
  // takes the new duration.
  seqScheduleOff(note, remaining) {
    // Compact in place: the audio thread must not allocate an array for each
    // sequencer event.
    let w = 0;
    for (let i = 0; i < this.seqOffQueue.length; i++) {
      const e = this.seqOffQueue[i];
      if (e.note !== note) this.seqOffQueue[w++] = e;
    }
    this.seqOffQueue.length = w;
    this.seqOffQueue.push({ note, remaining });
    this.seqLastNote = note;
  }

  // ---------- clip automation (SQ-4) ----------
  // Each lane is an absolute-value table {i, table, len, fit, rot} played on
  // the shared anchor clock: grid lanes cycle every `len` sixteenths, FIT
  // lanes spread `len` slots over `fit` beats. autoHeld keeps the stored knob
  // value of every automated param; it returns when the lane leaves the clip.
  autoRead(list) {
    const lanes = [];
    for (const l of Array.isArray(list) ? list : []) {
      const i = PID[l.k];
      const len = Math.max(1, l.len | 0);
      const table = new Float32Array(l.table);
      if (i === undefined || table.length !== len * 16) continue;
      lanes.push({ i, table, len, fit: Math.max(0, +l.fit || 0), rot: +l.rot || 0 });
    }
    return lanes;
  }

  autoTick(frame) {
    const lanes = this.hosted ? (this.clip ? this.clip.auto || null : null)
      : (this.seqPlaying ? this.seqAuto : null);
    if (lanes !== this.autoLanes) {
      this.autoLanes = lanes;
      for (const [i, v] of this.autoHeld) {
        if (lanes && lanes.some((l) => l.i === i)) continue;
        this.autoHeld.delete(i);
        this.p[i] = v;
      }
    }
    if (!lanes || !lanes.length) return;
    const bpm = this.hosted ? Math.max(60, Math.min(200, this.hostBpm || 120)) : this.bpm;
    const anchor = this.hosted ? this.hostAnchor : this.seqAnchor;
    const steps = Math.max(0, frame - anchor) / ((60 / bpm / 4) * sampleRate);
    for (let n = 0; n < lanes.length; n++) {
      const l = lanes[n];
      let x = ((l.fit ? ((steps / 4) % l.fit) / l.fit * l.len : steps) - l.rot) % l.len;
      if (x < 0) x += l.len;
      // A tiny negative remainder can round up to len when adding len.
      if (x >= l.len) x = 0;
      const f = x * 16, a = f | 0, b = a + 1 < l.table.length ? a + 1 : 0;
      if (!this.autoHeld.has(l.i)) this.autoHeld.set(l.i, this.p[l.i]);
      this.p[l.i] = l.table[a] + (l.table[b] - l.table[a]) * (f - a);
    }
  }


  polyDuration(r, bpm) {
    return 60 / Math.max(60, Math.min(200, bpm || 120)) * sampleRate * (r.mode === 'fit' ? r.cycleBeats / r.steps : .25);
  }
  polyFrame(r, k, dur, swing) {
    return k * dur + (r.mode === 'grid' && k % 2 ? Math.min(1, Math.max(0, swing || 0)) * SEQ_SWING_MAX * dur : 0);
  }
  polyPhase(r, elapsed, dur, swing, quantize) {
    let k = Math.max(0, Math.floor(elapsed / dur));
    if (this.polyFrame(r, k, dur, swing) > elapsed + 1e-6) k = Math.max(0, k - 1);
    if (quantize === Math.floor) return k;
    if (quantize === Math.ceil) return this.polyFrame(r, k, dur, swing) >= elapsed - 1e-6 ? k : k + 1;
    return elapsed - this.polyFrame(r, k, dur, swing) < this.polyFrame(r, k + 1, dur, swing) - elapsed ? k : k + 1;
  }
  polySource(r, k) { return ((k - r.rotation) % r.steps + r.steps) % r.steps; }
  polyClipArm() {
    const r = this.clip?.sequenceRhythm;
    if (!r?.enabled || this.clip.arp) return;
    const dur = this.polyDuration(r, this.hostBpm);
    const elapsed = Math.max(0, this.frameNow - this.hostAnchor);
    const k = this.polyPhase(r, elapsed, dur, this.hostSwing, Math.ceil);
    this.clipStep = k - 1;
    this.clipToNext = this.hostAnchor + this.polyFrame(r, k, dur, this.hostSwing) - this.frameNow;
  }
  // Tempo/swing change: keep the last fired step and only re-time the next one,
  // so a boundary that already fired can never fire again. Messages land between
  // quanta, so currentFrame is the right "now" here (frameNow would be stale).
  polyClipRetime() {
    const r = this.clip?.sequenceRhythm;
    if (!r?.enabled || this.clip.arp) return;
    if (this.clipStep < 0) { this.polyClipArm(); return; }
    const dur = this.polyDuration(r, this.hostBpm);
    this.clipToNext = this.hostAnchor + this.polyFrame(r, this.clipStep + 1, dur, this.hostSwing) - currentFrame;
  }
  polyClipFire() {
    const r = this.clip.sequenceRhythm, k = this.clipStep + 1;
    const dur = this.polyDuration(r, this.hostBpm);
    const s = this.polySource(r, k);
    this.polyEmit(r, k, dur, true);
    this.clipStep = k;
    this.clipToNext = this.hostAnchor + this.polyFrame(r, k + 1, dur, this.hostSwing) - this.frameNow;
    this.port.postMessage({ t: 'pos', step: s, bar: 0 });
  }
  polyFire(bpm) {
    const r = this.sequenceRhythm, k = this.polyK++;
    const dur = this.polyDuration(r, bpm);
    const s = this.polySource(r, k);
    const event = this.polyEmit(r, k, dur, false);
    this.seqStep = s;
    const sw = Math.min(1, Math.max(0, this.p[SEQ_SWING] || 0));
    this.seqToNext += this.polyFrame(r, k + 1, dur, sw) - this.polyFrame(r, k, dur, sw);
    this.port.postMessage({ t: 'step', s, pat: 0, ...event });
  }
  polyEmit(r, k, dur, hosted) {
    const s = this.polySource(r, k);
    const chord = hosted ? Array.from({ length: WT_POLY_LANES }, (_, lane) => this.clipRead(s, lane)).filter(st => st.on)
      : [this.seqRead(0, s)].filter(st => st.on);
    if (this.p[MASTER_MONO] && chord.length > 1) chord.length = 1;
    const root = (this.p[SEQ_ROOT] | 0) || 48;
    const bpm = Math.max(60, Math.min(200, (hosted ? this.hostBpm : this.p[SEQ_BPM]) || 120));
    const pos = this.polyFrame(r, k, dur, 0) / (60 / bpm / 4 * sampleRate) + .1;
    for (const st of chord) {
      const note = scaleNote(hosted ? this.clip.scale : this.seqScale, pos, root + st.semi);
      this.noteOn(note, st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL);
      this.seqScheduleOff(note, st.duration * dur);
    }
    return {};
  }

  // ---------- hosted clip transport ----------
  // A clip is bars*16 steps of the same 3-byte layout, bar-major. Pending
  // commands execute in the render quantum containing their atFrame — every
  // device on the shared context resolves the same frame to the same block.
  clipRead(abs, lane = 0) {
    const o = (abs * WT_POLY_LANES + lane) * SEQ_STRIDE;
    const flags = this.clip.data[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      duration: Math.max(1, Math.min(63, (flags >> 2) & 0x3f)),
      semi: Math.min(11, this.clip.data[o + 1]) + 12 * (Math.min(2, this.clip.data[o + 2]) - 1),
    };
  }

  hostTick(n) {
    const end = this.frameNow + n;
    if (this.clipStopAt >= 0 && this.clipStopAt < end) {
      this.clipStopAt = -1;
      if (this.clip) {
        this.clip = null;
        this.clipStep = -1;
        this.seqGateOff();
      }
      // ack even when nothing was playing — the stop may have targeted a
      // pending-only launch and the conductor clears its STOP marker on this
      this.port.postMessage({ t: 'clipstop', frame: this.frameNow });
    }
    if (this.clipPend && this.clipPend.at < end) {
      this.clip = this.clipPend;
      this.clipPend = null;
      // Phase-lock to the shared timebase: enter at the global song position
      // modulo the clip length, so a (re)launch can never desync devices —
      // position is derived from the anchor, never restarted at step 0.
      this.clipStep = this.clipPhase(Math.round) - 1;
      this.clipToNext = 0;
      this.polyClipArm();
      this.seqGateOff(); // the old clip's tail note ends where the new clip starts
      this.port.postMessage({ t: 'clipstart', frame: this.frameNow });
    }
    if (this.clip) {
      if (this.clipToNext <= 0) this.clipFire();
      this.clipToNext -= n;
    }
  }

  // Global step index (mod clip length) at the current frame. Activation
  // rounds (atFrame sits at a boundary, block-quantized slightly early);
  // mid-flight resizes floor (the last fired step).
  clipPhase(quantize) {
    const r = this.clip.sequenceRhythm;
    if (r?.enabled && !this.clip.arp) {
      const dur = this.polyDuration(r, this.hostBpm);
      return this.polyPhase(r, Math.max(0, this.frameNow - this.hostAnchor), dur, this.hostSwing, quantize);
    }
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = (60 / bpm) * (this.clip.arp?.rate ?? .25) * sampleRate;
    const total = this.clip.arp ? this.clip.arp.notes.length : this.clip.bars * SEQ_STEPS;
    const idx = quantize(Math.max(0, this.frameNow - this.hostAnchor) / dur);
    return ((idx % total) + total) % total;
  }

  clipFire() {
    if (this.clip.arp) { this.clipArpFire(); return; }
    if (this.clip.sequenceRhythm?.enabled) { this.polyClipFire(); return; }
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.hostSwing || 0));
    const total = this.clip.bars * SEQ_STEPS;
    const abs = (this.clipStep + 1) % total;
    const s = abs % SEQ_STEPS;
    const chord = Array.from({ length: WT_POLY_LANES }, (_, lane) => this.clipRead(abs, lane)).filter((st) => st.on);
    // Mono: the melody lives in the first active lane; the rest of a chord
    // step would just steal the line note for note.
    if (this.p[MASTER_MONO] && chord.length > 1) chord.length = 1;

    const offNow = s % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    // The anchor-grid sixteenth of this step: it also places the scale map.
    const idx = Math.round((this.frameNow - this.hostAnchor - offNow) / dur);
    if (chord.length) {
      const root = (this.p[SEQ_ROOT] | 0) || 48;
      for (const st of chord) {
        const note = scaleNote(this.clip.scale, idx, root + st.semi);
        this.noteOn(note, st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL);
        this.seqScheduleOff(note, st.duration * dur);
      }
    }

    this.clipStep = abs;
    const sNext = (s + 1) % SEQ_STEPS;
    const offNext = sNext % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    // Schedule the next step at its absolute anchor-grid time. A free-running
    // countdown (dur - offNow + offNext) drops the block-quantization residue
    // each fire and drifts late without bound against the shared timebase.
    this.clipToNext = this.hostAnchor + (idx + 1) * dur + offNext - this.frameNow;
    this.port.postMessage({ t: 'pos', step: s, bar: (abs / SEQ_STEPS) | 0 });
  }

  clipArpFire() {
    const a = this.clip.arp;
    const s = (this.clipStep + 1) % a.notes.length, r = s % 16;
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = 60 / bpm * a.rate * sampleRate;
    const swing = Math.max(0, Math.min(1, this.hostSwing || 0));
    const offNow = s % 2 ? swing * SEQ_SWING_MAX * dur : 0;
    const idx = Math.round((this.frameNow - this.hostAnchor - offNow) / dur);
    const next = this.hostAnchor + (idx + 1) * dur + ((s + 1) % 2 ? swing * SEQ_SWING_MAX * dur : 0);
    const interval = Math.max(1, next - this.frameNow);
    if (a.stack) this.compFire(a, s, r, interval);
    else {
      this.seqGateOff();
      if (a.hits[r] && a.notes[s] >= 0) {
        this.noteOn(a.notes[s], a.accents[r] ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL);
        this.seqScheduleOff(a.notes[s], interval * a.gate);
      }
    }
    this.clipStep = s;
    this.clipToNext = interval;
    this.port.postMessage({ t: 'pos', step: s, bar: 0 });
  }

  // One comp step: a held (tied) step keeps sounding; any other step ends the
  // old voicing and strikes its own for `lens` steps, gated on the last one.
  compFire(a, s, r, interval) {
    const len = a.lens[s];
    if (len === 0) return;
    this.seqGateOff();
    const vel = a.accents[r] ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL, off = (len - 1 + a.gate) * interval;
    // Strummed notes enter one by one in stack order, all releasing together;
    // the whole strum spans at most half of the sounding time.
    const notes = a.stack[s], gap = notes.length > 1 ? Math.min(a.strum * sampleRate / 1000, off * .5 / (notes.length - 1)) : 0;
    notes.forEach((n, k) => {
      if (k && gap > 0) this.seqOnQueue.push({ note: n, vel, remaining: k * gap, off: off - k * gap });
      else { this.noteOn(n, vel); this.seqScheduleOff(n, off); }
    });
  }

  seqFire() {
    const bpm = Math.max(60, Math.min(200, this.p[SEQ_BPM] || 120));
    if (this.arp) {
      const a = this.arp;
      const s = (this.seqStep + 1) % a.notes.length, r = s % 16;
      const dur = 60 / bpm * a.rate * sampleRate;
      const swing = Math.min(1, Math.max(0, this.p[SEQ_SWING] || 0));
      const interval = dur * (1 + (s % 2 ? -1 : 1) * swing * SEQ_SWING_MAX);
      const note = a.notes[s];
      if (a.stack) this.compFire(a, s, r, interval);
      else this.seqGateOff();
      if (!a.stack && a.hits[r] && note >= 0) {
        this.noteOn(note, a.accents[r] ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL);
        this.seqScheduleOff(note, interval * a.gate);
      }
      this.seqStep = s;
      this.seqToNext = interval;
      this.port.postMessage({ t: 'step', s, pat: 0 });
      return;
    }
    if (this.sequenceRhythm?.enabled) { this.polyFire(bpm); return; }
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.p[SEQ_SWING] || 0));
    if (this.seqStep + 1 >= SEQ_STEPS) {
      this.seqStep = -1;
      this.seqChainPos = (this.seqChainPos + 1) % this.seqChain.length;
    }
    const s = this.seqStep + 1;
    const pat = this.seqChain[this.seqChainPos] | 0;
    const st = this.seqRead(pat, s);

    if (st.on) {
      const root = (this.p[SEQ_ROOT] | 0) || 48;
      const n = scaleNote(this.seqScale, this.seqChainPos * SEQ_STEPS + s, root + st.semi);
      const vel = st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL;
      this.noteOn(n, vel);
      this.seqScheduleOff(n, st.duration * dur);
    }

    this.seqStep = s;
    const offNow = s % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    const sNext = (s + 1) % SEQ_STEPS;
    const offNext = sNext % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    this.seqToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'step', s, pat });
  }

  noteOn(n, vel) {
    // Track held keys mode-independently (in place compaction, no extra
    // allocation beyond one small per-event push, same profile as
    // seqOffQueue) so a mono<->poly flip can't strand a key that was only
    // ever tracked in the other mode. Re-press of a held pitch moves it to
    // the top of the stack.
    let w = 0;
    for (let i = 0; i < this.held.length; i++) if (this.held[i] !== n) this.held[w++] = this.held[i];
    this.held.length = w;
    this.held.push(n);

    if (this.p[MASTER_MONO]) this.monoNoteOn(n, vel);
    else this.polyNoteOn(n, vel);
  }

  monoNoteOn(n, vel) {
    // The newest gated voice carries the mono line; release any others (they
    // can only exist right after a poly -> mono flip).
    let voice = null;
    for (const v of this.voices) {
      if (!v.gate) continue;
      if (!voice) { voice = v; continue; }
      if (v.age > voice.age) { voice.noteOff(); voice = v; } else v.noteOff();
    }
    if (voice) {
      // Legato: retune only — renderVoice glides v.pitch toward v.note, and
      // not calling Voice.noteOn is what keeps the envelopes running.
      if (this.p[MASTER_LEGATO]) voice.note = n;
      else voice.retrigger(n, vel, this.rng, this.modRng);
      this.lastPitch = n;
      return;
    }
    this.polyNoteOn(n, vel); // from silence: normal trigger (glide-from-lastPitch, steal fade)
  }

  monoNoteOff(n) {
    for (const v of this.voices) {
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) {
        const back = this.held[this.held.length - 1];
        if (back !== undefined) { // fall back to the held note
          if (this.p[MASTER_LEGATO]) v.note = back; else v.retrigger(back, v.vel, this.rng, this.modRng);
          this.lastPitch = back;
        }
        else v.noteOff();
      }
    }
  }

  polyNoteOn(n, vel) {
    let voice = this.voices.find((v) => v.gate && v.note === n);
    // Same note already queued behind a steal fade: replace it in place.
    if (!voice) voice = this.voices.find((v) => v.pending && v.pending.n === n);
    if (!voice) voice = this.voices.find((v) => !v.active && !v.pending);
    if (!voice) {
      // Steal order: released, then gated (lowest age each). A voice with a
      // note already pending is taken only when every voice is pending, so a
      // burst of notes never overwrites its own queued notes.
      let rel = null, gated = null, pend = null;
      for (const v of this.voices) {
        if (v.pending) { if (!pend || v.pending.age < pend.pending.age) pend = v; }
        else if (v.gate) { if (!gated || v.age < gated.age) gated = v; }
        else if (!rel || v.age < rel.age) rel = v;
      }
      voice = rel || gated || pend;
    }
    const glide = this.p[MASTER_GLIDE] || 0;
    const start = glide > 0.001 ? this.lastPitch : n;
    this.lastPitch = n;
    if (voice.pending || (voice.active && Math.max(voice.ampEnv.level, voice.ampSm) > 1e-3)) {
      // Steal / same-note retrigger of an audible voice: hard-resetting phases
      // and filter state under a hot envelope clicks. Fade out instead (Env
      // state 5) and start the note once the voice reaches silence.
      voice.pending = { n, vel, start, age: this.clock++ };
      voice.gate = false;
      voice.ampEnv.state = 5;
    } else {
      voice.noteOn(n, vel, start, this.clock++, this.rng, this.phaseRand(WARPA, OSCA), this.phaseRand(WARPB, OSCB), this.modRng);
    }
  }

  noteOff(n) {
    let w = 0;
    for (let i = 0; i < this.held.length; i++) if (this.held[i] !== n) this.held[w++] = this.held[i];
    this.held.length = w;

    if (this.p[MASTER_MONO]) { this.monoNoteOff(n); return; }
    for (const v of this.voices) {
      // A note released before its steal fade finished must not start at all.
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) v.noteOff();
    }
  }

  // Oscillator start phase: random, except for a single-voice FM/AM/RM carrier.
  // That one starts at phase 0 with its modulator, so every note has the same
  // carrier/modulator phase relation and therefore the same timbre.
  phaseRand(wbase, base) {
    return !((this.p[wbase] | 0) >= WARP_FM && (this.p[base + O_UNI] | 0) <= 1);
  }

  // Configure one oscillator's per-block render state. Returns true if audible.
  // Reads the modulated snapshot `pm` for the per-param dests (pos/level/pan/detune/
  // spread); pitch and the pan global offset stay as direct additive terms.
  // `wbase` is the oscillator's warp block and `obase` the other oscillator.
  // `wantMod` keeps a silent oscillator's table state alive, because the other
  // oscillator reads it as an FM/AM/RM modulator (o.modOk).
  setupOsc(o, base, wbase, obase, voice, pm, mPitch, mPan, n, wantMod) {
    const p = this.p;
    o.modOk = false;
    if (!wantMod) o.pCinc = -1;
    if (!p[base + O_ON]) return false;
    const table = this.tables[p[base + O_TABLE] | 0];
    if (!table) return false;

    const basePitch = voice.pitch + this.bend + p[base + O_OCT] * 12 + p[base + O_SEMI] + p[base + O_FINE] / 100 + mPitch * 12;
    const freq = 440 * Math.pow(2, (basePitch - 69) / 12);
    if (!(freq > 0 && freq <= sampleRate * 0.45)) return false; // inverted: NaN-safe

    let level = Math.min(1.2, Math.max(0, pm[base + O_LEVEL]));
    level *= level;
    const audible = level >= 1e-5; // false for NaN too
    if (!audible && !wantMod) return false;

    // Warp: map the amount to the mode's parameter `w` and to `wf`, the
    // fastest read speed relative to the oscillator (for the mip choice).
    let mode = p[wbase] | 0, w = 0, wf = 1;
    const amt = Math.min(1, Math.max(0, pm[wbase + 1] || 0));
    switch (mode) {
      case WARP_SYNC: case WARP_SYNCWIN:
        // Slave ratio 1..16, held below sr/4 so the slave itself cannot fold.
        w = Math.min(Math.pow(2, amt * 4), Math.max(1, SYNC_MAX_CPS / (freq / sampleRate)));
        // Hard sync keeps half an octave of headroom for the reset crossfade.
        wf = mode === WARP_SYNC ? w * SYNC_HEADROOM : w;
        break;
      case WARP_BEND: case WARP_ASYM: w = Math.pow(2, (amt - 0.5) * 6); wf = w > 1 ? w : 1 / w; break;
      case WARP_PWM: w = 1 - 0.9 * amt; wf = 1 / w; break;    // width of the squeezed cycle
      case WARP_MIRROR: w = 0.5 + 0.4 * amt; wf = 1 / (1 - w); break; // turn point
      case WARP_QUANT: w = Math.pow(2, 1 + 7 * (1 - amt)); break;     // steps per cycle, 256..2
      case WARP_FM: case WARP_AM: case WARP_RM:
        // The modulator is the other oscillator; without it the warp is off.
        if (!p[obase + O_ON] || !this.tables[p[obase + O_TABLE] | 0]) { mode = 0; break; }
        if (mode === WARP_FM) {
          w = amt * amt * FM_MAX_CYCLES;
          // Peak deviation of a sine modulator: 2*pi*w*fm, relative to the carrier.
          const semis = (p[obase + O_OCT] - p[base + O_OCT]) * 12 + p[obase + O_SEMI] - p[base + O_SEMI]
            + (p[obase + O_FINE] - p[base + O_FINE]) / 100;
          wf = 1 + TWO_PI * w * Math.pow(2, semis / 12);
        } else { w = amt; wf = 1 + Math.min(1, amt * 4); }
        break;
      default: mode = 0;
    }
    if (!(wf <= WARP_MIP_MAX)) wf = WARP_MIP_MAX; // NaN-safe
    // The chunk ramps from the previous parameter, so cover the faster of the two.
    const wfMip = o.havePrev && o.pWarp === mode ? Math.max(wf, o.pWf) : wf;
    o.warp = mode; o.warpW = w; o.wf = wf;

    const uni = Math.max(1, Math.min(MAXUNI, p[base + O_UNI] | 0));
    const det = pm[base + O_DET];
    const spr = pm[base + O_SPR];
    const blend = Math.min(1, Math.max(0, pm[base + O_BLEND])); // clamp matches JUCE
    const basePan = Math.max(-1, Math.min(1, pm[base + O_PAN] + mPan));

    // position smoothing (avoids zipper on fast morph modulation)
    let pos = Math.min(1, Math.max(0, pm[base + O_POS]));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * smoothCoef(n, POS_TAU * sampleRate);
    const posF = o.posSm * (table.frames - 1);
    const f0 = posF | 0;
    const f1 = Math.min(table.frames - 1, f0 + 1);
    o.ft = posF - f0;

    const cps = freq / sampleRate;
    // |det|: modulation can push detune negative; the widest unison ratio is
    // 2^(|det|*50/1200) regardless of sign, and underestimating it would pick
    // a mip one notch too fine (the alias headroom is only 0.074 oct).
    const maxRatio = Math.pow(2, (Math.abs(det) * 50) / 1200);

    // Continuous mip selection. mipF is the exact (real-valued) mip the pitch
    // calls for; ceil(mipF) is the alias-free choice. For the first W octaves
    // above a mip boundary we crossfade from the finer mip — which is still
    // alias-free there, because mips are built against 0.475*sr while Nyquist
    // is 0.5*sr (0.5/0.475 = 2^0.074 of headroom). Result: glides and bends
    // never step in brightness, and static pitches never fold.
    const W = 0.07;
    const mipF = Math.log2((cps * maxRatio * wfMip * 1024) / 0.475);
    let mip = 0, mipBlend = 0;
    if (mipF > 0) {
      mip = Math.min(table.mips - 1, Math.ceil(mipF));
      const over = mipF - (mip - 1); // octaves above the previous boundary
      if (over < W) mipBlend = 1 - over / W;
    }
    const fineMip = mip > 0 ? mip - 1 : 0;

    o.off0 = (f0 * table.mips + mip) * table.size;
    o.off1 = (f1 * table.mips + mip) * table.size;
    o.off0b = (f0 * table.mips + fineMip) * table.size;
    o.off1b = (f1 * table.mips + fineMip) * table.size;
    o.mipBlend = mipBlend;
    o.data = table.data;
    o.mask = table.mask;
    o.size = table.size;
    o.uni = uni;

    // BLEND: weight outer (most-detuned) voices vs. the center. weight_u =
    // 1-(1-blend)*|sprd_u|. blend=1 => all weights 1 (identical to legacy).
    // Loudness held ~constant by normalising on sqrt of the sum of squared
    // weights instead of the raw voice count.
    let sumW2 = 0;
    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps * ratio * table.size;
      const weight = 1 - (1 - blend) * Math.abs(sprd);
      sumW2 += weight * weight;
      const pan = Math.max(-1, Math.min(1, sprd * spr + basePan));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a) * weight;
      o.gr[u] = Math.sin(a) * weight;
    }
    // `|| 1` guards uni=2,blend=0 (both endpoints -> sumW2=0). Matches the JUCE
    // `sumW2 > 0 ? sumW2 : 1` predicate exactly, so the engines stay in lockstep.
    o.gain = (level * 0.32) / Math.sqrt(sumW2 || 1);
    o.cinc = cps * table.size;
    o.modOk = true;
    return audible;
  }

  // Render the oscillator's modulator signal for the other oscillator's
  // FM/AM/RM warp: one undetuned, unwarped voice of its current table frame.
  renderMod(o, out, n) {
    const data = o.data, mask = o.mask, size = o.size;
    const off0 = o.off0, off1 = o.off1, ft = o.ft;
    const inc1 = o.cinc;
    const inc0 = o.pCinc > 0 ? o.pCinc : inc1;
    const dInc = (inc1 - inc0) / n;
    let ph = wrapOscPhase(o.mph, size);
    for (let i = 0; i < n; i++) {
      const idx = ph | 0;
      const f = ph - idx;
      const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
      const s0 = rdH(data, off0, im1, idx, i2, i3, f);
      out[i] = s0 + ft * (rdH(data, off1, im1, idx, i2, i3, f) - s0);
      ph += inc0 + dInc * i;
      if (ph >= size) ph -= size;
    }
    o.mph = wrapOscPhase(ph, size);
    o.pCinc = inc1;
  }

  // One interpolated read of the oscillator's current frame pair at `ph`
  // samples, with the mip crossfade. For the few reads outside the main loops.
  readFrame(o, ph, ft) {
    const data = o.data, mask = o.mask;
    const ii = ph | 0, idx = ii & mask, f = ph - ii;
    const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
    const c0 = rdH(data, o.off0, im1, idx, i2, i3, f);
    let s = c0 + ft * (rdH(data, o.off1, im1, idx, i2, i3, f) - c0);
    if (o.mipBlend >= 0.001) {
      const f0 = rdH(data, o.off0b, im1, idx, i2, i3, f);
      s += o.mipBlend * (f0 + ft * (rdH(data, o.off1b, im1, idx, i2, i3, f) - f0) - s);
    }
    return s;
  }

  // Warped oscillator read. `pn` is the oscillator phase in cycles; each mode
  // maps it to the read phase `q` (SYNC..QUANTIZE, FM) or to a gain (SYNC WIN,
  // AM, RM). The map parameter ramps across the chunk like the increments do.
  // `mod` is the other oscillator's signal for FM/AM/RM. Mirrors
  // Engine::renderOscWarp.
  renderOscWarp(o, tmpL, tmpR, n, mod) {
    const data = o.data, mask = o.mask, size = o.size, g = o.gain;
    const invN = 1 / n, invSize = 1 / size;
    const rp = o.havePrev && o.pUni === o.uni;
    const ft1 = o.ft;
    const ft0 = rp && o.pOff0 === o.off0 ? o.pFt : ft1;
    const dFt = (ft1 - ft0) * invN;
    const off0 = o.off0, off1 = o.off1, off0b = o.off0b, off1b = o.off1b;
    const blend = o.mipBlend, fine = blend >= 0.001;
    const mode = o.warp;
    if (!mod) mod = this.noMod;
    const w1 = o.warpW;
    const w0 = rp && o.pWarp === mode ? o.pWarpW : w1;
    const dW = (w1 - w0) * invN;
    for (let u = 0; u < o.uni; u++) {
      let ph = wrapOscPhase(o.phases[u], size);
      const inc1 = o.incs[u];
      const inc0 = rp ? o.pIncs[u] : inc1;
      const dInc = (inc1 - inc0) * invN;
      const gl1 = o.gl[u] * g, gr1 = o.gr[u] * g;
      const gl0 = rp ? o.pGl[u] : gl1, gr0 = rp ? o.pGr[u] : gr1;
      const dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
      for (let i = 0; i < n; i++) {
        const w = w0 + dW * i;
        const inc = inc0 + dInc * i;
        const pn = ph * invSize;
        let q = pn, amp = 1, edge = 0, xq = 0;
        switch (mode) {
          case WARP_SYNC: {
            q = pn * w;
            // Reset zone: SYNC_FADE samples centred on the master wrap, where
            // the outgoing and the incoming slave crossfade. `xq` is the
            // other slave's phase (the next one before the wrap, the previous
            // one after it) and `edge` its share. The next slave starts
            // before table phase 0, so a table's own step at phase 0 keeps
            // its band-limited shape. At ratio 1 both slaves are the same.
            const h = Math.min(0.25, SYNC_FADE * 0.5 * inc * invSize);
            if (pn > 1 - h) { xq = q - w; edge = 0.5 - 0.5 * Math.cos(Math.PI * (pn - 1 + h) / (2 * h)); }
            else if (pn < h) { xq = q + w; edge = 0.5 + 0.5 * Math.cos(Math.PI * (pn + h) / (2 * h)); }
            break;
          }
          case WARP_SYNCWIN: q = pn * w; amp = SYNCWIN_GAIN * (0.5 - 0.5 * Math.cos(TWO_PI * pn)); break;
          case WARP_BEND:
            // Each half-cycle runs through x/(x + w(1-x)): w > 1 pulls the
            // waveform to the centre, w < 1 pushes it to the cycle edges.
            if (pn < 0.5) { const x = 2 * pn; q = (0.5 * x) / (x + w * (1 - x)); }
            else { const x = 2 - 2 * pn; q = 1 - (0.5 * x) / (x + w * (1 - x)); }
            break;
          case WARP_ASYM: q = pn / (pn + w * (1 - pn)); break;
          case WARP_PWM: q = pn < w ? pn / w : 1; break;
          case WARP_MIRROR: q = pn < w ? pn / w : (1 - pn) / (1 - w); break;
          case WARP_QUANT: q = Math.floor(pn * w) / w; break;
          case WARP_FM: q = pn + w * mod[i]; break;
          case WARP_AM: amp = 1 - 0.5 * w + 0.5 * w * mod[i]; break;
          default: amp = 1 - w + w * mod[i]; // WARP_RM
        }
        const rph = (q - Math.floor(q)) * size;
        const ii = rph | 0, idx = ii & mask, f = rph - ii;
        const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
        const ftN = ft0 + dFt * i;
        const sc0 = rdH(data, off0, im1, idx, i2, i3, f);
        let s = sc0 + ftN * (rdH(data, off1, im1, idx, i2, i3, f) - sc0);
        if (fine) {
          const sf0 = rdH(data, off0b, im1, idx, i2, i3, f);
          s += blend * (sf0 + ftN * (rdH(data, off1b, im1, idx, i2, i3, f) - sf0) - s);
        }
        if (edge > 0) s += (this.readFrame(o, (xq - Math.floor(xq)) * size, ftN) - s) * edge;
        s *= amp;
        tmpL[i] += s * (gl0 + dGl * i);
        tmpR[i] += s * (gr0 + dGr * i);
        ph += inc;
        if (ph >= size) ph -= size;
      }
      o.phases[u] = wrapOscPhase(ph, size);
      o.pIncs[u] = inc1;
      o.pGl[u] = gl1; o.pGr[u] = gr1;
    }
    o.pFt = ft1; o.pOff0 = o.off0; o.pUni = o.uni;
    o.pWarp = mode; o.pWarpW = w1; o.pWf = o.wf;
    o.havePrev = true;
  }

  // Finding W1: phase increments, the morph fraction and the pan/level gain
  // products ramp from the previous chunk's targets to this chunk's across n
  // samples, so block-rate modulation (LFO->pitch, glide, POS, pan, level) has
  // no staircase. The ramp is suppressed on the first chunk after note-on, on a
  // unison-count change, and (for the morph fraction) when the frame pair
  // switched — ft is a fraction WITHIN a pair. Table reads are cubic Hermite.
  // Mirrors Engine::renderOsc.
  renderOsc(o, tmpL, tmpR, n, mod) {
    if (o.warp) { this.renderOscWarp(o, tmpL, tmpR, n, mod); return; }
    const data = o.data, mask = o.mask, size = o.size, g = o.gain;
    const invN = 1 / n;
    const rp = o.havePrev && o.pUni === o.uni;
    const ft1 = o.ft;
    const ft0 = rp && o.pOff0 === o.off0 ? o.pFt : ft1;
    const dFt = (ft1 - ft0) * invN;
    const off0 = o.off0, off1 = o.off1;
    const blend = o.mipBlend;
    for (let u = 0; u < o.uni; u++) {
      let ph = wrapOscPhase(o.phases[u], size);
      const inc1 = o.incs[u];
      const inc0 = rp ? o.pIncs[u] : inc1;
      const dInc = (inc1 - inc0) * invN;
      const gl1 = o.gl[u] * g, gr1 = o.gr[u] * g;
      const gl0 = rp ? o.pGl[u] : gl1, gr0 = rp ? o.pGr[u] : gr1;
      const dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
      if (blend < 0.001) {
        // fast path — single mip, no crossfade
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const f = ph - idx;
          const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
          const s0 = rdH(data, off0, im1, idx, i2, i3, f);
          const s1 = rdH(data, off1, im1, idx, i2, i3, f);
          const s = s0 + (ft0 + dFt * i) * (s1 - s0);
          tmpL[i] += s * (gl0 + dGl * i);
          tmpR[i] += s * (gr0 + dGr * i);
          // |inc| < size is guaranteed by the 0.45*sr pitch guard, so one
          // conditional subtract wraps; the loop exit re-wraps defensively.
          ph += inc0 + dInc * i;
          if (ph >= size) ph -= size;
        }
      } else {
        // crossfade path — blend coarse mip with finer mip near mip boundary
        const off0b = o.off0b, off1b = o.off1b;
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const f = ph - idx;
          const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
          const ftN = ft0 + dFt * i;
          // coarse mip
          const sc0 = rdH(data, off0, im1, idx, i2, i3, f);
          const sc1 = rdH(data, off1, im1, idx, i2, i3, f);
          const sc = sc0 + ftN * (sc1 - sc0);
          // fine mip (richer, may alias slightly near the boundary)
          const sf0 = rdH(data, off0b, im1, idx, i2, i3, f);
          const sf1 = rdH(data, off1b, im1, idx, i2, i3, f);
          const sf = sf0 + ftN * (sf1 - sf0);
          const s = sc + blend * (sf - sc);
          tmpL[i] += s * (gl0 + dGl * i);
          tmpR[i] += s * (gr0 + dGr * i);
          ph += inc0 + dInc * i;
          if (ph >= size) ph -= size;
        }
      }
      o.phases[u] = wrapOscPhase(ph, size);
      o.pIncs[u] = inc1;
      o.pGl[u] = gl1; o.pGr[u] = gr1;
    }
    o.pFt = ft1; o.pOff0 = o.off0; o.pUni = o.uni;
    o.pWarp = 0; o.pWf = 1;
    o.havePrev = true;
  }

  // Compute one filter's block-rate coefficients. CUTOFF is shared across all
  // types: it sets corner frequency (SVF), comb pitch (COMB) or vowel morph
  // position (VOWEL). RES sets resonance / feedback / formant sharpness.
  setupFilter(fs, base, v, e2, mCut, pm, n) {
    const p = this.p;
    const ftype = p[base + F_TYPE] | 0;
    fs.ftype = ftype;

    // The cutoff Log route is kept OUT of pm and passed as mCut here so the whole
    // exponent stays in a single Math.pow — bit-identical to the legacy
    // p[cutoff] × 2^(env·4·e2 + key·(note-60)/12 + x·5). env/key are still read from
    // pm so THEY remain modulatable; the base cutoff is read straight from p.
    let fc = p[base + F_CUT] *
      Math.pow(2, pm[base + F_ENV] * 4 * e2 + (pm[base + F_KEY] * (v.note - 60)) / 12 + mCut * MOD_LOG_D);
    if (!Number.isFinite(fc)) fc = 20; // JUCE's std::max(20.0, NaN) also yields 20
    fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * smoothCoef(n, CUT_TAU * sampleRate);
    const cut = fs.cutSm;
    fs.cutTarget = cut;              // runFilter ramps cutPrev -> cutTarget
    const res = Math.min(0.999, Math.max(0, pm[base + F_RES]));

    if (ftype <= 4) {
      // Cytomic SVF. The g-dependent coefficients are recomputed per <=32-sample
      // sub-block in runFilter from the ramped cutoff (finding W1); only the
      // damping term is fixed for the chunk.
      fs.twoPole = ftype === 1; // LP24 = two cascaded stages
      if (fs.twoPole) {
        // Finding B3: all the resonance in stage 1, stage 2 critically damped.
        const r2 = res * res;
        const resT = res + 0.0035 * r2 * r2;
        const kk = 2 - 1.93 * resT;
        fs.k1 = Math.max(LP24_K_MIN, 0.5 * kk * kk);
        fs.k2 = 2;
      } else {
        fs.k1 = 2 - 1.93 * res;
        fs.k2 = fs.k1;
      }
    } else if (ftype === 5) {
      // tuned feedback comb: delay length tracks cutoff pitch, RES -> feedback
      let len = sampleRate / cut;
      len = Math.min(COMB_MAX - 2, Math.max(1, len));
      fs.combLen = len;
      fs.combFb = res * 0.97; // < 1 keeps the resonator stable
    } else {
      // VOWEL: 3-band bandpass bank, CUTOFF morphs A-E-I-O-U on a log axis
      const norm = Math.min(0.999, Math.max(0, Math.log(cut / 20) / Math.log(1000)));
      const pos = norm * 4;
      const vi = Math.min(3, pos | 0);
      const fr = pos - vi;
      const q = 2 + res * 22; // higher RES -> narrower, more vocal formants
      for (let j = 0; j < 3; j++) {
        const f0 = Math.min(sampleRate * 0.45, VOWELS[vi][j] + (VOWELS[vi + 1][j] - VOWELS[vi][j]) * fr);
        const w0 = (2 * Math.PI * f0) / sampleRate;
        const alpha = Math.sin(w0) / (2 * q);
        const a0 = 1 + alpha;
        fs.fc[j * 3] = alpha / a0;             // b0 (b1 = 0, b2 = -b0): 0 dB peak BPF
        fs.fc[j * 3 + 1] = (-2 * Math.cos(w0)) / a0; // a1
        fs.fc[j * 3 + 2] = (1 - alpha) / a0;   // a2
        fs.famp[j] = F_AMPS[j];
      }
    }
  }

  // Apply one filter (optional ADAA drive + the selected type) to a stereo block,
  // reading in*, writing out*. Stage 1 saturates in -> out, stage 2 filters in place.
  runFilter(fs, inL, inR, outL, outR, drive, n) {
    // -- drive: 2x oversampled anti-aliased tanh (ADAA). The stage is in the path
    // only while the saturator is active; at drive 0 it is a bit-exact copy. --
    const adaaTarget = Math.max(0, Math.min(1, drive / ADAA_FADE_WIDTH));
    const adaaStart = fs.adaaMix;
    const active = adaaTarget > 0 || fs.adaaMix > 1e-6;
    const dg = active ? 1 + drive * 7 : 1;
    const dcomp = active ? 1 / Math.pow(dg, 0.55) : 1;
    const kF = dcomp / dg;
    const osL = fs.osL, osR = fs.osR;
    if (active) { osL.F = kF * lcosh(dg * osL.xp); osR.F = kF * lcosh(dg * osR.xp); }
    const wStep = 1 / (DRIVE_OS_RAMP_SEC * sampleRate);
    for (let i = 0; i < n; i++) {
      const m = adaaStart + (adaaTarget - adaaStart) * ((i + 1) / n);
      outL[i] = driveOsStep(osL, inL[i], active, active, m, wStep, dg, dcomp, kF);
      outR[i] = driveOsStep(osR, inR[i], active, active, m, wStep, dg, dcomp, kF);
    }
    fs.adaaMix = active ? adaaTarget : 0;

    const ftype = fs.ftype;
    if (ftype <= 4) {
      // Finding W1: the cutoff ramps from the previous chunk's value across the
      // chunk and the SVF coefficients are recomputed per <=32-sample
      // sub-block, so an automated or modulated cutoff never steps.
      const k1 = fs.k1, k2 = fs.k2;
      const c1c = fs.cutTarget;
      const c0c = fs.cutPrev > 0 ? fs.cutPrev : c1c;
      const F = fs.svf;
      for (let at = 0; at < n; at += 32) {
        const m = Math.min(32, n - at);
        const cut = c0c + (c1c - c0c) * ((at + m) / n);
        const gC = Math.tan((Math.PI * cut) / sampleRate);
        const a1 = 1 / (1 + gC * (gC + k1));
        const a2 = gC * a1, a3 = gC * a2;
        // Stage 2 (LP24) runs its own damping, so it needs its own coefficients.
        const a1b = 1 / (1 + gC * (gC + k2));
        const a2b = gC * a1b, a3b = gC * a2b;
        for (let ch = 0; ch < 2; ch++) {
          const buf = ch === 0 ? outL : outR;
          const o1 = ch * 2;
          let ic1 = F[o1], ic2 = F[o1 + 1];
          for (let i = at; i < at + m; i++) {
            const x = buf[i];
            const v3 = x - ic2;
            const v1 = a1 * ic1 + a2 * v3;
            const v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            switch (ftype) {
              case 0: case 1: buf[i] = v2; break;       // LP
              case 2: buf[i] = k1 * v1; break;          // BP (unity peak-ish)
              case 3: buf[i] = x - k1 * v1 - v2; break; // HP
              default: buf[i] = x - k1 * v1; break;     // notch
            }
          }
          F[o1] = ic1; F[o1 + 1] = ic2;
        }
        if (fs.twoPole) {
          for (let ch = 0; ch < 2; ch++) {
            const buf = ch === 0 ? outL : outR;
            const o1 = 4 + ch * 2;
            let ic1 = F[o1], ic2 = F[o1 + 1];
            for (let i = at; i < at + m; i++) {
              const x = buf[i];
              const v3 = x - ic2;
              const v1 = a1b * ic1 + a2b * v3;
              const v2 = ic2 + a2b * ic1 + a3b * v3;
              ic1 = 2 * v1 - ic1;
              ic2 = 2 * v2 - ic2;
              buf[i] = v2;
            }
            F[o1] = ic1; F[o1 + 1] = ic2;
          }
        }
      }
      fs.cutPrev = c1c;
    } else if (ftype === 5) {
      // resonant comb: y = (1-fb)·x + fb·y[n-len], fractional read for tuning.
      // The delay length ramps across the chunk (finding W1); the fractional
      // read already supports a per-sample length.
      const len1 = fs.combLen;
      const len0 = fs.combLenPrev > 0 ? fs.combLenPrev : len1;
      const dLen = (len1 - len0) / n;
      const fb = fs.combFb, g0 = 1 - fb;
      const cl = fs.combL, cr = fs.combR;
      let w = fs.combW;
      for (let i = 0; i < n; i++) {
        const len = len0 + dLen * (i + 1);
        let rd = w - len;
        rd = ((rd % COMB_MAX) + COMB_MAX) % COMB_MAX;
        const i0 = rd | 0;
        const frac = rd - i0;
        const i1 = i0 + 1 < COMB_MAX ? i0 + 1 : 0;
        const yL = g0 * outL[i] + fb * (cl[i0] + frac * (cl[i1] - cl[i0]));
        const yR = g0 * outR[i] + fb * (cr[i0] + frac * (cr[i1] - cr[i0]));
        cl[w] = yL; cr[w] = yR;
        outL[i] = yL; outR[i] = yR;
        w = w + 1 < COMB_MAX ? w + 1 : 0;
      }
      fs.combW = w;
      fs.combLenPrev = len1;
    } else {
      // VOWEL: parallel bank of 3 bandpass biquads (transposed direct form II)
      const fc = fs.fc, fa = fs.famp, z = fs.fmt;
      for (let ch = 0; ch < 2; ch++) {
        const buf = ch === 0 ? outL : outR;
        const zb = ch * 6;
        for (let i = 0; i < n; i++) {
          const x = buf[i];
          let acc = 0.04 * x; // slight broadband floor, matching the VOX voicing
          for (let j = 0; j < 3; j++) {
            const b0 = fc[j * 3], ca1 = fc[j * 3 + 1], ca2 = fc[j * 3 + 2];
            const zi = zb + j * 2;
            const y = b0 * x + z[zi];
            z[zi] = z[zi + 1] - ca1 * y; // s1 (b1 = 0)
            z[zi + 1] = -b0 * x - ca2 * y; // s2 (b2 = -b0)
            acc += fa[j] * y;
          }
          buf[i] = acc * 0.8;
        }
      }
    }
  }

  lfoHz(base) {
    if (this.p[base + L_SYNC]) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[base + L_SYNCRATE] | 0));
      return (this.bpm / 60) * LFO_DIV_F[i];
    }
    return this.p[base + L_RATE];
  }

  // Free-running global LFO phase, updated once per block. When synced, the
  // phase is derived from the transport position (ppq, in quarter notes) so a
  // synced LFO starts on the downbeat; rate edits then preserve phase.
  // Unsynced LFOs free-run at their Hz.
  // (Retrig LFOs are per-voice and note-aligned, so they bypass this.)
  updateGlobalLfo(g, base, ppq, n, rng = this.rng, rateMul = 1) {
    if (this.p[base + L_SYNC]) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[base + L_SYNCRATE] | 0));
      const ph = g.clock.phase(ppq, LFO_DIV_F[i]);
      if (ph < g.phase) g.hold = rng.next() * 2 - 1; // grid wrap -> new S&H value
      g.phase = ph;
    } else {
      g.clock.reset();
      g.advance(this.p[base + L_RATE] * rateMul, n, rng);
    }
  }

  // One route's source value after polarity and response curve. The aux scale
  // is left in this._routeAux (the caller multiplies value, amount, aux).
  routeSource(s, src, srcs) {
    const p = this.p;
    // Polarity: SRC keeps the native range; UNI/BI convert it (params.ts polarize).
    const pol = p[MATPOL1 + s] | 0, native = SOURCE_BIPOLAR[src];
    const bi = pol === 1 ? false : pol === 2 ? true : native;
    let sv = bi === native ? srcs[src] : bi ? srcs[src] * 2 - 1 : (srcs[src] + 1) * 0.5;
    // Response curve: bend the value's size before the amount (params.ts routeCurve).
    const crv = p[MATCRV1 + s];
    if (Math.abs(crv) >= 1e-3) {
      const k = Math.max(-1, Math.min(1, crv)) * 6;
      const f = Math.min(1, bi ? Math.abs(sv) : Math.max(0, sv));
      const y = (Math.exp(k * f) - 1) / (Math.exp(k) - 1);
      sv = bi && sv < 0 ? -y : y;
    }
    // Aux source: scale by its unipolar value (params.ts auxScale).
    const aux = p[MATAUX1 + s] | 0;
    this._routeAux = aux > 0 && aux < NUM_SOURCES ? (SOURCE_BIPOLAR[aux] ? (srcs[aux] + 1) * 0.5 : srcs[aux]) : 1;
    return sv;
  }

  // Run the mod matrix on one set of source values. The 16 fixed slots live in
  // the flat store at MAT1 + s*M_STRIDE. Fills this._modAccum (route sum per
  // target param) and this._mPitch / _mAmp / _mPan, and returns whether a
  // per-param route is active. Mirrors Engine::evalRoutes.
  evalRoutes(srcs) {
    const p = this.p, accum = this._modAccum, amtMod = this._amtMod;
    accum.fill(0);
    let mPitch = 0, mAmp = 0, mPan = 0, anyRoute = false, anyAmt = false;
    // First the routes that modulate another route's amount ("MOD n AMT").
    // They use their own stored amount, so the slot order does not matter.
    for (let s = 0, b = MAT1; s < MOD_MATRIX_SIZE; s++, b += M_STRIDE) {
      const dst = p[b + M_DST] | 0;
      if (dst <= 0 || dst >= DST_AMT_SLOT.length || DST_AMT_SLOT[dst] < 0) continue;
      const src = p[b + M_SRC] | 0;
      if (!src || src >= NUM_SOURCES) continue;
      if (!anyAmt) { amtMod.fill(0); anyAmt = true; }
      amtMod[DST_AMT_SLOT[dst]] += this.routeSource(s, src, srcs) * (p[b + M_AMT] || 0) * this._routeAux;
    }
    for (let s = 0, b = MAT1; s < MOD_MATRIX_SIZE; s++, b += M_STRIDE) {
      const src = p[b + M_SRC] | 0;
      const dst = p[b + M_DST] | 0;
      if (!src || !dst || src >= NUM_SOURCES || dst >= DST_PIDX.length || DST_AMT_SLOT[dst] >= 0) continue;
      let amt = p[b + M_AMT] || 0;
      // A full-depth route moves the amount over its whole range, like any Lin param.
      if (anyAmt && amtMod[s] !== 0) amt = clampTo(amt + amtMod[s] * 2, -1, 1);
      const x = this.routeSource(s, src, srcs) * amt * this._routeAux;
      const target = DST_PIDX[dst];
      if (target === D_PITCH) mPitch += x;
      else if (target === D_AMP) mAmp += x;
      else if (target === D_PAN) mPan += x;
      else if (target >= 0) { accum[target] += x; anyRoute = true; }
    }
    this._mPitch = mPitch; this._mAmp = mAmp; this._mPan = mPan;
    return anyRoute;
  }

  // Destinations outside the voice: the FX controls and the rates of the
  // free-running LFOs. With a voice they take the route sums of the viz voice,
  // the same ones the knob rings show. With no voice the routes run on the
  // last note's sources with the live macros, wheel, aftertouch and
  // free-running LFOs, so a macro still moves a ringing tail.
  updateGlobalMods(act) {
    const p = this.p;
    let any = false;
    for (let s = 0, b = MAT1; s < MOD_MATRIX_SIZE && !any; s++, b += M_STRIDE) {
      const t = DST_PIDX[p[b + M_DST] | 0];
      any = (p[b + M_SRC] | 0) > 0 && t >= 0 && GLOBAL_DST[t] === 1;
    }
    this.fxParams = p;
    if (!any) { this.gRate.fill(1); return; }
    if (!act) {
      const srcs = this._vizSrcs;
      srcs[9] = p[MACRO1]; srcs[10] = p[MACRO1 + 1]; srcs[11] = p[MACRO1 + 2]; srcs[12] = p[MACRO1 + 3];
      srcs[13] = p[PERF_MW]; srcs[14] = p[PERF_AT];
      if (!p[LFO1 + L_RETRIG] && !p[LFOX1]) srcs[1] = this.gLfo1.valueOff(p[LFO1 + L_SHAPE], p[LFO1 + L_PHASE], p, LFOX1);
      if (!p[LFO2 + L_RETRIG] && !p[LFOX2]) srcs[2] = this.gLfo2.valueOff(p[LFO2 + L_SHAPE], p[LFO2 + L_PHASE], p, LFOX2);
      if (!p[LFO3 + L_RETRIG] && !p[LFOX3]) srcs[6] = this.gLfo3.valueOff(p[LFO3 + L_SHAPE], p[LFO3 + L_PHASE], p, LFOX3);
      if (!p[LFO4 + L_RETRIG] && !p[LFOX4]) srcs[7] = this.gLfo4.valueOff(p[LFO4 + L_SHAPE], p[LFO4 + L_PHASE], p, LFOX4);
      this.evalRoutes(srcs);
    }
    const accum = this._modAccum, viz = this.modViz;
    const fxp = this._fxp;
    fxp.set(p);
    for (let i = 0; i < FX_MOD_PIDS.length; i++) {
      const pid = FX_MOD_PIDS[i], x = act ? viz[PIDX_DST[pid]] : accum[pid];
      if (x) fxp[pid] = clampTo(p[pid] + x * MOD_SPAN[pid], MOD_LO[pid], MOD_HI[pid]);
    }
    this.fxParams = fxp;
    for (let i = 0; i < 4; i++) {
      const pid = LFO_RATE_PIDS[i], x = act ? viz[PIDX_DST[pid]] : accum[pid];
      this.gRate[i] = x ? Math.pow(2, x * MOD_LOG_D) : 1;
    }
  }

  // Render one voice into L/R starting at sample `lo`, for `n` samples. `n` is
  // at most one 128-sample chunk, so every block-rate quantity below ramps
  // across a chunk rather than a whole host block (finding W1/W3).
  renderVoice(v, L, R, lo, n) {
    const p = this.p;

    // glide
    const gl = p[MASTER_GLIDE] || 0;
    if (gl > 0.001) {
      const c = 1 - Math.exp(-n / (gl * 0.3 * sampleRate + 1));
      v.pitch += (v.note - v.pitch) * c;
    } else v.pitch = v.note;

    // mod sources (chunk rate)
    // A one-shot LFO starts with its note, so it is always per-voice.
    const os1 = !!p[LFOX1], os2 = !!p[LFOX2], os3 = !!p[LFOX3], os4 = !!p[LFOX4];
    const rt1 = os1 || !!p[LFO1 + L_RETRIG], rt2 = os2 || !!p[LFO2 + L_RETRIG];
    const l1 = (rt1 ? v.lfo1 : this.gLfo1).valueOff(p[LFO1 + L_SHAPE], p[LFO1 + L_PHASE], p, LFOX1) * v.lfo1.riseGain(p[LFO1 + L_RISE]);
    const l2 = (rt2 ? v.lfo2 : this.gLfo2).valueOff(p[LFO2 + L_SHAPE], p[LFO2 + L_PHASE], p, LFOX2) * v.lfo2.riseGain(p[LFO2 + L_RISE]);
    const rt3 = os3 || !!p[LFO3 + L_RETRIG], rt4 = os4 || !!p[LFO4 + L_RETRIG];
    const l3 = (rt3 ? v.lfo3 : this.gLfo3).valueOff(p[LFO3 + L_SHAPE], p[LFO3 + L_PHASE], p, LFOX3) * v.lfo3.riseGain(p[LFO3 + L_RISE]);
    const l4 = (rt4 ? v.lfo4 : this.gLfo4).valueOff(p[LFO4 + L_SHAPE], p[LFO4 + L_PHASE], p, LFOX4) * v.lfo4.riseGain(p[LFO4 + L_RISE]);
    const e2 = v.modEnv.level;
    const srcs = this._srcs;
    srcs[1] = l1; srcs[2] = l2; srcs[3] = e2; srcs[4] = v.vel; srcs[5] = (v.note - 60) / 24;
    srcs[6] = l3; srcs[7] = l4; srcs[8] = v.modEnv2.level;
    srcs[9] = p[MACRO1]; srcs[10] = p[MACRO1 + 1]; srcs[11] = p[MACRO1 + 2]; srcs[12] = p[MACRO1 + 3];
    srcs[13] = p[PERF_MW]; srcs[14] = p[PERF_AT]; srcs[15] = v.rand;

    // modulation destinations — sum every active slot assigned to each target
    // (evalRoutes). Globals (pitch/amp/pan) keep their legacy additive math;
    // per-param dests accumulate a route sum indexed by param id, then fold
    // into a per-voice modulated snapshot `pm` via the Lin/Log curve rule. This
    // mirrors the VST engine exactly, so both engines sound identical.
    const anyRoute = this.evalRoutes(srcs);
    const mPitch = this._mPitch, mAmp = this._mAmp, mPan = this._mPan;
    const accum = this._modAccum;

    // Build the per-voice modulated snapshot: copy p then apply each targeted
    // param's curve rule. Lin: pm = p + x·(hi−lo); Log: pm = p · 2^(x·D), D=5.
    // Params that are never destinations pass through untouched, so every read
    // below can go straight to pm. Matches the engine's pm_ build.
    const pm = this._pm;
    pm.set(p);
    if (anyRoute) {
      for (let i = 0; i < NUM_PARAMS; i++) {
        const x = accum[i];
        if (x === 0) continue;
        // The filter cutoff routes are NOT folded into pm: they are applied as
        // the single-exponent mCut term inside setupFilter so the result is
        // bit-identical to the legacy single Math.pow. All other dests fold here.
        if (i === F1_CUT || i === F2_CUT) continue;
        const c = MOD_CURVE[i];
        if (c === 2) pm[i] = p[i] * Math.pow(2, x * MOD_LOG_D);
        else if (c === 1) pm[i] = p[i] + x * MOD_SPAN[i];
      }
    }

    // Envelope times follow their routes, inside the knob range. The read is
    // p itself for a time with no route.
    for (let e = 0; e < 3; e++) {
      const env = e === 0 ? v.ampEnv : e === 1 ? v.modEnv : v.modEnv2;
      const b = e === 0 ? ENV1_A : e === 1 ? ENV2_A : ENV3_A, x = e === 0 ? ENVX1 : e === 1 ? ENVX2 : ENVX3;
      const a = clampTo(pm[b], MOD_LO[b], MOD_HI[b]), d = clampTo(pm[b + 1], MOD_LO[b + 1], MOD_HI[b + 1]);
      const r = clampTo(pm[b + 3], MOD_LO[b + 3], MOD_HI[b + 3]);
      env.set(a, d, p[b + 2], r);
      env.setModel(p[x], a, d, r, p[x + 1], p[x + 2], p[x + 3], p[x + 4]);
    }

    // SPLIT routing sends osc A through filter 1 and osc B through filter 2, so
    // they need separate source buffers; every other routing sums into one path.
    const route = p[FILTER_ROUTE] | 0;
    const split = route === 2;

    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);
    const bL = this.bL, bR = this.bR;
    if (split) { bL.fill(0, 0, n); bR.fill(0, 0, n); }

    const aOn = this.setupOsc(v.oA, OSCA, WARPA, OSCB, v, pm, mPitch, mPan, n, (p[WARPB] | 0) >= WARP_FM);
    const bOn = this.setupOsc(v.oB, OSCB, WARPB, OSCA, v, pm, mPitch, mPan, n, (p[WARPA] | 0) >= WARP_FM);
    // FM/AM/RM: each carrier reads the other oscillator's modulator signal.
    // Both are rendered before either carrier, so mutual modulation is not a
    // feedback loop.
    let modA = null, modB = null;
    if (aOn && v.oA.warp >= WARP_FM) {
      if (v.oB.modOk) { this.renderMod(v.oB, this.modForA, n); modA = this.modForA; } else v.oA.warp = 0;
    }
    if (bOn && v.oB.warp >= WARP_FM) {
      if (v.oA.modOk) { this.renderMod(v.oA, this.modForB, n); modB = this.modForB; } else v.oB.warp = 0;
    }
    if (aOn) this.renderOsc(v.oA, tmpL, tmpR, n, modA); else v.oA.havePrev = false;
    if (bOn) this.renderOsc(v.oB, split ? bL : tmpL, split ? bR : tmpR, n, modB); else v.oB.havePrev = false;

    // sub oscillator (band-limited table square or sine)
    if (p[SUB_ON]) {
      const subLvl = pm[SUB_LEVEL];
      const lvl = subLvl * subLvl * 0.3;
      if (lvl > 1e-6) {
        const sf = 440 * Math.pow(2, (v.pitch + this.bend + p[SUB_OCT] * 12 + mPitch * 12 - 69) / 12);
        const inc1 = sf / sampleRate;
        if (inc1 > 0 && inc1 < 0.45) {
          // Finding W1: ramp the sub increment across the chunk so glide /
          // pitch modulation is staircase-free on the sub too.
          const inc0 = v.subIncPrev > 0 && v.subIncPrev < 0.45 ? v.subIncPrev : inc1;
          const dInc = (inc1 - inc0) / n;
          let ph = v.subPhase;
          const square = (p[SUB_SHAPE] | 0) === 1;
          // Band-limited table read (mip from the larger chunk increment).
          const sqTbl = square ? subSquareTable() : null;
          const sqMip = square ? subSquareMip(SUBSQ_MIP, Math.max(inc0, inc1)) : null;
          for (let i = 0; i < n; i++) {
            const inc = inc0 + dInc * i;
            let s;
            if (square) {
              s = subSquareRead(sqTbl, sqMip, ph) * 0.7;
            } else {
              s = Math.sin(2 * Math.PI * ph);
            }
            const o = s * lvl;
            tmpL[i] += o; tmpR[i] += o;
            ph += inc; if (ph >= 1) ph -= 1;
          }
          v.subPhase = ph;
          v.subIncPrev = inc1;
        } else v.subIncPrev = -1;
      }
    }

    // noise
    if (p[NOISE_ON]) {
      const noiseLvl = pm[NOISE_LEVEL];
      const lvl = noiseLvl * noiseLvl * 0.35;
      if (lvl > 1e-6) {
        const rng = this.rng;
        if ((p[NOISE_TYPE] | 0) === 1) {
          // Kellet pink filter with sample-rate-mapped poles/gains (finding W1;
          // identical to the fixed literals at the 48 kHz reference).
          const b = v.pb, pp = this.pinkP, pg = this.pinkG;
          for (let i = 0; i < n; i++) {
            const w = rng.next() * 2 - 1;
            b[0] = pp[0] * b[0] + w * pg[0];
            b[1] = pp[1] * b[1] + w * pg[1];
            b[2] = pp[2] * b[2] + w * pg[2];
            b[3] = pp[3] * b[3] + w * pg[3];
            b[4] = pp[4] * b[4] + w * pg[4];
            b[5] = -pp[5] * b[5] - w * pg[5];
            const pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
            b[6] = w * 0.115926;
            const o = pink * lvl;
            tmpL[i] += o; tmpR[i] += o;
          }
        } else {
          for (let i = 0; i < n; i++) {
            const o = (rng.next() * 2 - 1) * lvl;
            tmpL[i] += o; tmpR[i] += o;
          }
        }
      }
    }

    // ---- per-voice filters with routing ----
    const f1on = !!p[FLT1 + F_ON];
    const f2on = !!p[FLT2 + F_ON];
    if (f1on) this.setupFilter(v.f1, FLT1, v, e2, accum[F1_CUT], pm, n);
    if (f2on) this.setupFilter(v.f2, FLT2, v, e2, accum[F2_CUT], pm, n);

    const f1L = this.f1L, f1R = this.f1R, f2L = this.f2L, f2R = this.f2R;
    const dr1 = pm[FLT1 + F_DRIVE], dr2 = pm[FLT2 + F_DRIVE];
    let oL, oR; // routed output buffers feeding the DC blocker + amp

    if (split) {
      // osc A (+ sub/noise) -> F1, osc B -> F2, summed. Bypassed filters pass dry.
      if (f1on) this.runFilter(v.f1, tmpL, tmpR, f1L, f1R, dr1, n);
      if (f2on) this.runFilter(v.f2, bL, bR, f2L, f2R, dr2, n);
      const aL = f1on ? f1L : tmpL, aR = f1on ? f1R : tmpR;
      const sL = f2on ? f2L : bL, sR = f2on ? f2R : bR;
      for (let i = 0; i < n; i++) { f1L[i] = aL[i] + sL[i]; f1R[i] = aR[i] + sR[i]; }
      oL = f1L; oR = f1R;
    } else if (route === 1) {
      // parallel: both filters see the same signal, outputs summed
      if (f1on) this.runFilter(v.f1, tmpL, tmpR, f1L, f1R, dr1, n);
      if (f2on) this.runFilter(v.f2, tmpL, tmpR, f2L, f2R, dr2, n);
      if (f1on && f2on) {
        for (let i = 0; i < n; i++) { f1L[i] += f2L[i]; f1R[i] += f2R[i]; }
        oL = f1L; oR = f1R;
      } else if (f1on) { oL = f1L; oR = f1R; }
      else if (f2on) { oL = f2L; oR = f2R; }
      else { oL = tmpL; oR = tmpR; } // both bypassed -> dry
    } else {
      // serial: F1 -> F2, each bypassed when off
      let cL = tmpL, cR = tmpR;
      if (f1on) { this.runFilter(v.f1, cL, cR, f1L, f1R, dr1, n); cL = f1L; cR = f1R; }
      if (f2on) { this.runFilter(v.f2, cL, cR, f2L, f2R, dr2, n); cL = f2L; cR = f2R; }
      oL = cL; oR = cR;
    }

    // The AMP-mod factor ramps from the previous chunk's value (finding W1);
    // the DC blocker pole is sample-rate-derived.
    const ampFactor = Math.min(2, Math.max(0, 1 + mAmp));
    const af0 = v.ampFacPrev >= 0 ? v.ampFacPrev : ampFactor;
    const dAf = (ampFactor - af0) / n;
    const dcR = this.dcR;
    const slewK = 1 - Math.exp(-1 / (AMP_SLEW_SEC * sampleRate));
    const velK = 1 - Math.exp(-1 / (VEL_SLEW_SEC * sampleRate));
    if (v.velSm < 0) v.velSm = v.velGain;
    for (let i = 0; i < n; i++) {
      const sl = oL[i], sr = oR[i];
      // Per-voice DC blocker (1-pole highpass, ~3.5 Hz) — removes DC before
      // it reaches the FX chain's saturator where it would cause asymmetric clipping.
      const yL = sl - v.dcxL + dcR * v.dcyL;
      const yR = sr - v.dcxR + dcR * v.dcyR;
      v.dcxL = sl; v.dcyL = yL;
      v.dcxR = sr; v.dcyR = yR;
      v.ampSm += (v.ampEnv.process() - v.ampSm) * slewK;
      v.velSm += (v.velGain - v.velSm) * velK;
      const amp = v.ampSm * v.velSm * (af0 + dAf * i);
      L[lo + i] += yL * amp;
      R[lo + i] += yR * amp;
    }
    v.ampFacPrev = ampFactor;

    // advance chunk-rate modulators
    // LFO RATE routes scale the rate (free or synced) by 2^(x*D), like any Log param.
    const rateMul = (pid) => (accum[pid] ? Math.pow(2, accum[pid] * MOD_LOG_D) : 1);
    v.lfo1.advance(this.lfoHz(LFO1) * rateMul(LFO1 + L_RATE), n, this.rng, os1);
    v.lfo2.advance(this.lfoHz(LFO2) * rateMul(LFO2 + L_RATE), n, this.rng, os2);
    v.lfo3.advance(this.lfoHz(LFO3) * rateMul(LFO3 + L_RATE), n, this.modRng, os3);
    v.lfo4.advance(this.lfoHz(LFO4) * rateMul(LFO4 + L_RATE), n, this.modRng, os4);
    v.modEnv.processBlock(n);
    v.modEnv2.processBlock(n);

    this._modAny = anyRoute;
    return { posA: v.oA.posSm, posB: v.oB.posSm };
  }

  // Copy the just-rendered voice's per-destination route sums (this._modAccum,
  // valid right after renderVoice) into the telemetry snapshot. A NONZERO route
  // is what makes telemetry flow; `_modAny` records whether this voice had one,
  // so a route sitting exactly at a zero crossing still counts.
  snapshotModViz() {
    const mv = this.modViz;
    mv.fill(0);
    const accum = this._modAccum;
    for (let i = 0; i < NUM_PARAMS; i++) {
      const di = PIDX_DST[i];
      if (di) mv[di] = accum[i];
    }
    this.modVizAny = this._modAny;
    this._vizSrcs.set(this._srcs); // the sources of the same voice, for updateGlobalMods
  }

  // Finding W3: split the host block at every sequencer event so a step, its
  // gate-off and the render quantum land on their exact sample instead of the
  // next block start (up to 2.9 ms of jitter at 44.1 kHz, and a whole 4096-
  // sample host block on the extremes). Mirrors Engine::render's chunk loop:
  // every chunk is at most 128 samples, so the engine also behaves identically
  // at any host block size.
  process(inputs, outputs) {
    // The SQ-4 key bus (five mono channels); empty when nothing is connected.
    this.fx.keyIn = inputs[0]?.length ? inputs[0] : null;
    const out = outputs[0];
    const L = out[0];
    const R = out.length > 1 ? out[1] : out[0];
    L.fill(0);
    if (R !== L) R.fill(0);
    const n = L.length;
    const hosted = this.hosted;

    let off = 0;
    while (off < n) {
      let run = Math.min(128, n - off);
      this.frameNow = currentFrame + off;

      // Fire the step that is due at this sample, then shorten the chunk so the
      // next one starts exactly where the following step does.
      if (!hosted && this.seqPlaying) {
        if (this.seqToNext <= 0) this.seqFire();
        run = Math.min(run, Math.max(1, Math.ceil(this.seqToNext)));
      }
      // Cut the chunk at the earliest pending sequencer note-off, so each off
      // lands on its own sample (gate-off before the next step's trigger).
      const eo = this.earliestSeqOff();
      if (eo >= 0) run = Math.min(run, Math.max(1, Math.ceil(eo)));
      for (const e of this.seqOnQueue) run = Math.min(run, Math.max(1, Math.ceil(e.remaining)));
      // The hosted clip transport resolves its commands per chunk; at the usual
      // 128-sample quantum that is exactly the pre-split behaviour.
      if (hosted) this.hostTick(run);
      this.autoTick(this.frameNow);

      const ppq = hosted
        ? Math.max(0, this.frameNow - this.hostAnchor) * (this.bpm / 60) / sampleRate
        : this.transportBeats;
      this.renderChunk(L, R, off, run, ppq);
      // FX consume the automation value for this chunk, on the same clock as
      // the voices. Process in place without allocating channel subarrays.
      this.fx.setParams(this.fxParams, this.bpm); // p, or p with the FX routes applied
      this.fx.lab.setTransport(ppq, true); // the GLITCH grid follows this chunk's beat
      this.fx.process(L, R, run, off);
      this.transportBeats += (run / sampleRate) * (this.bpm / 60);
      if (!hosted && this.seqPlaying) this.seqToNext -= run;

      // Drain the per-note off queue: decrement every pending off, fire noteOff
      // for those now due, compact the survivors (no allocation).
      if (this.seqOffQueue.length) {
        let w = 0;
        for (let i = 0; i < this.seqOffQueue.length; i++) {
          const e = this.seqOffQueue[i];
          e.remaining -= run;
          if (e.remaining <= 0) this.noteOff(e.note);
          else this.seqOffQueue[w++] = e;
        }
        this.seqOffQueue.length = w;
        this.seqLastNote = w ? this.seqOffQueue[w - 1].note : -1;
      }
      if (this.seqOnQueue.length) {
        let w = 0;
        for (let i = 0; i < this.seqOnQueue.length; i++) {
          const e = this.seqOnQueue[i];
          e.remaining -= run;
          if (e.remaining <= 0) { this.noteOn(e.note, e.vel); this.seqScheduleOff(e.note, e.off); }
          else this.seqOnQueue[w++] = e;
        }
        this.seqOnQueue.length = w;
      }
      off += run;
    }
    if (this.fx.reverbMetering && this.fx.reverbSamples >= sampleRate / 30) {
      const fx = this.fx, energy = fx.reverbEnergy;
      const rms = index => Math.max(-90, 10 * Math.log10(Math.max(1e-9, energy[index] / fx.reverbSamples)));
      const product = Math.sqrt(energy[0] * energy[1]);
      this.port.postMessage({ t: 'reverb', left: rms(0), right: rms(1),
        correlation: product > fx.reverbSamples * 1e-9 ? Math.max(-1, Math.min(1, energy[2] / product)) : 0 });
      energy.fill(0); fx.reverbSamples = 0;
    }
    if (this.fx.echoMetering && this.fx.echoSamples >= sampleRate / 30) {
      const fx = this.fx;
      const rms = index => Math.max(-90, 10 * Math.log10(Math.max(1e-9, fx.echoEnergy[index] / fx.echoSamples)));
      this.port.postMessage({ t: 'echo', input: rms(0), left: rms(1), right: rms(2),
        time: fx.dlTime.cur, driftL: fx.dlDriftL, driftR: fx.dlDriftR });
      fx.echoEnergy.fill(0); fx.echoSamples = 0;
    }
    if (this.fx.metering && this.fx.meterSamples >= sampleRate / 30) {
      const fx = this.fx, ott = fx.ott, comp = fx.comp;
      const db = value => Math.max(-90, Math.min(60, 20 * Math.log10(Math.max(1e-9, value))));
      const rms = index => db(Math.sqrt(fx.meterEnergy[index] / fx.meterSamples));
      const ottActive = ott.depthTarget > 0;
      this.port.postMessage({
        t: 'dynamics',
        ott: {
          input: rms(0), output: rms(1),
          levels: Array.from(ott.env, value => ottActive ? db(value) : -90),
          gains: Array.from(ott.gain, (value, i) => ottActive && ott.env[i] > 1e-7 ? db(value) : 0),
          makeup: ottActive ? db(ott.autoGain.gain) : 0,
        },
        comp: { input: rms(2), output: rms(3), reduction: fx.meterReduction, trace: comp.takeTrace(),
          makeup: comp.wetTarget ? db(comp.autoGain.gain) : 0 },
      });
      fx.meterEnergy.fill(0); fx.meterSamples = 0; fx.meterReduction = 0;
    }
    return true;
  }

  // Samples until the soonest pending sequencer note-off, or -1 when none.
  earliestSeqOff() {
    const q = this.seqOffQueue;
    let best = -1;
    for (let i = 0; i < q.length; i++) {
      const r = q[i].remaining;
      if (best < 0 || r < best) best = r;
    }
    return best;
  }

  // Render one <=128-sample chunk of every sounding voice into L/R at `off`.
  renderChunk(L, R, off, n, ppq) {
    // Update the global (free-run/transport-locked) LFOs before voices read
    // them. ppq = beats since audio start (chunk-start position). Hosted
    // (SQ-4), the conductor's anchor is beat zero of the shared timebase, so
    // every device's synced LFO lands on the same downbeat regardless of when
    // it joined the song.
    // gRate: the LFO RATE route of each free-running LFO (updateGlobalMods).
    this.updateGlobalLfo(this.gLfo1, LFO1, ppq, n, this.rng, this.gRate[0]);
    this.updateGlobalLfo(this.gLfo2, LFO2, ppq, n, this.rng, this.gRate[1]);
    this.updateGlobalLfo(this.gLfo3, LFO3, ppq, n, this.modRng, this.gRate[2]);
    this.updateGlobalLfo(this.gLfo4, LFO4, ppq, n, this.modRng, this.gRate[3]);

    let act = 0;
    let viz = null;
    this.modVizAny = false;
    for (const v of this.voices) {
      if (!v.active && v.pending) {
        const pd = v.pending;
        v.pending = null;
        v.noteOn(pd.n, pd.vel, pd.start, this.clock++, this.rng, this.phaseRand(WARPA, OSCA), this.phaseRand(WARPB, OSCB), this.modRng);
      }
      if (!v.active) continue;
      const r = this.renderVoice(v, L, R, off, n);
      // Voice to visualize: the same one the wavetable viz follows — the last
      // gated (still-held) voice in pool order, falling back to any releasing
      // voice. _modAccum still holds exactly this voice's route sums here.
      if (v.gate || !viz) { viz = r; this.snapshotModViz(); }
      act++;
    }
    this.updateGlobalMods(act);

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      this.port.postMessage({
        t: 'viz',
        a: viz ? viz.posA : -1,
        b: viz ? viz.posB : -1,
        n: act,
      });
      // Live-mod telemetry rides the same throttle. slice(): a fresh buffer per
      // send (tiny, ~23 Hz) — postMessage would clone anyway, and the in-process
      // test harness must not see later mutations of the reused snapshot.
      if (this.modVizAny) {
        this.port.postMessage({ t: 'mod', d: this.modViz.slice() });
        this.modIdleSent = false;
      } else if (!this.modIdleSent) {
        this.modIdleSent = true;
        this.port.postMessage({ t: 'mod', d: null });
      }
    }
  }
}

registerProcessor('fable-wt', FableProcessor);
