import { defineConfig } from 'vitest/config';

// Unit tests cover the pure web logic (slot helpers, preset round-trip). They
// run in a plain node environment with no DOM. Scope the runner to `src` so it
// never picks up JUCE's vendored example tests under juce/build/_deps.
export default defineConfig({
  test: {
    include: ['src/**/*.{test,spec}.ts'],
  },
});
