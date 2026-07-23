#include "ModifierStickerOverlay.h"

#include "ModifierCategoryVisuals.h"
#include "ModifierGlyphRenderer.h"
#include "ModifierRegistry.h"

#include <array>
#include <cmath>

namespace
{
// Keep this explicit: serialized enum order differs from the registry's
// presentation order around Delay Dub Burst and Ducking.
constexpr std::array<ModifierType, ModifierStickerOverlay::stickerCount> canonicalTypes {{
    ModifierType::Reverse,
    ModifierType::Speed,
    ModifierType::Stretch,
    ModifierType::PitchUpOctave,
    ModifierType::PitchDownOctave,
    ModifierType::BeatSliceRandom,
    ModifierType::ArpSlice,
    ModifierType::SliceRepeater,
    ModifierType::PingPong,
    ModifierType::BufferDelayOn,
    ModifierType::BufferDelayDubBurst,
    ModifierType::BufferReverbOn,
    ModifierType::BufferLowPassOn,
    ModifierType::BufferHighPassOn,
    ModifierType::BufferShhhhhh,
    ModifierType::BufferTremolo,
    ModifierType::BufferChorusOn,
    ModifierType::BufferAutoPan,
    ModifierType::BufferDuckingOn,
    ModifierType::BufferSHLowPassOn,
    ModifierType::BufferSHHighPassOn,
    ModifierType::BufferGranularOn,
    ModifierType::BufferGranularMomentary,
    ModifierType::SwitchPart,
    ModifierType::QuarterNoteBurst,
    ModifierType::SwapModifierStack,
    ModifierType::ResetAll,
}};

static_assert (canonicalTypes.size() <= 64);
static_assert (ModifierStickerOverlay::columns * ModifierStickerOverlay::rows
               >= ModifierStickerOverlay::stickerCount);
}

ModifierStickerOverlay::Mask ModifierStickerOverlay::bitForType (ModifierType type) noexcept
{
    const int slot = slotForType (type);
    return slot >= 0 ? (Mask { 1 } << static_cast<unsigned int> (slot)) : Mask {};
}

int ModifierStickerOverlay::slotForType (ModifierType type) noexcept
{
    for (int slot = 0; slot < static_cast<int> (canonicalTypes.size()); ++slot)
        if (canonicalTypes[static_cast<size_t> (slot)] == type)
            return slot;

    return -1;
}

juce::Rectangle<float> ModifierStickerOverlay::getStickerBounds (
    juce::Rectangle<float> aperture,
    ModifierType type) noexcept
{
    const int slot = slotForType (type);
    if (slot < 0 || aperture.isEmpty())
        return {};

    const float cellWidth = aperture.getWidth() / static_cast<float> (columns);
    const float cellHeight = aperture.getHeight() / static_cast<float> (rows);
    const int column = slot % columns;
    const int rowFromTop = rows - 1 - slot / columns;

    const juce::Rectangle<float> cell {
        aperture.getX() + static_cast<float> (column) * cellWidth,
        aperture.getY() + static_cast<float> (rowFromTop) * cellHeight,
        cellWidth,
        cellHeight
    };

    const float gutter = juce::jmin (2.0f, juce::jmin (cellWidth, cellHeight) * 0.1f);
    return cell.reduced (gutter);
}

void ModifierStickerOverlay::draw (juce::Graphics& graphics,
                                   juce::Rectangle<float> aperture,
                                   Mask activeMask,
                                   Mask transientMask,
                                   const ControlSurfacePalette& palette,
                                   float animationPhase01,
                                   bool reducedMotion)
{
    if (aperture.isEmpty() || activeMask == 0)
        return;

    juce::Graphics::ScopedSaveState savedState (graphics);
    graphics.reduceClipRegion (aperture.getSmallestIntegerContainer());

    for (const auto type : canonicalTypes)
    {
        const Mask bit = bitForType (type);
        if ((activeMask & bit) == 0)
            continue;

        const auto chip = getStickerBounds (aperture, type);
        if (chip.isEmpty())
            continue;

        const float shortSide = juce::jmin (chip.getWidth(), chip.getHeight());
        const float cornerRadius = juce::jlimit (1.0f, 3.0f, shortSide * 0.18f);
        const float borderWidth = shortSide >= 14.0f ? 1.25f : 1.0f;
        const bool isTransient = (transientMask & bit) != 0;
        const auto categoryStyle = ModifierCategoryVisuals::forType (type, palette);
        const auto categoryColour = categoryStyle.colour;
        constexpr float baseFaceAlpha = 0.22f;
        const float transientPulse = isTransient && ! reducedMotion
            ? 0.5f + 0.5f * std::sin (
                juce::MathConstants<float>::twoPi * animationPhase01)
            : 0.0f;
        const float faceAlpha = baseFaceAlpha + transientPulse * 0.12f;
        const auto litCategoryColour = categoryColour.interpolatedWith (
            juce::Colours::white, transientPulse * 0.10f);

        // Two restrained bloom passes keep the tiny chip luminous without
        // washing a category tint across the rest of the pad.
        graphics.setColour (
            litCategoryColour.withAlpha (0.04f + transientPulse * 0.06f));
        graphics.fillRoundedRectangle (chip.expanded (2.0f), cornerRadius + 2.0f);
        graphics.setColour (
            litCategoryColour.withAlpha (0.08f + transientPulse * 0.10f));
        graphics.fillRoundedRectangle (chip.expanded (1.0f), cornerRadius + 1.0f);

        const auto face = chip.reduced (borderWidth);
        graphics.setColour (litCategoryColour.withAlpha (faceAlpha));
        graphics.fillRoundedRectangle (face, juce::jmax (0.5f, cornerRadius - borderWidth));

        const auto transientBorder = palette.safetyYellow
            .interpolatedWith (juce::Colours::white, transientPulse * 0.16f)
            .withAlpha (0.72f + transientPulse * 0.28f);
        graphics.setColour (
            isTransient ? transientBorder : categoryColour.withAlpha (0.58f));
        graphics.drawRoundedRectangle (chip.reduced (borderWidth * 0.5f),
                                       juce::jmax (0.5f, cornerRadius - borderWidth * 0.5f),
                                       borderWidth);

        ModifierGlyphState glyphState;
        glyphState.descriptor = ModifierRegistry::makeDescriptor (type);
        glyphState.phase01 = animationPhase01;
        glyphState.emphasis01 = 0.96f;
        glyphState.compact = true;
        glyphState.reducedMotion = reducedMotion;

        // Estimate the translucent face over the pad display so compact
        // glyphs can choose a legible monochrome foreground.
        auto stickerGlyphPalette = palette;
        const auto blendedSurface =
            palette.display.overlaidWith (litCategoryColour.withAlpha (faceAlpha));
        const auto glyphColour = palette.display
            .overlaidWith (categoryColour.withAlpha (baseFaceAlpha))
            .contrasting (1.0f);
        stickerGlyphPalette.raisedTile = blendedSurface;
        stickerGlyphPalette.ink = glyphColour;
        stickerGlyphPalette.mutedInk = glyphColour;
        stickerGlyphPalette.vermilion = glyphColour;
        stickerGlyphPalette.safetyYellow = glyphColour;
        stickerGlyphPalette.signalGreen = glyphColour;
        stickerGlyphPalette.ultramarine = glyphColour;
        stickerGlyphPalette.violet = glyphColour;

        const float glyphInset = juce::jmax (0.75f, shortSide * 0.06f);
        ModifierGlyphRenderer::draw (graphics, face.reduced (glyphInset),
                                     glyphState, stickerGlyphPalette);
    }
}
