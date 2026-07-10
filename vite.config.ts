import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { resolve } from 'node:path';

// FableSynth ships three surfaces from one build:
//   index.html      the marketing landing page (vanilla TS + canvas, no React)
//   app/index.html  the playable synth SPA (React + the AudioWorklet engine)
//   drum/index.html the DR-1 drum machine SPA (React + AudioWorklet)
// The AudioWorklet DSP core (src/engine/worklet.js) is imported with `?url` so
// Vite copies it verbatim — it must stay a self-contained module that runs in
// the audio render thread. `base: './'` keeps every asset path relative so the
// site works unchanged under a GitHub Pages project subpath (/<repo>/).
export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    rollupOptions: {
      input: {
        landing: resolve(__dirname, 'index.html'),
        app: resolve(__dirname, 'app/index.html'),
        drum: resolve(__dirname, 'drum/index.html'),
      },
    },
  },
});
