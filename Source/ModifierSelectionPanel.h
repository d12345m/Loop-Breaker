#pragma once
#include <JuceHeader.h>
#include "Modifier.h"
#include "ThemeEngine.h"

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
            t->setColour(juce::ToggleButton::textColourId, Theme::text());
            t->setColour(juce::ToggleButton::tickColourId, Theme::accent());
            t->setColour(juce::ToggleButton::tickDisabledColourId, Theme::borderStrong());
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
        addToggle(ModifierType::Speed, "Speed");
        addToggle(ModifierType::Stretch, "Stretch");
        addToggle(ModifierType::PitchUpOctave, "Pitch Up Octave");
        addToggle(ModifierType::PitchDownOctave, "Pitch Down Octave");
        addToggle(ModifierType::QuarterNoteBurst, "Quarter-Note Burst");
        addToggle(ModifierType::PingPong, "Ping Pong");
    // Buffer FX (placeholders)
    addToggle(ModifierType::BufferReverbOn, "Reverb");
    addToggle(ModifierType::BufferDelayOn, "Delay");
    addToggle(ModifierType::BufferDelayDubBurst, "Delay Dub Burst");
        // Removed granular delay division/wet/feedback toggles to simplify GUI; runtime randomization will choose values.
    addToggle(ModifierType::BufferLowPassOn, "Low-Pass Filter");
    addToggle(ModifierType::BufferHighPassOn, "High-Pass Filter");
    // Global (master) filters apply to all tracks under the hood
    addToggle(ModifierType::MasterLowPassOn, "Master Low-Pass");
    addToggle(ModifierType::MasterHighPassOn, "Master High-Pass");
    // PingPong and Wow/Flutter are randomized within Delay variants; no separate toggles.
    addToggle(ModifierType::BufferTremolo, "Tremolo");
    addToggle(ModifierType::BufferChorusOn, "Chorus");
    addToggle(ModifierType::BufferAutoPan, "Auto-Pan");
    addToggle(ModifierType::BufferSHLowPassOn, "S&H Low-Pass");
    addToggle(ModifierType::BufferSHHighPassOn, "S&H High-Pass");
    addToggle(ModifierType::BufferVolumeRampDown, "Volume Ramp Down");
    // Ducking is enabled by default; GUI toggle removed.
    // Speed variants removed; Speed is randomized at runtime.
    addToggle(ModifierType::ResetAll, "Reset");
    addToggle(ModifierType::BeatSliceRandom, "Beat Slice");
    addToggle(ModifierType::ArpSlice, "Arp Slice");
    addToggle(ModifierType::SliceRepeater, "Slice Repeater");
    addToggle(ModifierType::SwapModifierStack, "Swap Stack");
    addToggle(ModifierType::SwitchPart, "Switch Part");
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
