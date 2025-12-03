#pragma once
#include <JuceHeader.h>
#include "Modifier.h"

// Simple developer panel listing modifiers with checkboxes; checking one forces it as upcoming.
class ModifierSelectionPanel : public juce::Component
{
public:
    using SelectionCallback = std::function<void(ModifierType)>;
    using VariantCallback = std::function<void(ModifierType, const juce::String&)>;

    ModifierSelectionPanel()
    {
        auto addToggle = [&](ModifierType type, const juce::String& label)
        {
            auto* t = new juce::ToggleButton(label);
            toggles.add(t);
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
        addToggle(ModifierType::Speed, "Speed (rand)");
    // Buffer FX (placeholders)
    addToggle(ModifierType::BufferReverbOn, "Reverb On");
    addToggle(ModifierType::BufferDelayOn, "Delay On");
    addToggle(ModifierType::BufferDelayDubBurst, "Delay Dub Burst");
        // Removed granular delay division/wet/feedback toggles to simplify GUI; runtime randomization will choose values.
    addToggle(ModifierType::BufferLowPassOn, "LPF On");
    addToggle(ModifierType::BufferHighPassOn, "HPF On");
    // PingPong and Wow/Flutter are randomized within Delay variants; no separate toggles.
    addToggle(ModifierType::BufferTremolo, "Tremolo On");
    // Ducking is enabled by default; GUI toggle removed.
    // Speed variants removed; Speed is randomized at runtime.
    addToggle(ModifierType::ResetAll, "Reset");
    addToggle(ModifierType::BeatSliceRandom, "Slice (rand)");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        int h = 24;
        for (auto* t : toggles)
            t->setBounds(area.removeFromTop(h).reduced(2));
    }

    void setForceSelectionCallback(SelectionCallback cb) { onForceSelection = std::move(cb); }
    void setForceVariantCallback(VariantCallback cb) { onForceVariant = std::move(cb); }

private:
    // Parent Component owns children added via addAndMakeVisible; store non-owning pointers to avoid double deletion
    juce::Array<juce::ToggleButton*> toggles;
    SelectionCallback onForceSelection;
    VariantCallback onForceVariant;

    void addVariantToggle(ModifierType type, const juce::String& label, const juce::String& variant)
    {
        auto* t = new juce::ToggleButton(label);
        toggles.add(t);
        t->onClick = [this, type, variant, t]
        {
            if (t->getToggleState())
            {
                for (auto* other : toggles)
                    if (other != t) other->setToggleState(false, juce::dontSendNotification);
                if (onForceVariant) onForceVariant(type, variant);
            }
        };
        addAndMakeVisible(t);
    }

    // Removed delay preset helper methods and state
};
