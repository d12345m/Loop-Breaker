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
#include "ModifierRegistry.h"
#include "ModifierScheduler.h"
#include "ModifierVariantFormatter.h"

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
            upcomingVariant = ModifierVariantFormatter::full (*desc);

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

    void setPlannedQueue (const std::vector<PlannedModifier>& queue)
    {
        plannedQueue = queue;
        if (! plannedQueue.empty())
            setUpcoming (plannedQueue.front().descriptor);
        else
            setUpcoming (std::nullopt);
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
        const auto fullBounds = bounds;

        // A flat instrument tile: separation comes from rules, not shadows.
        g.setColour (glyphPalette.raisedTile);
        g.fillRect (bounds);
        g.setColour (glyphPalette.ink.withAlpha (0.72f));
        g.drawRect (bounds, 1.0f);

        auto nextBounds = bounds;
        juce::Rectangle<float> queueBounds;
        if (plannedQueue.size() > 1 && bounds.getWidth() >= 300.0f)
        {
            const float queueWidth = juce::jlimit (130.0f, 260.0f, bounds.getWidth() * 0.44f);
            queueBounds = nextBounds.removeFromRight (queueWidth);
            g.setColour (glyphPalette.ink.withAlpha (0.72f));
            g.drawVerticalLine (juce::roundToInt (queueBounds.getX()),
                                bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }

        auto content = nextBounds.reduced (10.0f, 8.0f);
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
        if (! plannedQueue.empty())
            drawTargetPips (g, nextRow.withTrimmedLeft (44.0f), plannedQueue.front().targets,
                            glyphPalette, true);

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
        if (! queueBounds.isEmpty())
        {
            const float cellWidth = queueBounds.getWidth() * 0.5f;
            for (int i = 0; i < 2; ++i)
            {
                auto cell = queueBounds.withX (queueBounds.getX() + cellWidth * static_cast<float> (i))
                                       .withWidth (cellWidth);
                if (i > 0)
                {
                    g.setColour (glyphPalette.ink.withAlpha (0.45f));
                    g.drawVerticalLine (juce::roundToInt (cell.getX()),
                                        cell.getY() + 1.0f, cell.getBottom() - 1.0f);
                }

                const size_t queueIndex = static_cast<size_t> (i + 1);
                drawQueueCell (g, cell, queueIndex < plannedQueue.size()
                                            ? &plannedQueue[queueIndex] : nullptr,
                               i + 1, glyphPalette);
            }
        }

        auto progressTrack = fullBounds.withTrimmedTop (fullBounds.getHeight() - 3.0f);
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
    static juce::Colour targetColour (int target, const ControlSurfacePalette& palette)
    {
        switch (target % 5)
        {
            case 0:  return palette.vermilion;
            case 1:  return palette.ultramarine;
            case 2:  return palette.signalGreen;
            case 3:  return palette.violet;
            default: return palette.safetyYellow;
        }
    }

    static void drawTargetPips (juce::Graphics& g, juce::Rectangle<float> area,
                                const juce::Array<int>& targets,
                                const ControlSurfacePalette& palette, bool alignRight)
    {
        if (targets.isEmpty())
            return;

        constexpr float diameter = 5.0f;
        constexpr float gap = 3.0f;
        const float width = targets.size() * diameter + juce::jmax (0, targets.size() - 1) * gap;
        float x = alignRight ? area.getRight() - width : area.getX();
        const float y = area.getCentreY() - diameter * 0.5f;
        for (int target : targets)
        {
            g.setColour (targetColour (target, palette));
            g.fillEllipse (x, y, diameter, diameter);
            x += diameter + gap;
        }
    }

    static void drawQueueCell (juce::Graphics& g, juce::Rectangle<float> bounds,
                               const PlannedModifier* planned, int position,
                               const ControlSurfacePalette& palette)
    {
        auto content = bounds.reduced (7.0f, 7.0f);
        auto& fonts = ThemeFonts::getInstance();
        auto labelRow = content.removeFromTop (14.0f);
        g.setColour (palette.mutedInk);
        g.setFont (fonts.monoBoldFont (9.0f));
        g.drawText ("QUEUE " + juce::String (position), labelRow,
                    juce::Justification::centredLeft);

        if (planned == nullptr)
        {
            g.drawText ("--", content, juce::Justification::centred);
            return;
        }

        drawTargetPips (g, labelRow.withTrimmedLeft (42.0f), planned->targets, palette, true);

        const auto compactVariant = ModifierVariantFormatter::compact (planned->descriptor);
        if (compactVariant.isNotEmpty())
        {
            auto variantRow = content.removeFromBottom (11.0f);
            g.setColour (palette.mutedInk);
            g.setFont (fonts.monoBoldFont (7.5f));
            g.drawFittedText (compactVariant, variantRow.toNearestInt(),
                              juce::Justification::centred, 1, 0.72f);
        }

        auto nameRow = content.removeFromBottom (16.0f);
        g.setColour (palette.ink);
        g.setFont (fonts.modifierNameFont (11.0f));
        g.drawFittedText (planned->descriptor.shortName.toUpperCase(), nameRow.toNearestInt(),
                          juce::Justification::centred, 2, 0.68f);

        ModifierGlyphState state;
        state.descriptor = planned->descriptor;
        state.phase01 = ModifierRegistry::get (planned->descriptor.type).representativeGlyphPhase01;
        state.emphasis01 = 0.82f;
        state.compact = true;
        state.reducedMotion = true;
        ModifierGlyphRenderer::draw (g, content.reduced (2.0f), state, palette);
    }

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
    std::vector<PlannedModifier> plannedQueue;
    juce::String upcomingName { "--" };
    juce::String upcomingVariant;
    double secondsRemaining = 0.0;
    double barsRemaining = 0.0;
    double progress = 0.0; // 0..1
    bool suppressed = false;
    float glyphPhase = 0.0f;
};
