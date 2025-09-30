/*
 ============================================================================== 
   PadGridComponent.h
   --------------------------------------------------------------------------
   Simple 2x4 grid of pad toggle buttons representing the 8 audio buffers.
   Provides selection state for scheduling modifiers (user targeted buffers).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class PadGridComponent : public juce::Component
{
public:
    PadGridComponent()
    {
        for (int i = 0; i < numPads; ++i)
        {
            auto* btn = padButtons.add(new juce::ToggleButton("Pad " + juce::String(i + 1)));
            btn->setClickingTogglesState(true);
            addAndMakeVisible(btn);
        }
    }

    juce::Array<int> getSelectedPadIndices() const
    {
        juce::Array<int> indices;
        for (int i = 0; i < padButtons.size(); ++i)
            if (padButtons[i]->getToggleState())
                indices.add(i);
        return indices;
    }

    void clearSelections()
    {
        for (auto* b : padButtons) b->setToggleState(false, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        int rows = 2;
        int cols = 4;
        int padW = area.getWidth() / cols;
        int padH = area.getHeight() / rows;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                int idx = r * cols + c;
                if (idx < padButtons.size())
                    padButtons[idx]->setBounds(area.getX() + c * padW, area.getY() + r * padH, padW - 4, padH - 4);
            }
    }

private:
    static constexpr int numPads = 8;
    juce::OwnedArray<juce::ToggleButton> padButtons;
};
