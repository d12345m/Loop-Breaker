#if JUCE_UNIT_TESTS
#include <JuceHeader.h>
#include "ModifierScheduler.h"
#include "SessionSettings.h"
#include "AppState.h"
#include "TimeStretchSoundTouch.h"

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
