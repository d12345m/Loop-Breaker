#include "GlyphContactSheet.h"

#include "ModifierGlyphRenderer.h"
#include "ModifierProbabilityManager.h"
#include "ModifierRegistry.h"
#include "ThemeFonts.h"

ModifierDescriptor GlyphContactSheet::makeRepresentativeDescriptor (ModifierType type)
{
    ModifierDescriptor descriptor = ModifierRegistry::makeDescriptor (type);

    switch (type)
    {
        case ModifierType::Speed:                   descriptor.plannedSpeed = 2.0; break;
        case ModifierType::Stretch:                 descriptor.plannedStretch = 0.5; break;
        case ModifierType::BeatSliceRandom:         descriptor.plannedSliceDivision = "1/16"; break;
        case ModifierType::ArpSlice:
            descriptor.plannedArpSequenceLength = 4;
            descriptor.plannedArpTotalSlices = 16;
            descriptor.plannedArpRepeatBars = 2;
            break;
        case ModifierType::SliceRepeater:
            descriptor.plannedSliceRepeaterReps = 8;
            descriptor.plannedSliceRepeaterTotal = 16;
            break;
        case ModifierType::PingPong:                descriptor.plannedPingPongDivision = 0.125; break;
        case ModifierType::BufferDelayOn:
        case ModifierType::BufferDelayDubBurst:
            descriptor.plannedDelayDivision = "1/8";
            descriptor.plannedDelayWet = 0.5;
            descriptor.plannedDelayFeedback = 0.65;
            break;
        case ModifierType::BufferReverbOn:
            descriptor.plannedWet = 0.5;
            descriptor.plannedFxFadeBars = 2.0;
            break;
        case ModifierType::BufferChorusOn:
            descriptor.plannedChorusMix = 0.5;
            descriptor.plannedChorusDepth = 0.7;
            descriptor.plannedChorusRateHz = 0.8;
            break;
        case ModifierType::BufferAutoPan:
            descriptor.plannedPanMix = 0.75;
            descriptor.plannedPanDepth = 0.8;
            descriptor.plannedPanRateHz = 0.5;
            break;
        case ModifierType::BufferSHLowPassOn:
        case ModifierType::BufferSHHighPassOn:     descriptor.plannedSHDivisionBars = 0.125; break;
        case ModifierType::BufferGranularOn:
        case ModifierType::BufferGranularMomentary:
            descriptor.plannedGrainDensityHz = 18.0;
            descriptor.plannedGrainSizeMs = 180.0;
            descriptor.plannedGrainPitchSpread = 12.0;
            descriptor.plannedGrainMix = 0.65;
            break;
        case ModifierType::Reverse:
        case ModifierType::PitchUpOctave:
        case ModifierType::PitchDownOctave:
        case ModifierType::BufferLowPassOn:
        case ModifierType::BufferHighPassOn:
        case ModifierType::BufferVolumeRampDown:
        case ModifierType::BufferTremolo:
        case ModifierType::BufferDuckingOn:
        case ModifierType::MasterHighPassOn:
        case ModifierType::MasterLowPassOn:
        case ModifierType::SwitchPart:
        case ModifierType::QuarterNoteBurst:
        case ModifierType::SwapModifierStack:
        case ModifierType::ResetAll:
        case ModifierType::Unknown:
            break;
    }

    return descriptor;
}

bool GlyphContactSheet::exportPng (const juce::File& destination,
                                   const ControlSurfacePalette& palette)
{
    constexpr int imageWidth = 1800;
    constexpr int titleBandHeight = 64;
    constexpr int columnBandHeight = 46;
    constexpr int headerHeight = titleBandHeight + columnBandHeight;
    constexpr int rowHeight = 158;
    constexpr int nameWidth = 270;
    constexpr int phaseWidth = 300;
    constexpr int compactWidth = imageWidth - nameWidth - (4 * phaseWidth);
    constexpr float phases[] = { 0.0f, 0.25f, 0.5f, 0.75f };

    const auto& allTypes = ModifierProbabilityManager::allModifierTypes();
    const int imageHeight = headerHeight + rowHeight * static_cast<int> (allTypes.size());

    juce::Image image (juce::Image::RGB, imageWidth, imageHeight, true);
    juce::Graphics g (image);
    auto& fonts = ThemeFonts::getInstance();

    g.fillAll (palette.canvas);

    g.setColour (palette.raisedTile);
    g.fillRect (0, 0, imageWidth, titleBandHeight);
    g.setColour (palette.ink);
    g.setFont (fonts.headingFont (28.0f));
    g.drawText ("LOOP BREAKER / GLYPH CONTACT SHEET",
                24, 0, imageWidth - 48, titleBandHeight,
                juce::Justification::centredLeft);

    g.setColour (palette.canvas);
    g.fillRect (0, titleBandHeight, imageWidth, columnBandHeight);

    const auto drawColumnLabel = [&] (const juce::String& label, int x, int width)
    {
        g.setColour (palette.mutedInk);
        g.setFont (fonts.monoBoldFont (13.0f));
        g.drawText (label, x, titleBandHeight, width, columnBandHeight,
                    juce::Justification::centred);
    };

    drawColumnLabel ("MODIFIER", 0, nameWidth);
    for (int column = 0; column < 4; ++column)
        drawColumnLabel ("PHASE " + juce::String (phases[column], 2),
                         nameWidth + column * phaseWidth, phaseWidth);
    drawColumnLabel ("COMPACT / REDUCED",
                     nameWidth + 4 * phaseWidth, compactWidth);

    g.setColour (palette.ink.withAlpha (0.24f));
    g.drawHorizontalLine (titleBandHeight, 0.0f, static_cast<float> (imageWidth));
    g.drawHorizontalLine (headerHeight, 0.0f, static_cast<float> (imageWidth));

    for (int row = 0; row < static_cast<int> (allTypes.size()); ++row)
    {
        const auto descriptor = makeRepresentativeDescriptor (
            allTypes[static_cast<size_t> (row)]);
        const int top = headerHeight + row * rowHeight;

        g.setColour ((row % 2 == 0 ? palette.raisedTile : palette.canvas).withAlpha (0.92f));
        g.fillRect (0, top, imageWidth, rowHeight);
        g.setColour (palette.ink.withAlpha (0.24f));
        g.drawHorizontalLine (top, 0.0f, static_cast<float> (imageWidth));

        g.setColour (palette.ink);
        g.setFont (fonts.bodyBoldFont (18.0f));
        g.drawFittedText (descriptor.shortName.toUpperCase(), 20, top + 36,
                          nameWidth - 36, 34, juce::Justification::centredLeft, 1);
        g.setColour (palette.mutedInk);
        g.setFont (fonts.monoFont (11.0f));
        g.drawText (descriptor.description.toUpperCase(), 20, top + 75,
                    nameWidth - 36, 24, juce::Justification::centredLeft);

        for (int column = 0; column < 4; ++column)
        {
            const auto cell = juce::Rectangle<float> (
                static_cast<float> (nameWidth + column * phaseWidth),
                static_cast<float> (top),
                static_cast<float> (phaseWidth),
                static_cast<float> (rowHeight));

            ModifierGlyphState state;
            state.descriptor = descriptor;
            state.phase01 = phases[column];
            ModifierGlyphRenderer::draw (g, cell.reduced (72.0f, 10.0f), state, palette);
        }

        const auto compactCell = juce::Rectangle<float> (
            static_cast<float> (nameWidth + 4 * phaseWidth),
            static_cast<float> (top),
            static_cast<float> (compactWidth),
            static_cast<float> (rowHeight));
        ModifierGlyphState compactState;
        compactState.descriptor = descriptor;
        compactState.phase01 = 0.25f;
        compactState.compact = true;
        compactState.reducedMotion = true;
        ModifierGlyphRenderer::draw (g, compactCell.withSizeKeepingCentre (104.0f, 104.0f),
                                     compactState, palette);
    }

    g.setColour (palette.ink.withAlpha (0.18f));
    for (int column = 0; column <= 4; ++column)
    {
        const int x = nameWidth + column * phaseWidth;
        g.drawVerticalLine (x, static_cast<float> (titleBandHeight),
                            static_cast<float> (imageHeight));
    }

    if (destination.existsAsFile() && ! destination.deleteFile())
        return false;

    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return false;

    const bool written = juce::PNGImageFormat().writeImageToStream (image, stream);
    stream.flush();
    return written && stream.getStatus().wasOk();
}
