// Dumps the web factory session in the *download* form — exactly the bytes
// the SESSIONS browser writes to disk (sessionLibrary.ts's exportSessionJson:
// patches embedded, pretty-printed, trailing newline) — so sq4_host_test can
// prove a file downloaded from the browser loads in the VST. Regenerate with:
//   npx tsx scripts/dump-session-file.ts > juce/test/fixtures/web-session-download.json
import { factorySession } from '../src/seq/factory';
import { exportSessionJson } from '../src/seq/sessionLibrary';

process.stdout.write(exportSessionJson(factorySession()));
