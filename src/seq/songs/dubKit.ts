import { FACTORY_KITS, kitToState } from '../../drum/kits';
import type { PatchDoc } from '../protocol';

// MINIMAL's tuned TINE rim sounds like a cowbell. Use a real clap on pad 3:
// soften its top end and shorten the envelope while retaining the clap flams.
export function dubKit(overrides: Record<string, number> = {}): PatchDoc {
  return { kind: 'inline', base: 9, data: { params: {
    ...kitToState(FACTORY_KITS[9]).params,
    // Firm, dry kick: a faster pitch drop and less flattened saturation.
    'pad0.aenv.att': 0.0005, 'pad0.aenv.hold': 0.012,
    'pad0.aenv.dec': 0.19, 'pad0.aenv.curve': 0.45,
    'pad0.penv.amt': 26, 'pad0.penv.dec': 0.032,
    'pad0.fx.drive.amt': 0.18, 'pad0.fx.drive.mix': 0.22,
    'pad0.fx.comp.on': 0, 'pad0.fx.reverb.on': 0,
    'pad0.lvl': 0.88,
    'pad1.aenv.att': 0.0005, 'pad1.penv.dec': 0.032,
    'pad1.fx.drive.amt': 0.18, 'pad1.fx.drive.mix': 0.22,
    'pad1.fx.comp.on': 0, 'pad1.fx.reverb.on': 0,
    'pad2.aenv.att': 0.0005, 'pad2.aenv.hold': 0.012,
    'pad2.aenv.dec': 0.10, 'pad2.fx.reverb.mix': 0.035,
    'pad3.oscA.level': 0, 'pad3.noise.level': 0, 'pad3.ring.mix': 0,
    'pad3.oscB.table': 1, 'pad3.oscB.level': 0.75, 'pad3.oscB.tune': -2,
    'pad3.oscB.pos': 0, 'pad3.oscB.detune': 1, 'pad3.penv.amt': 0,
    'pad3.aenv.att': 0.0005, 'pad3.aenv.hold': 0.018,
    'pad3.aenv.dec': 0.095, 'pad3.aenv.curve': 0.55,
    'pad3.flt.on': 1, 'pad3.flt.type': 0, 'pad3.flt.cut': 5200,
    'pad3.flt.res': 0.1, 'pad3.flt.drive': 0,
    'pad3.lvl': 0.56, 'pad3.v2l': 0.35,
    'pad3.fx.drive.on': 0, 'pad3.fx.comp.on': 0,
    'pad3.fx.chorus.on': 0, 'pad3.fx.delay.on': 0,
    'pad3.fx.reverb.on': 1, 'pad3.fx.reverb.size': 0.22, 'pad3.fx.reverb.mix': 0.04,
    ...overrides,
  } } };
}

// Shared soft 808 closed/open hats with their original choke relationship.
export function soft808DubKit() {
  const hats: Record<string, number> = {};
  for (const pad of [5, 6]) {
    const open = pad === 6;
    const sound = {
      'oscA.level': 0, 'noise.level': 0, 'ring.mix': 0,
      'oscB.table': open ? 3 : 2, // Actual 808OH / 808CH one-shots.
      'oscB.level': 0.9, 'oscB.tune': 0, 'oscB.fine': 0,
      'oscB.pos': 0, 'oscB.detune': 1, 'penv.amt': 0,
      'aenv.att': 0.0005, 'aenv.hold': 0.005,
      'aenv.dec': open ? 0.17 : 0.05, 'aenv.curve': 0.4,
      'flt.on': 1, 'flt.type': 0, 'flt.cut': 6500, 'flt.res': 0.05, 'flt.drive': 0,
      'lvl': open ? 0.60 : 0.72, 'v2l': 0.4, 'choke': 1,
      'fx.drive.on': 0, 'fx.comp.on': 0, 'fx.chorus.on': 0, 'fx.delay.on': 0,
      'fx.reverb.on': 0,
    };
    for (const [field, value] of Object.entries(sound)) hats[`pad${pad}.${field}`] = value;
  }
  return dubKit(hats);
}

