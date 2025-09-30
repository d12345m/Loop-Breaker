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
    // Base unquantized target (absolute seconds)
    double base = accumulatedSecondsTotal + settings.getSecondsBetweenModifiers();

    if (quantizationEnabled.load())
    {
        const int subDiv = juce::jmax(1, subdivisionsPerBar.load());
        const double secondsPerBar = settings.getSecondsPerBar();
        const double secondsPerSubdivision = secondsPerBar / static_cast<double>(subDiv);

        // Compute how many subdivisions have elapsed at 'base' time
        double barsAtBase = base / secondsPerBar; // continuous
        double subDivsAtBase = barsAtBase * subDiv;
        // Snap forward to the NEXT integer subdivision boundary (ceiling) to avoid retrograde timing
        double snappedSubDivIndex = std::ceil(subDivsAtBase - 1e-9); // small bias prevents floating errors near integer
        double snappedBars = snappedSubDivIndex / subDiv;
        double snappedTime = snappedBars * secondsPerBar;
        // Safety: ensure snapped time is never earlier than a tiny horizon ahead of 'now'
        const double minLead = 1e-5; // ~10 microseconds; effectively immediate next subdivision
        if (snappedTime < accumulatedSecondsTotal + minLead)
        {
            // Advance one more subdivision if we somehow snapped to (or before) current time
            snappedTime += secondsPerSubdivision;
        }
        nextTriggerAbsoluteSeconds = snappedTime;
    }
    else
    {
        nextTriggerAbsoluteSeconds = base;
    }
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

void ModifierScheduler::setQuantizationEnabled(bool enabled)
{
    quantizationEnabled.store(enabled);
}

void ModifierScheduler::setQuantizationSubdivision(int value)
{
    if (value < 1) value = 1;
    subdivisionsPerBar.store(value);
}
