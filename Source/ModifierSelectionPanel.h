#pragma once
#include <JuceHeader.h>
#include "Modifier.h"
#include "ThemeEngine.h"

// Developer panel listing modifiers with checkboxes. Queue-aware hosts can
// expose a slot selector; legacy hosts continue to force only the front item.
class ModifierSelectionPanel : public juce::Component
{
public:
    using SelectionCallback = std::function<void(ModifierType)>;
    using QueueSelectionCallback = std::function<void(int, ModifierType)>;
    using VariantCallback = std::function<void(ModifierType, const juce::String&)>;

    ModifierSelectionPanel()
    {
        queueSlotLabel.setText ("FORCE SLOT", juce::dontSendNotification);
        queueSlotLabel.setJustificationType (juce::Justification::centredLeft);
        queueSlotLabel.setVisible (false);
        addAndMakeVisible (queueSlotLabel);
        queueSlotBox.addItem ("NEXT", 1);
        queueSlotBox.addItem ("QUEUE 1", 2);
        queueSlotBox.addItem ("QUEUE 2", 3);
        queueSlotBox.setSelectedId (1, juce::dontSendNotification);
        queueSlotBox.setVisible (false);
        addAndMakeVisible (queueSlotBox);

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
                    if (onForceQueueSelection)
                        onForceQueueSelection (queueSlotBox.getSelectedId() - 1, type);
                    else if (onForceSelection)
                        onForceSelection(type);
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
    // PingPong and Wow/Flutter are randomized within Delay variants; no separate toggles.
    addToggle(ModifierType::BufferTremolo, "Tremolo");
    addToggle(ModifierType::BufferChorusOn, "Chorus");
    addToggle(ModifierType::BufferAutoPan, "Auto-Pan");
    addToggle(ModifierType::BufferSHLowPassOn, "S&H Low-Pass");
    addToggle(ModifierType::BufferSHHighPassOn, "S&H High-Pass");
    addToggle(ModifierType::BufferGranularOn, "Granular");
    addToggle(ModifierType::BufferGranularMomentary, "Granular Burst");
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
        if (onForceQueueSelection)
        {
            auto slotRow = area.removeFromTop (h);
            queueSlotLabel.setBounds (slotRow.removeFromLeft (88).reduced (2));
            queueSlotBox.setBounds (slotRow.reduced (2));
        }
        for (auto* t : toggles)
            t->setBounds(area.removeFromTop(h).reduced(2));
    }

    void setForceSelectionCallback(SelectionCallback cb) { onForceSelection = std::move(cb); }
    void setForceQueueSelectionCallback (QueueSelectionCallback cb)
    {
        onForceQueueSelection = std::move (cb);
        const bool visible = static_cast<bool> (onForceQueueSelection);
        queueSlotLabel.setVisible (visible);
        queueSlotBox.setVisible (visible);
        resized();
    }
    void setForceVariantCallback(VariantCallback cb) { onForceVariant = std::move(cb); }

private:
    // Parent Component owns children added via addAndMakeVisible; store non-owning pointers to avoid double deletion
    juce::Array<juce::ToggleButton*> toggles;
    juce::Label queueSlotLabel;
    juce::ComboBox queueSlotBox;
    SelectionCallback onForceSelection;
    QueueSelectionCallback onForceQueueSelection;
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
