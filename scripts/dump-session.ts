// Dumps the web factory session as the exact JSON string the web app persists
// (protocol.ts's saveSession does JSON.stringify(doc) into
// localStorage['fable.session.v1']) — used to produce the JUCE side's
// cross-compat fixture, juce/test/fixtures/web-session.json, so sq4_host_test
// can prove the two factories AND codecs agree end-to-end. Regenerate with:
//   npx tsx scripts/dump-session.ts > juce/test/fixtures/web-session.json
import { factorySession } from '../src/seq/factory';

console.log(JSON.stringify(factorySession()));
