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
            upcomingDescription = desc->description;
        }
        else
        {
            upcomingName = "--";
            upcomingDescription = "";
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
        g.drawText("Next: " + upcomingName, topHalf, juce::Justification::centredLeft);

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
            juce::Font pausedFont(11.0f, juce::Font::bold);
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
    double secondsRemaining = 0.0;
    double barsRemaining = 0.0;
    double progress = 0.0; // 0..1
    bool suppressed = false;
};
