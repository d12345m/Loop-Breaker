#pragma once

#include "ControlSurfacePalette.h"
#include "ModifierRegistry.h"

namespace ModifierCategoryVisuals
{
struct Style
{
    int pipCount;
    juce::Colour colour;
};

/** Returns the shared category treatment used by queue pips and pad stickers.
    The registry's UI label is authoritative because the Special group contains
    modifiers from more than one runtime category. */
inline Style forType (ModifierType type, const ControlSurfacePalette& palette)
{
    const juce::String category = ModifierRegistry::get (type).categoryLabel;
    if (category == "Buffer")
        return { 1, palette.vermilion };
    if (category == "Channel Effect")
        return { 2, palette.safetyYellow };
    if (category == "Special")
        return { 3, palette.signalGreen };
    return { 0, palette.mutedInk };
}
}
