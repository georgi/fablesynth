import type { DynamicsMessage } from '../../engine/dynamics';
import type { EchoMessage } from '../../engine/echo';
import type { ReverbMessage } from '../../engine/reverb';
import type { ReactNode } from 'react';

/** Small structural surface shared by WT-1, BL-1 and DR-1 FX visualizers. */
export interface FxTelemetryEngine {
  subscribeDynamics(listener: (message: DynamicsMessage) => void): () => void;
  subscribeEcho(listener: (message: EchoMessage) => void): () => void;
  subscribeReverb(listener: (message: ReverbMessage) => void): () => void;
}

export interface FxPanelAdapter {
  engine: FxTelemetryEngine;
  params: Record<string, number>;
  setParam: (id: string, value: number) => void;
  /** Namespaces per-pad controls, e.g. `pad3.` for DR-1. */
  prefix?: string;
  /** Optional honest context shown in panel captions (DR-1 bus return). */
  context?: string;
  title?: string;
  renderKnob?: (id: string, key?: string) => ReactNode;
  renderPower?: (id: string) => ReactNode;
}

export const fxId = (prefix: string | undefined, id: string) => `${prefix ?? ''}${id}`;
