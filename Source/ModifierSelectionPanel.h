#pragma once
#include <JuceHeader.h>
#include "Modifier.h"

// Simple developer panel listing modifiers with checkboxes; checking one forces it as upcoming.
class ModifierSelectionPanel : public juce::Component
{
public:
    using SelectionCallback = std::function<void(ModifierType)>;

    ModifierSelectionPanel()
    {
        auto addToggle = [&](ModifierType type, const juce::String& label)
        {
            auto* t = toggles.add(new juce::ToggleButton(label));
            t->onClick = [this, type, t]
            {
                if (t->getToggleState())
                {
                    // Untoggle all others to keep single active selection semantic
                    for (auto* other : toggles)
                        if (other != t) other->setToggleState(false, juce::dontSendNotification);
                    if (onForceSelection) onForceSelection(type);
                }
                else if (onForceSelection)
                {
                    // If user unchecks the active one, we just leave scheduler alone (no revert)
                }
            };
            addAndMakeVisible(t);
        };

        addToggle(ModifierType::Reverse, "Reverse");
        addToggle(ModifierType::Speed, "Speed");
        addToggle(ModifierType::ResetAll, "Reset");
        addToggle(ModifierType::BeatSliceRandom, "Slice");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        int h = 24;
        for (auto* t : toggles)
            t->setBounds(area.removeFromTop(h).reduced(2));
    }

    void setForceSelectionCallback(SelectionCallback cb) { onForceSelection = std::move(cb); }

private:
    juce::OwnedArray<juce::ToggleButton> toggles;
    SelectionCallback onForceSelection;
};
