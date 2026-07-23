#pragma once

#include <JuceHeader.h>

#include "ControlSurfacePalette.h"
#include "Modifier.h"

#include <cstdint>

/**
    Stateless layout and renderer for the compact modifier stickers shown over
    a pad aperture.

    Sticker slots follow the canonical modifier registry/contact-sheet order.
    The two retired master-filter IDs are deliberately excluded, leaving 27
    supported sticker types in a fixed 6-column by 5-row grid.
*/
class ModifierStickerOverlay
{
public:
    using Mask = std::uint64_t;

    static constexpr int stickerCount = 27;
    static constexpr int columns = 6;
    static constexpr int rows = 5;

    /** Returns the mask bit assigned to type, or zero for an unsupported type. */
    static Mask bitForType (ModifierType type) noexcept;

    /** Returns type's canonical row-major slot, or -1 for an unsupported type. */
    static int slotForType (ModifierType type) noexcept;

    /** Returns the inset sticker chip bounds for type within aperture. */
    static juce::Rectangle<float> getStickerBounds (juce::Rectangle<float> aperture,
                                                    ModifierType type) noexcept;

    /** Draws all active sticker chips; transient stickers receive a vermilion border. */
    static void draw (juce::Graphics& graphics,
                      juce::Rectangle<float> aperture,
                      Mask activeMask,
                      Mask transientMask,
                      const ControlSurfacePalette& palette);
};
