#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"

/**
    Semantic colours used by the control-surface glyph language.

    Keeping these names separate from the broader application theme makes the
    renderer deterministic and prevents individual glyphs from inventing their
    own colour choices.  Existing themes are mapped into the same roles; the
    Control Surface theme supplies the approved colours verbatim.
*/
struct ControlSurfacePalette
{
    juce::Colour canvas;
    juce::Colour raisedTile;
    juce::Colour display;
    juce::Colour ink;
    juce::Colour mutedInk;
    juce::Colour vermilion;
    juce::Colour safetyYellow;
    juce::Colour signalGreen;
    juce::Colour ultramarine;
    juce::Colour violet;

    static ControlSurfacePalette fromTheme (const ThemePalette& theme)
    {
        return {
            theme.bg,
            theme.panel,
            theme.padLoaded,
            theme.textPrimary,
            theme.textSecondary,
            theme.accent1,
            theme.warn,
            theme.good,
            theme.accent2,
            theme.accent3
        };
    }
};
