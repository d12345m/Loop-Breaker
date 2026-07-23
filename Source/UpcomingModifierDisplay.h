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
#include "UiGridLayout.h"

#include <cmath>

class UpcomingModifierDisplay : public juce::Component,
                                private juce::Timer
{
public:
    UpcomingModifierDisplay()
    {
        startTimerHz (15);  // shimmer animation (reduced from 30 Hz)
    }

    void setPortraitLayout (bool shouldUsePortraitLayout)
    {
        if (portraitLayout == shouldUsePortraitLayout)
            return;

        portraitLayout = shouldUsePortraitLayout;
        resetNameMarquee();
        repaint();
    }

    void setUiScale (float newScale)
    {
        newScale = juce::jlimit (0.75f, 2.0f, newScale);
        if (std::abs (uiScale - newScale) < 0.001f)
            return;

        uiScale = newScale;
        resetNameMarquee();
        repaint();
    }

    void setUpcoming(const std::optional<ModifierDescriptor>& desc)
    {
        upcomingDescriptor = desc;
        const juce::String newName = desc.has_value() ? desc->shortName : "--";
        if (newName != upcomingName)
            resetNameMarquee();

        upcomingName = newName;
        if (desc.has_value())
        {
            upcomingVariant = ModifierVariantFormatter::full (*desc);

        }
        else
        {
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
        if ((plannedQueue.size() > 1) != (queue.size() > 1))
            resetNameMarquee();

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

    void setTempoBpm (double bpm)
    {
        tempoBpm = juce::jlimit (20.0, 400.0, bpm);
    }

    void resized() override
    {
        resetNameMarquee();
    }

    void paintProgressBar (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        if (bounds.isEmpty())
            return;

        const auto glyphPalette = ControlSurfacePalette::fromTheme (
            ThemeEngine::getInstance().getCurrentPalette());
        if (progress > 0.0)
        {
            g.setColour (suppressed ? glyphPalette.safetyYellow : glyphPalette.vermilion);
            g.fillRect (bounds.withWidth (bounds.getWidth()
                                          * juce::jlimit (0.0f, 1.0f,
                                                         static_cast<float> (progress))));
        }
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

        auto nextBounds = bounds;
        juce::Rectangle<float> queueBounds;
        std::array<juce::Rectangle<int>, 4> portraitCells;
        const bool useFourModuleLayout = portraitLayout
                                      && plannedQueue.size() > 1
                                      && bounds.getWidth() >= 300.0f;
        if (useFourModuleLayout)
        {
            for (int i = 0; i < 4; ++i)
                portraitCells[static_cast<size_t> (i)] =
                    UiGridLayout::equalColumn (getLocalBounds(), i, 4);

            nextBounds = portraitCells[0].getUnion (portraitCells[1]).toFloat();
            queueBounds = portraitCells[2].getUnion (portraitCells[3]).toFloat();
            g.setColour (glyphPalette.ink.withAlpha (0.72f));
            g.drawVerticalLine (portraitCells[2].getX(),
                                bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }
        else if (plannedQueue.size() > 1 && bounds.getWidth() >= 300.0f)
        {
            const float queueWidth = juce::jlimit (130.0f * uiScale,
                                                   260.0f * uiScale,
                                                   bounds.getWidth() * 0.44f);
            queueBounds = nextBounds.removeFromRight (queueWidth);
            g.setColour (glyphPalette.ink.withAlpha (0.72f));
            g.drawVerticalLine (juce::roundToInt (queueBounds.getX()),
                                bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }

        juce::Rectangle<float> meta;
        juce::Rectangle<float> glyphCell;
        if (useFourModuleLayout)
        {
            meta = portraitCells[0].toFloat()
                       .withTrimmedLeft (10.0f * uiScale)
                       .withTrimmedRight (3.0f * uiScale)
                       .reduced (0.0f, 8.0f * uiScale);
            glyphCell = portraitCells[1].toFloat();
            g.setColour (glyphPalette.ink.withAlpha (0.55f));
            g.drawVerticalLine (portraitCells[1].getX(),
                                bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }
        else
        {
            auto content = nextBounds.reduced (10.0f * uiScale, 8.0f * uiScale);
            const float metaWidth = juce::jlimit (
                90.0f * uiScale,
                juce::jmax (90.0f * uiScale, content.getWidth() * 0.48f),
                content.getWidth() * 0.38f);
            meta = content.removeFromLeft (metaWidth);

            // The vertical rule makes the cell read as a modular control board.
            g.setColour (glyphPalette.ink.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (meta.getRight() + 3.0f * uiScale),
                                bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);

            glyphCell = nextBounds.withLeft (meta.getRight() + 3.0f * uiScale);
        }

        const auto glyph = glyphCell.reduced (6.0f * uiScale, 8.0f * uiScale);

        auto& fonts = ThemeFonts::getInstance();
        auto nextRow = meta.removeFromTop (18.0f * uiScale);
        g.setColour (suppressed ? glyphPalette.safetyYellow : glyphPalette.vermilion);
        g.setFont (fonts.monoBoldFont (12.0f * uiScale));
        g.drawText (suppressed ? "PAUSED" : "NEXT", nextRow, juce::Justification::centredLeft);
        if (upcomingDescriptor.has_value())
        {
            // Queue-cell pips are centred in a 14 px label row inset 7 px
            // from the tile top. Use that same vertical geometry here.
            auto pipRow = nextRow.withY (bounds.getY() + 7.0f * uiScale)
                                  .withHeight (14.0f * uiScale);
            drawCategoryPips (g, pipRow.withTrimmedLeft (44.0f * uiScale)
                                       .withTrimmedRight (4.0f * uiScale),
                              upcomingDescriptor->type,
                              glyphPalette, true, uiScale);
        }

        meta.removeFromTop (2.0f * uiScale);
        auto nameRow = meta.removeFromTop (juce::jmin (28.0f * uiScale,
                                                      meta.getHeight() * 0.42f));
        g.setColour (glyphPalette.ink);
        const auto nameFont = fonts.modifierNameFont (20.0f * uiScale);
        const auto displayName = upcomingName.toUpperCase();
        const float nameWidth =
            juce::GlyphArrangement::getStringWidth (nameFont, displayName);
        const float marqueeOverflow = portraitLayout
            ? juce::jmax (0.0f, nameWidth - nameRow.getWidth())
            : 0.0f;
        if (std::abs (marqueeOverflow - nameMarqueeOverflowPx) > 0.5f)
        {
            nameMarqueeOverflowPx = marqueeOverflow;
            nameMarqueeElapsedSeconds = 0.0;
        }

        const bool nameOverflows = marqueeOverflow > 1.0f;
        nameMarqueeActive = nameOverflows && ! suppressed;
        g.setFont (nameFont);

        const auto& animation =
            ThemeEngine::getInstance().getAnimationConfig();
        if (nameMarqueeActive && animation.enabled)
        {
            const double speedMultiplier =
                juce::jmax (0.25, static_cast<double> (animation.animationSpeed));
            const float travel = marqueeOverflow;
            const double pauseSeconds = 0.8 / speedMultiplier;
            const double scrollSeconds = static_cast<double> (travel)
                                       / (28.0 * uiScale * speedMultiplier);
            const double cycleSeconds = 2.0 * (pauseSeconds + scrollSeconds);
            const double cyclePosition = std::fmod (
                nameMarqueeElapsedSeconds, cycleSeconds);

            float offset = 0.0f;
            if (cyclePosition <= pauseSeconds)
            {
                offset = 0.0f;
            }
            else if (cyclePosition <= pauseSeconds + scrollSeconds)
            {
                offset = -travel * static_cast<float> (
                    (cyclePosition - pauseSeconds) / scrollSeconds);
            }
            else if (cyclePosition <= 2.0 * pauseSeconds + scrollSeconds)
            {
                offset = -travel;
            }
            else
            {
                offset = -travel * (1.0f - static_cast<float> (
                    (cyclePosition - 2.0 * pauseSeconds - scrollSeconds)
                    / scrollSeconds));
            }

            juce::Graphics::ScopedSaveState marqueeClip (g);
            g.reduceClipRegion (nameRow.getSmallestIntegerContainer());
            g.drawText (displayName,
                        nameRow.withX (nameRow.getX() + offset)
                               .withWidth (nameWidth + 2.0f * uiScale),
                        juce::Justification::centredLeft, false);
        }
        else
        {
            g.drawFittedText (displayName, nameRow.toNearestInt(),
                              juce::Justification::centredLeft, 1,
                              portraitLayout ? 0.55f : 0.7f);
        }

        if (upcomingVariant.isNotEmpty())
        {
            auto variantRow = meta.removeFromTop (juce::jmin (28.0f * uiScale,
                                                             meta.getHeight() * 0.55f));
            g.setColour (glyphPalette.mutedInk);
            g.setFont (fonts.monoFont (10.5f * uiScale));
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
            g.setFont (fonts.monoFont (10.5f * uiScale));
            g.drawText (countdown, meta.removeFromBottom (16.0f * uiScale),
                        juce::Justification::centredLeft);
        }

        if (upcomingDescriptor.has_value())
        {
            ModifierGlyphState glyphState;
            glyphState.descriptor = *upcomingDescriptor;
            glyphState.phase01 = glyphPhase;
            glyphState.emphasis01 = suppressed ? 0.46f : 1.0f;
            glyphState.compact = glyph.getWidth() < 110.0f * uiScale;
            glyphState.reducedMotion = ! ThemeEngine::getInstance().getAnimationConfig().enabled;
            ModifierGlyphRenderer::draw (g, glyph, glyphState, glyphPalette);
        }

        // Planned queue cells.
        if (! queueBounds.isEmpty())
        {
            const float cellWidth = queueBounds.getWidth() * 0.5f;
            for (int i = 0; i < 2; ++i)
            {
                auto cell = useFourModuleLayout
                    ? portraitCells[static_cast<size_t> (i + 2)].toFloat()
                    : queueBounds.withX (queueBounds.getX()
                                         + cellWidth * static_cast<float> (i))
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
                               i + 1, glyphPalette, uiScale);
            }
        }
    }

private:
    struct CategoryPipStyle
    {
        int count;
        juce::Colour colour;
    };

    static CategoryPipStyle categoryPipStyle (ModifierType type,
                                              const ControlSurfacePalette& palette)
    {
        // Use the registry's UI label rather than ModifierCategory: Special is
        // deliberately a Probability-tab group containing multiple runtime categories.
        const juce::String category = ModifierRegistry::get (type).categoryLabel;
        if (category == "Buffer")
            return { 1, palette.ultramarine };
        if (category == "Channel Effect")
            return { 2, palette.signalGreen };
        if (category == "Special")
            return { 3, palette.vermilion };
        return { 0, palette.mutedInk };
    }

    static void drawCategoryPips (juce::Graphics& g, juce::Rectangle<float> area,
                                  ModifierType type, const ControlSurfacePalette& palette,
                                  bool alignRight, float scale)
    {
        const auto style = categoryPipStyle (type, palette);
        if (style.count == 0)
            return;

        const float diameter = 5.0f * scale;
        const float gap = 3.0f * scale;
        const float width = style.count * diameter + juce::jmax (0, style.count - 1) * gap;
        float x = alignRight ? area.getRight() - width : area.getX();
        const float y = area.getCentreY() - diameter * 0.5f;
        g.setColour (style.colour);
        for (int i = 0; i < style.count; ++i)
        {
            g.fillEllipse (x, y, diameter, diameter);
            x += diameter + gap;
        }
    }

    static void drawQueueCell (juce::Graphics& g, juce::Rectangle<float> bounds,
                               const PlannedModifier* planned, int position,
                               const ControlSurfacePalette& palette, float scale)
    {
        auto content = bounds.reduced (7.0f * scale, 7.0f * scale);
        auto& fonts = ThemeFonts::getInstance();
        auto labelRow = content.removeFromTop (14.0f * scale);
        g.setColour (palette.mutedInk);
        g.setFont (fonts.monoBoldFont (9.0f * scale));
        g.drawText ("QUEUE " + juce::String (position), labelRow,
                    juce::Justification::centredLeft);

        if (planned == nullptr)
        {
            g.drawText ("--", content, juce::Justification::centred);
            return;
        }

        drawCategoryPips (g, labelRow.withTrimmedLeft (42.0f * scale),
                          planned->descriptor.type, palette, true, scale);

        const auto compactVariant = ModifierVariantFormatter::compact (planned->descriptor);
        if (compactVariant.isNotEmpty())
        {
            auto variantRow = content.removeFromBottom (11.0f * scale);
            g.setColour (palette.mutedInk);
            g.setFont (fonts.monoBoldFont (7.5f * scale));
            g.drawFittedText (compactVariant, variantRow.toNearestInt(),
                              juce::Justification::centred, 1, 0.72f);
        }

        auto nameRow = content.removeFromBottom (16.0f * scale);
        g.setColour (palette.ink);
        g.setFont (fonts.modifierNameFont (11.0f * scale));
        g.drawFittedText (planned->descriptor.shortName.toUpperCase(), nameRow.toNearestInt(),
                          juce::Justification::centred, 2, 0.68f);

        ModifierGlyphState state;
        state.descriptor = planned->descriptor;
        state.phase01 = ModifierRegistry::get (planned->descriptor.type).representativeGlyphPhase01;
        state.emphasis01 = 0.82f;
        state.compact = true;
        state.reducedMotion = true;
        ModifierGlyphRenderer::draw (g, content.reduced (2.0f * scale), state, palette);
    }

    void timerCallback() override
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double elapsedSeconds = lastAnimationTickMs > 0.0
            ? juce::jlimit (0.0, 0.25, (nowMs - lastAnimationTickMs) / 1000.0)
            : (1.0 / 15.0);
        lastAnimationTickMs = nowMs;

        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
        bool shouldRepaint = false;
        if (anim.enabled && upcomingDescriptor.has_value() && ! suppressed)
        {
            // One complete glyph phase per 4/4 bar. At 120 BPM this remains
            // close to the former default speed while following host tempo.
            constexpr double beatsPerCycle = 4.0;
            glyphPhase += static_cast<float> ((tempoBpm / (60.0 * beatsPerCycle))
                                              * elapsedSeconds);
            if (glyphPhase > 1.0f) glyphPhase -= 1.0f;
            shouldRepaint = true;
        }

        if (anim.enabled && nameMarqueeActive)
        {
            nameMarqueeElapsedSeconds += elapsedSeconds;
            shouldRepaint = true;
        }

        if (shouldRepaint)
            repaint();
    }

    void resetNameMarquee() noexcept
    {
        nameMarqueeElapsedSeconds = 0.0;
        nameMarqueeOverflowPx = -1.0f;
        nameMarqueeActive = false;
    }

    std::optional<ModifierDescriptor> upcomingDescriptor;
    std::vector<PlannedModifier> plannedQueue;
    juce::String upcomingName { "--" };
    juce::String upcomingVariant;
    double secondsRemaining = 0.0;
    double barsRemaining = 0.0;
    double progress = 0.0; // 0..1
    bool suppressed = false;
    bool portraitLayout = false;
    float glyphPhase = 0.0f;
    double nameMarqueeElapsedSeconds = 0.0;
    float nameMarqueeOverflowPx = -1.0f;
    bool nameMarqueeActive = false;
    double tempoBpm = 120.0;
    double lastAnimationTickMs = 0.0;
    float uiScale = 1.0f;
};
