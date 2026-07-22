/*
 ============================================================================== 
   ModifierScheduler.h
   --------------------------------------------------------------------------
   Schedules, plans, and triggers a truthful depth-three modifier queue.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Modifier.h"
#include "SessionSettings.h"
#include "ModifierProbabilityManager.h"

#include <deque>
#include <vector>

struct PlannedModifier
{
    ModifierDescriptor descriptor;
    juce::Array<int> targets;
};

class ModifierSchedulerListener
{
public:
    virtual ~ModifierSchedulerListener() = default;
    virtual void upcomingModifierChanged(const ModifierDescriptor& /*desc*/) {}
    virtual void plannedQueueChanged(const std::vector<PlannedModifier>& /*queue*/) {}
    virtual void modifierTriggered(const ModifierDescriptor& /*desc*/, const juce::Array<int>& /*targets*/) {}
    virtual void musicalCueReached() {}
};

class ModifierScheduler : private juce::AsyncUpdater
{
public:
    static constexpr int kPlannedQueueDepth = 3;

    explicit ModifierScheduler(const SessionSettings& settingsRef);
    ~ModifierScheduler();

    void start();
    void stop();
    bool isRunning() const { return running; }
    // Reset timeline counters (seconds/bars) to zero; safe whether running or stopped
    void resetTimeline();

    // Force immediate selection of next upcoming modifier (does not trigger it)
    void selectNextModifier();

    // Getter for UI
    std::optional<ModifierDescriptor> getUpcomingModifier() const;
    std::vector<PlannedModifier> getPlannedQueueSnapshot() const;

    // Listener management
    void addListener(ModifierSchedulerListener* l) { listeners.addIfNotAlreadyThere(l); }
    void removeListener(ModifierSchedulerListener* l) { listeners.removeFirstMatchingValue(l); }

    // Called externally each audio block with elapsed seconds (sample accurate source)
    void updateTime(double secondsElapsed);

    // DAW-agnostic host timeline sync (preferred when the host provides PPQ + tempo).
    // ppqPosition is in quarter notes; bpm is quarter-note BPM.
    void updateHostTimeline(double ppqPosition, double bpm);

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

    // Bit i is set when pad i currently contains audio and can receive a modifier.
    // Updating availability retargets queued descriptors before they can fire.
    void setAvailableTargetMask (uint32_t mask);

    // Deterministic randomness (optional). Call before start() for reproducible sequences.
    void setRandomSeed(int64_t seed);

    // Quantization control: when enabled, next trigger time is snapped forward to the next
    // subdivision grid within the bar structure. subdivisionsPerBar = 1 means bar starts,
    // 2 = half-bar, 4 = quarter-bar (i.e. beats in 4/4), 8 = eighth notes, etc.
    void setQuantizationEnabled(bool enabled);
    void setQuantizationSubdivision(int subdivisionsPerBar); // must be >= 1
    bool isQuantizationEnabled() const { return quantizationEnabled.load(); }
    int  getQuantizationSubdivision() const { return subdivisionsPerBar.load(); }

    // Suppression: when true, timing windows continue to advance and upcoming selection rotates,
    // but modifierTriggered callbacks are not emitted. Used to keep visual progress without affecting audio.
    void setSuppressed(bool shouldSuppress) { suppressed.store(shouldSuppress); }
    bool isSuppressed() const { return suppressed.load(); }

    // Limit random selection to entries marked scheduler-eligible in ModifierRegistry.
    void setRestrictToImplemented(bool enabled) { restrictToImplemented.store(enabled); }
    bool isRestrictToImplemented() const { return restrictToImplemented.load(); }

    // Force upcoming modifier (developer/testing aid). If type not found, ignored.
    void forceUpcomingModifier(ModifierType type);
    void forcePlannedModifier (int queueIndex, ModifierType type);
    void forceUpcomingVariant(ModifierType type, const juce::String& variant);

    // Developer controls
    void triggerNow();
    void skipUpcoming();

#if defined (JUCE_DEBUG) || defined (JUCE_UNIT_TESTS)
    // Test inspection helpers (not for production use)
    double TEST_getNextTriggerAbsoluteSeconds() const { return nextTriggerAbsoluteSeconds; }
    double TEST_getLastTriggerAbsoluteSeconds() const { return lastTriggerAbsoluteSeconds; }
    void   TEST_offsetNextTriggerSeconds(double delta) { nextTriggerAbsoluteSeconds += delta; }
#endif

private:
    const SessionSettings& settings; // reference to live settings
    bool running = false;
    double accumulatedSecondsTotal = 0.0;             // continuous timeline in seconds
    double nextTriggerAbsoluteSeconds = 0.0;           // absolute time of next trigger
    double lastTriggerAbsoluteSeconds = 0.0;            // absolute time of previous trigger (or start)

    // Host-synced timeline (PPQ-based). When enabled, accumulatedSecondsTotal is derived from PPQ+tempo.
    std::atomic<bool> hostTimelineActive { false };
    double lastHostPpqPosition = 0.0;
    double nextTriggerPpq = 0.0;
    double nextMainTriggerPpq = 0.0; // main loop schedule (barsBetweenModifiers), independent of burst ticks

    // Quarter-note burst scheduling (rapid-fire modifiers)
    std::atomic<int> quarterNoteBurstRemaining { 0 }; // number of quarter-note triggers remaining

    juce::OwnedArray<IModifier> prototypeCache; // list of available types
    std::deque<PlannedModifier> plannedQueue;
    mutable juce::SpinLock plannedQueueLock;
    juce::Array<int> userSelectedBuffers;
    mutable juce::SpinLock targetStateLock;
    std::atomic<uint32_t> availableTargetMask { 0xffu };

    // RNG state (shared by descriptor + target selection) for deterministic runs/tests
    mutable juce::Random rng; // mutable to allow use in const selection helpers
    mutable juce::SpinLock rngLock; // protect rng when accessed from different threads

    // Quantization parameters (atomic for safe UI thread mutation)
    std::atomic<bool> quantizationEnabled { false };
    std::atomic<int>  subdivisionsPerBar { 1 }; // 1 = whole bar

    std::atomic<bool> restrictToImplemented { true }; // default: only schedule implemented modifiers
    std::atomic<bool> suppressed { false }; // skip firing while keeping progress

    // Variant and target planning are frozen in each PlannedModifier entry.

    void scheduleNextTrigger();
    void scheduleNextTriggerHost(double currentPpq, double bpm);
    void triggerIfDue();
    void triggerIfDueHost(double currentPpq, double bpm);
    void broadcastUpcoming();
    void handleAsyncUpdate() override;
    std::optional<PlannedModifier> planNextModifier() const;
    std::optional<PlannedModifier> getFrontPlannedModifier() const;
    void fillPlannedQueue();
    void advancePlannedQueue();
    void replaceFrontPlannedModifier(PlannedModifier replacement);
    void clearPlannedQueue();
    void refreshPlannedTargets();
    ModifierDescriptor pickRandomDescriptor() const;
    juce::Array<int> selectTargetBuffers(const ModifierDescriptor& desc) const;
    void maybeResnapQuantized();
    ModifierDescriptor prepareVariantDescriptor(const ModifierDescriptor& base) const;

    // Returns the number of bars for the next trigger window, depending on cadence mode.
    // Fixed: barsBetweenModifiers. Variable: random in [barsRangeMin, barsRangeMax].
    int computeNextBarInterval() const;

    // Returns the number of seconds for the next trigger (Timed mode only).
    double computeNextTimedInterval() const;

    juce::Array<ModifierSchedulerListener*> listeners;
};
