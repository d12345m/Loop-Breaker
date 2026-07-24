/*
 ==============================================================================
   BackgroundAnimator.h
   --------------------------------------------------------------------------
   Sits behind all content as the bottom-most child of the editor.
   Paints a slowly hue-cycling (or reactive) background using a cached
   juce::Image for minimal CPU overhead.

   Modes:
     Static    — solid bg colour, zero overhead.
     SlowCycle — hue rotates continuously (~1° per second by default).
     Reactive  — brief hue shift toward modifier accent on trigger, then relax.

   Overlay effects (Phase 5 additions):
     - Noise/grain texture: a pre-generated static noise image composited at
       low opacity (3–5%) for analog warmth. Available in all themes.
     - Scanline effect: thin horizontal lines at 2px intervals, 5% opacity.
       Active only in the "Pixel Grid" theme.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "Animator.h"
#include "PlatformConfig.h"

class BackgroundAnimator : public juce::Component,
                           private juce::Timer,
                           public ThemeListener
{
public:
    BackgroundAnimator()
    {
        setInterceptsMouseClicks (false, false);  // click-through
        ThemeEngine::getInstance().addListener (this);
        generateNoiseTexture (256, 256);  // pre-generate a tileable noise image
        startTimerHz (LoopBreakerConfig::uiRefreshRateHz);
    }

    ~BackgroundAnimator() override
    {
        ThemeEngine::getInstance().removeListener (this);
    }

    // ── ThemeListener ───────────────────────────────────────────────────
    void themeChanged() override
    {
        cachedBg = {};   // invalidate cache
        repaint();
    }

    // ── Reactive trigger ────────────────────────────────────────────────
    /** Call when a modifier fires — briefly shifts hue toward @p accentColour. */
    void triggerReactivePulse (juce::Colour accentColour)
    {
        if (! isReactive())
            return;

        reactiveTarget = accentColour;
        reactiveBlend  = 0.35f;   // peak blend
        // Animator decays it back to 0 over 800 ms
        reactiveDecay.start (800, [this] (float p)
        {
            reactiveBlend = 0.35f * (1.0f - p);
            repaint();
        }, {}, Animator::Easing::EaseOut);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const auto& anim    = ThemeEngine::getInstance().getAnimationConfig();
        const bool isPixelGrid = (palette.name == "Pixel Grid (Dark)");

        auto mode = anim.backgroundMode;
        if (! anim.enabled)
            mode = BackgroundMode::Static;

        if (mode == BackgroundMode::Static)
        {
            g.setColour (palette.bg);
            g.fillRect (bounds);
            paintOverlayEffects (g, bounds, palette, anim, isPixelGrid);
            return;
        }

        // Compute the animated background colour
        juce::Colour baseBg = palette.bg;

        if (mode == BackgroundMode::SlowCycle || mode == BackgroundMode::Reactive)
        {
            // Rotate hue — scale the effect based on background brightness.
            // Dark themes need boosted saturation to see any change;
            // light themes should stay subtle (just a gentle tint).
            float h, s, b;
            baseBg.getHSB (h, s, b);

            const bool isLight = (b > 0.5f);
            h = std::fmod (h + hueOffset, 1.0f);

            if (isLight)
            {
                // Light themes: gentle tint — low saturation, preserve brightness
                s = juce::jlimit (0.0f, 0.12f, s + 0.06f);
                // b stays as-is (already bright)
            }
            else
            {
                // Dark themes: boost saturation so the shift is visible
                s = juce::jmax (s, 0.55f);
                b = juce::jmax (b, 0.14f);
            }

            baseBg = juce::Colour::fromHSV (h, s, b, baseBg.getFloatAlpha());
        }

        if (mode == BackgroundMode::Reactive && reactiveBlend > 0.001f)
        {
            baseBg = baseBg.interpolatedWith (reactiveTarget, reactiveBlend);
        }

        // Use cached image to avoid full repaint overhead
        const bool needsResize = cachedBg.isNull()
                                 || cachedBg.getWidth()  != bounds.getWidth()
                                 || cachedBg.getHeight() != bounds.getHeight();

        if (needsResize || cachedBgColour != baseBg)
        {
            // Only allocate when the size changes; otherwise reuse the buffer
            if (needsResize)
                cachedBg = juce::Image (juce::Image::ARGB, juce::jmax (1, bounds.getWidth()),
                                        juce::jmax (1, bounds.getHeight()), false);

            juce::Graphics ig (cachedBg);

            ig.setColour (baseBg);
            ig.fillRect (cachedBg.getBounds());

            // Subtle radial spotlight (very gentle — 8% central lightening)
            {
                auto cx = (float) cachedBg.getWidth()  * 0.5f;
                auto cy = (float) cachedBg.getHeight() * 0.5f;
                auto r  = juce::jmax (cx, cy) * 1.2f;
                juce::ColourGradient spot (baseBg.brighter (0.08f), cx, cy,
                                           juce::Colours::transparentBlack, cx + r, cy, true);
                ig.setGradientFill (spot);
                ig.fillRect (cachedBg.getBounds());
            }

            cachedBgColour = baseBg;
        }

        g.drawImageAt (cachedBg, 0, 0);

        // Overlay effects (noise, scanlines) on top of the background
        paintOverlayEffects (g, bounds, palette, anim, isPixelGrid);
    }

private:
    // ── Overlay Effects ─────────────────────────────────────────────────

    /** Paint noise grain texture and scanlines on top of the background. */
    void paintOverlayEffects (juce::Graphics& g, const juce::Rectangle<int>& bounds,
                              const ThemePalette& palette, const AnimationConfig& anim,
                              bool isPixelGrid)
    {
        // ── Noise / grain texture overlay ──
        // Available in themes that opt in with a non-zero glow intensity.
        // Composited at 3–5% opacity for a subtle analog warmth.
        if (palette.glowIntensity > 0.01f && ! noiseTexture.isNull())
        {
            const float noiseAlpha = 0.03f + 0.02f * palette.glowIntensity;  // 3–5%
            g.setOpacity (noiseAlpha);

            // Tile the noise texture across the full bounds
            const int tw = noiseTexture.getWidth();
            const int th = noiseTexture.getHeight();
            for (int y = bounds.getY(); y < bounds.getBottom(); y += th)
                for (int x = bounds.getX(); x < bounds.getRight(); x += tw)
                    g.drawImageAt (noiseTexture, x, y);

            g.setOpacity (1.0f);
        }

        // ── Scanline effect (Pixel Grid theme only) ──
        // Thin horizontal lines at 2px intervals, 5% opacity — like a CRT refresh.
        if (isPixelGrid && anim.enabled)
        {
            const float scanlineAlpha = 0.05f;
            g.setColour (juce::Colours::white.withAlpha (scanlineAlpha));

            // Offset scanlines by the sweep position for a slow sweep animation
            const int lineSpacing = 2;
            const int startY = bounds.getY() + scanlineSweepOffset;

            for (int y = startY; y < bounds.getBottom(); y += lineSpacing)
            {
                if (y >= bounds.getY())
                    g.fillRect (bounds.getX(), y, bounds.getWidth(), 1);
            }

            // Draw one brighter "sweep" line that moves down the screen (CRT refresh)
            if (anim.enabled)
            {
                const int sweepY = bounds.getY() + static_cast<int> (scanlineSweepPhase * (float) bounds.getHeight());
                const float sweepAlpha = 0.08f;
                g.setColour (juce::Colours::white.withAlpha (sweepAlpha));
                g.fillRect (bounds.getX(), sweepY, bounds.getWidth(), 2);
                // Soft glow above/below the sweep
                g.setColour (juce::Colours::white.withAlpha (sweepAlpha * 0.4f));
                g.fillRect (bounds.getX(), sweepY - 2, bounds.getWidth(), 2);
                g.fillRect (bounds.getX(), sweepY + 2, bounds.getWidth(), 2);
            }
        }
    }

    // ── Noise Texture Generation ────────────────────────────────────────

    /** Generate a tileable monochrome noise image for analog-feel overlay. */
    void generateNoiseTexture (int w, int h)
    {
        noiseTexture = juce::Image (juce::Image::ARGB, w, h, true);

        juce::Image::BitmapData bitmapData (noiseTexture, juce::Image::BitmapData::writeOnly);

        // Simple LCG-based noise for deterministic, fast generation
        juce::uint32 seed = 0xDEADBEEF;
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                seed = seed * 1664525u + 1013904223u;
                const juce::uint8 v = static_cast<juce::uint8> ((seed >> 16) & 0xFF);
                // White noise pixel at full alpha — opacity is controlled at composite time
                bitmapData.setPixelColour (x, y, juce::Colour (v, v, v, (juce::uint8) 255));
            }
        }
    }

    // ── Timer ───────────────────────────────────────────────────────────

    void timerCallback() override
    {
        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
        const bool isPixelGrid = (ThemeEngine::getInstance().getCurrentPalette().name == "Pixel Grid (Dark)");

        auto mode = anim.backgroundMode;
        if (! anim.enabled)
            mode = BackgroundMode::Static;

        // Skip all work if the component isn't showing on screen (e.g. DAW hid the editor)
        if (! isShowing())
            return;

        bool needsRepaint = false;

        if (mode != BackgroundMode::Static)
        {
            // Advance hue offset
            constexpr double dt = LoopBreakerConfig::uiRefreshIntervalSeconds;
            hueOffset += (float) (anim.backgroundCycleRate * anim.animationSpeed * dt);
            if (hueOffset >= 1.0f)
                hueOffset -= 1.0f;

            // Tick reactive decay
            reactiveDecay.tick (dt);

            // Invalidate cached colour so paint() regenerates into the
            // existing image buffer (avoids reallocating ~1.9 MB per frame).
            cachedBgColour = juce::Colour();
            needsRepaint = true;
        }

        // Advance scanline sweep for Pixel Grid theme
        if (isPixelGrid && anim.enabled)
        {
            scanlineSweepPhase += static_cast<float> (
                LoopBreakerConfig::uiRefreshIntervalSeconds)
                * anim.animationSpeed * 0.15f; // ~1 sweep per 6-7 seconds
            if (scanlineSweepPhase >= 1.0f)
                scanlineSweepPhase -= 1.0f;
            needsRepaint = true;
        }

        if (needsRepaint)
            repaint();
    }

    bool isReactive() const
    {
        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
        return anim.enabled && anim.backgroundMode == BackgroundMode::Reactive;
    }

    float hueOffset = 0.0f;

    // Reactive pulse state
    juce::Colour reactiveTarget { juce::Colours::cyan };
    float        reactiveBlend  = 0.0f;
    Animator     reactiveDecay;

    // Cached background image
    juce::Image  cachedBg;
    juce::Colour cachedBgColour;

    // Noise / grain texture (pre-generated, tileable)
    juce::Image  noiseTexture;

    // Scanline effect state (Pixel Grid theme)
    float scanlineSweepPhase  = 0.0f;   // 0→1 sweep position
    int   scanlineSweepOffset = 0;       // static offset for the grid pattern

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackgroundAnimator)
};
