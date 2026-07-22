/*
 ==============================================================================
   ThemeFonts.h
   --------------------------------------------------------------------------
   Centralized font manager for the visual design overhaul (Phase 5).

   Loads three bundled font families from BinaryData at startup:
     - Rajdhani (display / headings — stylized but readable)
     - JetBrains Mono (monospaced — value readouts, timestamps, code)
     - Press Start 2P (pixel — used by Pixel Grid and Game Boy)

   Provides role-based font accessors that automatically switch to the
   pixel font for the two pixel themes and the mono face for IIgs Writer.

   All fonts are SIL Open Font Licensed (OFL).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"

class ThemeFonts
{
public:
    /** Singleton access. */
    static ThemeFonts& getInstance()
    {
        static ThemeFonts instance;
        return instance;
    }

    // ── Font Role Accessors ─────────────────────────────────────────────
    // Each returns a juce::Font sized to the given height. The typeface is
    // automatically chosen based on the current theme and role.

    /** Display / branding font — Rajdhani Bold. Used for plugin title, section headers. */
    juce::Font displayFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.5f);
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height).boldened();
        return makeFont (displayBoldTypeface, height);
    }

    /** Section heading font — Rajdhani Bold at a slightly smaller size. */
    juce::Font headingFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.6f);
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height).boldened();
        return makeFont (displayBoldTypeface, height);
    }

    /** Body / label font — System sans-serif (no bundled override needed). */
    juce::Font bodyFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.6f);
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height);
        return juce::Font (juce::FontOptions().withHeight (height));
    }

    /** Bold body font. */
    juce::Font bodyBoldFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.6f);
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height).boldened();
        return juce::Font (juce::FontOptions().withHeight (height)).boldened();
    }

    /** Monospaced font — JetBrains Mono. For value readouts, timestamps, countdown. */
    juce::Font monoFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.6f);
        return makeFont (monoTypeface, height);
    }

    /** Monospaced bold font. */
    juce::Font monoBoldFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.6f);
        return makeFont (monoTypeface, height).boldened();
    }

    /** Pixel font — Press Start 2P. Used for pixel themes or special badges. */
    juce::Font pixelFont (float height) const
    {
        return makeFont (pixelTypeface, height);
    }

    /** Control label font — compact sans/mono/pixel face selected by theme. */
    juce::Font controlLabelFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, juce::jmax (7.0f, height * 0.6f));
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height);
        return juce::Font (juce::FontOptions().withHeight (height));
    }

    /** Modifier name font for the HUD — bold, slightly stylized. */
    juce::Font modifierNameFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, height * 0.6f);
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height).boldened();
        return makeFont (displayBoldTypeface, height);
    }

    /** Tab label font — bold, slightly stylized for the tab bar. */
    juce::Font tabFont (float height) const
    {
        if (usesPixelFont())
            return makeFont (pixelTypeface, juce::jmax (7.0f, height * 0.55f));
        if (isIIgsWriterTheme())
            return makeFont (monoTypeface, height).boldened();
        return makeFont (displayBoldTypeface, height);
    }

    /** Keyboard shortcut / badge font — monospaced. */
    juce::Font badgeFont (float height) const
    {
        return monoFont (height);
    }

    // ── Direct typeface access (for advanced use) ───────────────────────
    juce::Typeface::Ptr getDisplayBoldTypeface() const  { return displayBoldTypeface; }
    juce::Typeface::Ptr getDisplayRegularTypeface() const { return displayRegularTypeface; }
    juce::Typeface::Ptr getMonoTypeface() const         { return monoTypeface; }
    juce::Typeface::Ptr getPixelTypeface() const        { return pixelTypeface; }

private:
    ThemeFonts()
    {
        // Load typefaces from BinaryData (embedded .ttf resources)
        displayBoldTypeface    = loadTypeface (BinaryData::RajdhaniBold_ttf,
                                               BinaryData::RajdhaniBold_ttfSize);
        displayRegularTypeface = loadTypeface (BinaryData::RajdhaniRegular_ttf,
                                               BinaryData::RajdhaniRegular_ttfSize);
        monoTypeface           = loadTypeface (BinaryData::JetBrainsMonoRegular_ttf,
                                               BinaryData::JetBrainsMonoRegular_ttfSize);
        pixelTypeface          = loadTypeface (BinaryData::PressStart2PRegular_ttf,
                                               BinaryData::PressStart2PRegular_ttfSize);
    }

    static juce::Typeface::Ptr loadTypeface (const char* data, int dataSize)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) dataSize);
    }

    static juce::Font makeFont (juce::Typeface::Ptr typeface, float height)
    {
        if (typeface == nullptr)
            return juce::Font (juce::FontOptions().withHeight (height));

        return juce::Font (juce::FontOptions (typeface).withHeight (height));
    }

    static bool usesPixelFont()
    {
        const auto& name = ThemeEngine::getInstance().getCurrentPalette().name;
        return name == "Pixel Grid (Dark)" || name == "Game Boy (Light)";
    }

    static bool isIIgsWriterTheme()
    {
        return ThemeEngine::getInstance().getCurrentPalette().name == "IIgs Writer (Blue)";
    }

    juce::Typeface::Ptr displayBoldTypeface;
    juce::Typeface::Ptr displayRegularTypeface;
    juce::Typeface::Ptr monoTypeface;
    juce::Typeface::Ptr pixelTypeface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeFonts)
};
