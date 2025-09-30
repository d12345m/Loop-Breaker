/*
 ============================================================================== 
   ModifierScheduler.cpp
   --------------------------------------------------------------------------
   Stub scheduling logic that randomly selects modifiers and simulates trigger
   timing based on SessionSettings (barsBetweenModifiers).
 ==============================================================================
*/

#include "ModifierScheduler.h"

ModifierScheduler::ModifierScheduler(const SessionSettings& settingsRef)
    : settings(settingsRef)
{
    prototypeCache = ModifierFactory::createAllPrototypes();
}

ModifierScheduler::~ModifierScheduler()
{
    stop();
}

void ModifierScheduler::start()
{
    if (running)
        return;
    running = true;
    accumulatedSecondsTotal = 0.0;
    scheduleNextTrigger();
    selectNextModifier();
}

void ModifierScheduler::stop()
{
    if (!running) return;
    running = false;
    upcoming.reset();
}

void ModifierScheduler::selectNextModifier()
{
    upcoming = pickRandomDescriptor();
    broadcastUpcoming();
}

void ModifierScheduler::updateTime(double secondsElapsed)
{
    if (!running) return;
    accumulatedSecondsTotal += secondsElapsed;
    triggerIfDue();
}

void ModifierScheduler::setUserSelectedBuffers(const juce::Array<int>& indices)
{
    userSelectedBuffers = indices;
}

// (timer removed – driven exclusively by audio callback time now)

void ModifierScheduler::scheduleNextTrigger()
{
    // Set next trigger absolute time from now using current settings
    nextTriggerAbsoluteSeconds = accumulatedSecondsTotal + settings.getSecondsBetweenModifiers();
}

void ModifierScheduler::triggerIfDue()
{
    if (!running || !upcoming.has_value()) return;
    static constexpr double eps = 1e-6;
    if (accumulatedSecondsTotal + eps < nextTriggerAbsoluteSeconds) return;

    // Trigger current upcoming
    auto descriptor = upcoming.value();
    auto targets = selectTargetBuffers(descriptor);
    for (auto* l : listeners) l->modifierTriggered(descriptor, targets);

    // Prepare next window
    scheduleNextTrigger();
    selectNextModifier();
}

void ModifierScheduler::broadcastUpcoming()
{
    if (!upcoming.has_value()) return;
    for (auto* l : listeners) l->upcomingModifierChanged(upcoming.value());
}

ModifierDescriptor ModifierScheduler::pickRandomDescriptor() const
{
    if (prototypeCache.isEmpty())
        return {};
    const juce::SpinLock::ScopedLockType lock(rngLock);
    int index = rng.nextInt(prototypeCache.size());
    return prototypeCache[index]->getDescriptor();
}

juce::Array<int> ModifierScheduler::selectTargetBuffers(const ModifierDescriptor& desc) const
{
    juce::Array<int> targets;
    if (desc.category == ModifierCategory::MasterEffect || desc.category == ModifierCategory::GlobalUtility)
        return targets; // Empty means 'master/global'

    // If user selected pads, use them; else select 1-4 semi-random
    if (!userSelectedBuffers.isEmpty())
        return userSelectedBuffers;

    const juce::SpinLock::ScopedLockType lock(rngLock);
    int count = rng.nextInt({1, 4}); // 1..4
    std::set<int> unique;
    while ((int)unique.size() < count)
        unique.insert(rng.nextInt({0, 7}));
    for (int v : unique) targets.add(v);
    return targets;
}

void ModifierScheduler::setRandomSeed(int64_t seed)
{
    const juce::SpinLock::ScopedLockType lock(rngLock);
    rng.setSeed(static_cast<int64_t>(seed));
}
