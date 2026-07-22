#if JUCE_UNIT_TESTS
#include <JuceHeader.h>
#include "ModifierScheduler.h"
#include "ModifierRegistry.h"
#include "SessionSettings.h"
#include "AppState.h"
#include "TimeStretchSoundTouch.h"

class ModifierRegistryTest : public juce::UnitTest
{
public:
    ModifierRegistryTest() : juce::UnitTest ("Modifier Registry") {}

    void runTest() override
    {
        beginTest ("Registry covers every serialized modifier exactly once");

        constexpr auto typeCount = static_cast<size_t> (ModifierType::Unknown);
        std::array<bool, typeCount> seen {};
        expectEquals (ModifierRegistry::entries().size(), typeCount);

        for (const auto& entry : ModifierRegistry::entries())
        {
            const auto index = static_cast<size_t> (entry.type);
            expect (index < typeCount, "Registry contains Unknown or an invalid type");
            if (index < typeCount)
            {
                expect (! seen[index], "Registry contains a duplicate type");
                seen[index] = true;
            }

            expect (juce::String (entry.displayName).isNotEmpty());
            expect (juce::String (entry.shortName).isNotEmpty());
            expect (juce::String (entry.categoryLabel).isNotEmpty());
            expect (juce::String (entry.description).isNotEmpty());
            expect (entry.representativeGlyphPhase01 >= 0.0f
                    && entry.representativeGlyphPhase01 <= 1.0f);
        }

        for (bool wasSeen : seen)
            expect (wasSeen, "Registry is missing a serialized modifier type");

        beginTest ("Prototype factory and eligibility metadata agree");

        auto prototypes = ModifierFactory::createAllPrototypes();
        int expectedPrototypeCount = 0;
        for (const auto& entry : ModifierRegistry::entries())
        {
            if (entry.prototypeEligible)
                ++expectedPrototypeCount;

            bool found = false;
            for (const auto* prototype : prototypes)
            {
                const auto descriptor = prototype->getDescriptor();
                if (descriptor.type != entry.type)
                    continue;

                found = true;
                expectEquals (descriptor.shortName, juce::String (entry.shortName));
                expectEquals (descriptor.description, juce::String (entry.description));
                expectEquals (static_cast<int> (descriptor.category),
                              static_cast<int> (entry.category));
            }
            expect (found == entry.prototypeEligible,
                    "Prototype presence does not match registry eligibility");
        }
        expectEquals (prototypes.size(), expectedPrototypeCount);

        beginTest ("Always-on ducking remains visible but is not scheduled");

        const auto& ducking = ModifierRegistry::get (ModifierType::BufferDuckingOn);
        expect (ducking.alwaysOn);
        expect (ducking.probabilityVisible);
        expect (! ducking.prototypeEligible);
        expect (! ducking.schedulerEligible);
    }
};

static ModifierRegistryTest modifierRegistryTestInstance;

class ModifierSchedulerPlannedQueueTest : public juce::UnitTest
{
public:
    ModifierSchedulerPlannedQueueTest() : juce::UnitTest ("ModifierScheduler Planned Queue") {}

    static bool sameTargets (const juce::Array<int>& a, const juce::Array<int>& b)
    {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }

    static bool samePlan (const PlannedModifier& a, const PlannedModifier& b)
    {
        return a.descriptor.type == b.descriptor.type
            && a.descriptor.description == b.descriptor.description
            && sameTargets (a.targets, b.targets);
    }

    struct CaptureListener : ModifierSchedulerListener
    {
        int triggerCount = 0;
        int cueCount = 0;
        ModifierDescriptor lastDescriptor;
        juce::Array<int> lastTargets;

        void modifierTriggered (const ModifierDescriptor& descriptor,
                                const juce::Array<int>& targets) override
        {
            ++triggerCount;
            lastDescriptor = descriptor;
            lastTargets = targets;
        }

        void musicalCueReached() override { ++cueCount; }
    };

    void runTest() override
    {
        beginTest ("Start fills a three-item queue with planned variants and targets");
        SessionSettings settings;
        ModifierScheduler scheduler (settings);
        scheduler.setRandomSeed (1234);
        scheduler.start();

        auto initial = scheduler.getPlannedQueueSnapshot();
        expectEquals (static_cast<int> (initial.size()), ModifierScheduler::kPlannedQueueDepth);
        for (const auto& planned : initial)
        {
            expect (planned.descriptor.type != ModifierType::Unknown);
            const auto& metadata = ModifierRegistry::get (planned.descriptor.type);
            expect (metadata.schedulerEligible);
            if (planned.descriptor.category != ModifierCategory::MasterEffect
                && planned.descriptor.category != ModifierCategory::GlobalUtility)
                expect (! planned.targets.isEmpty());
        }

        beginTest ("Skip pops the front and preserves already planned entries");
        scheduler.skipUpcoming();
        auto afterSkip = scheduler.getPlannedQueueSnapshot();
        expectEquals (static_cast<int> (afterSkip.size()), ModifierScheduler::kPlannedQueueDepth);
        expect (samePlan (afterSkip[0], initial[1]));
        expect (samePlan (afterSkip[1], initial[2]));

        beginTest ("Force replaces only the front queue entry");
        const auto beforeForce = afterSkip;
        scheduler.forceUpcomingModifier (ModifierType::ResetAll);
        const auto afterForce = scheduler.getPlannedQueueSnapshot();
        expectEquals (static_cast<int> (afterForce.size()), ModifierScheduler::kPlannedQueueDepth);
        expect (afterForce[0].descriptor.type == ModifierType::ResetAll);
        expect (samePlan (afterForce[1], beforeForce[1]));
        expect (samePlan (afterForce[2], beforeForce[2]));

        beginTest ("Debug forcing can replace an individual queued entry");
        const auto beforeQueuedForce = scheduler.getPlannedQueueSnapshot();
        scheduler.forcePlannedModifier (1, ModifierType::Reverse);
        const auto afterQueuedForce = scheduler.getPlannedQueueSnapshot();
        expect (samePlan (afterQueuedForce[0], beforeQueuedForce[0]));
        expect (afterQueuedForce[1].descriptor.type == ModifierType::Reverse);
        expect (samePlan (afterQueuedForce[2], beforeQueuedForce[2]));

        beginTest ("Target changes refresh the queue before the front item fires");
        scheduler.stop();
        scheduler.setUserSelectedBuffers ({ 1, 3 });
        scheduler.start();
        scheduler.setUserSelectedBuffers ({ 7 });
        const auto retargetedFront = scheduler.getPlannedQueueSnapshot().front();
        CaptureListener capture;
        scheduler.addListener (&capture);
        scheduler.triggerNow();
        expectEquals (capture.triggerCount, 1);
        expect (sameTargets (capture.lastTargets, retargetedFront.targets));
        expect (capture.lastTargets.size() == 1 && capture.lastTargets[0] == 7);
        expect (capture.lastDescriptor.description == retargetedFront.descriptor.description);
        scheduler.removeListener (&capture);

        beginTest ("Random targets are restricted to pads that contain audio");
        SessionSettings loadedSettings;
        ModifierScheduler loadedScheduler (loadedSettings);
        loadedScheduler.setAvailableTargetMask ((1u << 2u) | (1u << 5u));
        loadedScheduler.setRandomSeed (4422);
        loadedScheduler.start();
        for (const auto& planned : loadedScheduler.getPlannedQueueSnapshot())
            for (int target : planned.targets)
                expect (target == 2 || target == 5);

        beginTest ("Seeded queue planning is reproducible");
        SessionSettings settingsA;
        SessionSettings settingsB;
        ModifierScheduler schedulerA (settingsA);
        ModifierScheduler schedulerB (settingsB);
        schedulerA.setRandomSeed (8675309);
        schedulerB.setRandomSeed (8675309);
        schedulerA.start();
        schedulerB.start();
        const auto queueA = schedulerA.getPlannedQueueSnapshot();
        const auto queueB = schedulerB.getPlannedQueueSnapshot();
        expectEquals (queueA.size(), queueB.size());
        for (size_t i = 0; i < juce::jmin (queueA.size(), queueB.size()); ++i)
            expect (samePlan (queueA[i], queueB[i]));

        beginTest ("Probability changes affect only newly appended entries");
        auto beforeProbabilityChange = schedulerA.getPlannedQueueSnapshot();
        for (auto type : ModifierProbabilityManager::allModifierTypes())
            settingsA.modifierProbabilities.setWeight (type, 0.0f);
        settingsA.modifierProbabilities.setWeight (ModifierType::Reverse, 1.0f);
        schedulerA.skipUpcoming();
        auto afterProbabilityChange = schedulerA.getPlannedQueueSnapshot();
        expect (samePlan (afterProbabilityChange[0], beforeProbabilityChange[1]));
        expect (samePlan (afterProbabilityChange[1], beforeProbabilityChange[2]));
        expect (afterProbabilityChange[2].descriptor.type == ModifierType::Reverse);

        beginTest ("Suppression advances the queue without firing a modifier");
        const auto suppressedFront = schedulerB.getPlannedQueueSnapshot().front();
        CaptureListener suppressedCapture;
        schedulerB.addListener (&suppressedCapture);
        schedulerB.setSuppressed (true);
        schedulerB.triggerNow();
        const auto afterSuppressedTrigger = schedulerB.getPlannedQueueSnapshot();
        expectEquals (suppressedCapture.triggerCount, 0);
        expectEquals (suppressedCapture.cueCount, 1);
        expect (! samePlan (afterSuppressedTrigger.front(), suppressedFront));
        expect (samePlan (afterSuppressedTrigger.front(), queueB[1]));
        schedulerB.removeListener (&suppressedCapture);

        beginTest ("Zero probability produces an empty queue, never Unknown");
        SessionSettings mutedSettings;
        for (auto type : ModifierProbabilityManager::allModifierTypes())
            mutedSettings.modifierProbabilities.setWeight (type, 0.0f);
        mutedSettings.barsBetweenModifiers = 1;
        ModifierScheduler mutedScheduler (mutedSettings);
        CaptureListener mutedCapture;
        mutedScheduler.addListener (&mutedCapture);
        mutedScheduler.start();
        expect (mutedScheduler.getPlannedQueueSnapshot().empty());
        mutedScheduler.updateTime (mutedSettings.getSecondsPerBar());
        expectEquals (mutedCapture.triggerCount, 0);
        expectEquals (mutedCapture.cueCount, 1);
        expect (mutedScheduler.getPlannedQueueSnapshot().empty());
        mutedScheduler.removeListener (&mutedCapture);
    }
};

static ModifierSchedulerPlannedQueueTest modifierSchedulerPlannedQueueTestInstance;

class ModifierSchedulerQuantizeTest : public juce::UnitTest {
public:
    ModifierSchedulerQuantizeTest() : juce::UnitTest("ModifierScheduler Quantization") {}
    void runTest() override {
        beginTest("Quantized trigger snaps to expected beat grid");
        SessionSettings settings; // default: 120 BPM, 4/4, barsBetweenModifiers=4
        settings.barsBetweenModifiers = 1; // easier window
        ModifierScheduler scheduler(settings);
        scheduler.setRandomSeed(12345);
        scheduler.start();
        // Enable quantization to beats (4 per bar)
        scheduler.setQuantizationEnabled(true);
        scheduler.setQuantizationSubdivision(4);
        // Simulate audio time advancing in small increments until just before trigger
        double sr = 48000.0;
        // We'll advance half a beat then ensure trigger not yet fired prematurely
        double secondsPerBeat = settings.getSecondsPerBeat();
        double step = secondsPerBeat / 8.0; // 32nd note increments
        double accumulated = 0.0;
        bool triggeredEarly = false;
        while (accumulated < secondsPerBeat * 0.49) {
            scheduler.updateTime(step);
            accumulated += step;
            if (scheduler.getProgressToNextTrigger() >= 1.0) {
                triggeredEarly = true; break;
            }
        }
        expect(!triggeredEarly, "Scheduler triggered before quantized boundary");

        // Advance to just after beat boundary; ensure trigger occurs
        while (scheduler.getProgressToNextTrigger() < 1.0) {
            scheduler.updateTime(step);
        }
        // Force any pending trigger check
        scheduler.updateTime(0.0);
        // After trigger, progress should reset near 0
        expect(scheduler.getProgressToNextTrigger() < 0.2, "Progress did not reset after trigger");

        beginTest("Changing subdivision resnaps future trigger");
        scheduler.setQuantizationSubdivision(8); // finer grid
        double before = scheduler.getSecondsUntilNextTrigger();
        scheduler.setQuantizationSubdivision(16); // even finer
        double after = scheduler.getSecondsUntilNextTrigger();
        expect(after <= before + 0.5, "Resnap should not push trigger excessively far");
    }
};

static ModifierSchedulerQuantizeTest modifierSchedulerQuantizeTestInstance;

#include "Modifier.h"

class ModifierSchedulerVariantsTest : public juce::UnitTest {
public:
    ModifierSchedulerVariantsTest() : juce::UnitTest("ModifierScheduler Variants") {}
    void runTest() override {
        beginTest("forceUpcomingVariant sets structured fields (Speed)");
        {
            SessionSettings settings;
            ModifierScheduler scheduler(settings);
            scheduler.setRestrictToImplemented(true);
            scheduler.forceUpcomingVariant(ModifierType::Speed, "2.0");
            auto upcoming = scheduler.getUpcomingModifier();
            expect(upcoming.has_value(), "No upcoming after forceUpcomingVariant");
            if (upcoming.has_value()) {
                expectEquals((int)upcoming->type, (int)ModifierType::Speed, "Type mismatch");
                expect(upcoming->plannedSpeed.has_value(), "plannedSpeed not set");
                if (upcoming->plannedSpeed.has_value())
                    expectWithinAbsoluteError(upcoming->plannedSpeed.value(), 2.0, 1e-9, "plannedSpeed value incorrect");
            }
        }

        beginTest("forceUpcomingVariant sets structured fields (Stretch)");
        {
            SessionSettings settings;
            ModifierScheduler scheduler(settings);
            scheduler.setRestrictToImplemented(true);
            scheduler.forceUpcomingVariant(ModifierType::Stretch, "0.5");
            auto upcoming = scheduler.getUpcomingModifier();
            expect(upcoming.has_value(), "No upcoming after forceUpcomingVariant");
            if (upcoming.has_value()) {
                expectEquals((int)upcoming->type, (int)ModifierType::Stretch, "Type mismatch");
                expect(upcoming->plannedStretch.has_value(), "plannedStretch not set");
                if (upcoming->plannedStretch.has_value())
                    expectWithinAbsoluteError(upcoming->plannedStretch.value(), 0.5, 1e-9, "plannedStretch value incorrect");
            }
        }

        beginTest("forceUpcomingVariant sets structured fields (BeatSliceRandom)");
        {
            SessionSettings settings;
            ModifierScheduler scheduler(settings);
            scheduler.setRestrictToImplemented(true);
            scheduler.forceUpcomingVariant(ModifierType::BeatSliceRandom, "1/16");
            auto upcoming = scheduler.getUpcomingModifier();
            expect(upcoming.has_value(), "No upcoming after forceUpcomingVariant");
            if (upcoming.has_value()) {
                expectEquals((int)upcoming->type, (int)ModifierType::BeatSliceRandom, "Type mismatch");
                expect(upcoming->plannedSliceDivision.isNotEmpty(), "plannedSliceDivision not set");
                if (upcoming->plannedSliceDivision.isNotEmpty())
                    expectEquals(upcoming->plannedSliceDivision, juce::String("1/16"), "plannedSliceDivision value incorrect");
            }
        }

        beginTest("selectNextModifier plans structured variants when applicable");
        {
            SessionSettings settings;
            settings.barsBetweenModifiers = 1;
            ModifierScheduler scheduler(settings);
            scheduler.setRestrictToImplemented(true);
            scheduler.setRandomSeed(42);
            scheduler.start();
            // Try a few rounds to encounter Speed, Stretch, and BeatSliceRandom
            bool sawSpeedWithPlanned = false;
            bool sawStretchWithPlanned = false;
            bool sawSliceWithPlanned = false;
            for (int i = 0; i < 40 && (!sawSpeedWithPlanned || !sawStretchWithPlanned || !sawSliceWithPlanned); ++i) {
                auto up = scheduler.getUpcomingModifier();
                if (up.has_value()) {
                    if (up->type == ModifierType::Speed)
                        sawSpeedWithPlanned |= up->plannedSpeed.has_value();
                    else if (up->type == ModifierType::Stretch)
                        sawStretchWithPlanned |= up->plannedStretch.has_value();
                    else if (up->type == ModifierType::BeatSliceRandom)
                        sawSliceWithPlanned |= up->plannedSliceDivision.isNotEmpty();
                }
                // advance to next selection quickly
                scheduler.updateTime(settings.getSecondsBetweenModifiers() + 0.001);
            }
            expect(sawSpeedWithPlanned, "Did not encounter Speed with plannedSpeed set");
            expect(sawStretchWithPlanned, "Did not encounter Stretch with plannedStretch set");
            expect(sawSliceWithPlanned, "Did not encounter BeatSliceRandom with plannedSliceDivision set");
        }
    }
};

static ModifierSchedulerVariantsTest modifierSchedulerVariantsTestInstance;

// New test: ensure AppState handles multiple simultaneous targets without crashes.
class AppStateMultipleTargetsTest : public juce::UnitTest {
public:
    AppStateMultipleTargetsTest() : juce::UnitTest("AppState Multiple Targets Safety") {}

    void runTest() override {
        beginTest("Applying Reverse and Speed to multiple targets is safe (no crash)");
        AppState app; // constructs ChannelStrips and hooks scheduler listener
        // Construct a descriptor for Reverse
        ModifierDescriptor reverseDesc;
        reverseDesc.type = ModifierType::Reverse;
        reverseDesc.shortName = "Reverse";
        juce::Array<int> targets; targets.addArray({0,1,2,3});
        // Call directly on AppState listener implementation
        app.modifierTriggered(reverseDesc, targets);

        // Speed with planned value
        ModifierDescriptor speedDesc;
        speedDesc.type = ModifierType::Speed;
        speedDesc.shortName = "Speed";
        speedDesc.plannedSpeed = 2.0;
        app.modifierTriggered(speedDesc, targets);

        // Stretch with planned value
        ModifierDescriptor stretchDesc;
        stretchDesc.type = ModifierType::Stretch;
        stretchDesc.shortName = "Stretch";
        stretchDesc.plannedStretch = 0.5;
        app.modifierTriggered(stretchDesc, targets);

        // If we reached here, consider it pass (no assertions to check engine state without audio loaded)
        expect(true);
    }
};

static AppStateMultipleTargetsTest appStateMultipleTargetsTestInstance;

class ModifierSchedulerEveryNBarsTest : public juce::UnitTest {
public:
    ModifierSchedulerEveryNBarsTest() : juce::UnitTest("ModifierScheduler Every N Bars") {}
    
    struct CaptureListener : public ModifierSchedulerListener {
        ModifierScheduler& sched;
        juce::Array<double> triggerBars;
        explicit CaptureListener(ModifierScheduler& s) : sched(s) {}
        void modifierTriggered(const ModifierDescriptor&, const juce::Array<int>&) override {
            triggerBars.add(sched.getAccumulatedBars());
        }
    };

    void runTest() override {
        beginTest("Triggers fire exactly every N bars within < 1 block tolerance");
        SessionSettings settings; // default 4/4, 120 BPM
        settings.barsBetweenModifiers = 2; // test interval
        ModifierScheduler scheduler(settings);
        scheduler.setQuantizationEnabled(false); // base periodic mode
        scheduler.setRandomSeed(42);
        CaptureListener listener(scheduler);
        scheduler.addListener(&listener);
        scheduler.start();

        const double secondsPerBar = settings.getSecondsPerBar();
        const double step = secondsPerBar / 256.0; // ~7.8ms at 120BPM; treat as "one block"

        const int triggersToCapture = 6;
        while (listener.triggerBars.size() < triggersToCapture) {
            scheduler.updateTime(step);
        }

        // Expect first trigger at N bars, then every N bars afterward
        const double expectedIntervalBars = (double) settings.barsBetweenModifiers;
        const double toleranceBars = step / secondsPerBar + 1e-6; // < one step worth of bars

        // First trigger
        expectWithinAbsoluteError(listener.triggerBars[0], expectedIntervalBars, toleranceBars,
                                  "First trigger not at N bars");

        for (int i = 1; i < listener.triggerBars.size(); ++i) {
            double interval = listener.triggerBars[i] - listener.triggerBars[i-1];
            expectWithinAbsoluteError(interval, expectedIntervalBars, toleranceBars,
                                      "Subsequent trigger not spaced by N bars");
        }

        scheduler.removeListener(&listener);
        scheduler.stop();
    }
};

static ModifierSchedulerEveryNBarsTest modifierSchedulerEveryNBarsTestInstance;

class SoundTouchSmokeTest : public juce::UnitTest {
public:
    SoundTouchSmokeTest() : juce::UnitTest("SoundTouch Smoke") {}

    void runTest() override {
        beginTest("SoundTouch can time-stretch a mono buffer (basic output)");

        // Keep the test tiny & deterministic. This is mainly a compile/link smoke test.
        TimeStretchSoundTouch ts;
        ts.prepare(48000.0, 1);
        ts.setTempoRatio(0.5f); // slower => longer

        juce::HeapBlock<float> in(2048);
        juce::HeapBlock<float> out(8192);
        for (int i = 0; i < 2048; ++i)
            in[i] = std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * (double)i / 48000.0);

        const int received = ts.processMono(in.getData(), 2048, out.getData(), 8192, true);
        expect(received > 0, "Expected SoundTouch to produce some output after flush");
    }
};

static SoundTouchSmokeTest soundTouchSmokeTestInstance;

#endif // JUCE_UNIT_TESTS
