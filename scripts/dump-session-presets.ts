// Dumps all 40 web factory session presets as JSON — the JUCE side's
// cross-platform fixture (juce/test/fixtures/web-session-presets.json), so
// sq4_host_test can prove the two generators agree byte-for-byte. Regenerate:
//   npx tsx scripts/dump-session-presets.ts > juce/test/fixtures/web-session-presets.json
import { FACTORY_SESSION_PRESETS } from '../src/seq/sessionPresets';

console.log(JSON.stringify(FACTORY_SESSION_PRESETS.map(({ name, family, variation, energy, session }) => ({ name, family, variation, energy, session }))));
