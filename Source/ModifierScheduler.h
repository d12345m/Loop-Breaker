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

class ModifierScheduler
{
public:
    explicit ModifierScheduler(const SessionSettings& settingsRef);
    ~ModifierScheduler();

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

    // Called externally each audio block with elapsed seconds (sample accurate source)
    void updateTime(double secondsElapsed);

    // Exposed timeline metrics (valid while running)
    double getAccumulatedSecondsInBar() const { return fmod(accumulatedSecondsTotal, settings.getSecondsPerBar()); }
    double getAccumulatedBars() const { return accumulatedSecondsTotal / settings.getSecondsPerBar(); }
    double getSecondsUntilNextTrigger() const { return juce::jmax(0.0, nextTriggerAbsoluteSeconds - accumulatedSecondsTotal); }
    double getBarsUntilNextTrigger() const { return getSecondsUntilNextTrigger() / settings.getSecondsPerBar(); }
    double getProgressToNextTrigger() const {
        double totalWindow = nextTriggerAbsoluteSeconds - lastTriggerAbsoluteSeconds;
        if (totalWindow <= 0.0) return 0.0;
        return juce::jlimit(0.0, 1.0, (accumulatedSecondsTotal - lastTriggerAbsoluteSeconds) / totalWindow);
    }

    // Provide externally selected buffer indices (user pad selections)
    void setUserSelectedBuffers(const juce::Array<int>& indices);

    // Deterministic randomness (optional). Call before start() for reproducible sequences.
    void setRandomSeed(int64_t seed);

    // Quantization control: when enabled, next trigger time is snapped forward to the next
    // subdivision grid within the bar structure. subdivisionsPerBar = 1 means bar starts,
    // 2 = half-bar, 4 = quarter-bar (i.e. beats in 4/4), 8 = eighth notes, etc.
    void setQuantizationEnabled(bool enabled);
    void setQuantizationSubdivision(int subdivisionsPerBar); // must be >= 1
    bool isQuantizationEnabled() const { return quantizationEnabled.load(); }
    int  getQuantizationSubdivision() const { return subdivisionsPerBar.load(); }

    // Limit random selection to implemented modifiers only (Reverse, Speed, ResetAll for now)
    void setRestrictToImplemented(bool enabled) { restrictToImplemented.store(enabled); }
    bool isRestrictToImplemented() const { return restrictToImplemented.load(); }

private:
    const SessionSettings& settings; // reference to live settings
    bool running = false;
    double accumulatedSecondsTotal = 0.0;             // continuous timeline in seconds
    double nextTriggerAbsoluteSeconds = 0.0;           // absolute time of next trigger
    double lastTriggerAbsoluteSeconds = 0.0;            // absolute time of previous trigger (or start)

    juce::OwnedArray<IModifier> prototypeCache; // list of available types
    std::optional<ModifierDescriptor> upcoming;
    juce::Array<int> userSelectedBuffers;

    // RNG state (shared by descriptor + target selection) for deterministic runs/tests
    mutable juce::Random rng; // mutable to allow use in const selection helpers
    mutable juce::SpinLock rngLock; // protect rng when accessed from different threads

    // Quantization parameters (atomic for safe UI thread mutation)
    std::atomic<bool> quantizationEnabled { false };
    std::atomic<int>  subdivisionsPerBar { 1 }; // 1 = whole bar

    std::atomic<bool> restrictToImplemented { true }; // default: only schedule implemented modifiers

    void scheduleNextTrigger();
    void triggerIfDue();
    void broadcastUpcoming();
    ModifierDescriptor pickRandomDescriptor() const;
    juce::Array<int> selectTargetBuffers(const ModifierDescriptor& desc) const;

    juce::Array<ModifierSchedulerListener*> listeners;
};
