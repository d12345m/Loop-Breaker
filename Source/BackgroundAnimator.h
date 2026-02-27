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
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "Animator.h"

class BackgroundAnimator : public juce::Component,
                           private juce::Timer,
                           public ThemeListener
{
public:
    BackgroundAnimator()
    {
        setInterceptsMouseClicks (false, false);  // click-through
        ThemeEngine::getInstance().addListener (this);
        startTimerHz (15);   // 15 FPS background refresh — decoupled from 20 Hz UI
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

        auto mode = anim.backgroundMode;
        if (! anim.enabled || ! anim.backgroundColorCycle)
            mode = BackgroundMode::Static;

        if (mode == BackgroundMode::Static)
        {
            g.setColour (palette.bg);
            g.fillRect (bounds);
            return;
        }

        // Compute the animated background colour
        juce::Colour baseBg = palette.bg;

        if (mode == BackgroundMode::SlowCycle || mode == BackgroundMode::Reactive)
        {
            // Rotate hue
            float h, s, b;
            baseBg.getHSB (h, s, b);
            h = std::fmod (h + hueOffset, 1.0f);
            baseBg = juce::Colour::fromHSV (h, s, b, baseBg.getFloatAlpha());
        }

        if (mode == BackgroundMode::Reactive && reactiveBlend > 0.001f)
        {
            baseBg = baseBg.interpolatedWith (reactiveTarget, reactiveBlend);
        }

        // Use cached image to avoid full repaint overhead
        if (cachedBg.isNull() || cachedBg.getWidth() != bounds.getWidth()
            || cachedBg.getHeight() != bounds.getHeight()
            || cachedBgColour != baseBg)
        {
            cachedBg = juce::Image (juce::Image::ARGB, juce::jmax (1, bounds.getWidth()),
                                    juce::jmax (1, bounds.getHeight()), true);
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
    }

private:
    void timerCallback() override
    {
        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();

        auto mode = anim.backgroundMode;
        if (! anim.enabled || ! anim.backgroundColorCycle)
            mode = BackgroundMode::Static;

        if (mode == BackgroundMode::Static)
            return;   // nothing to animate

        // Advance hue offset
        const double dt = 1.0 / 15.0;   // ~66.7 ms per tick
        hueOffset += (float) (anim.backgroundCycleRate * anim.animationSpeed * dt);
        if (hueOffset >= 1.0f)
            hueOffset -= 1.0f;

        // Tick reactive decay
        reactiveDecay.tick (dt);

        cachedBg = {};   // invalidate — colour changed
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackgroundAnimator)
};
