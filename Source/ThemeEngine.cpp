/*
 ==============================================================================
   ThemeEngine.cpp
   --------------------------------------------------------------------------
   Implementation of the runtime theme engine — built-in palettes, color
   lookup, listener notification, and theme switching.
 ==============================================================================
*/

#include "ThemeEngine.h"

// ─── Helper: hex colour shorthand ────────────────────────────────────────────

static juce::Colour hex (juce::uint32 rgb)
{
    return juce::Colour (static_cast<juce::uint8> ((rgb >> 16) & 0xFF),
                         static_cast<juce::uint8> ((rgb >> 8)  & 0xFF),
                         static_cast<juce::uint8> (rgb & 0xFF));
}

static juce::Colour hexA (juce::uint32 rgb, float alpha)
{
    return hex (rgb).withAlpha (alpha);
}

// ─── ThemePalette::getColor ──────────────────────────────────────────────────

juce::Colour ThemePalette::getColor (ColorRole role) const
{
    switch (role)
    {
        case ColorRole::Bg:            return bg;
        case ColorRole::BgAlt:         return bgAlt;
        case ColorRole::Panel:         return panel;
        case ColorRole::PanelAlt:      return panelAlt;
        case ColorRole::Border:        return border;
        case ColorRole::BorderGlow:    return borderGlow;
        case ColorRole::TextPrimary:   return textPrimary;
        case ColorRole::TextSecondary: return textSecondary;
        case ColorRole::TextOnAccent:  return textOnAccent;
        case ColorRole::Accent1:       return accent1;
        case ColorRole::Accent2:       return accent2;
        case ColorRole::Accent3:       return accent3;
        case ColorRole::Good:          return good;
        case ColorRole::Warn:          return warn;
        case ColorRole::Bad:           return bad;
        case ColorRole::KnobFill:      return knobFill;
        case ColorRole::KnobTrack:     return knobTrack;
        case ColorRole::WaveformFill:  return waveformFill;
        case ColorRole::Playhead:      return playhead;
        case ColorRole::PadEmpty:      return padEmpty;
        case ColorRole::PadLoaded:     return padLoaded;
        case ColorRole::PadSelected:   return padSelected;
        case ColorRole::PadPlaying:    return padPlaying;
    }
    return juce::Colours::magenta; // should never happen
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Built-in Palettes
// ═══════════════════════════════════════════════════════════════════════════════

static ThemePalette makeNeonRave()
{
    ThemePalette p;
    p.name          = "Neon Rave";

    p.bg            = hex (0x0A0E14);
    p.bgAlt         = hex (0x111822);
    p.panel         = hex (0x161E2A);
    p.panelAlt      = hex (0x1A2332);

    p.border        = hex (0x2A3545);
    p.borderGlow    = hex (0x00E5FF);

    p.textPrimary   = hex (0xE8ECF0);
    p.textSecondary = hex (0x7A8A9E);
    p.textOnAccent  = hex (0x0A0E14);

    p.accent1       = hex (0x00E5FF);
    p.accent2       = hex (0xFF00E5);
    p.accent3       = hex (0x39FF14);

    p.good          = hex (0x39FF14);
    p.warn          = hex (0xFFB800);
    p.bad           = hex (0xFF2D55);

    p.knobFill      = hex (0x00E5FF);
    p.knobTrack     = hex (0x1A2332);
    p.waveformFill  = hex (0x00BFA6);
    p.playhead      = hex (0x00E5FF);

    p.padEmpty      = hex (0x111822);
    p.padLoaded     = hex (0x1A2332);
    p.padSelected   = hexA (0x00E5FF, 0.15f);
    p.padPlaying    = hex (0x39FF14);

    p.glowIntensity = 0.85f;
    p.borderRadius  = 6.0f;

    return p;
}

static ThemePalette makeVintageEmber()
{
    ThemePalette p;
    p.name          = "Vintage Ember";

    p.bg            = hex (0x1A0F0A);
    p.bgAlt         = hex (0x241610);
    p.panel         = hex (0x2D1C14);
    p.panelAlt      = hex (0x352218);

    p.border        = hex (0x4A3528);
    p.borderGlow    = hex (0xFF8C00);

    p.textPrimary   = hex (0xF5E6D0);
    p.textSecondary = hex (0xA08070);
    p.textOnAccent  = hex (0x1A0F0A);

    p.accent1       = hex (0xFF6B2B);
    p.accent2       = hex (0xFFAA00);
    p.accent3       = hex (0xE03030);

    p.good          = hex (0x7ACC3D);
    p.warn          = hex (0xFFAA00);
    p.bad           = hex (0xE03030);

    p.knobFill      = hex (0xFF6B2B);
    p.knobTrack     = hex (0x352218);
    p.waveformFill  = hex (0xFF8C42);
    p.playhead      = hex (0xFFAA00);

    p.padEmpty      = hex (0x241610);
    p.padLoaded     = hex (0x352218);
    p.padSelected   = hexA (0xFF6B2B, 0.15f);
    p.padPlaying    = hex (0xFFAA00);

    p.glowIntensity = 0.6f;
    p.borderRadius  = 5.0f;

    return p;
}

static ThemePalette makePixelGrid()
{
    ThemePalette p;
    p.name          = "Pixel Grid";

    p.bg            = hex (0x000000);
    p.bgAlt         = hex (0x111111);
    p.panel         = hex (0x1A1A1A);
    p.panelAlt      = hex (0x222222);

    p.border        = hex (0x333333);
    p.borderGlow    = hex (0x33FF33);

    p.textPrimary   = hex (0x33FF33);
    p.textSecondary = hex (0x1A8A1A);
    p.textOnAccent  = hex (0x000000);

    p.accent1       = hex (0x33FF33);
    p.accent2       = hex (0x33FFFF);
    p.accent3       = hex (0xFF33FF);

    p.good          = hex (0x33FF33);
    p.warn          = hex (0xFFFF33);
    p.bad           = hex (0xFF3333);

    p.knobFill      = hex (0x33FF33);
    p.knobTrack     = hex (0x222222);
    p.waveformFill  = hex (0x33FF33);
    p.playhead      = hex (0x33FFFF);

    p.padEmpty      = hex (0x111111);
    p.padLoaded     = hex (0x1A1A1A);
    p.padSelected   = hexA (0x33FF33, 0.15f);
    p.padPlaying    = hex (0x33FF33);

    p.glowIntensity = 0.9f;
    p.borderRadius  = 2.0f;

    return p;
}

static ThemePalette makeUltraviolet()
{
    ThemePalette p;
    p.name          = "Ultraviolet";

    p.bg            = hex (0x0C0618);
    p.bgAlt         = hex (0x120B22);
    p.panel         = hex (0x1A1030);
    p.panelAlt      = hex (0x221640);

    p.border        = hex (0x3D2A60);
    p.borderGlow    = hex (0xBF40FF);

    p.textPrimary   = hex (0xE8DEFF);
    p.textSecondary = hex (0x8A70B0);
    p.textOnAccent  = hex (0x0C0618);

    p.accent1       = hex (0xBF40FF);
    p.accent2       = hex (0x4D7CFF);
    p.accent3       = hex (0xFF40A0);

    p.good          = hex (0x40FFA0);
    p.warn          = hex (0xFFD040);
    p.bad           = hex (0xFF4080);

    p.knobFill      = hex (0xBF40FF);
    p.knobTrack     = hex (0x221640);
    p.waveformFill  = hex (0x7C5CFF);
    p.playhead      = hex (0xBF40FF);

    p.padEmpty      = hex (0x120B22);
    p.padLoaded     = hex (0x221640);
    p.padSelected   = hexA (0xBF40FF, 0.15f);
    p.padPlaying    = hex (0xBF40FF);

    p.glowIntensity = 0.8f;
    p.borderRadius  = 6.0f;

    return p;
}

static ThemePalette makeStudioClean()
{
    ThemePalette p;
    p.name          = "Studio Clean";

    p.bg            = hex (0x1E1E1E);
    p.bgAlt         = hex (0x252525);
    p.panel         = hex (0x2D2D2D);
    p.panelAlt      = hex (0x333333);

    p.border        = hex (0x444444);
    p.borderGlow    = hex (0xFFFFFF);

    p.textPrimary   = hex (0xEBEBEB);
    p.textSecondary = hex (0x999999);
    p.textOnAccent  = hex (0x1E1E1E);

    p.accent1       = hex (0x5B9BD5);
    p.accent2       = hex (0x4EC9B0);
    p.accent3       = hex (0xD4845E);

    p.good          = hex (0x6CC644);
    p.warn          = hex (0xE5C07B);
    p.bad           = hex (0xE06C75);

    p.knobFill      = hex (0x5B9BD5);
    p.knobTrack     = hex (0x333333);
    p.waveformFill  = hex (0x5B9BD5);
    p.playhead      = hex (0xEBEBEB);

    p.padEmpty      = hex (0x252525);
    p.padLoaded     = hex (0x333333);
    p.padSelected   = hexA (0x5B9BD5, 0.15f);
    p.padPlaying    = hex (0x5B9BD5);

    p.glowIntensity = 0.0f;
    p.borderRadius  = 4.0f;

    return p;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ThemeEngine implementation
// ═══════════════════════════════════════════════════════════════════════════════

ThemeEngine::ThemeEngine()
{
    // Register all built-in palettes
    builtInPalettes.push_back (makeNeonRave());
    builtInPalettes.push_back (makeVintageEmber());
    builtInPalettes.push_back (makePixelGrid());
    builtInPalettes.push_back (makeUltraviolet());
    builtInPalettes.push_back (makeStudioClean());

    // Default to Neon Rave
    currentPalette = builtInPalettes.front();
}

ThemeEngine& ThemeEngine::getInstance()
{
    static ThemeEngine instance;
    return instance;
}

juce::Colour ThemeEngine::getColor (ColorRole role) const
{
    if (isTransitioning())
        return blendedPalette.getColor (role);
    return currentPalette.getColor (role);
}

juce::Colour ThemeEngine::color (ColorRole role)
{
    return getInstance().getColor (role);
}

juce::Colour ThemeEngine::overlay (float alpha)
{
    return juce::Colours::black.withAlpha (juce::jlimit (0.0f, 1.0f, alpha));
}

void ThemeEngine::setTheme (const juce::String& themeName)
{
    for (const auto& palette : builtInPalettes)
    {
        if (palette.name.equalsIgnoreCase (themeName))
        {
            if (animConfig.enabled && currentPalette.name != palette.name)
            {
                startTransition (palette);
            }
            else
            {
                currentPalette = palette;
                blendedPalette = palette;
                transitionProgress = 1.0f;
                notifyListeners();
            }
            return;
        }
    }

    // If name not found, do nothing (keep current theme)
    DBG ("ThemeEngine::setTheme — unknown theme name: " + themeName);
}

void ThemeEngine::setTheme (const ThemePalette& palette)
{
    if (animConfig.enabled && currentPalette.name != palette.name)
    {
        startTransition (palette);
    }
    else
    {
        currentPalette = palette;
        blendedPalette = palette;
        transitionProgress = 1.0f;
        notifyListeners();
    }
}

juce::StringArray ThemeEngine::getAvailableThemeNames() const
{
    juce::StringArray names;
    for (const auto& p : builtInPalettes)
        names.add (p.name);
    return names;
}

const ThemePalette* ThemeEngine::getBuiltInPalette (const juce::String& name) const
{
    for (const auto& p : builtInPalettes)
        if (p.name.equalsIgnoreCase (name))
            return &p;
    return nullptr;
}

void ThemeEngine::addListener (ThemeListener* l)
{
    if (l != nullptr)
        listeners.push_back (l);
}

void ThemeEngine::removeListener (ThemeListener* l)
{
    listeners.erase (std::remove (listeners.begin(), listeners.end(), l), listeners.end());
}

void ThemeEngine::notifyListeners()
{
    for (auto* l : listeners)
        if (l != nullptr)
            l->themeChanged();
}

// ─── Theme crossfade transition ──────────────────────────────────────────────

static juce::Colour lerpColour (const juce::Colour& a, const juce::Colour& b, float t)
{
    return a.interpolatedWith (b, t);
}

void ThemeEngine::startTransition (const ThemePalette& target)
{
    transitionFrom = isTransitioning() ? blendedPalette : currentPalette;
    currentPalette = target;
    transitionProgress = 0.0f;
    updateBlendedPalette();
    notifyListeners();
    startTimerHz (transitionFps);
}

void ThemeEngine::updateBlendedPalette()
{
    const float t = transitionProgress;
    auto& a = transitionFrom;
    auto& b = currentPalette;

    blendedPalette.name         = b.name;
    blendedPalette.bg           = lerpColour (a.bg, b.bg, t);
    blendedPalette.bgAlt        = lerpColour (a.bgAlt, b.bgAlt, t);
    blendedPalette.panel        = lerpColour (a.panel, b.panel, t);
    blendedPalette.panelAlt     = lerpColour (a.panelAlt, b.panelAlt, t);
    blendedPalette.border       = lerpColour (a.border, b.border, t);
    blendedPalette.borderGlow   = lerpColour (a.borderGlow, b.borderGlow, t);
    blendedPalette.textPrimary  = lerpColour (a.textPrimary, b.textPrimary, t);
    blendedPalette.textSecondary= lerpColour (a.textSecondary, b.textSecondary, t);
    blendedPalette.textOnAccent = lerpColour (a.textOnAccent, b.textOnAccent, t);
    blendedPalette.accent1      = lerpColour (a.accent1, b.accent1, t);
    blendedPalette.accent2      = lerpColour (a.accent2, b.accent2, t);
    blendedPalette.accent3      = lerpColour (a.accent3, b.accent3, t);
    blendedPalette.good         = lerpColour (a.good, b.good, t);
    blendedPalette.warn         = lerpColour (a.warn, b.warn, t);
    blendedPalette.bad          = lerpColour (a.bad, b.bad, t);
    blendedPalette.knobFill     = lerpColour (a.knobFill, b.knobFill, t);
    blendedPalette.knobTrack    = lerpColour (a.knobTrack, b.knobTrack, t);
    blendedPalette.waveformFill = lerpColour (a.waveformFill, b.waveformFill, t);
    blendedPalette.playhead     = lerpColour (a.playhead, b.playhead, t);
    blendedPalette.padEmpty     = lerpColour (a.padEmpty, b.padEmpty, t);
    blendedPalette.padLoaded    = lerpColour (a.padLoaded, b.padLoaded, t);
    blendedPalette.padSelected  = lerpColour (a.padSelected, b.padSelected, t);
    blendedPalette.padPlaying   = lerpColour (a.padPlaying, b.padPlaying, t);

    blendedPalette.glowIntensity = a.glowIntensity + (b.glowIntensity - a.glowIntensity) * t;
    blendedPalette.borderRadius  = a.borderRadius  + (b.borderRadius  - a.borderRadius)  * t;
}

void ThemeEngine::timerCallback()
{
    const float step = 1.0f / ((float) transitionDurationMs / 1000.0f * (float) transitionFps);
    transitionProgress = juce::jmin (1.0f, transitionProgress + step);

    // Ease-in-out for smooth feel
    float eased = transitionProgress < 0.5f
                      ? 2.0f * transitionProgress * transitionProgress
                      : 1.0f - std::pow (-2.0f * transitionProgress + 2.0f, 2.0f) / 2.0f;
    float savedProgress = transitionProgress;
    transitionProgress = eased;
    updateBlendedPalette();
    transitionProgress = savedProgress;

    notifyListeners();

    if (transitionProgress >= 1.0f)
    {
        blendedPalette = currentPalette;
        stopTimer();
    }
}
