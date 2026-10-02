/* FableSynth landing — live wavetable terrain hero + scroll choreography.
   Vanilla TS, no audio: a visual echo of the synth's signature 3D wavetable
   view (osc A cyan fading back into the chassis), drawn from the DSP thread in
   the real product, faked with morphing harmonics here. */

const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

/* ---------- nav: solidify on scroll ---------- */
const nav = document.getElementById('nav');
const onScroll = () => nav?.classList.toggle('scrolled', window.scrollY > 24);
onScroll();
window.addEventListener('scroll', onScroll, { passive: true });

/* ---------- reveal on enter ---------- */
const reveals = document.querySelectorAll<HTMLElement>('.reveal');
if (reduceMotion || !('IntersectionObserver' in window)) {
  reveals.forEach((el) => el.classList.add('in'));
} else {
  const io = new IntersectionObserver(
    (entries) => {
      for (const e of entries) {
        if (e.isIntersecting) {
          e.target.classList.add('in');
          io.unobserve(e.target);
        }
      }
    },
    { rootMargin: '0px 0px -12% 0px', threshold: 0.12 },
  );
  reveals.forEach((el) => io.observe(el));
}

/* ---------- hero: wavetable terrain ---------- */
const canvas = document.getElementById('wt-canvas') as HTMLCanvasElement | null;
const ctx = canvas?.getContext('2d') ?? null;

if (canvas && ctx) {
  const FRAMES = 34; // depth rows, back -> front
  const COLS = 150; // horizontal resolution per row
  let w = 0;
  let h = 0;
  let dpr = 1;

  const resize = () => {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    w = canvas.clientWidth;
    h = canvas.clientHeight;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  };
  resize();
  window.addEventListener('resize', resize);

  // A morphing single-cycle waveform: a few harmonics whose weights drift.
  const wave = (x: number, morph: number): number => {
    const p = x * Math.PI * 2;
    const a1 = 0.6 + 0.4 * Math.sin(morph);
    const a2 = 0.5 * Math.cos(morph * 0.7 + 1.3);
    const a3 = 0.34 * Math.sin(morph * 1.7 + 0.6);
    const a4 = 0.22 * Math.cos(morph * 2.3);
    return (
      a1 * Math.sin(p) +
      a2 * Math.sin(p * 2 + morph) +
      a3 * Math.sin(p * 3) +
      a4 * Math.sin(p * 5 + morph * 0.5)
    );
  };

  const lerp = (a: number, b: number, t: number) => a + (b - a) * t;

  const draw = (time: number) => {
    const t = time * 0.00018;
    ctx.clearRect(0, 0, w, h);

    // Composition: terrain sits in the right-centre, anchored low so the
    // headline (lower-left) breathes over the calmer back rows.
    const baseY = h * 0.62;
    const spanX = Math.min(w * 0.78, 1020);
    const originX = w * 0.5;
    const rowGap = Math.min(h * 0.012, 11);
    const amp = Math.min(h * 0.07, 64);

    // The "playing" frame sweeps slowly through the table.
    const playFrame = (Math.sin(t * 0.9) * 0.5 + 0.5) * (FRAMES - 1);

    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';

    for (let f = 0; f < FRAMES; f++) {
      const depth = f / (FRAMES - 1); // 0 = back, 1 = front
      const persp = 0.42 + depth * 0.58; // back rows narrower
      const morph = t * 1.4 + f * 0.26;
      const rowY = baseY - f * rowGap;
      const ampF = amp * (0.35 + 0.65 * depth);

      const dist = Math.abs(f - playFrame);
      const isPlay = dist < 0.6;
      const heat = Math.max(0, 1 - dist / 3.2); // glow near the playhead

      ctx.beginPath();
      for (let i = 0; i <= COLS; i++) {
        const u = i / COLS;
        const x = originX + (u - 0.5) * spanX * persp;
        const y = rowY - wave(u, morph) * ampF;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }

      // Back rows are dim slate; front + playhead bloom cyan, warmed amber.
      const baseL = lerp(0.16, 0.42, depth);
      if (isPlay) {
        ctx.strokeStyle = 'rgba(120, 240, 255, 0.95)';
        ctx.lineWidth = 2;
        ctx.shadowColor = 'rgba(77, 232, 255, 0.9)';
        ctx.shadowBlur = 18;
      } else {
        const r = Math.round(lerp(110, 77, depth) + heat * 60);
        const g = Math.round(lerp(125, 200, depth) + heat * 40);
        const b = Math.round(lerp(170, 230, depth));
        const aL = baseL + heat * 0.4;
        ctx.strokeStyle = `rgba(${r}, ${g}, ${b}, ${aL})`;
        ctx.lineWidth = lerp(0.7, 1.3, depth);
        ctx.shadowColor = 'rgba(77, 232, 255, 0.5)';
        ctx.shadowBlur = heat * 10;
      }
      ctx.stroke();
    }
    ctx.shadowBlur = 0;
  };

  if (reduceMotion) {
    // Single static frame, no animation loop.
    draw(3200);
  } else {
    let raf = 0;
    let running = true;
    const loop = (time: number) => {
      if (running) draw(time);
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);

    // Pause the loop while the hero is scrolled out of view.
    const heroVisible = new IntersectionObserver(
      ([e]) => {
        running = e.isIntersecting;
      },
      { threshold: 0 },
    );
    heroVisible.observe(canvas);
    document.addEventListener('visibilitychange', () => {
      running = !document.hidden && canvas.getBoundingClientRect().bottom > 0;
    });
    window.addEventListener('pagehide', () => cancelAnimationFrame(raf));
  }
}

/* ---------- demo film + chapter clips ----------
   One 59 s film, recorded from the real instruments. The hero shows a silent
   highlight loop until the visitor asks for sound; the sections replay their
   own chapter of the same file. Only one source is audible at a time. */

type Voice = { silence: () => void };
let audible: Voice | null = null;
const claimSound = (v: Voice) => {
  if (audible && audible !== v) audible.silence();
  audible = v;
};
const releaseSound = (v: Voice) => {
  if (audible === v) audible = null;
};
const fmt = (t: number) => `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, '0')}`;

const screen = document.querySelector<HTMLElement>('.stage-screen');
const film = screen?.querySelector<HTMLVideoElement>('.stage-film') ?? null;
const loopVid = screen?.querySelector<HTMLVideoElement>('.stage-loop') ?? null;

if (screen && film && loopVid) {
  const chapters = [...document.querySelectorAll<HTMLButtonElement>('.chapter')];
  const now = screen.querySelector<HTMLElement>('.sb-now');
  const muteBtn = screen.querySelector<HTMLButtonElement>('.sb-mute');
  const toggleBtn = screen.querySelector<HTMLButtonElement>('.sb-toggle');
  const setState = (s: 'loop' | 'film' | 'paused' | 'ended') => {
    screen.dataset.state = s;
    toggleBtn?.setAttribute('aria-label', s === 'paused' ? 'Play' : 'Pause');
  };
  const voice: Voice = {
    silence: () => {
      if (!film.paused) film.pause();
    },
  };

  if (reduceMotion) {
    loopVid.removeAttribute('autoplay');
    loopVid.pause();
  } else {
    // Hold the silent loop while the stage is off screen.
    new IntersectionObserver(([e]) => {
      if (screen.dataset.state !== 'loop') return;
      if (e.isIntersecting) loopVid.play().catch(() => {});
      else loopVid.pause();
    }, { threshold: 0.15 }).observe(screen);
  }

  const playFrom = (t: number) => {
    claimSound(voice);
    loopVid.pause();
    film.muted = muteBtn?.getAttribute('aria-pressed') === 'true';
    const go = () => {
      film.currentTime = t;
      film.play().catch(() => setState('paused'));
    };
    if (film.readyState >= 1) go();
    else film.addEventListener('loadedmetadata', go, { once: true });
    if (film.preload === 'none') {
      film.preload = 'auto';
      film.load();
    }
    setState('film');
  };

  screen.querySelector('.stage-play')?.addEventListener('click', () => playFrom(0));
  screen.querySelector('.stage-replay')?.addEventListener('click', () => playFrom(0));
  chapters.forEach((c) => c.addEventListener('click', () => playFrom(Number(c.dataset.start))));

  toggleBtn?.addEventListener('click', () => {
    if (film.paused) {
      claimSound(voice);
      film.play().catch(() => {});
    } else film.pause();
  });
  film.addEventListener('play', () => setState('film'));
  film.addEventListener('pause', () => {
    if (screen.dataset.state === 'film' && !film.ended) setState('paused');
  });
  film.addEventListener('ended', () => {
    setState('ended');
    releaseSound(voice);
  });
  muteBtn?.addEventListener('click', () => {
    const on = muteBtn.getAttribute('aria-pressed') !== 'true';
    muteBtn.setAttribute('aria-pressed', String(on));
    muteBtn.setAttribute('aria-label', on ? 'Unmute' : 'Mute');
    film.muted = on;
  });
  screen.querySelector('.sb-full')?.addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else (film.requestFullscreen?.() ?? Promise.resolve()).catch(() => {});
  });
  screen.addEventListener('keydown', (e) => {
    if (screen.dataset.state === 'loop' || (e.target as HTMLElement).closest('button, a')) return;
    if (e.key === ' ' || e.key === 'k') {
      e.preventDefault();
      toggleBtn?.click();
    }
  });

  // Chapter rail doubles as the scrubber: each segment fills as it plays.
  film.addEventListener('timeupdate', () => {
    const t = film.currentTime;
    if (now) now.textContent = fmt(t);
    for (const c of chapters) {
      const a = Number(c.dataset.start);
      const b = Number(c.dataset.end);
      const p = Math.min(1, Math.max(0, (t - a) / (b - a)));
      c.style.setProperty('--p', p.toFixed(3));
      c.setAttribute('aria-current', String(t >= a && t < b));
    }
  });
}

// Section clips: lazy-load the 720p film, loop one chapter, sound on request.
const clips = [...document.querySelectorAll<HTMLElement>('.clip')];
clips.forEach((fig) => {
  const v = fig.querySelector('video');
  const btn = fig.querySelector<HTMLButtonElement>('.clip-sound');
  if (!v || !btn) return;
  const start = Number(fig.dataset.start);
  const end = Number(fig.dataset.end);
  const label = btn.querySelector('span');
  const setSound = (on: boolean) => {
    v.muted = !on;
    btn.setAttribute('aria-pressed', String(on));
    if (label) label.textContent = on ? 'Sound on' : 'Hear it';
    if (!on) releaseSound(voice);
  };
  const voice: Voice = {
    silence: () => {
      setSound(false);
      if (reduceMotion) v.pause();
    },
  };
  const ensureSrc = () => {
    if (v.getAttribute('src')) return;
    v.src = v.dataset.src ?? '';
    v.addEventListener('loadedmetadata', () => { v.currentTime = start; }, { once: true });
  };
  const play = () => {
    ensureSrc();
    v.play().catch(() => {});
  };
  v.addEventListener('timeupdate', () => {
    if (v.currentTime >= end || v.currentTime < start - 0.5) v.currentTime = start;
  });
  btn.addEventListener('click', () => {
    const on = btn.getAttribute('aria-pressed') !== 'true';
    if (on) {
      claimSound(voice);
      ensureSrc();
      if (v.readyState >= 1) v.currentTime = start;
      setSound(true);
      play();
    } else {
      setSound(false);
      if (reduceMotion) v.pause();
    }
  });
  new IntersectionObserver(([e]) => {
    if (e.isIntersecting && !reduceMotion) play();
    if (!e.isIntersecting) {
      v.pause();
      if (btn.getAttribute('aria-pressed') === 'true') setSound(false);
    }
  }, { threshold: 0.35 }).observe(fig);
});
