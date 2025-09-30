/*
 ============================================================================== 
   ModifierScheduler.h
   --------------------------------------------------------------------------
   Schedules & selects modifiers at musical bar boundaries.
   Current implementation is a stub emitting only descriptor selection events.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Modifier.h"
#include "SessionSettings.h"

class ModifierSchedulerListener
{
public:
    virtual ~ModifierSchedulerListener() = default;
    virtual void upcomingModifierChanged(const ModifierDescriptor& /*desc*/) {}
    virtual void modifierTriggered(const ModifierDescriptor& /*desc*/, const juce::Array<int>& /*targets*/) {}
};

class ModifierScheduler : private juce::Timer
{
public:
    explicit ModifierScheduler(const SessionSettings& settingsRef);
    ~ModifierScheduler() override;

    void start();
    void stop();
    bool isRunning() const { return running; }

    // Force immediate selection of next upcoming modifier (does not trigger it)
    void selectNextModifier();

    // Getter for UI
    std::optional<ModifierDescriptor> getUpcomingModifier() const { return upcoming; }

    // Listener management
    void addListener(ModifierSchedulerListener* l) { listeners.addIfNotAlreadyThere(l); }
    void removeListener(ModifierSchedulerListener* l) { listeners.removeFirstMatchingValue(l); }

    // Called externally each audio block (future) to accumulate musical time.
    void updateTime(double secondsElapsed);

    // Provide externally selected buffer indices (user pad selections)
    void setUserSelectedBuffers(const juce::Array<int>& indices);

private:
    const SessionSettings& settings; // reference to live settings
    bool running = false;
    double accumulatedSeconds = 0.0;
    double nextTriggerAtSeconds = 0.0;

    juce::OwnedArray<IModifier> prototypeCache; // list of available types
    std::optional<ModifierDescriptor> upcoming;
    juce::Array<int> userSelectedBuffers;

    void timerCallback() override; // low-frequency UI / scheduling tick
    void scheduleTimeTargets();
    void triggerIfDue();
    void broadcastUpcoming();
    ModifierDescriptor pickRandomDescriptor() const;
    juce::Array<int> selectTargetBuffers(const ModifierDescriptor& desc) const;

    juce::Array<ModifierSchedulerListener*> listeners;
};
