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
    accumulatedSeconds = 0.0;
    scheduleTimeTargets();
    selectNextModifier();
    startTimerHz(10); // 100ms cadence for now
}

void ModifierScheduler::stop()
{
    if (!running) return;
    running = false;
    stopTimer();
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
    accumulatedSeconds += secondsElapsed;
    triggerIfDue();
}

void ModifierScheduler::setUserSelectedBuffers(const juce::Array<int>& indices)
{
    userSelectedBuffers = indices;
}

void ModifierScheduler::timerCallback()
{
    // For now just poll trigger condition on timer as a backup (in case updateTime not wired)
    triggerIfDue();
}

void ModifierScheduler::scheduleTimeTargets()
{
    nextTriggerAtSeconds = settings.getSecondsBetweenModifiers();
}

void ModifierScheduler::triggerIfDue()
{
    if (!running || !upcoming.has_value()) return;
    if (accumulatedSeconds < nextTriggerAtSeconds) return;

    // Trigger current upcoming
    auto descriptor = upcoming.value();
    auto targets = selectTargetBuffers(descriptor);
    for (auto* l : listeners) l->modifierTriggered(descriptor, targets);

    // Prepare next window
    accumulatedSeconds = 0.0;
    scheduleTimeTargets();
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
    juce::Random r;
    int index = r.nextInt(prototypeCache.size());
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

    juce::Random r;
    int count = r.nextInt({1, 4}); // 1..4
    std::set<int> unique;
    while ((int)unique.size() < count)
        unique.insert(r.nextInt({0, 7}));
    for (int v : unique) targets.add(v);
    return targets;
}
