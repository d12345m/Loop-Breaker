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
#include "Animator.h"

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

            // Show base description without any appended arrow details (UI will show variant separately)
            auto d = desc->description;
            int arrow = d.lastIndexOf("->");
            upcomingDescription = (arrow >= 0 ? d.substring(0, arrow).trim() : d);
        }
        else
        {
            upcomingName = "--";
            upcomingDescription = "";
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
        auto bounds = getLocalBounds().toFloat();
        const float cr = palette.borderRadius;

        // ── Panel background ──
        g.setColour(palette.panel);
        g.fillRoundedRectangle(bounds, cr);

        // Top-edge divider (thin accent line)
        g.setColour(palette.accent1.withAlpha(0.35f));
        g.fillRect(bounds.getX() + 4.0f, bounds.getY(), bounds.getWidth() - 8.0f, 1.0f);

        // Border
        g.setColour(palette.border);
        g.drawRoundedRectangle(bounds, cr, 1.0f);

        const float pad = 8.0f;
        auto content = bounds.reduced(pad, 4.0f);

        // ── Bottom row: description + countdown ──
        auto bottomRow = content.removeFromBottom(22.0f);

        // ── Top row: "NEXT" badge + modifier name + variant (truly centered) ──
        auto topRow = content;

        // "NEXT" badge
        {
            auto badgeRect = juce::Rectangle<float>(topRow.getX(), topRow.getY() + 2.0f, 52.0f, 22.0f);
            g.setColour(palette.accent1.withAlpha(0.18f));
            g.fillRoundedRectangle(badgeRect, 3.0f);
            g.setColour(palette.accent1);
            g.setFont(ThemeFonts::getInstance().monoBoldFont(13.0f));
            g.drawText("NEXT", badgeRect, juce::Justification::centred);
        }

        // Modifier name + variant, centered in the full width
        {
            auto textArea = topRow;
            auto nameFont = ThemeFonts::getInstance().modifierNameFont(26.0f);
            auto variantFont = ThemeFonts::getInstance().monoFont(16.0f);

            // Measure name width
            juce::GlyphArrangement nameGlyphs;
            nameGlyphs.addLineOfText(nameFont, upcomingName, 0.0f, 0.0f);
            float nameWidth = nameGlyphs.getBoundingBox(0, -1, false).getWidth();

            // Measure variant width
            float variantWidth = 0.0f;
            const float gap = upcomingVariant.isNotEmpty() ? 10.0f : 0.0f;
            if (upcomingVariant.isNotEmpty())
            {
                juce::GlyphArrangement varGlyphs;
                varGlyphs.addLineOfText(variantFont, upcomingVariant, 0.0f, 0.0f);
                variantWidth = varGlyphs.getBoundingBox(0, -1, false).getWidth();
            }

            float totalWidth = nameWidth + gap + variantWidth;
            float startX = textArea.getCentreX() - totalWidth * 0.5f;

            // Draw name
            auto nameRect = textArea.withX(startX).withWidth(nameWidth);
            g.setColour(palette.accent1);
            g.setFont(nameFont);
            g.drawText(upcomingName, nameRect, juce::Justification::centredLeft, true);

            // Draw variant
            if (upcomingVariant.isNotEmpty())
            {
                auto varRect = textArea.withX(startX + nameWidth + gap).withWidth(variantWidth);
                g.setColour(palette.textSecondary);
                g.setFont(variantFont);
                g.drawText(upcomingVariant, varRect, juce::Justification::centredLeft, true);
            }
        }

        // ── Middle row: description (centered) + countdown (right) ──
        {
            // Description
            g.setColour(palette.textSecondary);
            g.setFont(ThemeFonts::getInstance().bodyFont(16.0f));
            auto descArea = bottomRow.withTrimmedRight(160.0f);
            g.drawFittedText(upcomingDescription, descArea.toNearestInt(), juce::Justification::centred, 1);

            // Countdown (monospaced, right-aligned)
            auto countArea = bottomRow.removeFromRight(160.0f);
            g.setColour(palette.textSecondary);
            g.setFont(ThemeFonts::getInstance().monoFont(16.0f));
            juce::String timeStr = juce::String(secondsRemaining, 1) + "s  |  "
                                 + juce::String(barsRemaining, 2) + " bars";
            juce::String suffix = suppressed ? "  [OFF]" : "";
            g.drawFittedText(timeStr + suffix, countArea.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

private:
    void timerCallback() override
    {
        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
        if (anim.enabled && anim.progressBarShimmer && progress > 0.01 && !suppressed)
        {
            shimmerPhase += (1.0f / 15.0f) * anim.animationSpeed * 0.5f;  // ~2s cycle
            if (shimmerPhase > 1.0f) shimmerPhase -= 1.0f;
            repaint();
        }
    }

    juce::String upcomingName { "--" };
    juce::String upcomingDescription;
    juce::String upcomingVariant;
    double secondsRemaining = 0.0;
    double barsRemaining = 0.0;
    double progress = 0.0; // 0..1
    bool suppressed = false;
    float shimmerPhase = 0.0f;
};
