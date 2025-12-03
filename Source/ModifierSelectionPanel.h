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
    addVariantToggle(ModifierType::BufferReverbOn, "Reverb 25%", "0.25");
    addVariantToggle(ModifierType::BufferReverbOn, "Reverb 50%", "0.50");
    addVariantToggle(ModifierType::BufferReverbOn, "Reverb 75%", "0.75");
    addVariantToggle(ModifierType::BufferReverbOn, "Reverb 100%", "1.00");
    addToggle(ModifierType::BufferReverbOff, "Reverb Off");
        addToggle(ModifierType::BufferDelayOn, "Delay On");
        // Delay division + wet multi-select group (allow simultaneous selection creating combined variant)
    makeDelayDivisionToggle("Delay 1/4", "1/4");
    makeDelayDivisionToggle("Delay 1/8", "1/8");
    makeDelayDivisionToggle("Delay 1/8D", "1/8D");
    makeDelayDivisionToggle("Delay 1/8T", "1/8T");
        makeDelayWetToggle("Delay Wet 25%", "0.25");
        makeDelayWetToggle("Delay Wet 50%", "0.50");
        makeDelayWetToggle("Delay Wet 75%", "0.75");
        makeDelayWetToggle("Delay Wet 100%", "1.00");
    makeDelayFeedbackToggle("Delay FB 25%", "0.25");
    makeDelayFeedbackToggle("Delay FB 50%", "0.50");
    makeDelayFeedbackToggle("Delay FB 75%", "0.75");
    makeDelayFeedbackToggle("Delay FB 100%", "1.00");
    addToggle(ModifierType::BufferDelayOff, "Delay Off");
    addToggle(ModifierType::BufferDelayDubBurst, "Delay Dub Burst");
    addToggle(ModifierType::BufferLowPassOn, "LPF On");
    addToggle(ModifierType::BufferLowPassOff, "LPF Off");
    addToggle(ModifierType::BufferHighPassOn, "HPF On");
    addToggle(ModifierType::BufferHighPassOff, "HPF Off");
    addToggle(ModifierType::BufferDelayPingPongOn, "Delay PingPong On");
    addToggle(ModifierType::BufferDelayPingPongOff, "Delay PingPong Off");
    addToggle(ModifierType::BufferDelayWowFlutterOn, "Wow/Flutter On");
    addToggle(ModifierType::BufferDelayWowFlutterOff, "Wow/Flutter Off");
    addToggle(ModifierType::BufferTremolo, "Tremolo On");
    addToggle(ModifierType::BufferTremoloOff, "Tremolo Off");
    addToggle(ModifierType::BufferDuckingOn, "Ducking On");
    addToggle(ModifierType::BufferDuckingOff, "Ducking Off");
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

    // Delay division toggle that coexists with delay wet toggles
    void makeDelayDivisionToggle(const juce::String& label, const juce::String& division)
    {
        auto* t = new juce::ToggleButton(label);
        toggles.add(t);
        delayDivisionToggles.add(t);
        t->onClick = [this, t, division]
        {
            if (t->getToggleState())
            {
                selectedDelayDivisions.addIfNotAlreadyThere(division);
            }
            else
            {
                selectedDelayDivisions.removeString(division);
            }
            dispatchCombinedDelayVariant();
        };
        addAndMakeVisible(t);
    }

    void makeDelayWetToggle(const juce::String& label, const juce::String& wetStr)
    {
        auto* t = new juce::ToggleButton(label);
        toggles.add(t);
        delayWetToggles.add(t);
        t->onClick = [this, t, wetStr]
        {
            if (t->getToggleState())
                currentDelayWet = wetStr;
            else if (currentDelayWet == wetStr)
                currentDelayWet.clear();
            // Single selection among wet toggles
            for (auto* other : delayWetToggles)
                if (other != t && other->getToggleState()) other->setToggleState(false, juce::dontSendNotification);
            dispatchCombinedDelayVariant();
        };
        addAndMakeVisible(t);
    }

    void dispatchCombinedDelayVariant()
    {
        if (!onForceVariant) return;
        juce::String divisionsPart;
        for (int i = 0; i < selectedDelayDivisions.size(); ++i)
        {
            divisionsPart << (i?",":"") << selectedDelayDivisions[i];
        }
        juce::String wetPart = currentDelayWet;
        juce::String feedbackPart = currentDelayFeedback;
        juce::String combined;
        if (divisionsPart.isNotEmpty()) combined << divisionsPart;
        if (wetPart.isNotEmpty())
        {
            if (combined.isNotEmpty()) combined << "|";
            combined << wetPart;
        }
        if (feedbackPart.isNotEmpty())
        {
            if (combined.isNotEmpty()) combined << "|";
            combined << "fb:" << feedbackPart;
        }
        if (combined.isNotEmpty())
            onForceVariant(ModifierType::BufferDelayOn, combined);
    }

    juce::Array<juce::ToggleButton*> delayDivisionToggles;
    juce::Array<juce::ToggleButton*> delayWetToggles;
    juce::Array<juce::ToggleButton*> delayFeedbackToggles;
    juce::StringArray selectedDelayDivisions;
    juce::String currentDelayWet;
    juce::String currentDelayFeedback;
    void makeDelayFeedbackToggle(const juce::String& label, const juce::String& fbStr)
    {
        auto* t = new juce::ToggleButton(label);
        toggles.add(t);
        delayFeedbackToggles.add(t);
        t->onClick = [this, t, fbStr]
        {
            if (t->getToggleState()) currentDelayFeedback = fbStr; else if (currentDelayFeedback == fbStr) currentDelayFeedback.clear();
            for (auto* other : delayFeedbackToggles)
                if (other != t && other->getToggleState()) other->setToggleState(false, juce::dontSendNotification);
            dispatchCombinedDelayVariant();
        };
        addAndMakeVisible(t);
    }
};
