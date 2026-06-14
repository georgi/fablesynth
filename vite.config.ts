import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// FableSynth is a static SPA. The AudioWorklet DSP core (src/engine/worklet.js)
// is imported with `?url` so Vite copies it verbatim — it must stay a
// self-contained module that runs in the audio render thread.
export default defineConfig({
  plugins: [react()],
  base: './',
});
