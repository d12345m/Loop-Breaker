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

void ModifierScheduler::resetTimeline()
{
    // Reset accumulated time and trigger window boundaries
    accumulatedSecondsTotal = 0.0;
    lastTriggerAbsoluteSeconds = 0.0;
    nextTriggerAbsoluteSeconds = 0.0;
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
                juce::String frag(" -> Delay ");
                juce::String combo;
                if (! base.plannedDelayDivisions.isEmpty())
                    combo << base.plannedDelayDivisions.joinIntoString(",");
                if (base.plannedDelayWet.has_value())
                {
                    if (combo.isNotEmpty()) combo << " | ";
                    combo << (int)std::round(base.plannedDelayWet.value() * 100.0) << "%";
                }
                if (base.plannedDelayFeedback.has_value())
                {
                    if (combo.isNotEmpty()) combo << " | ";
                    combo << "FB " << (int)std::round(base.plannedDelayFeedback.value() * 100.0) << "%";
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
                || t == ModifierType::ResetAll
                || t == ModifierType::BeatSliceRandom
                || t == ModifierType::BufferReverbOn
                || t == ModifierType::BufferDelayOn
                || t == ModifierType::BufferDelayDubBurst
                || t == ModifierType::BufferLowPassOn
                || t == ModifierType::BufferHighPassOn
                || t == ModifierType::MasterLowPassOn
                || t == ModifierType::MasterHighPassOn
                || t == ModifierType::BufferTremolo
                || t == ModifierType::SwitchPart);
            if (!allowed) continue;
            // Gate SwitchPart: only selectable when more than one part is configured
            if (t == ModifierType::SwitchPart && !moreThanOnePart) continue;
            candidateIndices.add(i);
        }
    }
    // Fallback (when not restricting): include all descriptors but still gate SwitchPart by parts count
    if (candidateIndices.isEmpty())
    {
        for (int i = 0; i < prototypeCache.size(); ++i)
        {
            auto t = prototypeCache[i]->getDescriptor().type;
            if (t == ModifierType::SwitchPart && !moreThanOnePart)
                continue;
            candidateIndices.add(i);
        }
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
        // Choose number of slices directly from set {4,8,16,32,64}
        static const int sliceCounts[] { 4, 8, 16, 32, 64 };
        const juce::SpinLock::ScopedLockType lock(rngLock);
        int idx = rng.nextInt((int)std::size(sliceCounts));
        int chosen = sliceCounts[idx];
        // Store as a simple numeric label for reuse in apply
        modified.plannedSliceDivision = juce::String(chosen);
        modified.description = base.description + " -> " + juce::String(chosen) + " slices";
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
        modified.description = base.description + " -> Reverb " + juce::String((int)std::round(wet * 100.0)) + "% | " + fadeLabel;
    }
    else if (base.type == ModifierType::BufferDelayOn)
    {
        static const juce::StringArray divs { "1/4", "1/8", "1/8D", "1/8T", "1/16" };
        static const double wets[] { 0.25, 0.5, 0.75, 1.0 };
        static const double fbs[]  { 0.25, 0.5, 0.75 };
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
      parts << label << " | "
          << (int)std::round(wet * 100.0) << "%" << " | "
          << "FB " << (int)std::round(fb * 100.0) << "%" << " | "
          << fadeLabel;
      if (pp) parts << " | PP";
      if (wf) parts << " | WowFlutter";
        modified.description = base.description + " -> Delay " + parts;
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
        juce::String mode = jump ? "jump->decay" : "ramp up/down";
        juce::String name = (base.type == ModifierType::MasterLowPassOn ? "Master LPF" : "Master HPF");
        modified.description = base.description + " -> " + name + " " + juce::String((int)dur) + " bars" + " | " + mode;
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
