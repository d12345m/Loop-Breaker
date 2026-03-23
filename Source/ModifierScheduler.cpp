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
    hostTimelineActive.store(false);
    lastHostPpqPosition = 0.0;
    nextTriggerPpq = 0.0;
    nextMainTriggerPpq = 0.0;
    quarterNoteBurstRemaining.store(0);
    scheduleNextTrigger();
    selectNextModifier();
}

void ModifierScheduler::stop()
{
    if (!running) return;
    running = false;
    upcoming.reset();
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
    auto base = pickRandomDescriptor();
    upcoming = prepareVariantDescriptor(base);
    broadcastUpcoming();
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
        if (!upcoming.has_value())
            selectNextModifier();
        return;
    }

    triggerIfDueHost(ppqPosition, bpm);
}

void ModifierScheduler::setUserSelectedBuffers(const juce::Array<int>& indices)
{
    userSelectedBuffers = indices;
}

// (timer removed – driven exclusively by audio callback time now)

void ModifierScheduler::scheduleNextTrigger()
{
    // Base target aligned to the *next* bar boundary after a fixed bar interval.
    const double secondsPerBar = settings.getSecondsPerBar();
    const double currentBars = (secondsPerBar > 0.0 ? (accumulatedSecondsTotal / secondsPerBar) : 0.0);
    const double nextBarIndex = std::floor(currentBars + 1e-9) + (double) settings.barsBetweenModifiers;
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
            // Next trigger is at the next bar boundary, plus barsBetweenModifiers.
            const double currentBar = (barLenPpq > 0.0 ? std::floor((currentPpq / barLenPpq) + 1e-9) : 0.0);
            const double targetBar = currentBar + (double) settings.barsBetweenModifiers;
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
    if (!running || !upcoming.has_value()) return;
    static constexpr double eps = 1e-6;
    if (accumulatedSecondsTotal + eps < nextTriggerAbsoluteSeconds) return;
    {
        // capture upcoming before mutation
        auto descriptor = upcoming.value();
        if (!suppressed.load())
        {
            auto targets = selectTargetBuffers(descriptor);
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
            if (!userSelectedBuffers.isEmpty())
                userSelectedBuffers.clearQuick();
        }
        // Even if suppressed, we still advance scheduling windows & pick next upcoming
        lastTriggerAbsoluteSeconds = accumulatedSecondsTotal;
        scheduleNextTrigger();
        selectNextModifier();
    }
}

void ModifierScheduler::triggerIfDueHost(double currentPpq, double bpm)
{
    if (!running || !upcoming.has_value())
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
        auto descriptor = upcoming.value();

        if (descriptor.type == ModifierType::Unknown)
        {
            // no-op
        }

        if (descriptor.type == ModifierType::Unknown)
        {
            // no-op
        }

        if (descriptor.type == ModifierType::Unknown)
        {
            // no-op
        }

        if (descriptor.type == ModifierType::Unknown)
        {
            // no-op
        }

        if (descriptor.type == ModifierType::Unknown)
        {
            // no-op
        }

        if (!suppressed.load())
        {
            auto targets = selectTargetBuffers(descriptor);
            const bool needsTargets = (descriptor.category != ModifierCategory::MasterEffect
                                    && descriptor.category != ModifierCategory::GlobalUtility);
            if (!needsTargets || !targets.isEmpty())
            {
                for (auto* l : listeners) l->modifierTriggered(descriptor, targets);
            }
            if (!userSelectedBuffers.isEmpty())
                userSelectedBuffers.clearQuick();
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
                // then step forward by barsBetweenModifiers whole bars.
                const double currentBar = std::floor((nextTriggerPpq / barLenPpq) + 1e-9);
                const double targetBar  = currentBar + (double) settings.barsBetweenModifiers;
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
                nextMainTriggerPpq = nextTriggerPpq + ((double) settings.barsBetweenModifiers * barLenPpq);
        }

        lastTriggerAbsoluteSeconds = nextTriggerPpq * secondsPerQuarter;

        // Schedule next boundary and choose next modifier.
        scheduleNextTriggerHost(nextTriggerPpq, bpm);
        selectNextModifier();

        // Keep accumulatedSecondsTotal consistent for UI.
        accumulatedSecondsTotal = currentPpq * secondsPerQuarter;
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
            // Prepare a randomized variant so planned fields & description are populated
            upcoming = prepareVariantDescriptor(proto->getDescriptor());
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
    const bool moreThanOnePart = settings.parts.getNumParts() > 1;
    if (restrictToImplemented.load())
    {
        for (int i = 0; i < prototypeCache.size(); ++i)
        {
            auto t = prototypeCache[i]->getDescriptor().type;
            // Base implemented list
            bool allowed = (t == ModifierType::Reverse
                || t == ModifierType::Speed
                || t == ModifierType::Stretch
                || t == ModifierType::PitchUpOctave
                || t == ModifierType::PitchDownOctave
                || t == ModifierType::ResetAll
                || t == ModifierType::BeatSliceRandom
                || t == ModifierType::ArpSlice
                || t == ModifierType::SliceRepeater
                || t == ModifierType::BufferReverbOn
                || t == ModifierType::BufferDelayOn
                || t == ModifierType::BufferDelayDubBurst
                || t == ModifierType::BufferLowPassOn
                || t == ModifierType::BufferHighPassOn
                || t == ModifierType::MasterLowPassOn
                || t == ModifierType::MasterHighPassOn
                || t == ModifierType::BufferTremolo
                || t == ModifierType::BufferChorusOn
                || t == ModifierType::BufferAutoPan
                || t == ModifierType::BufferVolumeRampDown
                    || t == ModifierType::SwitchPart
                    || t == ModifierType::QuarterNoteBurst);
            if (!allowed) continue;
            // SwitchPart gated by parts count is handled inside weighted selection
            candidateIndices.add(i);
        }
    }
    // Fallback (when not restricting): include all descriptors
    if (candidateIndices.isEmpty())
    {
        for (int i = 0; i < prototypeCache.size(); ++i)
            candidateIndices.add(i);
    }

    // Use weighted random selection via ModifierProbabilityManager.
    // SwitchPart is auto-excluded when moreThanOnePart is false.
    const auto& probMgr = settings.modifierProbabilities;
    auto typeForIndex = [this](int idx) -> ModifierType {
        return prototypeCache[idx]->getDescriptor().type;
    };

    const juce::SpinLock::ScopedLockType lock(rngLock);
    int chosen = probMgr.chooseWeighted(candidateIndices, typeForIndex, moreThanOnePart, rng);

    // If every candidate has weight 0, fall back to uniform random (safety net)
    if (chosen < 0)
    {
        // Filter out SwitchPart if needed
        juce::Array<int> fallback;
        for (int ci : candidateIndices)
        {
            auto t = prototypeCache[ci]->getDescriptor().type;
            if (t == ModifierType::SwitchPart && !moreThanOnePart) continue;
            fallback.add(ci);
        }
        if (fallback.isEmpty()) fallback = candidateIndices;
        chosen = fallback[rng.nextInt(fallback.size())];
    }

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
        // Planned field: none; App will choose target part != current at trigger time
        modified.description = base.description + " -> Switch Part";
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
    return modified;
}

juce::Array<int> ModifierScheduler::selectTargetBuffers(const ModifierDescriptor& desc) const
{
    juce::Array<int> targets;
    if (desc.category == ModifierCategory::MasterEffect || desc.category == ModifierCategory::GlobalUtility)
        return targets; // Empty means 'master/global'

    // If user selected pads, use them; else select 1-4 weighted-random
    if (!userSelectedBuffers.isEmpty())
        return userSelectedBuffers;

    const juce::SpinLock::ScopedLockType lock(rngLock);

    // Build pool of eligible pads (padTargetProbability > 0)
    juce::Array<int>   eligible;
    juce::Array<float> weights;
    float totalWeight = 0.0f;

    for (int i = 0; i < 8; ++i)
    {
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

    int maxCount = juce::jmin (4, eligible.size());
    int count = rng.nextInt ({1, maxCount}); // 1..maxCount

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

    for (int v : unique) targets.add (v);
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
