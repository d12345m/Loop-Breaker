/*
 ============================================================================== 
   UpcomingModifierDisplay.h
   --------------------------------------------------------------------------
   UI component that shows upcoming modifier name / description.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Modifier.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "ModifierGlyphRenderer.h"

class UpcomingModifierDisplay : public juce::Component,
                                private juce::Timer
{
public:
    UpcomingModifierDisplay()
    {
        startTimerHz (15);  // shimmer animation (reduced from 30 Hz)
    }

    void setUpcoming(const std::optional<ModifierDescriptor>& desc)
    {
        upcomingDescriptor = desc;
        if (desc.has_value())
        {
            upcomingName = desc->shortName;
            // Prefer structured variant fields for display
            upcomingVariant.clear();
            if (desc->plannedSpeed.has_value())
                upcomingVariant = juce::String(desc->plannedSpeed.value(), 2) + "x";
            else if (desc->plannedStretch.has_value())
                upcomingVariant = juce::String(desc->plannedStretch.value(), 2) + "x";
            else if (desc->plannedSliceDivision.isNotEmpty())
                upcomingVariant = desc->plannedSliceDivision + " slices";
            else if (desc->plannedWet.has_value())
            {
                juce::String fadeLabel;
                if (desc->plannedFxFadeBars.has_value())
                {
                    double fb = desc->plannedFxFadeBars.value();
                    fadeLabel = fb <= 0.0 ? "Instant" : (fb == 1.0 ? "1 bar" : juce::String((int)fb) + " bars");
                }
                upcomingVariant = juce::String("Wet ") + juce::String((int)std::round(desc->plannedWet.value() * 100.0)) + "%" + (fadeLabel.isNotEmpty() ? juce::String(" | Fade: ") + fadeLabel : juce::String());
            }
            else if (desc->plannedDelayDivision.isNotEmpty() || desc->plannedDelayWet.has_value())
            {
                juce::String parts;
                if (!desc->plannedDelayDivisions.isEmpty())
                    parts << desc->plannedDelayDivisions.joinIntoString(",");
                else if (desc->plannedDelayDivision.isNotEmpty())
                    parts << desc->plannedDelayDivision;
                if (desc->plannedDelayWet.has_value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "Wet " << juce::String((int)std::round(desc->plannedDelayWet.value() * 100.0)) << "%";
                }
                if (desc->plannedDelayFeedback.has_value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "Feedback " << (int)std::round(desc->plannedDelayFeedback.value() * 100.0) << "%";
                }
                if (desc->plannedFxFadeBars.has_value())
                {
                    auto bars = desc->plannedFxFadeBars.value();
                    juce::String fadeLabel = bars <= 0.0 ? "Instant" : (bars == 1.0 ? "1 bar" : juce::String((int)bars) + " bars");
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "Fade: " << fadeLabel;
                }
                if (desc->plannedDelayPingPong.has_value() && desc->plannedDelayPingPong.value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "Ping-Pong";
                }
                if (desc->plannedWowFlutter.has_value() && desc->plannedWowFlutter.value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "Wow/Flutter";
                }
                upcomingVariant = parts;
            }
            else if (desc->plannedChorusMix.has_value())
            {
                juce::String parts;
                parts << "Mix " << (int)std::round(desc->plannedChorusMix.value() * 100.0) << "%";
                if (desc->plannedChorusDepth.has_value())
                    parts << " | Depth " << juce::String(desc->plannedChorusDepth.value(), 2);
                if (desc->plannedChorusRateHz.has_value())
                    parts << " | Rate " << juce::String(desc->plannedChorusRateHz.value(), 1) << " Hz";
                upcomingVariant = parts;
            }
            else if (desc->plannedPanMix.has_value())
            {
                juce::String parts;
                parts << "Mix " << (int)std::round(desc->plannedPanMix.value() * 100.0) << "%";
                if (desc->plannedPanDepth.has_value())
                    parts << " | Depth " << juce::String(desc->plannedPanDepth.value(), 2);
                upcomingVariant = parts;
            }
            else if (desc->plannedPingPongDivision.has_value())
            {
                double div = desc->plannedPingPongDivision.value();
                if (div >= 1.0)       upcomingVariant = "Whole note";
                else if (div >= 0.5)  upcomingVariant = "Half note";
                else if (div >= 0.25) upcomingVariant = "Quarter note";
                else if (div >= 0.125) upcomingVariant = "1/8 note";
                else                   upcomingVariant = "1/16 note";
            }
            else if (desc->plannedBurstBars.has_value())
            {
                int bars = desc->plannedBurstBars.value();
                upcomingVariant = juce::String(bars) + (bars == 1 ? " bar" : " bars");
            }
            else if (desc->plannedImmediateJump.has_value())
            {
                juce::String parts;
                if (desc->plannedFxFadeBars.has_value())
                    parts << juce::String((int)desc->plannedFxFadeBars.value()) << " bars | ";
                parts << (desc->plannedImmediateJump.value() ? "Jump to target then decay" : "Ramp up then ramp down");
                upcomingVariant = parts;
            }
            else if (desc->plannedGrainMix.has_value())
            {
                juce::String parts;
                parts << "Mix " << (int)std::round(desc->plannedGrainMix.value() * 100.0) << "%";
                if (desc->plannedGrainDensityHz.has_value())
                    parts << " | " << juce::String(desc->plannedGrainDensityHz.value(), 0) << " g/s";
                if (desc->plannedGrainSizeMs.has_value())
                    parts << " | " << juce::String(desc->plannedGrainSizeMs.value(), 0) << "ms";
                if (desc->plannedGrainPitchSpread.has_value())
                {
                    double sp = desc->plannedGrainPitchSpread.value();
                    parts << " | " << (sp <= 0.0 ? juce::String("unison") : (juce::String(juce::CharPointer_UTF8("\xc2\xb1")) + juce::String((int)(sp / 12.0)) + " oct"));
                }
                if (desc->plannedFxFadeBars.has_value())
                {
                    auto bars = desc->plannedFxFadeBars.value();
                    parts << " | " << (bars <= 0.0 ? juce::String("instant") : juce::String((int)bars) + " bars");
                }
                upcomingVariant = parts;
            }

        }
        else
        {
            upcomingName = "--";
            upcomingVariant.clear();
        }
        repaint();
    }

    void setCountdown(double seconds, double bars, double progress01)
    {
        secondsRemaining = seconds;
        barsRemaining = bars;
        progress = progress01;
        repaint();
    }

    void setSuppressed(bool s)
    {
        suppressed = s;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const auto glyphPalette = ControlSurfacePalette::fromTheme (palette);
        auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        // A flat instrument tile: separation comes from rules, not shadows.
        g.setColour (glyphPalette.raisedTile);
        g.fillRect (bounds);
        g.setColour (glyphPalette.ink.withAlpha (0.72f));
        g.drawRect (bounds, 1.0f);

        auto content = bounds.reduced (10.0f, 8.0f);
        const float metaWidth = juce::jlimit (90.0f,
                                              juce::jmax (90.0f, content.getWidth() * 0.48f),
                                              content.getWidth() * 0.38f);
        auto meta = content.removeFromLeft (metaWidth);
        auto glyph = content.reduced (6.0f, 0.0f);

        // The vertical rule makes the cell read as a modular control board.
        g.setColour (glyphPalette.ink.withAlpha (0.55f));
        g.drawVerticalLine (juce::roundToInt (meta.getRight() + 3.0f),
                            bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);

        auto& fonts = ThemeFonts::getInstance();
        auto nextRow = meta.removeFromTop (18.0f);
        g.setColour (suppressed ? glyphPalette.safetyYellow : glyphPalette.vermilion);
        g.setFont (fonts.monoBoldFont (12.0f));
        g.drawText (suppressed ? "PAUSED" : "NEXT", nextRow, juce::Justification::centredLeft);

        meta.removeFromTop (2.0f);
        auto nameRow = meta.removeFromTop (juce::jmin (28.0f, meta.getHeight() * 0.42f));
        g.setColour (glyphPalette.ink);
        g.setFont (fonts.modifierNameFont (20.0f));
        g.drawFittedText (upcomingName.toUpperCase(), nameRow.toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);

        if (upcomingVariant.isNotEmpty())
        {
            auto variantRow = meta.removeFromTop (juce::jmin (28.0f, meta.getHeight() * 0.55f));
            g.setColour (glyphPalette.mutedInk);
            g.setFont (fonts.monoFont (10.5f));
            g.drawFittedText (upcomingVariant.toUpperCase(), variantRow.toNearestInt(),
                              juce::Justification::centredLeft, 2, 0.75f);
        }

        juce::String countdown;
        if (suppressed)
            countdown = "MODIFIERS HELD";
        else if (secondsRemaining > 0.0 || barsRemaining > 0.0)
            countdown = juce::String (barsRemaining, barsRemaining < 1.0 ? 2 : 1)
                      + " BAR  /  " + juce::String (secondsRemaining, 1) + " S";

        if (countdown.isNotEmpty())
        {
            g.setColour (suppressed ? glyphPalette.safetyYellow : glyphPalette.mutedInk);
            g.setFont (fonts.monoFont (10.5f));
            g.drawText (countdown, meta.removeFromBottom (16.0f), juce::Justification::centredLeft);
        }

        if (upcomingDescriptor.has_value())
        {
            ModifierGlyphState glyphState;
            glyphState.descriptor = *upcomingDescriptor;
            glyphState.phase01 = glyphPhase;
            glyphState.emphasis01 = suppressed ? 0.46f : 1.0f;
            glyphState.compact = glyph.getWidth() < 110.0f;
            glyphState.reducedMotion = ! ThemeEngine::getInstance().getAnimationConfig().enabled;
            ModifierGlyphRenderer::draw (g, glyph, glyphState, glyphPalette);
        }

        // Stable brand rule plus a truthful coarse countdown fill.
        auto progressTrack = bounds.removeFromBottom (3.0f);
        g.setColour (glyphPalette.ink.withAlpha (0.28f));
        g.fillRect (progressTrack.withHeight (1.0f));
        if (progress > 0.0)
        {
            g.setColour (suppressed ? glyphPalette.safetyYellow : glyphPalette.vermilion);
            g.fillRect (progressTrack.withWidth (progressTrack.getWidth()
                                                * juce::jlimit (0.0f, 1.0f, static_cast<float> (progress))));
        }
    }

private:
    void timerCallback() override
    {
        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
        if (anim.enabled && upcomingDescriptor.has_value() && ! suppressed)
        {
            glyphPhase += (1.0f / 15.0f) * anim.animationSpeed * 0.45f;
            if (glyphPhase > 1.0f) glyphPhase -= 1.0f;
            repaint();
        }
    }

    std::optional<ModifierDescriptor> upcomingDescriptor;
    juce::String upcomingName { "--" };
    juce::String upcomingVariant;
    double secondsRemaining = 0.0;
    double barsRemaining = 0.0;
    double progress = 0.0; // 0..1
    bool suppressed = false;
    float glyphPhase = 0.0f;
};
