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
    auto base = pickRandomDescriptor();
    upcoming = prepareVariantDescriptor(base);
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

void ModifierScheduler::triggerNow()
{
    if (!running || !upcoming.has_value()) return;
    // Set next trigger to now and attempt immediate trigger
    nextTriggerAbsoluteSeconds = accumulatedSecondsTotal;
    triggerIfDue();
}

void ModifierScheduler::skipUpcoming()
{
    if (!running || !upcoming.has_value()) return;
    // Simulate that we reached the trigger without firing, then select next and reschedule
    lastTriggerAbsoluteSeconds = accumulatedSecondsTotal;
    scheduleNextTrigger();
    selectNextModifier();
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

void ModifierScheduler::forceUpcomingVariant(ModifierType type, const juce::String& variant)
{
    for (auto* proto : prototypeCache)
    {
        if (proto->getDescriptor().type == type)
        {
            auto base = proto->getDescriptor();
            if (type == ModifierType::Speed)
            {
                double val = variant.getDoubleValue();
                if (val <= 0.0) val = 1.0;
                base.plannedSpeed = val;
                base.description = base.description + " -> " + juce::String(val, 2) + "x";
            }
            else if (type == ModifierType::BufferReverbOn)
            {
                double wet = variant.getDoubleValue();
                wet = juce::jlimit(0.0, 1.0, wet);
                base.plannedWet = wet;
                base.description = base.description + " -> Reverb " + juce::String((int)std::round(wet * 100.0)) + "%";
            }
            else if (type == ModifierType::BeatSliceRandom)
            {
                base.plannedSliceDivision = variant; // expect one of division labels
                base.description = base.description + " -> " + base.plannedSliceDivision;
            }
            else if (type == ModifierType::BufferDelayOn)
            {
                // Accept either division labels (contain '/' or 'D' or 'T') OR numeric wet values
                bool looksLikeDivision = variant.containsChar('/') || variant.contains("D") || variant.contains("T");
                if (looksLikeDivision)
                {
                    base.plannedDelayDivision = variant; // e.g. 1/8D
                    base.description = base.description + " -> Delay " + base.plannedDelayDivision;
                }
                else
                {
                    double wet = variant.getDoubleValue();
                    wet = juce::jlimit(0.0, 1.0, wet);
                    base.plannedDelayWet = wet;
                    base.description = base.description + " -> Delay Wet " + juce::String((int)std::round(wet * 100.0)) + "%";
                }
            }
            upcoming = base;
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
                if (t == ModifierType::Reverse || t == ModifierType::Speed || t == ModifierType::ResetAll || t == ModifierType::BeatSliceRandom || t == ModifierType::BufferReverbOn || t == ModifierType::BufferReverbWet25 || t == ModifierType::BufferReverbWet50 || t == ModifierType::BufferReverbWet75 || t == ModifierType::BufferReverbWet100 || t == ModifierType::BufferReverbOff || t == ModifierType::BufferDelayOn || t == ModifierType::BufferDelayOff || t == ModifierType::BufferLowPassOn || t == ModifierType::BufferLowPassOff || t == ModifierType::BufferHighPassOn || t == ModifierType::BufferHighPassOff || t == ModifierType::BufferTremolo || t == ModifierType::BufferTremoloOff)
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

ModifierDescriptor ModifierScheduler::prepareVariantDescriptor(const ModifierDescriptor& base) const
{
    ModifierDescriptor modified = base;
    if (base.type == ModifierType::Speed)
    {
        static const double speeds[] { 0.25, 0.5, 1.0, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double chosen = speeds[rng.nextInt((int)std::size(speeds))];
        modified.plannedSpeed = chosen;
        modified.description = base.description + " -> " + juce::String(chosen, 2) + "x";
    }
    else if (base.type == ModifierType::BeatSliceRandom)
    {
        // Choose division label
        static const juce::StringArray sliceOptions { "1/4", "1/8", "1/8T", "1/16", "1/32", "1/64" };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int idx = rng.nextInt(sliceOptions.size());
        juce::String label = sliceOptions[idx];
        modified.plannedSliceDivision = label;
        modified.description = base.description + " -> " + label;
    }
    else if (base.type == ModifierType::BufferReverbOn)
    {
        static const double wets[] { 0.25, 0.5, 0.75, 1.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double wet = wets[rng.nextInt((int)std::size(wets))];
    modified.plannedWet = wet;
    modified.description = base.description + " -> Reverb " + juce::String((int)std::round(wet * 100.0)) + "%";
    }
    else if (base.type == ModifierType::BufferDelayOn)
    {
        static const juce::StringArray divs { "1/4", "1/8", "1/8D", "1/8T" };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int idx = rng.nextInt(divs.size());
        juce::String label = divs[idx];
        modified.plannedDelayDivision = label;
        modified.description = base.description + " -> Delay " + label;
    }
    return modified;
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
