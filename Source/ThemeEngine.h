/*
 ==============================================================================
   ThemeEngine.h
   --------------------------------------------------------------------------
   Runtime-switchable theme engine with dynamic color palettes, animation
   configuration, and listener-based repaint notification.

   Replaces the static Theme.h namespace with a singleton that components
   query at paint time via ThemeEngine::getColor(ColorRole).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>

// ─── Color Roles ─────────────────────────────────────────────────────────────
// Every visual element references one of these roles instead of a hardcoded
// colour. The ThemePalette struct assigns a concrete juce::Colour to each role.

enum class ColorRole
{
    Bg,
    BgAlt,
    Panel,
    PanelAlt,

    Border,
    BorderGlow,

    TextPrimary,
    TextSecondary,
    TextOnAccent,

    Accent1,
    Accent2,
    Accent3,

    Good,
    Warn,
    Bad,

    KnobFill,
    KnobTrack,

    WaveformFill,
    Playhead,

    PadEmpty,
    PadEmptySelected,
    PadLoaded,
    PadLoadedSelected,
    PadSelectedIndicator,
    PadFlash,
    PadPlaying
};

// ─── ThemePalette ────────────────────────────────────────────────────────────

struct ThemePalette
{
    juce::String name;

    // Backgrounds
    juce::Colour bg;
    juce::Colour bgAlt;
    juce::Colour panel;
    juce::Colour panelAlt;

    // Borders
    juce::Colour border;
    juce::Colour borderGlow;

    // Text
    juce::Colour textPrimary;
    juce::Colour textSecondary;
    juce::Colour textOnAccent;

    // Accents
    juce::Colour accent1;
    juce::Colour accent2;
    juce::Colour accent3;

    // Semantic
    juce::Colour good;
    juce::Colour warn;
    juce::Colour bad;

    // Special
    juce::Colour knobFill;
    juce::Colour knobTrack;
    juce::Colour waveformFill;
    juce::Colour playhead;

    // Complete, opaque pad-state fills. Selection is represented by choosing
    // a state colour, never by compositing a shared tint over another fill.
    juce::Colour padEmpty;
    juce::Colour padEmptySelected;
    juce::Colour padLoaded;
    juce::Colour padLoadedSelected;
    juce::Colour padSelectedIndicator;
    juce::Colour padFlash;
    juce::Colour padPlaying;

    // Animation / glow
    float glowIntensity = 0.85f;
    float borderRadius  = 6.0f;

    /** Retrieve the colour for a given role. */
    juce::Colour getColor (ColorRole role) const;
};

// ─── AnimationConfig ─────────────────────────────────────────────────────────

enum class BackgroundMode
{
    Static,
    SlowCycle,
    Reactive
};

struct AnimationConfig
{
    bool  enabled               = false;
    bool  backgroundColorCycle  = false;
    bool  padPulseOnTrigger     = false;
    bool  progressBarShimmer    = false;
    bool  knobGlowOnChange      = false;
    float animationSpeed        = 1.0f;   // 0.25 – 2.0
    float backgroundCycleRate   = 0.02f;  // hue degrees per frame

    BackgroundMode backgroundMode = BackgroundMode::Static;
};

// ─── ThemeListener ───────────────────────────────────────────────────────────

class ThemeListener
{
public:
    virtual ~ThemeListener() = default;
    virtual void themeChanged() = 0;
};

// ─── ThemeEngine (singleton) ─────────────────────────────────────────────────

class ThemeEngine : private juce::Timer
{
public:
    /** Access the singleton instance. */
    static ThemeEngine& getInstance();

    // ── Palette access ──────────────────────────────────────────────────

    /** Get the current theme's colour for a role.
        During a crossfade transition, returns the interpolated colour. */
    juce::Colour getColor (ColorRole role) const;

    /** Convenience: `ThemeEngine::color(role)` — short-hand static accessor. */
    static juce::Colour color (ColorRole role);

    /** Get the full current palette (uses blended palette during transitions). */
    const ThemePalette& getCurrentPalette() const  { return isTransitioning() ? blendedPalette : currentPalette; }

    /** Get the current animation config. */
    const AnimationConfig& getAnimationConfig() const  { return animConfig; }
    AnimationConfig& getAnimationConfigMutable()       { return animConfig; }

    /** True if a crossfade is in progress. */
    bool isTransitioning() const { return transitionProgress < 1.0f; }

    // ── Theme switching ─────────────────────────────────────────────────

    /** Set the theme by name (must match a built-in palette name).
        If animations are enabled, crossfades over 500ms. */
    void setTheme (const juce::String& themeName);

    /** Set a custom palette directly. */
    void setTheme (const ThemePalette& palette);

    /** Get the names of all built-in themes. */
    juce::StringArray getAvailableThemeNames() const;

    /** Get a built-in palette by name. Returns nullptr if not found. */
    const ThemePalette* getBuiltInPalette (const juce::String& name) const;

    // ── Listener management ─────────────────────────────────────────────

    void addListener    (ThemeListener* l);
    void removeListener (ThemeListener* l);

    // ── Overlay helper (replaces Theme::overlay) ────────────────────────
    static juce::Colour overlay (float alpha);

private:
    ThemeEngine();

    void notifyListeners();
    void startTransition (const ThemePalette& target);
    void updateBlendedPalette();

    // ── Timer (for crossfade) ───────────────────────────────────────────
    void timerCallback() override;

    ThemePalette  currentPalette;    // The destination (or final) palette
    ThemePalette  transitionFrom;    // Palette we're fading from
    ThemePalette  blendedPalette;    // Interpolated palette (used during transition)
    float         transitionProgress = 1.0f; // 0→1 over transitionDurationMs
    static constexpr int transitionDurationMs = 500;
    static constexpr int transitionFps = 30;

    AnimationConfig animConfig;

    std::vector<ThemeListener*> listeners;

    // Built-in palettes (populated in constructor)
    std::vector<ThemePalette> builtInPalettes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeEngine)
};

// ─── Backward-compatible Theme:: namespace ───────────────────────────────────
// Provides drop-in replacements for the old Theme::xxx() functions.
// Components can be incrementally migrated to ThemeEngine::color(ColorRole).

namespace Theme
{
    inline juce::Colour bg()            { return ThemeEngine::color (ColorRole::Bg); }
    inline juce::Colour panel()         { return ThemeEngine::color (ColorRole::Panel); }
    inline juce::Colour panelAlt()      { return ThemeEngine::color (ColorRole::PanelAlt); }
    inline juce::Colour border()        { return ThemeEngine::color (ColorRole::Border); }
    inline juce::Colour borderStrong()  { return ThemeEngine::color (ColorRole::BorderGlow); }
    inline juce::Colour text()          { return ThemeEngine::color (ColorRole::TextPrimary); }
    inline juce::Colour textSubtle()    { return ThemeEngine::color (ColorRole::TextSecondary); }
    inline juce::Colour accent()        { return ThemeEngine::color (ColorRole::Accent1); }
    inline juce::Colour accent2()       { return ThemeEngine::color (ColorRole::Accent2); }
    inline juce::Colour good()          { return ThemeEngine::color (ColorRole::Good); }
    inline juce::Colour warn()          { return ThemeEngine::color (ColorRole::Warn); }
    inline juce::Colour bad()           { return ThemeEngine::color (ColorRole::Bad); }
    inline juce::Colour overlay(float a){ return ThemeEngine::overlay (a); }
}
