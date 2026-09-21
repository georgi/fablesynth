/** Compact, read-only bank metadata for agent-side musical orientation. */
type Technique = {
  id: string;
  title: string;
  purpose: string;
  cues: string[];
};

// These are intentionally phrased as transferable sound-design decisions, not
// preset dumps. They distil public educational material into FableSynth-native
// starting points, leaving the agent to inspect the current parameter catalog
// and make a distinct, reviewable proposal.
const techniqueLibrary: Record<string, readonly Technique[]> = {
  'WT-1': [
    {
      id: 'technique.dark-dub-chord',
      title: 'Dark dub chord',
      purpose: 'A short, soft-edged chord stab with space around it.',
      cues: [
        'Build an original minor or suspended voicing from complementary oscillator layers.',
        'Keep the sustained body dark with a low-pass filter, but use a brief filter-envelope lift for the attack.',
        'Use noise and saturation sparingly for texture; preserve headroom for the repeats.',
        'Use a tempo-related dotted delay and spacious reverb as rhythmic elements, not a wash.',
      ],
    },
    {
      id: 'technique.slow-chord-evolution',
      title: 'Slow chord evolution',
      purpose: 'Add movement without turning a warm chord into a bright lead.',
      cues: [
        'Use shallow, slow modulation of oscillator shape or blend and filter cutoff.',
        'Keep modulation rates musical and subtle so phrase changes remain audible.',
        'Vary effect depth gently rather than relying on high resonance or treble.',
      ],
    },
    {
      id: 'technique.warm-expressive-lead',
      title: 'Warm expressive lead',
      purpose: 'A close, human melodic voice that stays soft in a dense mix.',
      cues: [
        'Start from a rounded source and a lower register before adding width or movement.',
        'Use velocity or a gentle envelope to shape presence; avoid bright octave attacks and excessive resonance.',
        'Keep delay audible by leaving rests between phrases and using a restrained wet mix.',
      ],
    },
    {
      id: 'technique.organic-pluck',
      title: 'Organic percussive pluck',
      purpose: 'A tactile, rhythmic part that can support a groove without becoming a bell.',
      cues: [
        'Use a quick amplitude contour with a filtered, low-to-mid harmonic source.',
        'Let a small filter-envelope movement define the strike instead of harsh high frequencies.',
        'Keep the release short enough for the rhythm, then add only a trace of room or delay.',
      ],
    },
    {
      id: 'technique.wide-supporting-pad',
      title: 'Wide supporting pad',
      purpose: 'A slow, warm bed that complements—not masks—the active parts.',
      cues: [
        'Use complementary oscillator layers and modest detune for width while keeping the lowest frequencies focused.',
        'Move filter, blend, or texture slowly with shallow modulation rather than fast tremolo.',
        'Shape the spectrum around the bass and lead roles; brighter layers should remain secondary.',
      ],
    },
    {
      id: 'technique.character-keys',
      title: 'Character keys',
      purpose: 'A playable electric-key or organ-like voice with its own gesture and register.',
      cues: [
        'Balance a clear fundamental with a restrained upper layer so chords stay intelligible.',
        'Use a medium attack and release when legato feel matters, or a shorter contour for rhythmic comping.',
        'Add subtle motion or ambience after the core tone is useful on its own.',
      ],
    },
  ],
  'BL-1': [
    {
      id: 'technique.foundation-bass',
      title: 'Foundational bass',
      purpose: 'A stable low role that leaves room for dub-delay chords.',
      cues: [
        'Start with a focused mono low-frequency source and a clear, controlled transient.',
        'Use filtering and restrained drive for weight instead of bright octave layers.',
        'Write rests and phrase variants with the drums; do not fill every subdivision.',
      ],
    },
  ],
  'DR-1': [
    {
      id: 'technique.dub-drum-foundation',
      title: 'Dub drum foundation',
      purpose: 'Firm rhythm with a shared space rather than effects on every hit.',
      cues: [
        'Keep kick and bass roles distinct; preserve the kick transient and low-end headroom.',
        'Use soft clap and short, choked 808-style hats instead of pitched metallic percussion.',
        'Treat group effects as a controlled shared space with modest send and return levels.',
      ],
    },
  ],
  'SQ-4': [
    {
      id: 'technique.dub-arrangement',
      title: 'Dub arrangement and master space',
      purpose: 'Make four parts feel like one performance while preserving contrast.',
      cues: [
        'Compose drums and bass together, then leave deliberate gaps for chord repeats.',
        'Give one or two parts the evolving motion; keep the rest stable enough to anchor the groove.',
        'Use group and master processing for cohesion and protection, not to flatten every transient.',
      ],
    },
  ],
};

export function soundDesignReferences(instrument: string): Record<string, unknown> {
  return {
    techniqueLibrary: {
      readOnly: true,
      description: 'Original FableSynth design cues distilled from public educational sound-design material. They are not preset data or instructions to copy settings.',
      entries: techniqueLibrary[instrument] ?? [],
    },
  };
}

export function presetReferences(
  instrument: string,
  factoryNames: readonly string[],
  currentPreset: string,
  userNames: readonly string[] = [],
): Record<string, unknown> {
  const entries = [
    ...factoryNames.map((name, index) => ({ id: `preset.${index}`, name, source: 'factory', instrument })),
    ...userNames.map((name, index) => ({ id: `user.${index}`, name, source: 'user', instrument })),
  ];
  return {
    presetCatalog: {
      readOnly: true,
      description: 'Compact sound references for musical orientation. Filter entries before returning them.',
      currentPreset,
      entries,
    },
    ...soundDesignReferences(instrument),
  };
}
