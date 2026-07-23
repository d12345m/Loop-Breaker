#include <JuceHeader.h>
#include "GlyphContactSheet.h"
#include "ModifierStickerOverlay.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"

#include <iostream>

namespace
{
int runUnitTests()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("LoopBreaker", 0x4c4f4f50);

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    return failures == 0 ? 0 : 1;
}

int exportGlyphSheet (const juce::String& requestedPath)
{
    const auto destination = juce::File::isAbsolutePath (requestedPath)
                           ? juce::File (requestedPath)
                           : juce::File::getCurrentWorkingDirectory().getChildFile (requestedPath);
    const auto palette = ControlSurfacePalette::fromTheme (
        ThemeEngine::getInstance().getCurrentPalette());

    if (! GlyphContactSheet::exportPng (destination, palette))
    {
        std::cerr << "Failed to export glyph contact sheet to "
                  << destination.getFullPathName() << '\n';
        return 1;
    }

    std::cout << "Exported glyph contact sheet to "
              << destination.getFullPathName() << '\n';
    return 0;
}

int exportStickerStudy (const juce::String& requestedPath)
{
    const auto destination = juce::File::isAbsolutePath (requestedPath)
                           ? juce::File (requestedPath)
                           : juce::File::getCurrentWorkingDirectory().getChildFile (requestedPath);
    const auto palette = ControlSurfacePalette::fromTheme (
        ThemeEngine::getInstance().getCurrentPalette());
    auto& fonts = ThemeFonts::getInstance();

    constexpr int width = 1600;
    constexpr int height = 1040;
    juce::Image image (juce::Image::RGB, width, height, true);
    juce::Graphics g (image);
    g.fillAll (palette.canvas);

    g.setColour (palette.raisedTile);
    g.fillRect (0, 0, width, 84);
    g.setColour (palette.ink);
    g.setFont (fonts.headingFont (28.0f));
    g.drawText ("LOOP BREAKER / PAD STICKER STUDY",
                32, 0, width - 64, 84, juce::Justification::centredLeft);

    using Stickers = ModifierStickerOverlay;
    const auto maskFor = [] (std::initializer_list<ModifierType> types)
    {
        Stickers::Mask mask {};
        for (const auto type : types)
            mask |= Stickers::bitForType (type);
        return mask;
    };

    const auto drawPad = [&] (juce::Rectangle<float> bounds,
                              const juce::String& label,
                              Stickers::Mask active,
                              Stickers::Mask transient)
    {
        g.setColour (palette.ink.withAlpha (0.32f));
        g.fillRoundedRectangle (bounds.translated (3.0f, 4.0f), 7.0f);
        g.setColour (palette.raisedTile);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (palette.ink.withAlpha (0.42f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

        auto title = bounds.removeFromTop (36.0f);
        g.setColour (palette.ink);
        g.setFont (fonts.monoBoldFont (13.0f));
        g.drawText (label, title.reduced (12.0f, 0.0f),
                    juce::Justification::centredLeft);

        auto aperture = bounds.reduced (12.0f, 10.0f);
        g.setColour (palette.display);
        g.fillRect (aperture);
        g.setColour (palette.ink.withAlpha (0.32f));
        g.drawRect (aperture, 1.0f);

        juce::Path waveform;
        const float centreY = aperture.getCentreY();
        waveform.startNewSubPath (aperture.getX(), centreY);
        for (int x = 0; x <= static_cast<int> (aperture.getWidth()); x += 5)
        {
            const float phase = static_cast<float> (x) * 0.055f;
            const float envelope = 0.4f + 0.6f * std::sin (
                static_cast<float> (x) * 0.008f
                + juce::MathConstants<float>::halfPi);
            waveform.lineTo (aperture.getX() + static_cast<float> (x),
                             centreY + std::sin (phase) * 18.0f * envelope);
        }
        g.setColour (palette.ultramarine.withAlpha (0.44f));
        g.strokePath (waveform, juce::PathStrokeType (1.0f));

        Stickers::draw (g, aperture, active | transient, transient, palette);
    };

    drawPad ({ 32.0f, 116.0f, 746.0f, 330.0f }, "PAD 01 / PERSISTENT STACK",
             maskFor ({ ModifierType::Reverse, ModifierType::Speed,
                        ModifierType::BufferReverbOn, ModifierType::BufferTremolo,
                        ModifierType::BufferSHLowPassOn }), 0);
    drawPad ({ 822.0f, 116.0f, 746.0f, 330.0f }, "PAD 02 / ANOTHER STACK",
             maskFor ({ ModifierType::PitchUpOctave, ModifierType::BeatSliceRandom,
                        ModifierType::BufferDelayOn, ModifierType::BufferChorusOn,
                        ModifierType::BufferGranularOn }), 0);
    drawPad ({ 32.0f, 478.0f, 746.0f, 330.0f }, "PAD 01 / SWAP IN FLIGHT",
             maskFor ({ ModifierType::PitchUpOctave, ModifierType::BeatSliceRandom,
                        ModifierType::BufferDelayOn, ModifierType::BufferChorusOn,
                        ModifierType::BufferGranularOn }),
             maskFor ({ ModifierType::SwapModifierStack }));
    drawPad ({ 822.0f, 478.0f, 746.0f, 330.0f }, "PAD 02 / TEMPORARY OVERLAYS",
             maskFor ({ ModifierType::Reverse, ModifierType::Speed,
                        ModifierType::BufferReverbOn, ModifierType::BufferTremolo,
                        ModifierType::BufferSHLowPassOn }),
             maskFor ({ ModifierType::BufferDelayDubBurst,
                        ModifierType::BufferVolumeRampDown,
                        ModifierType::SwitchPart }));

    const auto stressLabel = juce::Rectangle<int> (32, 840, width - 64, 24);
    g.setColour (palette.mutedInk);
    g.setFont (fonts.monoBoldFont (12.0f));
    g.drawText ("FIXED SLOT STRESS TEST / ALL 27 POSITIONS",
                stressLabel, juce::Justification::centredLeft);
    const auto stressAperture = juce::Rectangle<float> (32.0f, 874.0f,
                                                        width - 64.0f, 130.0f);
    g.setColour (palette.display);
    g.fillRect (stressAperture);
    const auto allBits = (Stickers::Mask { 1 } << Stickers::stickerCount) - 1;
    Stickers::draw (g, stressAperture, allBits,
                    maskFor ({ ModifierType::BufferDelayDubBurst,
                               ModifierType::BufferVolumeRampDown,
                               ModifierType::BufferGranularMomentary,
                               ModifierType::SwitchPart,
                               ModifierType::QuarterNoteBurst,
                               ModifierType::SwapModifierStack }),
                    palette);

    if (destination.existsAsFile() && ! destination.deleteFile())
        return 1;

    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return 1;

    const bool written = juce::PNGImageFormat().writeImageToStream (image, stream);
    stream.flush();
    if (! written || ! stream.getStatus().wasOk())
        return 1;

    std::cout << "Exported pad sticker study to "
              << destination.getFullPathName() << '\n';
    return 0;
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 1)
        return runUnitTests();

    if (argc == 3 && juce::String (argv[1]) == "--export-glyph-sheet")
        return exportGlyphSheet (juce::String::fromUTF8 (argv[2]));

    if (argc == 3 && juce::String (argv[1]) == "--export-sticker-study")
        return exportStickerStudy (juce::String::fromUTF8 (argv[2]));

    std::cerr << "Usage: " << argv[0]
              << " [--export-glyph-sheet <output.png>]"
                 " [--export-sticker-study <output.png>]\n";
    return 2;
}
