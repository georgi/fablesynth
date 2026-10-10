#pragma once
// Authored WT-1 / BL-1 pattern presets — C++ copy of src/patternPresets.ts and
// src/bass/patternPresets.ts, in the text notation of src/shared/stepNotation.ts
// (see StepNotation.h). Keep the strings identical to the web catalogs.

#include "StepNotation.h"

#include <vector>

namespace fable {

inline const std::vector<NotatedPreset>& notePatternPresets() {
    static const std::vector<NotatedPreset> presets {
        { "MINOR ARP", {
            "C D# G C' G D# C D# G C' G D# C D# G A#",
            "G# C D# G# D# C G# C D# G# D# C G# C D# G",
            "F G# C' F' C' G# F G# C' F' C' G# F G# C' D#'",
            "G A# D' G' D' A# G A# D' G' D' A# G A# D' B",
        } },
        { "DUB STABS", {
            ". . C! . . . . . . . D# . . . . .",
            ". . C! . . . . . . . A#, . . . . .",
            ". . G#,! . . . . . . . C . . . . .",
            ". . A#,! . . . . . . . G, . . . G, .",
        } },
        { "HOUSE STABS", {
            ". . G! . . G . . . . G! . . A# - .",
            ". . G! . . G . . . . F! . . D# - .",
        } },
        { "PLUCK MELODY", {
            "G! . D#' . C' . G . A# . G . F . D# .",
            "F! . G . G# . G . F . D# . D . C .",
            "G! . D#' . C' . G . A# . C' . D' . D#' .",
            "F'! - . D#' . D' . C' - . . . . . . .",
        } },
        { "TRANCE GATE", {
            "C! C C' C C C! C C' C C C! C C' C C C",
            "G#,! G#, G# G#, G#, G#,! G#, G# G#, G#, G#,! G#, G# G#, G#, G#,",
            "D#! D# D#' D# D# D#! D# D#' D# D# D#! D# D#' D# D# D#",
            "A#,! A#, A# A#, A#, A#,! A#, A# A#, A#, A#,! A#, A# A#, A#, A#,",
        } },
        { "BERLIN SEQUENCE", {
            "C! G C' D#' C' G C! G C' D#' C' G C! G A# G",
            "C! G C' D#' C' G C! G C' D#' C' G C! G F' D#'",
        } },
        { "SYNTHWAVE LEAD", {
            "C'! - - - - - A# - G - - - - - D# -",
            "F! - - - - - G - G# - - - G - - -",
            "C'! - - - - - D' - D#' - - - D' - C' -",
            "A#! - - - - - - - G - - - - - - -",
        } },
        { "CALL + RESPONSE", {
            "C'! . A# . G . . . . . . . . . . .",
            ". . . . . . . . G . F . D#! - - -",
            "C'! . A# . G . D#' . . . . . . . . .",
            ". . . . . . . . G F . D . C! - -",
        } },
        { "FUNK RIFF", {
            "C! . C . D#! . . C . . F . F# . G! .",
            ". . A# . G . F . D#! . . C . . . .",
        } },
        { "AMBIENT DRONE", {
            "C - - - - - - - - - - - - - - -",
            "G#, - - - - - - - - - - - - - - -",
            "D# - - - - - - - - - - - - - - -",
            "A#, - - - - - - - - - - - - - - -",
        } },
    };
    return presets;
}

inline const std::vector<NotatedPreset>& bassPatternPresets() {
    static const std::vector<NotatedPreset> presets {
        { "ACID ROLLER", {
            "C! C, . D#~ C . G! F#~ C . A#, C~ . D#'! C~ .",
            "C! C, . D#~ C . G! F#~ C . A#, C~ . D#'! C~ .",
            "C! C, . D#~ C . G! F#~ C . A#, C~ . D#'! C~ .",
            "C! . C F~ . C, G~ . C! D# . A#,~ C . C'!~ .",
        } },
        { "DUB TECHNO", {
            "C, - - . . . . . . . . . C, - . .",
            ". . . . . . G, - - . . . . . . .",
            "C, - - . . . . . . . . . C, - . .",
            ". . . . . . A#, - - . . . G, - . .",
        } },
        { "DEEP HOUSE", {
            ". . C - . . C' . . . C - . D# . .",
            ". . C - . . C' . . . G, - . A#, . .",
            ". . G#, - . . G#' . . . G#, - . C . .",
            ". . A#, - . . A# . . . G, - . F, . .",
        } },
        { "ROLLING TECHNO", {
            ". C C! C . C C! C . C C! C . C C! C",
            ". C C! C . C C! C . C C! D# . C C! A#,",
        } },
        { "MINIMAL PULSE", {
            "C! . . C . . C . . C! . . C . . .",
            "C! . . C . . C . . C! . . D# . C .",
        } },
        { "ELECTRO FUNK", {
            "C! . . C . . D# . C . G, . A#, . C' .",
            "C! . . C . . D# . F . D# . C . G, A#,",
        } },
        { "UK GARAGE", {
            "C! - . . . . . C, . . D# - . . . .",
            "C! - . . . . . C, . . G - F . D# .",
            "G#,! - . . . . . G#, . . C - . . . .",
            "A#,! - . . . . . A#, . . D# - D . A#, .",
        } },
        { "DISCO OCTAVES", {
            "C, C . C, C . C, C . C, C . C, C . C,",
            "G#, G# . G#, G# . G#, G# . A#, A# . A#, A# . A#,",
        } },
        { "SLIDE WALK", {
            "C! . D#~ . F~ . G! . A#~ . G~ . F . D#~ .",
            "C! . C'~ . A#~ . G! . F~ . D#~ . D . C~ .",
        } },
        { "REESE", {
            "C, - - - - - - - - - - - D#, - - -",
            "F, - - - - - - - G, - - - - - - -",
        } },
    };
    return presets;
}

} // namespace fable
