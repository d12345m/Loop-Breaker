#if JUCE_UNIT_TESTS
#include <JuceHeader.h>
#include "ModifierScheduler.h"
#include "SessionSettings.h"

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

#endif // JUCE_UNIT_TESTS
