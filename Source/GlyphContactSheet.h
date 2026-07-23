#pragma once

#include <JuceHeader.h>
#include "ControlSurfacePalette.h"
#include "Modifier.h"

/**
    Deterministic review fixtures and PNG export for the production modifier
    glyph language.
*/
class GlyphContactSheet
{
public:
    /** Returns a descriptor populated with representative planned values. */
    static ModifierDescriptor makeRepresentativeDescriptor (ModifierType type);

    /**
        Writes every production modifier at phases 0.00, 0.25, 0.50, and 0.75,
        followed by a compact reduced-motion frame.
    */
    static bool exportPng (const juce::File& destination,
                           const ControlSurfacePalette& palette);

private:
    GlyphContactSheet() = delete;
};
