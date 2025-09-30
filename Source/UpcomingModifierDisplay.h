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

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.2f));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("Next: " + upcomingName, getLocalBounds().removeFromTop(getHeight()/2), juce::Justification::centredLeft);
        g.setFont(juce::Font(12.0f));
        g.setColour(juce::Colours::lightgrey);
        g.drawText(upcomingDescription, getLocalBounds().removeFromTop(getHeight()), juce::Justification::centredLeft);
    }

private:
    juce::String upcomingName { "--" };
    juce::String upcomingDescription;
};
