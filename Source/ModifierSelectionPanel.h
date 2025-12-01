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
        addToggle(ModifierType::Speed, "Speed (rand)");
    // Buffer FX (placeholders)
    addToggle(ModifierType::BufferReverbOn, "Reverb (on)");
    addToggle(ModifierType::BufferDelayOn, "Delay (on)");
    addToggle(ModifierType::BufferLowPassOn, "LPF (on)");
    addToggle(ModifierType::BufferHighPassOn, "HPF (on)");
    addToggle(ModifierType::BufferTremolo, "Tremolo (on)");
        // Speed variants
        addVariantToggle(ModifierType::Speed, "Speed 0.25x", "0.25");
        addVariantToggle(ModifierType::Speed, "Speed 0.50x", "0.5");
        addVariantToggle(ModifierType::Speed, "Speed 1.00x", "1.0");
        addVariantToggle(ModifierType::Speed, "Speed 2.00x", "2.0");
        addToggle(ModifierType::ResetAll, "Reset");
        addToggle(ModifierType::BeatSliceRandom, "Slice (rand)");
        addVariantToggle(ModifierType::BeatSliceRandom, "Slice 1/4", "1/4");
        addVariantToggle(ModifierType::BeatSliceRandom, "Slice 1/8", "1/8");
        addVariantToggle(ModifierType::BeatSliceRandom, "Slice 1/8T", "1/8T");
        addVariantToggle(ModifierType::BeatSliceRandom, "Slice 1/16", "1/16");
        addVariantToggle(ModifierType::BeatSliceRandom, "Slice 1/32", "1/32");
        addVariantToggle(ModifierType::BeatSliceRandom, "Slice 1/64", "1/64");
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
    juce::OwnedArray<juce::ToggleButton> toggles;
    SelectionCallback onForceSelection;
    VariantCallback onForceVariant;

    void addVariantToggle(ModifierType type, const juce::String& label, const juce::String& variant)
    {
        auto* t = toggles.add(new juce::ToggleButton(label));
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
};
