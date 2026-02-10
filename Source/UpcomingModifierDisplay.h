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
#include "Theme.h"

class UpcomingModifierDisplay : public juce::Component
{
public:
    void setUpcoming(const std::optional<ModifierDescriptor>& desc)
    {
        if (desc.has_value())
        {
            upcomingName = desc->shortName;
            // Prefer structured variant fields for display
            upcomingVariant.clear();
            if (desc->plannedSpeed.has_value())
                upcomingVariant = juce::String(desc->plannedSpeed.value(), 2) + "x";
            else if (desc->plannedSliceDivision.isNotEmpty())
                upcomingVariant = desc->plannedSliceDivision;
            else if (desc->plannedWet.has_value())
            {
                juce::String fadeLabel;
                if (desc->plannedFxFadeBars.has_value())
                {
                    double fb = desc->plannedFxFadeBars.value();
                    fadeLabel = fb <= 0.0 ? "instant" : (fb == 1.0 ? "1 bar" : juce::String((int)fb) + " bars");
                }
                upcomingVariant = juce::String("Reverb ") + juce::String((int)std::round(desc->plannedWet.value() * 100.0)) + "%" + (fadeLabel.isNotEmpty() ? juce::String(" | ") + fadeLabel : juce::String());
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
                    parts << juce::String((int)std::round(desc->plannedDelayWet.value() * 100.0)) << "%";
                }
                if (desc->plannedDelayFeedback.has_value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "FB " << (int)std::round(desc->plannedDelayFeedback.value() * 100.0) << "%";
                }
                if (desc->plannedFxFadeBars.has_value())
                {
                    auto bars = desc->plannedFxFadeBars.value();
                    juce::String fadeLabel = bars <= 0.0 ? "instant" : (bars == 1.0 ? "1 bar" : juce::String((int)bars) + " bars");
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << fadeLabel;
                }
                if (desc->plannedDelayPingPong.has_value() && desc->plannedDelayPingPong.value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "PP";
                }
                if (desc->plannedWowFlutter.has_value() && desc->plannedWowFlutter.value())
                {
                    if (parts.isNotEmpty()) parts << " | ";
                    parts << "WowFlutter";
                }
                upcomingVariant = juce::String("Delay ") + parts;
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
        auto bounds = getLocalBounds().toFloat();
        g.setColour(Theme::panel());
        g.fillRoundedRectangle(bounds.reduced(1.0f), 10.0f);
        g.setColour(Theme::border());
        g.drawRoundedRectangle(bounds.reduced(1.0f), 10.0f, 1.0f);

        g.setColour(Theme::text());
        {
            auto f = juce::Font(juce::FontOptions().withHeight(18.0f));
            f.setBold(true);
            g.setFont(f);
        }
        auto topHalf = getLocalBounds().removeFromTop(getHeight()/2);
    juce::String nextLine = "Next: " + upcomingName;
    if (upcomingVariant.isNotEmpty()) nextLine += "  ->  " + upcomingVariant;
        g.drawText(nextLine, topHalf, juce::Justification::centredLeft);

        // Simple horizontal progress bar at the right side of top half
        auto barArea = topHalf.removeFromRight(160).reduced(4);
        g.setColour(Theme::border().withAlpha(0.8f));
        g.fillRoundedRectangle(barArea.toFloat(), 4.0f);
        auto fill = barArea.withWidth(int(barArea.getWidth() * progress));
        g.setColour((suppressed ? Theme::warn() : Theme::accent2()).withAlpha(0.75f));
        g.fillRoundedRectangle(fill.toFloat(), 4.0f);
        g.setColour(Theme::borderStrong());
        g.drawRoundedRectangle(barArea.toFloat(), 4.0f, 1.0f);
        if (suppressed)
        {
            g.setColour(Theme::warn());
            auto pausedFont = juce::Font(juce::FontOptions().withHeight(11.0f));
            pausedFont.setBold(true);
            g.setFont(pausedFont);
            g.drawFittedText("PAUSED", barArea, juce::Justification::centred, 1);
        }

        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        g.setColour(Theme::textSubtle());
        auto bottom = getLocalBounds();
        juce::String timeStr = juce::String(secondsRemaining, 1) + "s | " + juce::String(barsRemaining, 2) + " bars";
        juce::String suffix = suppressed ? " [modifiers off]" : "";
        g.drawFittedText(upcomingDescription + "  (" + timeStr + ")" + suffix, bottom, juce::Justification::centredLeft, 1);
    }

private:
    juce::String upcomingName { "--" };
    juce::String upcomingDescription;
    juce::String upcomingVariant;
    double secondsRemaining = 0.0;
    double barsRemaining = 0.0;
    double progress = 0.0; // 0..1
    bool suppressed = false;
};
