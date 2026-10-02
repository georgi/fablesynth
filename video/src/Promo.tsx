import React from 'react';
import { AbsoluteFill, Audio, Easing, Sequence, interpolate, spring, staticFile, useCurrentFrame, useVideoConfig } from 'remotion';
import { Background, Callout, Card, Flash, Headline, Kicker, Stage, Sub, Terrain, Wipe, lerp, usePulse } from './components';
import { BAR, BEAT, C, DISPLAY, MONO, at } from './theme';

const useSpring = (delay = 0, damping = 20, stiffness = 90) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  return spring({ frame: f - delay, fps, config: { damping, stiffness } });
};

const Text: React.FC<{ left?: number; top?: number; right?: number; center?: boolean; children: React.ReactNode }> = ({ left, top, right, center, children }) => (
  <div
    style={{
      position: 'absolute',
      left: center ? 0 : left,
      right: center ? 0 : right,
      top,
      display: 'flex',
      flexDirection: 'column',
      gap: 18,
      alignItems: center ? 'center' : 'flex-start',
    }}
  >
    {children}
  </div>
);

/** Fades a scene out over its last frames so cuts land softly when needed. */
const Exit: React.FC<{ dur: number; len?: number; children: React.ReactNode }> = ({ dur, len = 8, children }) => {
  const f = useCurrentFrame();
  return <AbsoluteFill style={{ opacity: lerp(f, dur - len, dur, 1, 0, Easing.in(Easing.quad)) }}>{children}</AbsoluteFill>;
};

// ------------------------------------------------------------------ 1. Intro
const Intro: React.FC = () => {
  const f = useCurrentFrame();
  const dur = at(2);
  const out = lerp(f, dur - 18, dur, 0, 1, Easing.in(Easing.cubic));
  const lineW = lerp(f, 0, 26, 0, 1500);
  return (
    <AbsoluteFill>
      <Background accent={C.cyan} accent2={C.violet} />
      <AbsoluteFill style={{ transform: `scale(${1 + out * 0.25})`, opacity: 1 - out, filter: `blur(${out * 14}px)` }}>
        <div style={{ position: 'absolute', left: 260, top: 150, opacity: lerp(f, 0, 30, 0, 0.9), transform: `scale(${lerp(f, 0, 110, 0.92, 1.04)})` }}>
          <Terrain width={1400} height={520} draw={lerp(f, 0, 50, 0, 1)} />
        </div>
        <div
          style={{
            position: 'absolute',
            left: 960 - lineW / 2,
            top: 600,
            width: lineW,
            height: 2,
            background: `linear-gradient(90deg, transparent, ${C.cyan}, transparent)`,
            boxShadow: `0 0 18px ${C.cyan}`,
          }}
        />
        <Text center top={630}>
          <Headline text="FABLESYNTH" size={128} highlight={{ from: 5, color: C.cyan }} delay={24} stagger={1.6} align="center" />
          <Sub text="WAVETABLE · DRUMS · ACID BASS · SESSIONS" delay={52} size={24} />
        </Text>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 2. WT-1 hero
const WtHero: React.FC = () => {
  const f = useCurrentFrame();
  const s = useSpring(0, 22, 60);
  return (
    <AbsoluteFill>
      <Background accent={C.cyan} />
      <Stage perspective={1800}>
        <Card
          name="wt-full"
          width={1560}
          height={900}
          x={330}
          y={lerp(s, 0, 1, 520, 70, Easing.linear)}
          z={lerp(s, 0, 1, -700, -160, Easing.linear)}
          rx={lerp(s, 0, 1, 48, 12, Easing.linear)}
          ry={-18 + f * 0.03}
          rz={lerp(s, 0, 1, 8, 2, Easing.linear)}
        />
      </Stage>
      <AbsoluteFill style={{ background: 'linear-gradient(90deg, rgba(6,7,11,0.92) 0%, rgba(6,7,11,0.6) 30%, transparent 55%)' }} />
      <Text left={110} top={360}>
        <Kicker text="01 / WT-1" delay={6} />
        <Headline text={'WAVETABLE\nSYNTH'} size={76} delay={10} />
        <Sub text="VST3 · AU · STANDALONE · BROWSER" delay={30} />
      </Text>
      <Flash color={C.cyan} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 3. Oscillators
const Oscillators: React.FC = () => {
  const f = useCurrentFrame();
  const a = useSpring(0, 18, 80);
  const b = useSpring(6, 18, 80);
  return (
    <AbsoluteFill>
      <Background accent={C.cyan} accent2={C.amber} />
      <Stage>
        <Card name="wt-oscB" width={900} accent={C.amber} x={480} y={-170 + (1 - b) * -300} z={-420} ry={-16} rx={4} opacity={0.75 * b} offsetMs={400} />
        <Card name="wt-oscA" width={1280} x={-60 + f * 0.4} y={180 + (1 - a) * 600} z={0} ry={10 - f * 0.03} rx={8} />
      </Stage>
      <Text left={110} top={80}>
        <Kicker text="DUAL OSCILLATORS" delay={4} />
        <Headline text={'MORPH THROUGH\n3D WAVETABLES'} size={54} delay={8} />
      </Text>
      <Sequence from={30} layout="none">
        <Callout x={1515} y={600} dx={120} dy={-90} label="POS · LIVE MORPH" color={C.cyan} />
      </Sequence>
      <Wipe color={C.cyan} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 4. Filter / LFO / Matrix
const Modulation: React.FC = () => {
  const f = useCurrentFrame();
  const a = useSpring(0, 18, 90);
  const b = useSpring(7, 18, 90);
  const c = useSpring(14, 18, 90);
  return (
    <AbsoluteFill>
      <Background accent={C.violet} accent2={C.cyan} />
      <Stage>
        <Card name="wt-filter" width={760} accent={C.violet} x={-420 + (1 - a) * -500} y={150} ry={16 - f * 0.04} rx={4} opacity={a} />
        <Card name="wt-lfos" width={900} accent={C.cyan} x={440 + (1 - b) * 500} y={-140} ry={-14 + f * 0.03} rx={4} opacity={b} />
        <Card name="wt-matrix" width={760} accent={C.violet} x={470} y={250 + (1 - c) * 400} ry={-12} rx={6} opacity={c} />
      </Stage>
      <Text left={110} top={70}>
        <Kicker text="DUAL FILTER · LFOS · MOD MATRIX" color={C.violet} delay={2} />
        <Headline text={'SCULPT. MODULATE.\nMOVE.'} size={52} delay={6} highlight={{ from: 17, color: C.violet }} />
      </Text>
      <Sequence from={34} layout="none">
        <Callout x={1240} y={760} dx={-150} dy={130} label="DRAG A SOURCE ONTO ANY KNOB" color={C.violet} />
      </Sequence>
      <Flash color={C.violet} peak={0.25} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 5. FX carousel
const FX = ['wt-eq', 'wt-ott', 'wt-comp', 'wt-drive', 'wt-chorus', 'wt-echo', 'wt-reverb'];
const FxRack: React.FC = () => {
  const f = useCurrentFrame();
  const dur = at(2);
  const scroll = interpolate(f, [0, dur], [-1.2, 6.4], { easing: Easing.inOut(Easing.sin) });
  const R = 1500;
  return (
    <AbsoluteFill>
      <Background accent={C.cyan} accent2={C.violet} />
      <Stage perspective={1600}>
        {FX.map((n, i) => {
          const th = (i - scroll) * 0.3;
          const vis = Math.abs(th) < 1.35;
          return vis ? (
            <Card
              key={n}
              name={n}
              width={460}
              offsetMs={i * 300}
              accent={n === 'wt-echo' || n === 'wt-reverb' ? C.violet : C.cyan}
              x={R * Math.sin(th)}
              z={R * Math.cos(th) - R}
              y={90}
              ry={(-th * 180) / Math.PI}
              opacity={lerp(Math.abs(th), 1.0, 1.35, 1, 0, Easing.linear)}
            />
          ) : null;
        })}
      </Stage>
      <Text center top={70}>
        <Kicker text="SIGNAL CHAIN" delay={2} />
        <Headline text="STUDIO FX RACK" size={58} align="center" delay={6} />
      </Text>
      <div style={{ position: 'absolute', bottom: 70, left: 0, right: 0, display: 'flex', justifyContent: 'center' }}>
        <Sub text="EQ · OTT · COMP · DRIVE · CHORUS · TAPE ECHO · REVERB" delay={16} color={C.ice} size={24} />
      </div>
      <Flash color={C.cyan} peak={0.4} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 6. Wavetable editor
const Editor: React.FC = () => {
  const f = useCurrentFrame();
  const s = useSpring(0, 20, 90);
  return (
    <AbsoluteFill>
      <Background accent={C.cyan} />
      <Stage>
        <Card
          still="capture/wt-editor.png"
          crop={{ x: 368, y: 160, w: 1184, h: 764, sw: 1920, sh: 1080 }}
          width={1180}
          x={260}
          y={20}
          rx={(1 - s) * 18}
          ry={-8 + f * 0.05}
          scale={0.86 + 0.14 * s + f * 0.0006}
        />
      </Stage>
      <Text left={110} top={330}>
        <Kicker text="WAVETABLE EDITOR" delay={2} />
        <Headline text={'DRAW.\nIMPORT.\nMORPH.'} size={56} delay={6} stagger={1.4} />
      </Text>
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 7. DR-1
const Drums: React.FC = () => {
  const f = useCurrentFrame();
  const hero = f < BAR;
  const a = useSpring(0, 22, 70);
  const b = useSpring(Math.round(BAR), 18, 90);
  const c = useSpring(Math.round(BAR) + 6, 18, 90);
  const d = useSpring(Math.round(BAR) + 12, 18, 90);
  return (
    <AbsoluteFill>
      <Background accent={C.amber} accent2={C.cyan} />
      {hero ? (
        <>
          <Stage perspective={1800}>
            <Card name="dr-full" width={1560} height={900} accent={C.amber} x={330} y={60 + (1 - a) * 400} z={-150} rx={10 + (1 - a) * 30} ry={-18 + f * 0.04} rz={2} />
          </Stage>
          <AbsoluteFill style={{ background: 'linear-gradient(90deg, rgba(6,7,11,0.92) 0%, rgba(6,7,11,0.6) 30%, transparent 55%)' }} />
          <Text left={110} top={380}>
            <Kicker text="02 / DR-1" color={C.amber} delay={4} />
            <Headline text={'DRUM\nMACHINE'} size={76} delay={8} />
          </Text>
          <Flash color={C.amber} peak={0.45} />
        </>
      ) : (
        <Sequence from={Math.round(BAR)} layout="none">
          <Stage>
            <Card name="dr-seq" width={1480} accent={C.cyan} y={230 + (1 - b) * 500} z={-120} rx={22} />
            <Card name="dr-pads" width={430} accent={C.amber} x={-640 + (1 - c) * -400} y={-150} z={120} ry={20} rx={4} opacity={c} />
            <Card name="dr-noise" width={420} accent={C.amber} x={640 + (1 - d) * 400} y={-150} z={120} ry={-20} rx={4} opacity={d} />
          </Stage>
          <Text center top={90}>
            <Kicker text="DR-1" color={C.amber} />
            <Headline text={'PADS · LAYERS · STEPS'} size={50} align="center" delay={4} />
            <Sub text="16 PADS · OSC + 808 SAMPLES + NOISE/RING · POLY STEP SEQ" delay={12} />
          </Text>
          <Flash color={C.amber} peak={0.25} />
        </Sequence>
      )}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 8. BL-1
const Bass: React.FC = () => {
  const a = useSpring(0, 18, 90);
  const b = useSpring(6, 18, 90);
  const c = useSpring(12, 18, 90);
  const f = useCurrentFrame();
  return (
    <AbsoluteFill>
      <Background accent={C.green} accent2={C.violet} />
      <Stage>
        <Card name="bl-filter" width={600} accent={C.violet} x={-520 + (1 - a) * -500} y={-90} ry={16 - f * 0.03} opacity={a} />
        <Card name="bl-acc" width={760} accent={C.green} x={460 + (1 - b) * 500} y={-150} ry={-14} opacity={b} />
        <Card name="bl-seq" width={1520} accent={C.green} y={320 + (1 - c) * 400} z={-80} rx={20} />
      </Stage>
      <Text center top={60}>
        <Kicker text="03 / BL-1" color={C.green} delay={2} />
        <Headline text="ACID BASSLINE" size={58} align="center" delay={6} highlight={{ from: 5, color: C.green }} />
      </Text>
      <Sequence from={40} layout="none">
        <Callout x={1270} y={330} dx={110} dy={-70} label="ACCENT · SLIDE" color={C.green} />
      </Sequence>
      <Wipe color={C.green} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 9. SQ-4
const FOCUS = ['sq-focus-dr', 'sq-focus-bl', 'sq-focus-wt'];
const FOCUS_ACCENT = [C.amber, C.green, C.cyan];
const Session: React.FC = () => {
  const f = useCurrentFrame();
  const B = Math.round(BAR);
  const a = useSpring(0, 22, 70);
  if (f < 2 * B) {
    return (
      <AbsoluteFill>
        <Background accent={C.orange} accent2={C.amber} />
        <Stage perspective={1800}>
          <Card
            name={f < B ? 'sq-full' : 'sq-launch'}
            width={1500}
            accent={C.orange}
            x={300}
            y={60 + (1 - a) * 450}
            z={-120}
            rx={10 + (1 - a) * 30}
            ry={-18 + f * 0.035}
            rz={2}
            offsetMs={f < B ? 800 : 0}
          />
        </Stage>
        <AbsoluteFill style={{ background: 'linear-gradient(90deg, rgba(6,7,11,0.92) 0%, rgba(6,7,11,0.6) 30%, transparent 55%)' }} />
        <Text left={110} top={360}>
          <Kicker text="04 / SQ-4" color={C.orange} delay={4} />
          <Headline text={'SESSION\nLAUNCHER'} size={76} delay={8} />
          <Sub text="CLIPS · SCENES · LIVE ARRANGING" delay={26} />
        </Text>
        <Flash color={C.orange} peak={0.5} />
        <Sequence from={B} layout="none">
          <Flash color={C.orange} peak={0.18} dur={6} />
        </Sequence>
      </AbsoluteFill>
    );
  }
  if (f < 4 * B) {
    const g = f - 2 * B;
    // Step the front device on each half bar with an eased swap.
    const step = (2 * B) / 3;
    const k = Math.floor(g / step);
    const local = (g - k * step) / 12;
    const active = k === 0 ? 0 : k - 1 + Easing.inOut(Easing.cubic)(Math.min(1, local));
    return (
      <AbsoluteFill>
        <Background accent={FOCUS_ACCENT[Math.min(2, k)]} accent2={C.orange} />
        <Stage>
          {FOCUS.map((n, i) => {
            let r = (((i - active) % 3) + 3) % 3;
            if (r > 2.5) r -= 3;
            const exit = r < 0 ? -r : 0;
            return (
              <Card
                key={n}
                name={n}
                width={1150}
                accent={FOCUS_ACCENT[i]}
                x={230 + r * 230 + exit * -300}
                y={90 - r * 140 + exit * 200}
                z={-r * 380 + exit * 500}
                ry={-14}
                rx={4}
                opacity={r < 0 ? 1 - exit * 2 : 1 - r * 0.3}
              />
            );
          })}
        </Stage>
        <Text left={100} top={70}>
          <Kicker text="DEVICE FOCUS" color={C.orange} />
          <Headline text={'EVERY INSTRUMENT.\nONE SESSION.'} size={46} delay={3} />
        </Text>
        <Flash color={C.orange} peak={0.15} />
      </AbsoluteFill>
    );
  }
  const g = f - 4 * B;
  return (
    <AbsoluteFill>
      <Sequence from={4 * B} layout="none">
        <Background accent={C.orange} accent2={C.violet} />
        <Stage>
          <Card
            still="capture/sq-masterfx.png"
            crop={{ x: 240, y: 112, w: 1440, h: 690, sw: 1920, sh: 1080 }}
            width={1320}
            accent={C.orange}
            y={90}
            rx={8 - g * 0.06}
            scale={0.94 + g * 0.0012}
          />
        </Stage>
        <Text center top={40}>
          <Headline text="MASTER BUS" size={50} align="center" />
          <Sub text="EQ · OTT · COMP · LIMITER" delay={6} color={C.ice} />
        </Text>
        <Flash color={C.orange} peak={0.25} />
      </Sequence>
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 10. Beat montage
const MONTAGE = [
  'wt-oscA', 'dr-pads', 'bl-filter', 'wt-echo', 'dr-osc', 'wt-lfos', 'bl-osc', 'wt-reverb',
  'wt-filter', 'dr-noise', 'wt-ott', 'bl-env', 'wt-chorus', 'dr-sample', 'wt-comp', 'wt-oscB',
];
const WORDS = ['DESIGN', 'PROGRAM', 'SQUELCH', 'ECHO', 'LAYER', 'MODULATE', 'GLIDE', 'SPACE'];
const accentOf = (n: string) => (n.startsWith('dr') ? C.amber : n.startsWith('bl') ? C.green : n.includes('filter') || n.includes('reverb') || n.includes('echo') ? C.violet : C.cyan);
const MontageShot: React.FC<{ name: string; word?: string; i: number; len: number }> = ({ name, word, i, len }) => {
  const f = useCurrentFrame();
  const sign = i % 2 ? 1 : -1;
  const acc = accentOf(name);
  return (
    <AbsoluteFill>
      <Background accent={acc} accent2={C.void} />
      {word && (
        <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center' }}>
          <div
            style={{
              fontFamily: DISPLAY,
              fontSize: 230,
              letterSpacing: '0.08em',
              color: 'transparent',
              WebkitTextStroke: `2px ${acc}55`,
              transform: `scale(${1.15 - f * 0.006})`,
            }}
          >
            {word}
          </div>
        </AbsoluteFill>
      )}
      <Stage>
        <Card
          name={name}
          width={name.includes('lfos') || name.includes('osc') ? 1250 : 820}
          accent={acc}
          offsetMs={i * 170}
          rate={1.3}
          scale={lerp(f, 0, len, 1.12, 1.0)}
          ry={sign * (10 - f * 0.4)}
          rx={6}
          rz={sign * lerp(f, 0, len, 3, 0)}
        />
      </Stage>
      <Flash color={acc} peak={word ? 0.35 : 0.2} dur={6} />
    </AbsoluteFill>
  );
};
const Montage: React.FC = () => (
  <AbsoluteFill>
    {MONTAGE.map((n, i) => {
      const from = i < 8 ? Math.round(i * BEAT) : Math.round(8 * BEAT + (i - 8) * (BEAT / 2));
      const to = i < 7 ? Math.round((i + 1) * BEAT) : i < 15 ? Math.round(8 * BEAT + (i - 7) * (BEAT / 2)) : at(3);
      return (
        <Sequence key={n} from={from} durationInFrames={to - from}>
          <MontageShot name={n} word={WORDS[i]} i={i} len={to - from} />
        </Sequence>
      );
    })}
    <Sequence from={Math.round(12 * BEAT)}>
      <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', pointerEvents: 'none' }}>
        <div style={{ fontFamily: MONO, fontWeight: 600, fontSize: 30, letterSpacing: '0.5em', color: C.ice, background: 'rgba(6,7,11,0.7)', padding: '12px 28px', borderRadius: 8 }}>
          FOUR INSTRUMENTS · ONE ENGINE
        </div>
      </AbsoluteFill>
    </Sequence>
  </AbsoluteFill>
);

// ------------------------------------------------------------------ 11. Agent
const PROMPT_X0 = 54;
const PROMPT_W = 662;
const Agent: React.FC = () => {
  const f = useCurrentFrame();
  const s = useSpring(0, 26, 50);
  const typed = lerp(f, 18, 70, 0, 1, Easing.linear);
  const W = 1140;
  const k = W / 780;
  const caret = Math.floor(f / 8) % 2 === 0 || typed < 1;
  return (
    <AbsoluteFill>
      <Background accent={C.cyan} accent2={C.orange} />
      <Stage>
        <Card
          still="capture/sq-agent.png"
          crop={{ x: 570, y: 325, w: 780, h: 420, sw: 1920, sh: 1080 }}
          width={W}
          x={250}
          y={40}
          rx={(1 - s) * 20 + 2}
          ry={-10 + f * 0.03}
          scale={0.9 + 0.08 * s + f * 0.0005}
        >
          <div
            style={{
              position: 'absolute',
              left: PROMPT_X0 + typed * PROMPT_W,
              top: 304 * k,
              width: 700 - typed * PROMPT_W + 30,
              height: 27 * k,
              background: '#0b1016',
            }}
          />
          <div
            style={{
              position: 'absolute',
              left: PROMPT_X0 + typed * PROMPT_W + 2,
              top: 306 * k,
              width: 2,
              height: 26 * k,
              background: C.cyan,
              opacity: caret ? 1 : 0,
            }}
          />
        </Card>
      </Stage>
      <Text left={100} top={340}>
        <Kicker text="AI SOUND DESIGN" delay={4} />
        <Headline text={'FABLE\nAGENT'} size={70} delay={8} highlight={{ from: 5, color: C.cyan }} />
        <Sub text="DESCRIBE IT · REVIEW · APPLY" delay={30} color={C.ice} />
      </Text>
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 12. Soundtrack credit / build-up
const Credit: React.FC = () => {
  const f = useCurrentFrame();
  const dur = at(28) - at(26.5);
  const build = f / dur;
  const shake = build * build * 6;
  return (
    <AbsoluteFill>
      <Background accent={C.orange} accent2={C.cyan} />
      <AbsoluteFill style={{ opacity: 0.28, filter: 'blur(10px)', transform: `scale(${1.05 + build * 0.1})` }}>
        <Stage>
          <Card name="sq-launch" width={1920} glow={0} />
        </Stage>
      </AbsoluteFill>
      <AbsoluteFill style={{ transform: `translate(${Math.sin(f * 1.7) * shake}px, ${Math.cos(f * 2.3) * shake}px) scale(${1 + build * 0.06})` }}>
        <Text center top={360}>
          <Kicker text="THIS SOUNDTRACK" color={C.orange} />
          <Headline text="PHASE RUNNER" size={96} align="center" delay={4} stagger={1.4} />
          <Sub text="126 BPM · RENDERED BY THE FABLESYNTH SQ-4 ENGINE" delay={16} color={C.ice} />
        </Text>
      </AbsoluteFill>
      <AbsoluteFill style={{ background: '#fff', opacity: lerp(f, dur - 14, dur, 0, 0.5, Easing.in(Easing.quad)) }} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ 13. Outro
const CHIPS: [string, string, string][] = [
  ['WT-1', 'WAVETABLE', C.cyan],
  ['DR-1', 'DRUMS', C.amber],
  ['BL-1', 'ACID BASS', C.green],
  ['SQ-4', 'SESSIONS', C.orange],
];
const Outro: React.FC = () => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const dur = at(32) - at(28);
  const pulse = usePulse();
  const logo = spring({ frame: f, fps, config: { damping: 12, stiffness: 140 } });
  const fade = lerp(f, dur - 40, dur, 1, 0, Easing.in(Easing.quad));
  return (
    <AbsoluteFill style={{ opacity: fade }}>
      <Background accent={C.cyan} accent2={C.violet} />
      <div style={{ position: 'absolute', left: 160, top: 40, opacity: 0.55, transform: `scale(${1.15 + f * 0.0008})` }}>
        <Terrain width={1600} height={560} draw={1} t={f / 30 + 4} />
      </div>
      <Text center top={330}>
        <div style={{ transform: `scale(${0.6 + 0.4 * logo + pulse * 0.012})` }}>
          <Headline text="FABLESYNTH" size={140} highlight={{ from: 5, color: C.cyan }} align="center" stagger={0.6} />
        </div>
      </Text>
      <div style={{ position: 'absolute', top: 560, left: 0, right: 0, display: 'flex', justifyContent: 'center', gap: 28 }}>
        {CHIPS.map(([id, name, col], i) => {
          const s = spring({ frame: f - Math.round(BEAT * (4 + i)), fps, config: { damping: 14, stiffness: 160 } });
          return (
            <div
              key={id}
              style={{
                opacity: s,
                transform: `translateY(${(1 - s) * 40}px) scale(${0.8 + 0.2 * s})`,
                border: `1px solid ${col}88`,
                background: `${col}12`,
                boxShadow: `0 0 30px ${col}33`,
                borderRadius: 10,
                padding: '16px 26px',
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                gap: 8,
                minWidth: 210,
              }}
            >
              <div style={{ fontFamily: DISPLAY, fontSize: 34, letterSpacing: '0.12em', color: col }}>{id}</div>
              <div style={{ fontFamily: MONO, fontSize: 16, fontWeight: 600, letterSpacing: '0.3em', color: C.ice }}>{name}</div>
            </div>
          );
        })}
      </div>
      <div style={{ position: 'absolute', top: 760, left: 0, right: 0, display: 'flex', justifyContent: 'center' }}>
        <Sequence from={Math.round(BAR * 2)} layout="none">
          <Sub text="VST3 · AU · STANDALONE · IN YOUR BROWSER" color={C.ice} size={26} />
        </Sequence>
      </div>
      <Flash color="#ffffff" peak={0.9} dur={16} />
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------ Timeline
const SCENES: [number, number, React.FC][] = [
  [0, 2, Intro],
  [2, 4, WtHero],
  [4, 6, Oscillators],
  [6, 8, Modulation],
  [8, 10, FxRack],
  [10, 11, Editor],
  [11, 14, Drums],
  [14, 16, Bass],
  [16, 21, Session],
  [21, 24, Montage],
  [24, 26.5, Agent],
  [26.5, 28, Credit],
  [28, 32, Outro],
];

export const Promo: React.FC = () => (
  <AbsoluteFill style={{ background: C.void }}>
    <Audio src={staticFile('music.wav')} />
    {SCENES.map(([a, b, S], i) => (
      <Sequence key={i} from={at(a)} durationInFrames={at(b) - at(a)}>
        <S />
      </Sequence>
    ))}
  </AbsoluteFill>
);
