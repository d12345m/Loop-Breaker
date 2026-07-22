#include <JuceHeader.h>
#include "ThemeEngine.h"

namespace
{
juce::Colour rgb (juce::uint32 value)
{
    return juce::Colour (static_cast<juce::uint8> ((value >> 16) & 0xff),
                         static_cast<juce::uint8> ((value >> 8) & 0xff),
                         static_cast<juce::uint8> (value & 0xff));
}

juce::Colour compositeOver (juce::Colour overlay, juce::Colour base)
{
    const auto alpha = overlay.getFloatAlpha();
    const auto inverseAlpha = 1.0f - alpha;
    return juce::Colour::fromFloatRGBA (overlay.getFloatRed() * alpha + base.getFloatRed() * inverseAlpha,
                                        overlay.getFloatGreen() * alpha + base.getFloatGreen() * inverseAlpha,
                                        overlay.getFloatBlue() * alpha + base.getFloatBlue() * inverseAlpha,
                                        1.0f);
}

class ThemePadColourTest : public juce::UnitTest
{
public:
    ThemePadColourTest()
        : juce::UnitTest ("Theme pad colours", "LoopBreaker")
    {
    }

    void runTest() override
    {
        const auto& engine = ThemeEngine::getInstance();
        const auto names = engine.getAvailableThemeNames();

        beginTest ("Control Surface is the sole canonical control-surface theme");
        expect (names.contains ("Control Surface"));
        expect (! names.contains ("Control Surface (Light)"));
        for (const auto& name : names)
            expect (! name.startsWith ("Control Surface /"));

        const auto* controlSurface = engine.getBuiltInPalette ("Control Surface");
        expect (controlSurface != nullptr);
        if (controlSurface != nullptr)
        {
            expect (controlSurface->padLoaded == rgb (0xE3DED0));
            expect (controlSurface->waveformFill == rgb (0x292B29));
            expect (controlSurface->padSelectedIndicator == rgb (0x3F9364));
            expect (controlSurface->padFlash == rgb (0xF36A2D));
        }

        beginTest ("Removed Control Surface names migrate to the canonical palette");
        expect (engine.getBuiltInPalette ("Control Surface (Light)") == controlSurface);
        expect (engine.getBuiltInPalette ("Control Surface / Nostromo Bone") == controlSurface);
        expect (engine.getBuiltInPalette ("Control Surface / Amber Instrument") == controlSurface);

        beginTest ("Every theme brightens loaded and empty pads when selected");
        for (const auto& name : names)
        {
            const auto* palette = engine.getBuiltInPalette (name);
            expect (palette != nullptr);
            if (palette == nullptr)
                continue;

            const auto loadedSelected = compositeOver (palette->padSelected, palette->padLoaded);
            const auto emptySelected = compositeOver (palette->padSelected, palette->padEmpty);
            expect (loadedSelected.getPerceivedBrightness() > palette->padLoaded.getPerceivedBrightness(),
                    name + " should brighten loaded pads");
            expect (emptySelected.getPerceivedBrightness() > palette->padEmpty.getPerceivedBrightness(),
                    name + " should brighten empty pads");
        }

        beginTest ("Each theme has an intentional saturated modifier flash");
        struct ExpectedFlash
        {
            const char* name;
            juce::uint32 colour;
        };

        constexpr ExpectedFlash expected[] {
            { "Control Surface",    0xF36A2D },
            { "Basic Light",        0x2997FF },
            { "Basic Dark",         0x26D9C2 },
            { "IIgs Writer (Blue)", 0xFF72D2 },
            { "Game Boy (Light)",   0xE5F25A },
            { "Gruvbox (Dark)",     0x83D8C5 },
            { "Pixel Grid (Dark)",  0x5EEBFF }
        };

        for (const auto& item : expected)
        {
            const auto* palette = engine.getBuiltInPalette (item.name);
            expect (palette != nullptr);
            if (palette != nullptr)
                expect (palette->padFlash == rgb (item.colour), juce::String (item.name) + " flash colour");
        }
    }
};

ThemePadColourTest themePadColourTest;
}
