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
    {
        // capture upcoming before mutation
        auto descriptor = upcoming.value();
        if (!suppressed.load())
        {
            auto targets = selectTargetBuffers(descriptor);
            for (auto* l : listeners) l->modifierTriggered(descriptor, targets);
            // After a successful trigger, if user had explicitly selected buffers, clear them so
            // next cycle requires fresh selection (pad UI will be updated by listener/owner).
            if (!userSelectedBuffers.isEmpty())
                userSelectedBuffers.clearQuick();
        }
        // Even if suppressed, we still advance scheduling windows & pick next upcoming
        lastTriggerAbsoluteSeconds = accumulatedSecondsTotal;
        scheduleNextTrigger();
        selectNextModifier();
    }
}

void ModifierScheduler::broadcastUpcoming()
{
    if (!upcoming.has_value()) return;
    for (auto* l : listeners) l->upcomingModifierChanged(upcoming.value());
}

void ModifierScheduler::forceUpcomingModifier(ModifierType type)
{
    // Find prototype descriptor for given type
    for (auto* proto : prototypeCache)
    {
        if (proto->getDescriptor().type == type)
        {
            upcoming = proto->getDescriptor();
            broadcastUpcoming();
            return;
        }
    }
}

ModifierDescriptor ModifierScheduler::pickRandomDescriptor() const
{
    if (prototypeCache.isEmpty())
        return {};
    juce::Array<int> candidateIndices;
    if (restrictToImplemented.load())
    {
        for (int i = 0; i < prototypeCache.size(); ++i)
        {
            auto t = prototypeCache[i]->getDescriptor().type;
            if (t == ModifierType::Reverse || t == ModifierType::Speed || t == ModifierType::ResetAll || t == ModifierType::BeatSliceRandom)
                candidateIndices.add(i);
        }
    }
    if (candidateIndices.isEmpty())
    {
        for (int i = 0; i < prototypeCache.size(); ++i) candidateIndices.add(i);
    }
    const juce::SpinLock::ScopedLockType lock(rngLock);
    int choice = rng.nextInt(candidateIndices.size());
    return prototypeCache[candidateIndices[choice]]->getDescriptor();
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
    bool was = quantizationEnabled.exchange(enabled);
    if (running && was != enabled)
        maybeResnapQuantized();
}

void ModifierScheduler::setQuantizationSubdivision(int value)
{
    if (value < 1) value = 1;
    int prev = subdivisionsPerBar.exchange(value);
    if (running && prev != value && quantizationEnabled.load())
        maybeResnapQuantized();
}

void ModifierScheduler::maybeResnapQuantized()
{
    if (!quantizationEnabled.load()) return;
    // Recompute nextTriggerAbsoluteSeconds by treating current remaining time proportionally within new grid.
    // Simpler approach: snap from 'now' using remaining barsBetweenModifiers window boundaries.
    // We keep the existing upcoming selection but adjust the trigger wall-clock.
    double remaining = nextTriggerAbsoluteSeconds - accumulatedSecondsTotal;
    if (remaining < 0.0) remaining = 0.0;
    // Base new scheduling from now respecting full barsBetweenModifiers distance then quantize forward.
    // Reuse scheduleNextTrigger logic but temporarily adjust lastTrigger so progress stays monotonic.
    lastTriggerAbsoluteSeconds = accumulatedSecondsTotal; // restart window
    scheduleNextTrigger();
}
