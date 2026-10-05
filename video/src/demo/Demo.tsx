// FableSynth demo v2: every scene is a real recorded performance. What you see
// is what you hear — the audio is the take's own output, cut on the 126 BPM grid.
import React from 'react';
import { AbsoluteFill, Audio, Easing, Sequence, interpolate, spring, staticFile, useCurrentFrame, useVideoConfig } from 'remotion';
import { Flash, Headline, Kicker, Sub, Terrain, lerp } from '../components';
import { BAR, BEAT, C, DISPLAY, MONO, at } from '../theme';
import { Cam, Readout, Spectrum, TAKES, TakeName, TakePlayer, cursorAt, fadeWindow, rmsAt, useTakeMs, valueAt } from './Take';

type Scene = { name: TakeName; from: number; to: number; offsetMs: number; tail?: number };
// Global bar ranges and the take time (ms) of the bar line each scene starts on.
export const SCENES: Record<string, Scene> = {
  morph: { name: 'wt-morph', from: 0, to: 3, offsetMs: 300 },
  sweep: { name: 'bl-sweep', from: 3, to: 7, offsetMs: 1940 },
  mod: { name: 'wt-mod', from: 7, to: 11, offsetMs: 1940 },
  fx: { name: 'wt-fx', from: 11, to: 15, offsetMs: 35, tail: Math.round(BAR) },
  shift: { name: 'wt-shift', from: 15, to: 21, offsetMs: 35 },
  glitch: { name: 'wt-glitch', from: 21, to: 26, offsetMs: 35 },
  beat: { name: 'dr-build', from: 26, to: 31, offsetMs: 35 },
  auto: { name: 'sq-auto', from: 31, to: 36, offsetMs: 1162 },
  launch: { name: 'sq-launch', from: 36, to: 42, offsetMs: 950 },
};
export const DEMO_TOTAL = at(42);

export const PLATE: React.CSSProperties = { padding: '22px 30px', borderRadius: 16, background: 'rgba(6,7,11,0.8)', backdropFilter: 'blur(14px)', border: '1px solid rgba(255,255,255,0.07)', boxShadow: '0 20px 60px rgba(0,0,0,0.5)' };

export const Scrim: React.FC<{ strength?: number }> = ({ strength = 1 }) => (
  <AbsoluteFill
    style={{
      pointerEvents: 'none',
      background: `radial-gradient(1100px 520px at 0% 0%, rgba(6,7,11,${0.88 * strength}), rgba(6,7,11,0) 70%)`,
    }}
  />
);

export const Caption: React.FC<{ kicker: string; title: string; color: string; sub?: string; delay?: number; out?: number }> = ({ kicker, title, color, sub, delay = 4, out }) => {
  const f = useCurrentFrame();
  const o = (out !== undefined ? lerp(f, out, out + 10, 1, 0) : 1) * lerp(f, delay + 2, delay + 10, 0, 1);
  return (
    <div style={{ position: 'absolute', left: 70, top: 56, display: 'flex', flexDirection: 'column', gap: 16, opacity: o, padding: '26px 34px 28px', borderRadius: 16, background: 'rgba(6,7,11,0.8)', backdropFilter: 'blur(14px)', border: '1px solid rgba(255,255,255,0.07)', boxShadow: '0 20px 60px rgba(0,0,0,0.5)' }}>
      <Kicker text={kicker} color={color} delay={delay} />
      <Headline text={title} size={64} delay={delay + 4} />
      {sub && <Sub text={sub} delay={delay + 16} color={C.ice} size={24} />}
    </div>
  );
};

/** Pill that pops up when a press happens, e.g. "+ KICK". */
export const PressLabels: React.FC<{ name: TakeName; offsetMs: number; labels: string[]; color: string; cam: (t: number) => { x: number; y: number; z: number } }> = ({ name, offsetMs, labels, color, cam }) => {
  const t = useTakeMs(offsetMs);
  const c = TAKES[name].cursor;
  const presses: { t: number; x: number; y: number }[] = [];
  for (let i = 1; i < c.length; i++) if (c[i].d && !c[i - 1].d) presses.push(c[i]);
  return (
    <>
      {presses.map((p, i) => {
        const age = t - p.t;
        if (age < 0 || age > 900 || !labels[i]) return null;
        const k = cam(p.t);
        const sx = 960 + (p.x - k.x) * k.z, sy = 540 + (p.y - k.y) * k.z;
        const u = age / 900;
        return (
          <div
            key={i}
            style={{
              position: 'absolute',
              left: sx,
              top: sy - 60 - u * 50,
              transform: `translateX(-50%) scale(${0.8 + 0.2 * Math.min(1, age / 120)})`,
              opacity: u < 0.7 ? 1 : 1 - (u - 0.7) / 0.3,
              fontFamily: MONO,
              fontWeight: 600,
              fontSize: 24,
              letterSpacing: '0.2em',
              color: C.void,
              background: color,
              padding: '6px 14px',
              borderRadius: 6,
              boxShadow: `0 0 30px ${color}`,
              whiteSpace: 'nowrap',
            }}
          >
            {labels[i]}
          </div>
        );
      })}
    </>
  );
};

export const camLerp = (keys: Cam[]) => (t: number) => {
  if (t <= keys[0].t) return keys[0];
  for (let i = 0; i < keys.length - 1; i++) {
    const a = keys[i], b = keys[i + 1];
    if (t <= b.t) {
      const u = Easing.inOut(Easing.cubic)((t - a.t) / (b.t - a.t));
      return { t, x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u, z: a.z + (b.z - a.z) * u };
    }
  }
  return keys[keys.length - 1];
};

// ------------------------------------------------------------------ 1. Morph
const MORPH_CAM: Cam[] = [
  { t: 300, x: 960, y: 540, z: 1, card: 1 },
  { t: 1150, x: 520, y: 240, z: 2.55 },
  { t: 3600, x: 560, y: 235, z: 2.75 },
  { t: 5400, x: 540, y: 250, z: 2.2 },
];
const Morph: React.FC = () => {
  const s = SCENES.morph;
  const f = useCurrentFrame();
  const t = useTakeMs(s.offsetMs);
  const pos = valueAt(s.name, 'pos', t);
  const logoAt = Math.round((4650 - s.offsetMs) / 1000 * 30);
  const lf = f - logoAt;
  const logoIn = lf >= 0 ? spring({ frame: lf, fps: 30, config: { damping: 14, stiffness: 120 } }) : 0;
  return (
    <AbsoluteFill style={{ background: C.void }}>
      <AbsoluteFill style={{ filter: `blur(${logoIn * 10}px) brightness(${1 - logoIn * 0.55})` }}>
        <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={MORPH_CAM} accent={C.cyan} audio={false} />
        <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.cyan} />
        <Scrim />
        <Caption kicker="WT-1 · WAVETABLE SYNTH" title={'ONE FADER.'} sub="SINE → SUPERSAW, LIVE" color={C.cyan} delay={26} out={logoAt - 6} />
        <Readout label="WAVETABLE POS" value={pos} color={C.cyan} x={1850} y={600} show={fadeWindow(t, 1300, 4500)} align="right" />
      </AbsoluteFill>
      {lf >= 0 && (
        <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', transform: `scale(${0.85 + 0.15 * logoIn})`, opacity: logoIn }}>
          <div style={{ position: 'absolute', top: 210, left: 260, opacity: 0.6 }}>
            <Terrain width={1400} height={520} draw={Math.min(1, lf / 20)} />
          </div>
          <Headline text="FABLESYNTH" size={140} highlight={{ from: 5, color: C.cyan }} align="center" stagger={0.7} />
          <div style={{ marginTop: 26 }}>
            <Sub text="HEAR EVERY MOVE" color={C.ice} size={30} delay={8} />
          </div>
        </AbsoluteFill>
      )}
      {lf >= 0 && lf < 12 && <Flash color={C.cyan} peak={0.4} dur={12} />}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 2. BL-1 sweep
const SWEEP_CAM: Cam[] = [
  { t: 1940, x: 1100, y: 235, z: 2.45 },
  { t: 6300, x: 1080, y: 240, z: 2.6 },
  { t: 7700, x: 1060, y: 250, z: 2.3 },
  { t: 8500, x: 960, y: 470, z: 1.12 },
  { t: 9560, x: 960, y: 480, z: 1.2 },
];
const Sweep: React.FC = () => {
  const s = SCENES.sweep;
  const t = useTakeMs(s.offsetMs);
  return (
    <AbsoluteFill>
      <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={SWEEP_CAM} accent={C.green} audio={false} />
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.green} />
      <Scrim />
      <Caption kicker="BL-1 · ACID BASSLINE" title={'OPEN IT UP.'} sub="LP 24 · RESONANCE · LIVE" color={C.green} />
      <Readout label="CUTOFF" value={valueAt(s.name, 'cut', t)} color={C.green} x={70} y={600} show={fadeWindow(t, 2050, 7900)} />
      <Readout label="RESONANCE" value={valueAt(s.name, 'res', t)} color={C.violet} x={1850} y={600} show={fadeWindow(t, 6400, 7900)} align="right" />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 3. WT-1 mod drag
const MOD_CAM: Cam[] = [
  { t: 1940, x: 380, y: 560, z: 2.3 },
  { t: 3300, x: 370, y: 520, z: 2.3 },
  { t: 4300, x: 380, y: 500, z: 2.1 },
  { t: 5500, x: 900, y: 480, z: 1.35 },
  { t: 6350, x: 1260, y: 470, z: 2.3 },
  { t: 8700, x: 1240, y: 480, z: 2.2 },
  { t: 9560, x: 960, y: 480, z: 1.3 },
];
const Mod: React.FC = () => {
  const s = SCENES.mod;
  const t = useTakeMs(s.offsetMs);
  const drop = TAKES[s.name].marks.drop;
  const routeShow = fadeWindow(t, drop, 6000, 160);
  return (
    <AbsoluteFill>
      <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={MOD_CAM} accent={C.violet} audio={false} />
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.violet} />
      <Scrim />
      <Caption kicker="MOD MATRIX · DRAG & DROP" title={'DROP IT.\nHEAR IT MOVE.'} color={C.violet} />
      <div style={{ position: 'absolute', right: 70, top: 620, opacity: routeShow, transform: `scale(${0.9 + 0.1 * routeShow})`, textAlign: 'right', ...PLATE }}>
        <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color: C.violet }}>NEW ROUTE</div>
        <div style={{ fontFamily: DISPLAY, fontSize: 74, color: C.ice, textShadow: `0 0 40px ${C.violet}88, 0 4px 30px #000` }}>
          <span style={{ color: C.cyan }}>LFO 1</span> → CUTOFF
        </div>
      </div>
      <Readout label="LFO 1 RATE" value={valueAt(s.name, 'rate', t)} color={C.cyan} x={1850} y={600} show={fadeWindow(t, 6450, 9300)} align="right" />
      {t >= drop && t < drop + 400 && <AbsoluteFill style={{ background: C.violet, opacity: 0.35 * (1 - (t - drop) / 400), mixBlendMode: 'screen' }} />}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 4. WT-1 FX
const FX_CAM: Cam[] = [
  { t: 35, x: 1150, y: 640, z: 1.45 },
  { t: 1500, x: 1150, y: 600, z: 2.2 },
  { t: 3100, x: 1160, y: 600, z: 2.3 },
  { t: 3800, x: 1540, y: 600, z: 2.3 },
  { t: 6600, x: 1530, y: 610, z: 2.4 },
  { t: 7654, x: 1350, y: 620, z: 1.6 },
];
/** Stack of states that light up as the take reaches each mark; the first one is struck through. */
const StateStack: React.FC<{ t: number; items: [string, number, string][] }> = ({ t, items }) => {
  return (
    <div style={{ position: 'absolute', right: 70, top: 560, display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 12, ...PLATE }}>
      {items.map(([label, at, col], i) => {
        const on = t >= at;
        const nextOn = i < items.length - 1 && t >= items[i + 1][1];
        const age = (t - at) / 250;
        const s = on ? Math.min(1, age) : 0;
        return (
          <div
            key={label}
            style={{
              fontFamily: DISPLAY,
              fontSize: 54,
              letterSpacing: '0.06em',
              color: on ? (i === 0 && nextOn ? C.dim : C.ice) : 'transparent',
              opacity: on ? 1 : 0,
              transform: `translateX(${(1 - s) * 60}px) scale(${1 + (on && age < 1.5 ? 0.12 * (1 - age / 1.5) : 0)})`,
              textShadow: on && i > 0 ? `0 0 34px ${col}` : undefined,
              textDecoration: i === 0 && nextOn ? 'line-through' : undefined,
            }}
          >
            {label}
          </div>
        );
      })}
    </div>
  );
};
const Fx: React.FC = () => {
  const s = SCENES.fx;
  const t = useTakeMs(s.offsetMs);
  return (
    <AbsoluteFill>
      <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={FX_CAM} accent={C.cyan} audio={false} />
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.cyan} />
      <Scrim />
      <Caption kicker="WT-1 · STUDIO FX" title={'ADD SPACE.'} color={C.cyan} />
      <StateStack t={t} items={[['DRY', 0, C.dim], ['+ TAPE ECHO', TAKES[s.name].marks.echo, C.cyan], ['+ REVERB', TAKES[s.name].marks.reverb, C.violet]]} />
      <Readout label="REVERB SIZE" value={valueAt(s.name, 'revsize', t)} color={C.violet} x={70} y={600} show={fadeWindow(t, 5050, 6550)} />
      <Readout label="REVERB MIX" value={valueAt(s.name, 'revmix', t)} color={C.violet} x={70} y={600} show={fadeWindow(t, 6750, 7900)} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 5. LAB SHIFT
const SHIFT_CAM: Cam[] = [
  { t: 35, x: 960, y: 600, z: 1.25 },
  { t: 1500, x: 852, y: 555, z: 2.0 },
  { t: 4900, x: 850, y: 560, z: 2.05 },
  { t: 5600, x: 846, y: 570, z: 2.12 },
  { t: 10300, x: 846, y: 572, z: 2.16 },
  { t: 11465, x: 900, y: 600, z: 1.5 },
];
const Shift: React.FC = () => {
  const s = SCENES.shift;
  const t = useTakeMs(s.offsetMs);
  const m = TAKES[s.name].marks;
  return (
    <AbsoluteFill>
      <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={SHIFT_CAM} accent={C.cyan} audio={false} />
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.cyan} />
      <Scrim />
      <Caption kicker="LAB · NEW · FREQUENCY SHIFTER" title={'ECHOES\nTHAT SPIRAL.'} sub="EVERY REPEAT SHIFTS IN PITCH" color={C.cyan} />
      <StateStack t={t} items={[['DRY', 0, C.dim], ['+ SHIFT', m.on, C.cyan]]} />
      <Readout label="SPIRAL" value={valueAt(s.name, 'fb', t)} color={C.cyan} x={70} y={600} show={fadeWindow(t, 3300, 5300)} />
      <Readout label="SHIFT" value={valueAt(s.name, 'hz', t)} color={C.orange} x={70} y={600} show={fadeWindow(t, 5500, 11200)} />
      {t >= m.on && t < m.on + 400 && <AbsoluteFill style={{ background: C.cyan, opacity: 0.22 * (1 - (t - m.on) / 400), mixBlendMode: 'screen' }} />}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 6. LAB GLITCH
const GLITCH_CAM: Cam[] = [
  { t: 35, x: 1300, y: 600, z: 1.3 },
  { t: 1500, x: 1463, y: 555, z: 2.0 },
  { t: 5800, x: 1462, y: 560, z: 2.05 },
  { t: 6600, x: 1458, y: 570, z: 2.14 },
  { t: 9560, x: 1458, y: 572, z: 2.18 },
];
const Glitch: React.FC = () => {
  const s = SCENES.glitch;
  const f = useCurrentFrame();
  const t = useTakeMs(s.offsetMs);
  const m = TAKES[s.name].marks;
  // Small RGB split on the repeats, only while GLITCH is on and the level moves.
  const on = t >= m.on;
  const jitter = on ? Math.min(6, Math.max(0, rmsAt(s.name, t) + 34) * (Math.sin(f * 12.9898) * 0.5 + 0.5) * 0.6) : 0;
  return (
    <AbsoluteFill>
      <AbsoluteFill style={{ transform: `translateX(${jitter > 3 ? (f % 2 ? 1 : -1) * jitter : 0}px)` }}>
        <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={GLITCH_CAM} accent={C.orange} audio={false} />
      </AbsoluteFill>
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.orange} />
      <Scrim />
      <Caption kicker="LAB · NEW · BEAT REPEAT" title={'BREAK IT.\nIN TIME.'} sub="TEMPO-SYNCED STUTTER · TAPE DRIFT" color={C.orange} />
      <StateStack t={t} items={[['DRY', 0, C.dim], ['+ GLITCH', m.on, C.orange], ['1/32 ROLL', m.roll, C.violet], ['+ TAPE DRIFT', m.drift - 1600, C.cyan]]} />
      <Readout label="CHANCE" value={valueAt(s.name, 'chance', t)} color={C.orange} x={70} y={600} show={fadeWindow(t, 3300, 5900)} />
      <Readout label="DRIFT" value={valueAt(s.name, 'drift', t)} color={C.cyan} x={70} y={600} show={fadeWindow(t, 7100, 9500)} />
      {[m.on, m.roll].map((k) => t >= k && t < k + 400 && <AbsoluteFill key={k} style={{ background: C.orange, opacity: 0.22 * (1 - (t - k) / 400), mixBlendMode: 'screen' }} />)}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 7. DR-1 beat build
const BEAT_CAM: Cam[] = [
  { t: 35, x: 1000, y: 820, z: 1.55 },
  { t: 1700, x: 1000, y: 840, z: 1.6 },
  { t: 2700, x: 1080, y: 770, z: 1.7 },
  { t: 5200, x: 1080, y: 780, z: 1.65 },
  { t: 7300, x: 1100, y: 780, z: 1.55 },
  { t: 8400, x: 960, y: 560, z: 1, card: 0.55, ry: 4 },
  { t: 9560, x: 960, y: 560, z: 1.02, card: 0.5, ry: 2 },
];
const BEAT_LABELS = ['KICK', 'KICK', 'KICK', 'KICK', 'HAT', 'HAT', 'HAT', 'HAT', 'CLAP', 'CLAP', 'HAT', 'HAT', 'HAT', 'HAT', 'HAT', 'OPEN HAT', 'RIM', 'RIM'];
const Beat: React.FC = () => {
  const s = SCENES.beat;
  const t = useTakeMs(s.offsetMs);
  const level = rmsAt(s.name, t);
  return (
    <AbsoluteFill>
      <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={BEAT_CAM} accent={C.amber} audio={false} />
      <PressLabels name={s.name} offsetMs={s.offsetMs} labels={BEAT_LABELS} color={C.amber} cam={camLerp(BEAT_CAM)} />
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.amber} />
      <Scrim />
      <Caption kicker="DR-1 · DRUM MACHINE" title={'BUILD THE BEAT.'} sub="CLICK A STEP — HEAR IT NEXT BAR" color={C.amber} />
      <AbsoluteFill style={{ pointerEvents: 'none', boxShadow: `inset 0 0 ${Math.max(0, level + 40) * 6}px ${C.amber}55` }} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 8. SQ-4 automation
const AUTO_CAM: Cam[] = [
  { t: 1162, x: 960, y: 540, z: 1, card: 0.7 },
  { t: 1250, x: 1380, y: 650, z: 1.7 },
  { t: 2350, x: 760, y: 740, z: 1.9 },
  { t: 5240, x: 1400, y: 740, z: 1.9 },
  { t: 6300, x: 1050, y: 560, z: 1.25 },
  { t: 10686, x: 1050, y: 540, z: 1.42 },
];
const Auto: React.FC = () => {
  const s = SCENES.auto;
  const t = useTakeMs(s.offsetMs);
  const m = TAKES[s.name].marks;
  return (
    <AbsoluteFill>
      <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={AUTO_CAM} accent={C.green} audio={false} />
      <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.green} />
      <Scrim />
      <Caption kicker="SQ-4 · CLIP AUTOMATION" title={'PAINT THE MOVE.'} sub="THE FILTER PLAYS YOUR CURVE" color={C.green} />
      <div style={{ position: 'absolute', right: 70, top: 56, opacity: fadeWindow(t, m.drawn + 300, 20000), textAlign: 'right', ...PLATE }}>
        <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color: C.green }}>AUTOMATING</div>
        <div style={{ fontFamily: DISPLAY, fontSize: 64, color: C.ice, textShadow: `0 0 40px ${C.green}88, 0 4px 30px #000` }}>FILTER CUT</div>
      </div>
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 9. SQ-4 launch + outro
const LAUNCH_DROP = 4760;
const LAUNCH_CAM: Cam[] = [
  { t: 950, x: 760, y: 430, z: 1.55 },
  { t: 2600, x: 400, y: 450, z: 2.3 },
  { t: 4600, x: 380, y: 455, z: 2.45 },
  { t: LAUNCH_DROP, x: 960, y: 540, z: 1.0, card: 0.25 },
  { t: 12400, x: 960, y: 540, z: 1.08, card: 0.4, ry: -3 },
];
const CHIPS: [string, string, string][] = [
  ['WT-1', 'WAVETABLE', C.cyan],
  ['DR-1', 'DRUMS', C.amber],
  ['BL-1', 'ACID BASS', C.green],
  ['SQ-4', 'SESSIONS', C.orange],
];
const Launch: React.FC = () => {
  const s = SCENES.launch;
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = useTakeMs(s.offsetMs);
  const dropF = Math.round(((LAUNCH_DROP - s.offsetMs) / 1000) * fps);
  const post = f - dropF;
  const shake = post >= 0 ? Math.max(0, 10 * (1 - post / 18)) : 0;
  const outroF = dropF + Math.round(BAR * 1.5);
  const of = f - outroF;
  const dim = of >= 0 ? Math.min(1, of / 18) : 0;
  const dur = at(s.to) - at(s.from);
  const end = lerp(f, dur - 30, dur, 1, 0);
  const queued = t > 3081 && t < LAUNCH_DROP;
  return (
    <AbsoluteFill style={{ opacity: end }}>
      <AbsoluteFill style={{ filter: `blur(${dim * 12}px) brightness(${1 - dim * 0.6})` }}>
        <TakePlayer name={s.name} offsetMs={s.offsetMs} cam={LAUNCH_CAM} accent={C.orange} audio={false} shake={shake} />
        <Spectrum name={s.name} offsetMs={s.offsetMs} color={C.orange} opacity={0.9 - dim * 0.4} />
        {post < 0 && <Scrim />}
        {post < 0 && <Caption kicker="SQ-4 · SESSION LAUNCHER" title={'LAUNCH\nTHE DROP.'} sub={queued ? 'QUEUED · LANDS ON THE NEXT BAR' : 'SCENES · CLIPS · LIVE'} color={C.orange} />}
      </AbsoluteFill>
      {post >= 0 && post < 14 && <Flash color="#ffffff" peak={0.75} dur={14} />}
      {queued && (
        <div style={{ position: 'absolute', right: 70, top: 600, textAlign: 'right', ...PLATE }}>
          <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 22, letterSpacing: '0.35em', color: C.orange }}>NEXT BAR IN</div>
          <div style={{ fontFamily: DISPLAY, fontSize: 110, color: C.ice, textShadow: `0 0 40px ${C.orange}` }}>
            {Math.max(1, 4 - Math.floor(((t - (LAUNCH_DROP - 4 * (60000 / 126))) / (60000 / 126))))}
          </div>
        </div>
      )}
      {of >= 0 && (
        <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center' }}>
          <div style={{ position: 'absolute', top: 120, left: 260, opacity: 0.55 }}>
            <Terrain width={1400} height={520} draw={Math.min(1, of / 30)} t={of / 30 + 3} />
          </div>
          <div style={{ marginTop: -120 }}>
            <Headline text="FABLESYNTH" size={140} highlight={{ from: 5, color: C.cyan }} align="center" stagger={0.7} />
          </div>
          <div style={{ display: 'flex', gap: 26, marginTop: 40 }}>
            {CHIPS.map(([id, name, col], i) => {
              const sp = spring({ frame: of - Math.round(BEAT * (2 + i)), fps, config: { damping: 14, stiffness: 160 } });
              return (
                <div key={id} style={{ opacity: sp, transform: `translateY(${(1 - sp) * 40}px)`, border: `1px solid ${col}88`, background: `${col}14`, boxShadow: `0 0 30px ${col}33`, borderRadius: 10, padding: '14px 24px', minWidth: 200, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6 }}>
                  <div style={{ fontFamily: DISPLAY, fontSize: 32, color: col, letterSpacing: '0.12em' }}>{id}</div>
                  <div style={{ fontFamily: MONO, fontSize: 15, fontWeight: 600, letterSpacing: '0.3em', color: C.ice }}>{name}</div>
                </div>
              );
            })}
          </div>
          <div style={{ marginTop: 40 }}>
            <Sequence from={outroF + Math.round(BEAT * 7)} layout="none">
              <Sub text="VST3 · AU · STANDALONE · IN YOUR BROWSER" color={C.ice} size={26} />
            </Sequence>
          </div>
        </AbsoluteFill>
      )}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ Timeline
const VIEWS: [Scene, React.FC][] = [
  [SCENES.morph, Morph],
  [SCENES.sweep, Sweep],
  [SCENES.mod, Mod],
  [SCENES.fx, Fx],
  [SCENES.shift, Shift],
  [SCENES.glitch, Glitch],
  [SCENES.beat, Beat],
  [SCENES.auto, Auto],
  [SCENES.launch, Launch],
];

const XF = 8; // visual overlap past each cut, in frames

/** Outgoing scene keeps playing past its cut while it pushes in and blurs away. */
const SceneShell: React.FC<{ len: number; last: boolean; first: boolean; children: React.ReactNode }> = ({ len, last, first, children }) => {
  const f = useCurrentFrame();
  const e = Easing.out(Easing.cubic);
  const enter = first ? 1 : interpolate(f, [0, XF], [0, 1], { extrapolateLeft: 'clamp', extrapolateRight: 'clamp', easing: e });
  const exit = last ? 0 : interpolate(f, [len, len + XF], [0, 1], { extrapolateLeft: 'clamp', extrapolateRight: 'clamp', easing: Easing.in(Easing.quad) });
  return (
    <AbsoluteFill
      style={{
        opacity: enter * (1 - exit * 0.3),
        transform: `scale(${(1.06 - 0.06 * enter) * (1 + 0.1 * exit)})`,
        filter: enter < 1 || exit > 0 ? `blur(${(1 - enter) * 14 + exit * 18}px)` : undefined,
      }}
    >
      {children}
    </AbsoluteFill>
  );
};

const SceneAudio: React.FC<{ s: Scene; last: boolean }> = ({ s, last }) => {
  const { fps } = useVideoConfig();
  const len = at(s.to) - at(s.from);
  // One beat of ring-out under the next downbeat (FX keeps a full bar of tail).
  const tail = last ? 4 : s.tail ?? Math.round(BEAT);
  return (
    <Sequence from={at(s.from)} durationInFrames={len + tail}>
      <Audio
        src={staticFile(`takes/${s.name}/norm.wav`)}
        trimBefore={Math.round((s.offsetMs / 1000) * fps)}
        volume={(f) => (f < 2 ? f / 2 : f <= len ? 1 : Math.cos(((f - len) / tail) * (Math.PI / 2)))}
      />
    </Sequence>
  );
};

export const Demo: React.FC = () => (
  <AbsoluteFill style={{ background: C.void }}>
    {VIEWS.map(([s], i) => <SceneAudio key={s.name} s={s} last={i === VIEWS.length - 1} />)}
    {VIEWS.map(([s, V], i) => {
      const len = at(s.to) - at(s.from);
      const last = i === VIEWS.length - 1;
      return (
        <Sequence key={s.name} from={at(s.from)} durationInFrames={len + (last ? 0 : XF)}>
          <SceneShell len={len} first={i === 0} last={last}>
            <V />
          </SceneShell>
        </Sequence>
      );
    })}
  </AbsoluteFill>
);

export { cursorAt };
