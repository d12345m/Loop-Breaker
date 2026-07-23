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
        expect (names.contains ("Toxic Sorbet (Light)"));
        expect (names.contains ("Marathon Acid (Dark)"));
        expect (! names.contains ("Basic Light"));
        expect (! names.contains ("Basic Dark"));
        for (const auto& name : names)
            expect (! name.startsWith ("Control Surface /"));

        const auto* controlSurface = engine.getBuiltInPalette ("Control Surface");
        expect (controlSurface != nullptr);
        if (controlSurface != nullptr)
        {
            expect (controlSurface->padLoaded == rgb (0xE3DED0));
            expect (controlSurface->padEmptySelected == rgb (0xE5EED6));
            expect (controlSurface->padLoadedSelected == rgb (0xC7D9BD));
            expect (controlSurface->waveformFill == rgb (0x292B29));
            expect (controlSurface->padSelectedIndicator == rgb (0x3F9364));
            expect (controlSurface->padFlash == rgb (0xF36A2D));
        }

        beginTest ("Removed Control Surface names migrate to the canonical palette");
        expect (engine.getBuiltInPalette ("Control Surface (Light)") == controlSurface);
        expect (engine.getBuiltInPalette ("Control Surface / Nostromo Bone") == controlSurface);
        expect (engine.getBuiltInPalette ("Control Surface / Amber Instrument") == controlSurface);

        beginTest ("Retired basic theme names migrate to the product default");
        expect (engine.getBuiltInPalette ("Basic Light") == controlSurface);
        expect (engine.getBuiltInPalette ("Basic Dark") == controlSurface);

        beginTest ("Replacement themes keep their signature colours and geometry");
        const auto* sorbet = engine.getBuiltInPalette ("Toxic Sorbet (Light)");
        expect (sorbet != nullptr);
        if (sorbet != nullptr)
        {
            expect (sorbet->bg == rgb (0xFF9BD7));
            expect (sorbet->accent1 == rgb (0x4C00CC));
            expect (sorbet->waveformFill == rgb (0xFFD447));
            expect (sorbet->padLoaded == rgb (0x65005D));
            expect (sorbet->padLoadedSelected == rgb (0x3B1694));
            expectWithinAbsoluteError (sorbet->glowIntensity, 0.75f, 0.001f);
            expectWithinAbsoluteError (sorbet->borderRadius, 9.0f, 0.001f);
        }

        const auto* marathon = engine.getBuiltInPalette ("Marathon Acid (Dark)");
        expect (marathon != nullptr);
        if (marathon != nullptr)
        {
            expect (marathon->bg == rgb (0x05060D));
            expect (marathon->accent1 == rgb (0xB7FF00));
            expect (marathon->accent2 == rgb (0xFF2BD6));
            expect (marathon->padLoaded == rgb (0x0B1848));
            expect (marathon->padLoadedSelected == rgb (0x2E3C78));
            expectWithinAbsoluteError (marathon->glowIntensity, 1.0f, 0.001f);
            expectWithinAbsoluteError (marathon->borderRadius, 1.0f, 0.001f);
        }

        beginTest ("Orange Chaos has intentional high-energy signals");
        const auto* orangeChaos = engine.getBuiltInPalette ("Orange Chaos (Wild)");
        expect (orangeChaos != nullptr);
        if (orangeChaos != nullptr)
        {
            expect (orangeChaos->bg == rgb (0xFF4D00));
            expect (orangeChaos->accent1 == rgb (0xC6FF00));
            expect (orangeChaos->accent2 == rgb (0x2AEEFF));
            expect (orangeChaos->accent3 == rgb (0x4E7DFF));
            expect (orangeChaos->padLoaded == rgb (0x0B4966));
            expect (orangeChaos->padLoadedSelected == rgb (0x223F9A));
            expectWithinAbsoluteError (orangeChaos->glowIntensity, 1.0f, 0.001f);
            expectWithinAbsoluteError (orangeChaos->borderRadius, 12.0f, 0.001f);
        }

        beginTest ("Game Boy playhead matches its visible waveform colour");
        const auto* gameBoy = engine.getBuiltInPalette ("Game Boy (Light)");
        expect (gameBoy != nullptr);
        if (gameBoy != nullptr)
        {
            expect (gameBoy->waveformFill == rgb (0x9BBC0F));
            expect (gameBoy->playhead == gameBoy->waveformFill);
        }

        beginTest ("Every theme defines four opaque, distinct pad states");
        for (const auto& name : names)
        {
            const auto* palette = engine.getBuiltInPalette (name);
            expect (palette != nullptr);
            if (palette == nullptr)
                continue;

            expect (palette->padEmpty.isOpaque(), name + " empty state should be opaque");
            expect (palette->padEmptySelected.isOpaque(), name + " selected empty state should be opaque");
            expect (palette->padLoaded.isOpaque(), name + " loaded state should be opaque");
            expect (palette->padLoadedSelected.isOpaque(), name + " selected loaded state should be opaque");
            expect (palette->padEmpty != palette->padEmptySelected,
                    name + " should distinguish empty selection");
            expect (palette->padLoaded != palette->padLoadedSelected,
                    name + " should distinguish loaded selection");
            expect (palette->padEmpty != palette->padLoaded,
                    name + " should distinguish loaded pads");
            expect (palette->padEmptySelected != palette->padLoadedSelected,
                    name + " should distinguish selected loaded pads");
        }

        beginTest ("Each theme has an intentional saturated modifier flash");
        struct ExpectedFlash
        {
            const char* name;
            juce::uint32 colour;
        };

        constexpr ExpectedFlash expected[] {
            { "Control Surface",      0xF36A2D },
            { "Toxic Sorbet (Light)", 0x00D9C7 },
            { "Marathon Acid (Dark)", 0xFF2BD6 },
            { "IIgs Writer (Blue)",   0xFF72D2 },
            { "Orange Chaos (Wild)", 0xFFFD3D },
            { "Game Boy (Light)",     0xE5F25A },
            { "Gruvbox (Dark)",       0x83D8C5 },
            { "Pixel Grid (Dark)",    0x5EEBFF }
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
