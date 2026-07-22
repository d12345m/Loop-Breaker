#pragma once

#include <JuceHeader.h>
#include "ControlSurfacePalette.h"
#include "Modifier.h"

struct ModifierGlyphState
{
    ModifierDescriptor descriptor;
    float phase01 = 0.0f;
    float emphasis01 = 1.0f;
    bool compact = false;
    bool reducedMotion = false;
};

/** Stateless, normalized vector renderer shared by large and compact previews. */
class ModifierGlyphRenderer
{
public:
    static void draw (juce::Graphics&,
                      juce::Rectangle<float> bounds,
                      const ModifierGlyphState&,
                      const ControlSurfacePalette&);
};
