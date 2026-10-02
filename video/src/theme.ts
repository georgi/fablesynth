import { loadFont as loadMichroma } from '@remotion/google-fonts/Michroma';
import { loadFont as loadPlex } from '@remotion/google-fonts/IBMPlexMono';

export const DISPLAY = loadMichroma().fontFamily;
export const MONO = loadPlex('normal', { weights: ['400', '600'], subsets: ['latin'] }).fontFamily;

export const C = {
  void: '#06070b',
  panel: '#181c26',
  ice: '#dfe6f3',
  dim: '#6b768c',
  cyan: '#4de8ff',
  amber: '#ffa14d',
  violet: '#b18cff',
  green: '#4dff9e',
  orange: '#ff7a3d',
};

// The soundtrack is PHASE RUNNER at 126 BPM, rendered by the SQ-4 engine.
// Everything is cut on its bar grid.
export const FPS = 30;
export const BPM = 126;
export const BEAT = (FPS * 60) / BPM; // 14.2857 frames
export const BAR = BEAT * 4; // 57.142857 frames
export const at = (bar: number) => Math.round(bar * BAR);
export const TOTAL = at(32);
