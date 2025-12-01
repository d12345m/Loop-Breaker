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
                upcomingVariant = juce::String("Reverb ") + juce::String((int)std::round(desc->plannedWet.value() * 100.0)) + "%";
            else if (desc->plannedDelayDivision.isNotEmpty())
                upcomingVariant = juce::String("Delay ") + desc->plannedDelayDivision;

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
        g.fillAll(juce::Colours::black.withAlpha(0.2f));
        g.setColour(juce::Colours::white);
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
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(barArea);
        auto fill = barArea.withWidth(int(barArea.getWidth() * progress));
        g.setColour((suppressed ? juce::Colours::orange : juce::Colours::lime).withAlpha(0.6f));
        g.fillRect(fill);
        g.setColour(juce::Colours::white);
        g.drawRect(barArea);
        if (suppressed)
        {
            g.setColour(juce::Colours::orange.withAlpha(0.8f));
            auto pausedFont = juce::Font(juce::FontOptions().withHeight(11.0f));
            pausedFont.setBold(true);
            g.setFont(pausedFont);
            g.drawFittedText("PAUSED", barArea, juce::Justification::centred, 1);
        }

        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        g.setColour(juce::Colours::lightgrey);
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
