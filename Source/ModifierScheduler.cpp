/*
 ============================================================================== 
   ModifierScheduler.cpp
   --------------------------------------------------------------------------
   Stub scheduling logic that randomly selects modifiers and simulates trigger
   timing based on SessionSettings (barsBetweenModifiers).
 ==============================================================================
*/

#include "ModifierScheduler.h"
#include "ModifierRegistry.h"

ModifierScheduler::ModifierScheduler(const SessionSettings& settingsRef)
    : settings(settingsRef)
{
    prototypeCache = ModifierFactory::createAllPrototypes();
}

ModifierScheduler::~ModifierScheduler()
{
    stop();
    cancelPendingUpdate();
}

void ModifierScheduler::start()
{
    if (running)
        return;
    running = true;
    accumulatedSecondsTotal = 0.0;
    hostTimelineActive.store(false);
    lastHostPpqPosition = 0.0;
    nextTriggerPpq = 0.0;
    nextMainTriggerPpq = 0.0;
    quarterNoteBurstRemaining.store(0);
    clearPlannedQueue();
    scheduleNextTrigger();
    fillPlannedQueue();
}

void ModifierScheduler::stop()
{
    running = false;
    clearPlannedQueue();
    hostTimelineActive.store(false);
}

void ModifierScheduler::resetTimeline()
{
    // Reset accumulated time and trigger window boundaries
    accumulatedSecondsTotal = 0.0;
    lastTriggerAbsoluteSeconds = 0.0;
    nextTriggerAbsoluteSeconds = 0.0;
    hostTimelineActive.store(false);
    lastHostPpqPosition = 0.0;
    nextTriggerPpq = 0.0;
    nextMainTriggerPpq = 0.0;
    quarterNoteBurstRemaining.store(0);
    // Keep upcoming descriptor as-is if running; otherwise it will be selected on start()
    if (running)
    {
        // Restart scheduling window from zero without changing upcoming selection
        scheduleNextTrigger();
    }
}

void ModifierScheduler::selectNextModifier()
{
    if (auto replacement = planNextModifier())
        replaceFrontPlannedModifier (std::move (*replacement));
    else
        clearPlannedQueue();
}

std::optional<ModifierDescriptor> ModifierScheduler::getUpcomingModifier() const
{
    if (auto planned = getFrontPlannedModifier())
        return planned->descriptor;
    return std::nullopt;
}

std::vector<PlannedModifier> ModifierScheduler::getPlannedQueueSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
    return { plannedQueue.begin(), plannedQueue.end() };
}

void ModifierScheduler::updateTime(double secondsElapsed)
{
    if (!running) return;
    // If we have a host-synced timeline active, ignore delta-time updates.
    if (hostTimelineActive.load())
        return;
    accumulatedSecondsTotal += secondsElapsed;
    triggerIfDue();
}

void ModifierScheduler::updateHostTimeline(double ppqPosition, double bpm)
{
    if (!running)
        return;

    if (bpm <= 0.0)
        return;

    // Derive seconds from host musical timeline (ppq is quarter-note units).
    const double secondsPerQuarter = 60.0 / bpm;
    const double absoluteSeconds = ppqPosition * secondsPerQuarter;

    // Detect first sync or transport seek/jump.
    const bool first = ! hostTimelineActive.exchange(true);
    const double prevPpq = lastHostPpqPosition;
    const double deltaPpq = ppqPosition - prevPpq;
    lastHostPpqPosition = ppqPosition;

    const bool seekedBack = (!first && deltaPpq < -0.5);
    const bool bigJumpFwd = (!first && deltaPpq > 64.0); // more than 64 quarter-notes in a single callback is almost certainly a jump

    accumulatedSecondsTotal = absoluteSeconds;

    if (first || seekedBack || bigJumpFwd)
    {
        lastTriggerAbsoluteSeconds = absoluteSeconds;
        scheduleNextTriggerHost(ppqPosition, bpm);
        if (!getUpcomingModifier().has_value())
            fillPlannedQueue();
        return;
    }

    triggerIfDueHost(ppqPosition, bpm);
}

void ModifierScheduler::setUserSelectedBuffers(const juce::Array<int>& indices)
{
    {
        const juce::SpinLock::ScopedLockType lock (targetStateLock);
        userSelectedBuffers = indices;
    }
    refreshPlannedTargets();
}

void ModifierScheduler::setAvailableTargetMask (uint32_t mask)
{
    mask &= 0xffu;
    if (availableTargetMask.exchange (mask) != mask)
        refreshPlannedTargets();
}

// (timer removed – driven exclusively by audio callback time now)

void ModifierScheduler::scheduleNextTrigger()
{
    if (settings.cadenceMode == CadenceMode::Timed)
    {
        // Timed mode: next trigger at a random time offset from now (not bar-aligned).
        const double interval = computeNextTimedInterval();
        nextTriggerAbsoluteSeconds = accumulatedSecondsTotal + interval;
        return;
    }

    // Fixed or Variable mode — both snap to bar boundaries.
    const int barInterval = computeNextBarInterval();
    const double secondsPerBar = settings.getSecondsPerBar();
    const double currentBars = (secondsPerBar > 0.0 ? (accumulatedSecondsTotal / secondsPerBar) : 0.0);
    const double nextBarIndex = std::floor(currentBars + 1e-9) + (double) barInterval;
    double base = nextBarIndex * secondsPerBar;

    if (quantizationEnabled.load())
    {
        const int subDiv = juce::jmax(1, subdivisionsPerBar.load());
        const double secondsPerBarQ = settings.getSecondsPerBar();
        const double secondsPerSubdivision = secondsPerBarQ / static_cast<double>(subDiv);

        // Compute how many subdivisions have elapsed at 'base' time
        double barsAtBase = (secondsPerBarQ > 0.0 ? (base / secondsPerBarQ) : 0.0); // continuous
        double subDivsAtBase = barsAtBase * subDiv;
        // Snap forward to the NEXT integer subdivision boundary (ceiling) to avoid retrograde timing
        double snappedSubDivIndex = std::ceil(subDivsAtBase - 1e-9); // small bias prevents floating errors near integer
        double snappedBars = snappedSubDivIndex / subDiv;
        double snappedTime = snappedBars * secondsPerBarQ;
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

void ModifierScheduler::scheduleNextTriggerHost(double currentPpq, double bpm)
{
    // Compute PPQ per bar based on time signature.
    const double denom = (double) juce::jmax(1, settings.timeSigDenominator);
    const double barLenPpq = (double) settings.timeSigNumerator * (4.0 / denom);
    const double secondsPerQuarter = 60.0 / bpm;

    if (settings.cadenceMode == CadenceMode::Timed)
    {
        // Timed mode: next trigger at a random time offset (not bar-aligned).
        const double interval = computeNextTimedInterval();
        const double targetSeconds = (currentPpq * secondsPerQuarter) + interval;
        nextTriggerPpq = (secondsPerQuarter > 0.0) ? (targetSeconds / secondsPerQuarter) : (currentPpq + 1.0);
        nextMainTriggerPpq = nextTriggerPpq;
        nextTriggerAbsoluteSeconds = targetSeconds;
        return;
    }

    // Determine bar interval depending on Fixed vs Variable
    const int barInterval = computeNextBarInterval();

    const int burstRemaining = quarterNoteBurstRemaining.load();
    if (burstRemaining > 0)
    {
        // Next trigger is the next quarter-note boundary.
        const double nextQ = std::floor(currentPpq + 1e-9) + 1.0;
        nextTriggerPpq = nextQ;
    }
    else
    {
        // Resume the main loop schedule if it's already been computed.
        if (nextMainTriggerPpq > currentPpq + 1e-9)
        {
            nextTriggerPpq = nextMainTriggerPpq;
        }
        else
        {
            // Next trigger is at the next bar boundary, plus barInterval.
            const double currentBar = (barLenPpq > 0.0 ? std::floor((currentPpq / barLenPpq) + 1e-9) : 0.0);
            const double targetBar = currentBar + (double) barInterval;
            nextTriggerPpq = targetBar * barLenPpq;
            nextMainTriggerPpq = nextTriggerPpq;
        }

        // Optional extra snapping to subdivisions (kept for future flexibility)
        if (quantizationEnabled.load())
        {
            const int subDiv = juce::jmax(1, subdivisionsPerBar.load());
            const double grid = (barLenPpq > 0.0 ? (barLenPpq / (double) subDiv) : 1.0);
            if (grid > 0.0)
            {
                const double snapped = std::ceil((nextTriggerPpq / grid) - 1e-9) * grid;
                nextTriggerPpq = snapped;
                // Keep main schedule in sync when snapping is active.
                nextMainTriggerPpq = juce::jmax(nextMainTriggerPpq, nextTriggerPpq);
            }
        }
    }

    // Mirror PPQ schedule into seconds-based fields for UI.
    nextTriggerAbsoluteSeconds = nextTriggerPpq * secondsPerQuarter;
}

void ModifierScheduler::triggerIfDue()
{
    if (!running) return;
    static constexpr double eps = 1e-6;
    if (accumulatedSecondsTotal + eps < nextTriggerAbsoluteSeconds) return;
    {
        const auto planned = getFrontPlannedModifier();
        const auto descriptor = planned.has_value() ? planned->descriptor : ModifierDescriptor {};
        bool consumedExplicitTargets = false;

        const bool isNoop = ! planned.has_value();

        if (!isNoop && !suppressed.load())
        {
            const auto& targets = planned->targets;
            // If a buffer-targeting modifier has no eligible targets (all pad
            // probabilities are 0), silently skip the trigger.
            const bool needsTargets = (descriptor.category != ModifierCategory::MasterEffect
                                    && descriptor.category != ModifierCategory::GlobalUtility);
            if (!needsTargets || !targets.isEmpty())
            {
                for (auto* l : listeners) l->modifierTriggered(descriptor, targets);
            }
            // After a successful trigger, if user had explicitly selected buffers, clear them so
            // next cycle requires fresh selection (pad UI will be updated by listener/owner).
            {
                const juce::SpinLock::ScopedLockType lock (targetStateLock);
                if (! userSelectedBuffers.isEmpty())
                {
                    userSelectedBuffers.clearQuick();
                    consumedExplicitTargets = true;
                }
            }
        }
        else
        {
            for (auto* l : listeners) l->musicalCueReached();
        }
        // Even if suppressed, we still advance scheduling windows & pick next upcoming
        lastTriggerAbsoluteSeconds = accumulatedSecondsTotal;
        scheduleNextTrigger();
        advancePlannedQueue();
        if (consumedExplicitTargets)
            refreshPlannedTargets();
    }
}

void ModifierScheduler::triggerIfDueHost(double currentPpq, double bpm)
{
    if (!running)
        return;

    static constexpr double eps = 1e-6;
    const double secondsPerQuarter = 60.0 / bpm;
    const double denom = (double) juce::jmax(1, settings.timeSigDenominator);
    const double barLenPpq = (double) settings.timeSigNumerator * (4.0 / denom);

    // Limit catch-up triggers in case of unusual host blocks or jumps.
    int safety = 0;
    while (currentPpq + eps >= nextTriggerPpq && safety++ < 128)
    {
        const int burstRemainingBefore = quarterNoteBurstRemaining.load();
        const auto planned = getFrontPlannedModifier();
        const auto descriptor = planned.has_value() ? planned->descriptor : ModifierDescriptor {};
        bool consumedExplicitTargets = false;

        const bool isNoop = ! planned.has_value();

        if (!isNoop && !suppressed.load())
        {
            const auto& targets = planned->targets;
            const bool needsTargets = (descriptor.category != ModifierCategory::MasterEffect
                                    && descriptor.category != ModifierCategory::GlobalUtility);
            if (!needsTargets || !targets.isEmpty())
            {
                for (auto* l : listeners) l->modifierTriggered(descriptor, targets);
            }
            {
                const juce::SpinLock::ScopedLockType lock (targetStateLock);
                if (! userSelectedBuffers.isEmpty())
                {
                    userSelectedBuffers.clearQuick();
                    consumedExplicitTargets = true;
                }
            }
        }
        else
        {
            for (auto* l : listeners) l->musicalCueReached();
        }

        // If the triggered modifier is QuarterNoteBurst, enable rapid-fire mode.
        if (descriptor.type == ModifierType::QuarterNoteBurst)
        {
            const int quartersPerBar = juce::jmax(1, (int) std::round(barLenPpq));
            const int bars = juce::jmax(1, descriptor.plannedBurstBars.value_or(2));
            const int total = juce::jmax(1, bars) * quartersPerBar;
            quarterNoteBurstRemaining.store(total);

            // Only advance the main loop schedule when it hasn't already
            // been set ahead of the current position.  A burst-within-burst
            // must NOT overwrite a bar-aligned main schedule, otherwise the
            // post-burst resume point drifts off the bar grid.
            if (barLenPpq > 0.0 && nextMainTriggerPpq <= currentPpq + 1e-9)
            {
                // Snap to the bar boundary at or before the current trigger,
                // then step forward by the next bar interval.
                const double currentBar = std::floor((nextTriggerPpq / barLenPpq) + 1e-9);
                const double targetBar  = currentBar + (double) computeNextBarInterval();
                nextMainTriggerPpq = targetBar * barLenPpq;
            }
        }
        else
        {
            const int remaining = quarterNoteBurstRemaining.load();
            if (remaining > 0)
                quarterNoteBurstRemaining.store(remaining - 1);

            // If this was a normal MAIN trigger (not a burst tick), advance the main schedule.
            if (burstRemainingBefore <= 0 && barLenPpq > 0.0)
                nextMainTriggerPpq = nextTriggerPpq + ((double) computeNextBarInterval() * barLenPpq);
        }

        lastTriggerAbsoluteSeconds = nextTriggerPpq * secondsPerQuarter;

        // Schedule next boundary and choose next modifier.
        scheduleNextTriggerHost(nextTriggerPpq, bpm);
        advancePlannedQueue();
        if (consumedExplicitTargets)
            refreshPlannedTargets();

        // Keep accumulatedSecondsTotal consistent for UI.
        accumulatedSecondsTotal = currentPpq * secondsPerQuarter;
    }
}

void ModifierScheduler::broadcastUpcoming()
{
    if (auto upcoming = getUpcomingModifier())
        for (auto* l : listeners)
            l->upcomingModifierChanged (*upcoming);

    // Queue snapshots are a UI-facing API. AsyncUpdater coalesces rapid audio-thread
    // mutations and guarantees delivery from the message thread.
    triggerAsyncUpdate();
}

void ModifierScheduler::handleAsyncUpdate()
{
    const auto snapshot = getPlannedQueueSnapshot();
    for (auto* listener : listeners)
        listener->plannedQueueChanged (snapshot);
}

std::optional<PlannedModifier> ModifierScheduler::planNextModifier() const
{
    auto descriptor = prepareVariantDescriptor (pickRandomDescriptor());
    if (descriptor.type == ModifierType::Unknown)
        return std::nullopt;

    return PlannedModifier { descriptor, selectTargetBuffers (descriptor) };
}

std::optional<PlannedModifier> ModifierScheduler::getFrontPlannedModifier() const
{
    const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
    if (plannedQueue.empty())
        return std::nullopt;
    return plannedQueue.front();
}

void ModifierScheduler::fillPlannedQueue()
{
    while (true)
    {
        {
            const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
            if (plannedQueue.size() >= static_cast<size_t> (kPlannedQueueDepth))
                break;
        }

        auto planned = planNextModifier();
        if (! planned.has_value())
            break;

        const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
        plannedQueue.push_back (std::move (*planned));
    }

    updatePlannedPartDestinations();
    broadcastUpcoming();
}

void ModifierScheduler::advancePlannedQueue()
{
    {
        const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
        if (! plannedQueue.empty())
            plannedQueue.pop_front();
    }
    fillPlannedQueue();
}

void ModifierScheduler::replaceFrontPlannedModifier (PlannedModifier replacement)
{
    {
        const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
        if (plannedQueue.empty())
            plannedQueue.push_front (std::move (replacement));
        else
            plannedQueue.front() = std::move (replacement);
    }
    fillPlannedQueue();
}

void ModifierScheduler::clearPlannedQueue()
{
    {
        const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
        plannedQueue.clear();
    }
    triggerAsyncUpdate();
}

void ModifierScheduler::refreshPlannedTargets()
{
    std::vector<ModifierDescriptor> descriptors;
    {
        const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
        descriptors.reserve (plannedQueue.size());
        for (const auto& planned : plannedQueue)
            descriptors.push_back (planned.descriptor);
    }

    if (descriptors.empty())
        return;

    std::vector<juce::Array<int>> targets;
    targets.reserve (descriptors.size());
    for (const auto& descriptor : descriptors)
        targets.push_back (selectTargetBuffers (descriptor));

    {
        const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
        const auto count = juce::jmin (plannedQueue.size(), targets.size());
        for (size_t i = 0; i < count; ++i)
            plannedQueue[i].targets = std::move (targets[i]);
    }
    broadcastUpcoming();
}

void ModifierScheduler::refreshPlannedPartDestinations()
{
    updatePlannedPartDestinations();
    broadcastUpcoming();
}

void ModifierScheduler::updatePlannedPartDestinations()
{
    const int numParts = juce::jmax (1, settings.parts.getNumParts());
    int simulatedPart = juce::jlimit (0, numParts - 1, settings.parts.activePart);

    const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
    for (auto& planned : plannedQueue)
    {
        if (planned.descriptor.type != ModifierType::SwitchPart)
            continue;

        const int destination = numParts == 1 ? 0 : (simulatedPart + 1) % numParts;
        planned.descriptor.plannedDestinationPart = destination;
        planned.descriptor.description = ModifierRegistry::get (ModifierType::SwitchPart).description
                                       + juce::String (" -> Part ")
                                       + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + destination));
        simulatedPart = destination;
    }
}

void ModifierScheduler::triggerNow()
{
    if (!running || !getUpcomingModifier().has_value()) return;
    // Set next trigger to now and attempt immediate trigger
    nextTriggerAbsoluteSeconds = accumulatedSecondsTotal;
    triggerIfDue();
}

void ModifierScheduler::skipUpcoming()
{
    if (!running || !getUpcomingModifier().has_value()) return;
    // Simulate that we reached the trigger without firing, then select next and reschedule
    lastTriggerAbsoluteSeconds = accumulatedSecondsTotal;
    scheduleNextTrigger();
    advancePlannedQueue();
}

void ModifierScheduler::forceUpcomingModifier(ModifierType type)
{
    forcePlannedModifier (0, type);
}

void ModifierScheduler::forcePlannedModifier (int queueIndex, ModifierType type)
{
    if (! juce::isPositiveAndBelow (queueIndex, kPlannedQueueDepth))
        return;

    // Find prototype descriptor for given type
    for (auto* proto : prototypeCache)
    {
        if (proto->getDescriptor().type == type)
        {
            // Prepare a randomized variant so planned fields & description are populated
            auto descriptor = prepareVariantDescriptor (proto->getDescriptor());
            PlannedModifier replacement { descriptor, selectTargetBuffers (descriptor) };

            if (queueIndex == 0)
            {
                replaceFrontPlannedModifier (std::move (replacement));
                return;
            }

            {
                const juce::SpinLock::ScopedLockType lock (plannedQueueLock);
                const auto index = static_cast<size_t> (queueIndex);
                if (index >= plannedQueue.size())
                    return;
                plannedQueue[index] = std::move (replacement);
            }
            updatePlannedPartDestinations();
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
            else if (type == ModifierType::Stretch)
            {
                double val = variant.getDoubleValue();
                if (val <= 0.0) val = 1.0;
                base.plannedStretch = val;
                base.description = base.description + " -> " + juce::String(val, 2) + "x";
            }
            else if (type == ModifierType::BufferReverbOn)
            {
                double wet = variant.getDoubleValue();
                wet = juce::jlimit(0.0, 1.0, wet);
                base.plannedWet = wet;
                base.description = base.description + " -> Wet " + juce::String((int)std::round(wet * 100.0)) + "%";
            }
            else if (type == ModifierType::BufferChorusOn)
            {
                double mix = variant.getDoubleValue();
                mix = juce::jlimit(0.0, 1.0, mix);
                base.plannedChorusMix = mix;
                base.description = base.description + " -> Mix " + juce::String((int)std::round(mix * 100.0)) + "%";
            }
            else if (type == ModifierType::BufferAutoPan)
            {
                double mix = variant.getDoubleValue();
                mix = juce::jlimit(0.0, 1.0, mix);
                base.plannedPanMix = mix;
                base.description = base.description + " -> Mix " + juce::String((int)std::round(mix * 100.0)) + "%";
            }
            else if (type == ModifierType::BeatSliceRandom)
            {
                base.plannedSliceDivision = variant; // expect one of division labels
                base.description = base.description + " -> " + base.plannedSliceDivision;
            }
            else if (type == ModifierType::ArpSlice)
            {
                // Variant format: "seqLen|totalSlices|repeatBars" e.g. "4|16|2"
                auto parts = juce::StringArray::fromTokens(variant, "|", "");
                if (parts.size() >= 1) base.plannedArpSequenceLength = parts[0].getIntValue();
                if (parts.size() >= 2) base.plannedArpTotalSlices = parts[1].getIntValue();
                if (parts.size() >= 3) base.plannedArpRepeatBars = parts[2].getIntValue();
                base.description = base.description + " -> " + variant;
            }
            else if (type == ModifierType::SliceRepeater)
            {
                // Variant format: "reps|totalSlices" e.g. "8|16"
                auto parts = juce::StringArray::fromTokens(variant, "|", "");
                if (parts.size() >= 1) base.plannedSliceRepeaterReps = parts[0].getIntValue();
                if (parts.size() >= 2) base.plannedSliceRepeaterTotal = parts[1].getIntValue();
                base.description = base.description + " -> Repeat " + variant;
            }
            else if (type == ModifierType::BufferDelayOn)
            {
                // Combined syntax: divisions comma-separated | wet | fb:feedback
                auto parts = juce::StringArray::fromTokens(variant, "|", "");
                juce::String divisionsPart, wetPart, fbPart;
                if (parts.size() > 0) divisionsPart = parts[0].trim();
                if (parts.size() > 1) wetPart = parts[1].trim();
                if (parts.size() > 2) fbPart = parts[2].trim();
                auto isDivision = [](const juce::String& s){ return s.containsChar('/') || s.contains("D") || s.contains("T"); };
                if (divisionsPart.isNotEmpty())
                {
                    auto divTokens = juce::StringArray::fromTokens(divisionsPart, ",", "");
                    for (auto d : divTokens)
                    {
                        d = d.trim();
                        if (isDivision(d)) base.plannedDelayDivisions.addIfNotAlreadyThere(d);
                    }
                    if (base.plannedDelayDivisions.size() == 1)
                        base.plannedDelayDivision = base.plannedDelayDivisions[0]; // backward compatibility single
                }
                if (wetPart.isNotEmpty())
                {
                    double wet = wetPart.getDoubleValue();
                    wet = juce::jlimit(0.0, 1.0, wet);
                    base.plannedDelayWet = wet;
                }
                if (fbPart.isNotEmpty())
                {
                    // Expect fb:0.75 or just numeric if previous parsing stripped prefix
                    juce::String numeric = fbPart;
                    if (numeric.startsWithIgnoreCase("fb:")) numeric = numeric.substring(3).trim();
                    double fb = numeric.getDoubleValue();
                    fb = juce::jlimit(0.0, 0.95, fb);
                    base.plannedDelayFeedback = fb;
                }
                // Build description fragment
                juce::String frag(" -> ");
                juce::String combo;
                if (! base.plannedDelayDivisions.isEmpty())
                    combo << base.plannedDelayDivisions.joinIntoString(",");
                if (base.plannedDelayWet.has_value())
                {
                    if (combo.isNotEmpty()) combo << " | ";
                    combo << "Wet " << (int)std::round(base.plannedDelayWet.value() * 100.0) << "%";
                }
                if (base.plannedDelayFeedback.has_value())
                {
                    if (combo.isNotEmpty()) combo << " | ";
                    combo << "Feedback " << (int)std::round(base.plannedDelayFeedback.value() * 100.0) << "%";
                }
                base.description = base.description + frag + combo;
            }
            replaceFrontPlannedModifier ({ base, selectTargetBuffers (base) });
            return;
        }
    }
}

ModifierDescriptor ModifierScheduler::pickRandomDescriptor() const
{
    if (prototypeCache.isEmpty())
        return {};
    juce::Array<int> candidateIndices;
    const bool moreThanOnePart = settings.parts.getNumParts() > 1;
    for (int i = 0; i < prototypeCache.size(); ++i)
    {
        const auto type = prototypeCache[i]->getDescriptor().type;
        if (! ModifierRegistry::get (type).schedulerEligible)
            continue;

        // SwitchPart gating by part count is handled inside weighted selection.
        candidateIndices.add (i);
    }

    // Use weighted random selection via ModifierProbabilityManager.
    // SwitchPart is auto-excluded when moreThanOnePart is false.
    const auto& probMgr = settings.modifierProbabilities;
    auto typeForIndex = [this](int idx) -> ModifierType {
        return prototypeCache[idx]->getDescriptor().type;
    };

    const juce::SpinLock::ScopedLockType lock(rngLock);
    int chosen = probMgr.chooseWeighted(candidateIndices, typeForIndex, moreThanOnePart, rng);

    // If every candidate has weight 0, return an empty descriptor (no modifier)
    if (chosen < 0)
        return {};

    return prototypeCache[chosen]->getDescriptor();
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
    else if (base.type == ModifierType::Stretch)
    {
        static const double ratios[] { 0.25, 0.5, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double chosen = ratios[rng.nextInt((int)std::size(ratios))];
        modified.plannedStretch = chosen;
        modified.description = base.description + " -> " + juce::String(chosen, 2) + "x";
    }
    else if (base.type == ModifierType::BeatSliceRandom)
    {
        // Choose number of slices directly from set {4,8,16,32,64}
        static const int sliceCounts[] { 4, 8, 16, 32, 64 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int idx = rng.nextInt((int)std::size(sliceCounts));
        int chosen = sliceCounts[idx];
        // Store as a simple numeric label for reuse in apply
        modified.plannedSliceDivision = juce::String(chosen);
        modified.description = base.description + " -> " + juce::String(chosen) + " slices";
    }
    else if (base.type == ModifierType::ArpSlice)
    {
        // Sequence lengths: {1,2,3,4,6,8}
        static const int seqLens[] { 1, 2, 3, 4, 6, 8 };
        // Total slice counts for the grid: {16,32,64}
        static const int sliceCounts[] { 16, 32, 64 };
        // Repeat cycles before refreshing: {1,2,4,8}
        static const int repeatOptions[] { 1, 2, 4, 8 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int seqLen = seqLens[rng.nextInt((int)std::size(seqLens))];
        int totalSlices = sliceCounts[rng.nextInt((int)std::size(sliceCounts))];
        int repeatBars = repeatOptions[rng.nextInt((int)std::size(repeatOptions))];
        modified.plannedArpSequenceLength = seqLen;
        modified.plannedArpTotalSlices = totalSlices;
        modified.plannedArpRepeatBars = repeatBars;
        modified.description = base.description + " -> " + juce::String(seqLen) + " slice sequence / "
            + juce::String(totalSlices) + " slice grid / " + juce::String(repeatBars) + (repeatBars == 1 ? " cycle" : " cycles");
    }
    else if (base.type == ModifierType::SliceRepeater)
    {
        // Repetitions per slice: {4,8,16,32}
        static const int repOptions[] { 4, 8, 16, 32 };
        // Total slice grid: {16,32,64}
        static const int sliceCounts[] { 16, 32, 64 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int reps = repOptions[rng.nextInt((int)std::size(repOptions))];
        int totalSlices = sliceCounts[rng.nextInt((int)std::size(sliceCounts))];
        modified.plannedSliceRepeaterReps = reps;
        modified.plannedSliceRepeaterTotal = totalSlices;
        modified.description = base.description + " -> Repeat x" + juce::String(reps) + " / "
            + juce::String(totalSlices) + " slice grid";
    }
    else if (base.type == ModifierType::BufferReverbOn)
    {
        static const double wets[] { 0.25, 0.5, 0.75, 1.0 };
        static const double fades[] { 0.0, 1.0, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double wet = wets[rng.nextInt((int)std::size(wets))];
        double fadeBars = fades[rng.nextInt((int)std::size(fades))];
        modified.plannedWet = wet;
        modified.plannedFxFadeBars = fadeBars;
        juce::String fadeLabel = fadeBars <= 0.0 ? "instant" : (fadeBars == 1.0 ? "1 bar" : juce::String((int)fadeBars) + " bars");
        modified.description = base.description + " -> Wet " + juce::String((int)std::round(wet * 100.0)) + "% | Fade: " + fadeLabel;
    }
    else if (base.type == ModifierType::BufferDelayOn)
    {
        static const juce::StringArray divs { "1/4", "1/8", "1/8D", "1/8T", "1/16" };
        static const double wets[] { 0.25, 0.5, 0.75, 1.0 };
        static const double fbs[]  { 0.25, 0.5 };
        static const double fades[] { 0.0, 1.0, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        // Random division
        juce::String label = divs[rng.nextInt(divs.size())];
        modified.plannedDelayDivision = label;
        // Random wet & feedback
        double wet = wets[rng.nextInt((int)std::size(wets))];
        double fb  = fbs[rng.nextInt((int)std::size(fbs))];
        modified.plannedDelayWet = wet;
        modified.plannedDelayFeedback = fb;
        // Optional fade (bars) used to ramp feedback
        double fadeBars = fades[rng.nextInt((int)std::size(fades))];
        modified.plannedFxFadeBars = fadeBars;
        juce::String fadeLabel = fadeBars <= 0.0 ? "instant" : (fadeBars == 1.0 ? "1 bar" : juce::String((int)fadeBars) + " bars");
      // Random ping-pong and wow/flutter flags
      bool pp = rng.nextBool();
      bool wf = rng.nextBool();
      modified.plannedDelayPingPong = pp;
      modified.plannedWowFlutter = wf;
      // Description: include division, wet, feedback, fade, and flags
      juce::String parts;
      parts << label << " | Wet "
          << (int)std::round(wet * 100.0) << "%" << " | Feedback "
          << (int)std::round(fb * 100.0) << "%" << " | Fade: "
          << fadeLabel;
      if (pp) parts << " | Ping-Pong";
      if (wf) parts << " | Wow/Flutter";
        modified.description = base.description + " -> " + parts;
    }
    else if (base.type == ModifierType::SwitchPart)
    {
        const int numParts = juce::jmax (1, settings.parts.getNumParts());
        const int current = juce::jlimit (0, numParts - 1, settings.parts.activePart);
        const int destination = numParts == 1 ? 0 : (current + 1) % numParts;
        modified.plannedDestinationPart = destination;
        modified.description = base.description + " -> Part "
                             + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + destination));
    }
    else if (base.type == ModifierType::MasterLowPassOn || base.type == ModifierType::MasterHighPassOn)
    {
        // Temporary global filters: durations {2,4,8,16} bars and either ramp-up+down or immediate jump then ramp down
        static const double durations[] { 2.0, 4.0, 8.0, 16.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double dur = durations[rng.nextInt((int)std::size(durations))];
        bool jump = rng.nextBool();
        modified.plannedFxFadeBars = dur;
        modified.plannedImmediateJump = jump;
        juce::String mode = jump ? "Jump to target then decay" : "Ramp up then ramp down";
        modified.description = base.description + " -> " + juce::String((int)dur) + " bars | " + mode;
    }
    else if (base.type == ModifierType::BufferVolumeRampDown)
    {
        static const double fades[] { 1.0, 2.0, 4.0 };
        static const double holds[] { 1.0, 2.0, 3.0, 4.0 };
        const juce::SpinLock::ScopedLockType lock (rngLock);
        const double fadeBars = fades[rng.nextInt ((int) std::size (fades))];
        const double holdBars = holds[rng.nextInt ((int) std::size (holds))];
        modified.plannedFxFadeBars = fadeBars;
        modified.plannedVolumeHoldBars = holdBars;
        modified.description = base.description + " -> Fade " + juce::String ((int) fadeBars)
                             + " bars | Hold " + juce::String ((int) holdBars) + " bars";
    }
    else if (base.type == ModifierType::QuarterNoteBurst)
    {
        static const int barsOptions[] { 1, 2, 4 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int bars = barsOptions[rng.nextInt((int) std::size(barsOptions))];
        modified.plannedBurstBars = bars;
        modified.description = base.description + " -> " + juce::String(bars) + (bars == 1 ? " bar" : " bars");
    }
    else if (base.type == ModifierType::BufferChorusOn)
    {
        static const double depths[] { 0.25, 0.5, 0.75, 1.0 };
        static const double rates[]  { 0.5, 1.0, 1.5, 2.0, 3.0 };
        static const double mixes[]  { 0.25, 0.5, 0.75, 1.0 };
        static const double fades[]  { 0.0, 1.0, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double depth = depths[rng.nextInt((int)std::size(depths))];
        double rate  = rates[rng.nextInt((int)std::size(rates))];
        double mix   = mixes[rng.nextInt((int)std::size(mixes))];
        double fadeBars = fades[rng.nextInt((int)std::size(fades))];
        modified.plannedChorusDepth = depth;
        modified.plannedChorusRateHz = rate;
        modified.plannedChorusMix = mix;
        modified.plannedFxFadeBars = fadeBars;
        juce::String fadeLabel = fadeBars <= 0.0 ? "instant" : (fadeBars == 1.0 ? "1 bar" : juce::String((int)fadeBars) + " bars");
        modified.description = base.description + " -> Mix " + juce::String((int)std::round(mix * 100.0))
            + "% | Depth " + juce::String(depth, 2)
            + " | Rate " + juce::String(rate, 1) + " Hz | Fade: " + fadeLabel;
    }
    else if (base.type == ModifierType::BufferAutoPan)
    {
        // Musical divisions: 1/4 note = 0.25 bars, 1/2 note, 1 bar, 2 bars
        static const double divBars[] { 0.25, 0.5, 1.0, 2.0 };
        static const double depths[] { 0.5, 0.75, 1.0 };
        static const double mixes[]  { 0.5, 0.75, 1.0 };
        static const double fades[]  { 0.0, 1.0, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double divisionBars = divBars[rng.nextInt((int)std::size(divBars))];
        double depth = depths[rng.nextInt((int)std::size(depths))];
        double mix   = mixes[rng.nextInt((int)std::size(mixes))];
        double fadeBars = fades[rng.nextInt((int)std::size(fades))];
        // Convert division in bars to rateHz using current BPM
        double spbar = settings.getSecondsPerBar();
        double rateHz = (spbar > 0.0 && divisionBars > 0.0) ? (1.0 / (spbar * divisionBars)) : 2.0;
        modified.plannedPanRateHz = rateHz;
        modified.plannedPanDepth = depth;
        modified.plannedPanMix = mix;
        modified.plannedFxFadeBars = fadeBars;
        // Build description with musical division label
        juce::String divLabel;
        if (divisionBars <= 0.25) divLabel = "1/4 note";
        else if (divisionBars <= 0.5) divLabel = "1/2 note";
        else if (divisionBars <= 1.0) divLabel = "1 bar";
        else divLabel = juce::String((int)divisionBars) + " bars";
        juce::String fadeLabel2 = fadeBars <= 0.0 ? "instant" : (fadeBars == 1.0 ? "1 bar" : juce::String((int)fadeBars) + " bars");
        modified.description = base.description + " -> Mix " + juce::String((int)std::round(mix * 100.0))
            + "% | Depth " + juce::String(depth, 2)
            + " | Period: " + divLabel + " | Fade: " + fadeLabel2;
    }
    else if (base.type == ModifierType::PingPong)
    {
        static const double divisions[] { 1.0, 0.5, 0.25, 0.125, 0.0625 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double div = divisions[rng.nextInt((int)std::size(divisions))];
        modified.plannedPingPongDivision = div;
        juce::String divLabel;
        if (div >= 1.0)       divLabel = "Whole note";
        else if (div >= 0.5)  divLabel = "Half note";
        else if (div >= 0.25) divLabel = "Quarter note";
        else if (div >= 0.125) divLabel = "1/8 note";
        else                   divLabel = "1/16 note";
        modified.description = base.description + " -> " + divLabel;
    }
    else if (base.type == ModifierType::BufferSHLowPassOn || base.type == ModifierType::BufferSHHighPassOn)
    {
        // S&H rate: 1/16 = 0.0625to bars, 1/8 = 0.125 bars, 1/4 = 0.25 bars
        static const double divs[] { 0.0625, 0.125, 0.25 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double div = divs[rng.nextInt((int)std::size(divs))];
        modified.plannedSHDivisionBars = div;
        juce::String divLabel;
        if (div <= 0.0625)      divLabel = "1/16 note";
        else if (div <= 0.125)  divLabel = "1/8 note";
        else                    divLabel = "1/4 note";
        modified.description = base.description + " -> S&H rate: " + divLabel;
    }
    else if (base.type == ModifierType::BufferGranularOn || base.type == ModifierType::BufferGranularMomentary)
    {
        static const double densities[] { 4.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0 };
        static const double sizes[]     { 60.0, 100.0, 150.0, 200.0, 300.0, 500.0 };
        static const double pitches[]   { 0.0, 12.0, 24.0 };
        static const double mixes[]     { 0.5, 0.75, 1.0 };
        static const double textures[]  { 0.1, 0.3, 0.5, 0.8 };
        static const double fades[]     { 0.0, 1.0, 2.0 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        double density = densities[rng.nextInt((int)std::size(densities))];
        double size    = sizes[rng.nextInt((int)std::size(sizes))];
        double pitch   = pitches[rng.nextInt((int)std::size(pitches))];
        double mix     = mixes[rng.nextInt((int)std::size(mixes))];
        double texture = textures[rng.nextInt((int)std::size(textures))];
        modified.plannedGrainDensityHz   = density;
        modified.plannedGrainSizeMs      = size;
        modified.plannedGrainPitchSpread = pitch;
        modified.plannedGrainMix         = mix;
        modified.plannedGrainTexture     = texture;
        if (base.type == ModifierType::BufferGranularOn)
        {
            double fadeBars = fades[rng.nextInt((int)std::size(fades))];
            modified.plannedFxFadeBars = fadeBars;
            juce::String fadeLabel = fadeBars <= 0.0 ? "instant" : juce::String((int)fadeBars) + " bars";
            juce::String pitchLabel = pitch <= 0.0 ? "unison" : (juce::String(juce::CharPointer_UTF8("\xc2\xb1")) + juce::String((int)(pitch / 12.0)) + " oct");
            modified.description = base.description + " -> " + juce::String((int)density) + " g/s | "
                + juce::String((int)size) + "ms | " + pitchLabel + " | Mix "
                + juce::String((int)std::round(mix * 100.0)) + "% | Fade: " + fadeLabel;
        }
        else
        {
            static const double durations[] { 2.0, 4.0, 8.0, 16.0 };
            double dur = durations[rng.nextInt((int)std::size(durations))];
            modified.plannedFxFadeBars = dur;
            juce::String pitchLabel = pitch <= 0.0 ? "unison" : (juce::String(juce::CharPointer_UTF8("\xc2\xb1")) + juce::String((int)(pitch / 12.0)) + " oct");
            modified.description = base.description + " -> " + juce::String((int)density) + " g/s | "
                + juce::String((int)size) + "ms | " + pitchLabel + " | Mix "
                + juce::String((int)std::round(mix * 100.0)) + "% | " + juce::String((int)dur) + " bars";
        }
    }
    else if (base.type == ModifierType::SwapModifierStack)
    {
        modified.description = base.description + " -> Rotate stacks";
    }
    return modified;
}

juce::Array<int> ModifierScheduler::selectTargetBuffers(const ModifierDescriptor& desc) const
{
    juce::Array<int> targets;
    if (desc.category == ModifierCategory::MasterEffect || desc.category == ModifierCategory::GlobalUtility)
        return targets; // Empty means 'master/global'

    const auto availableMask = availableTargetMask.load();

    // If the user selected loaded pads, use those. Empty/unloaded selections do
    // not create target pips that can never produce an audible result.
    {
        const juce::SpinLock::ScopedLockType targetLock (targetStateLock);
        for (int index : userSelectedBuffers)
            if (juce::isPositiveAndBelow (index, 8)
                && (availableMask & (1u << static_cast<uint32_t> (index))) != 0)
                targets.addIfNotAlreadyThere (index);
    }
    const bool needsMultipleTargets = desc.type == ModifierType::SwapModifierStack;
    if (needsMultipleTargets)
    {
        // Swap Stack is entirely user-directed. Selection changes replan the
        // queue, so the set and order here remain editable until trigger time.
        if (targets.size() < 2)
            targets.clear();
        return targets;
    }

    if (! targets.isEmpty())
        return targets;

    const juce::SpinLock::ScopedLockType lock(rngLock);

    // Build pool of eligible pads (padTargetProbability > 0)
    juce::Array<int>   eligible;
    juce::Array<float> weights;
    float totalWeight = 0.0f;

    for (int i = 0; i < 8; ++i)
    {
        if ((availableMask & (1u << static_cast<uint32_t> (i))) == 0 || targets.contains (i))
            continue;

        float w = settings.padTargetProbabilities[static_cast<size_t>(i)];
        if (w > 0.0f)
        {
            eligible.add (i);
            weights.add (w);
            totalWeight += w;
        }
    }

    if (eligible.isEmpty())
        return targets; // all pads disabled – nothing to target

    const int maxTotalCount = juce::jmin (4, eligible.size() + targets.size());
    const int desiredTotalCount = rng.nextInt ({ 1, maxTotalCount + 1 });
    const int count = juce::jlimit (0, eligible.size(), desiredTotalCount - targets.size());

    std::set<int> unique;
    while ((int)unique.size() < count)
    {
        // Weighted random pick from eligible pool
        float r = rng.nextFloat() * totalWeight;
        float cumulative = 0.0f;
        for (int j = 0; j < eligible.size(); ++j)
        {
            cumulative += weights[j];
            if (r <= cumulative)
            {
                unique.insert (eligible[j]);
                break;
            }
        }
    }

    for (int v : unique) targets.addIfNotAlreadyThere (v);
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

int ModifierScheduler::computeNextBarInterval() const
{
    switch (settings.cadenceMode)
    {
        case CadenceMode::Variable:
        {
            const int lo = juce::jmax(1, juce::jmin(settings.barsRangeMin, settings.barsRangeMax));
            const int hi = juce::jmax(lo, juce::jmax(settings.barsRangeMin, settings.barsRangeMax));
            const juce::SpinLock::ScopedLockType lock(rngLock);
            return rng.nextInt({ lo, hi + 1 }); // inclusive range
        }
        case CadenceMode::Timed:
            // Not bar-based; callers should use computeNextTimedInterval() instead.
            // Return a safe fallback in case this is called unexpectedly.
            return settings.barsBetweenModifiers;

        case CadenceMode::Fixed:
        default:
            return settings.barsBetweenModifiers;
    }
}

double ModifierScheduler::computeNextTimedInterval() const
{
    const double lo = juce::jmax(0.5, juce::jmin(settings.timedIntervalMinSec, settings.timedIntervalMaxSec));
    const double hi = juce::jmax(lo, juce::jmax(settings.timedIntervalMinSec, settings.timedIntervalMaxSec));
    const juce::SpinLock::ScopedLockType lock(rngLock);
    return lo + rng.nextDouble() * (hi - lo);
}
