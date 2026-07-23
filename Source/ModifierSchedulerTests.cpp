#if JUCE_UNIT_TESTS
#include <JuceHeader.h>
#include "ModifierScheduler.h"
#include "ModifierRegistry.h"
#include "ModifierVariantFormatter.h"
#include "SessionSettings.h"
#include "AppState.h"
#include "PluginProcessor.h"
#include "TimeStretchSoundTouch.h"
#include "UpcomingModifierDisplay.h"

class ModifierRegistryTest : public juce::UnitTest
{
public:
    ModifierRegistryTest() : juce::UnitTest ("Modifier Registry", "LoopBreaker") {}

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

        beginTest ("Retired master filters retain IDs but are hidden and unschedulable");

        expectEquals (static_cast<int> (ModifierProbabilityManager::allModifierTypes().size()),
                      static_cast<int> (ModifierType::Unknown));

        const auto& visibleTypes = ModifierProbabilityManager::visibleModifierTypes();
        for (auto type : { ModifierType::MasterHighPassOn, ModifierType::MasterLowPassOn })
        {
            const auto& metadata = ModifierRegistry::get (type);
            expect (metadata.prototypeEligible, "Legacy descriptor must remain constructible");
            expect (! metadata.schedulerEligible);
            expect (! metadata.probabilityVisible);
            expect (std::find (visibleTypes.begin(), visibleTypes.end(), type) == visibleTypes.end());
            expectEquals (static_cast<int> (ModifierProbabilityManager::allModifierTypes()[static_cast<size_t> (type)]),
                          static_cast<int> (type));
        }
    }
};

static ModifierRegistryTest modifierRegistryTestInstance;

class ModifierVariantFormatterTest : public juce::UnitTest
{
public:
    ModifierVariantFormatterTest() : juce::UnitTest ("Modifier Variant Formatter", "LoopBreaker") {}

    void runTest() override
    {
        beginTest ("Structured slice and sample-hold plans have concise labels");
        auto arp = ModifierRegistry::makeDescriptor (ModifierType::ArpSlice);
        arp.plannedArpSequenceLength = 4;
        arp.plannedArpTotalSlices = 32;
        arp.plannedArpRepeatBars = 2;
        expectEquals (ModifierVariantFormatter::full (arp), juce::String ("4 STEP  /  32 GRID  /  2 BARS"));
        expectEquals (ModifierVariantFormatter::compact (arp), juce::String ("4/32"));

        auto sampleHold = ModifierRegistry::makeDescriptor (ModifierType::BufferSHLowPassOn);
        sampleHold.plannedSHDivisionBars = 0.125;
        expectEquals (ModifierVariantFormatter::full (sampleHold), juce::String ("HOLD 1/8"));

        auto volumeRamp = ModifierRegistry::makeDescriptor (ModifierType::BufferVolumeRampDown);
        volumeRamp.plannedFxFadeBars = 2.0;
        volumeRamp.plannedVolumeHoldBars = 3.0;
        expectEquals (ModifierVariantFormatter::full (volumeRamp),
                      juce::String ("FADE 2 BARS  /  HOLD 3 BARS"));

        beginTest ("Switch Part exposes its frozen destination");
        auto switchPart = ModifierRegistry::makeDescriptor (ModifierType::SwitchPart);
        switchPart.plannedDestinationPart = 2;
        expectEquals (ModifierVariantFormatter::full (switchPart), juce::String ("PART C"));
        expectEquals (ModifierVariantFormatter::compact (switchPart), juce::String ("PART C"));
    }
};

static ModifierVariantFormatterTest modifierVariantFormatterTestInstance;

class SessionUiGeometryTest : public juce::UnitTest
{
public:
    SessionUiGeometryTest() : juce::UnitTest ("Session UI Geometry", "LoopBreaker") {}

    void runTest() override
    {
        beginTest ("Progress bar has no inactive track");
        UpcomingModifierDisplay display;
        juce::Image image (juce::Image::ARGB, 120, 8, true);
        juce::Graphics graphics (image);
        const juce::Rectangle<float> progressBounds (10.0f, 2.0f, 100.0f, 3.0f);

        display.setCountdown (1.0, 1.0, 0.0);
        display.paintProgressBar (graphics, progressBounds);
        expectEquals (static_cast<int> (image.getPixelAt (12, 2).getAlpha()), 0);
        expectEquals (static_cast<int> (image.getPixelAt (108, 2).getAlpha()), 0);

        display.setCountdown (1.0, 1.0, 0.5);
        display.paintProgressBar (graphics, progressBounds);
        expect (image.getPixelAt (12, 2).getAlpha() > 0);
        expect (image.getPixelAt (12, 4).getAlpha() > 0);
        expectEquals (static_cast<int> (image.getPixelAt (108, 2).getAlpha()), 0);

        beginTest ("Four-column geometry is contiguous and evenly distributed");
        const juce::Rectangle<int> area (7, 3, 517, 40);
        int narrowest = area.getWidth();
        int widest = 0;
        int expectedLeft = area.getX();

        for (int column = 0; column < 4; ++column)
        {
            const auto cell = UiGridLayout::equalColumn (area, column, 4);
            expectEquals (cell.getX(), expectedLeft);
            expectedLeft = cell.getRight();
            narrowest = juce::jmin (narrowest, cell.getWidth());
            widest = juce::jmax (widest, cell.getWidth());
        }

        expectEquals (expectedLeft, area.getRight());
        expect (widest - narrowest <= 1);
    }
};

static SessionUiGeometryTest sessionUiGeometryTestInstance;

class ModifierSchedulerPlannedQueueTest : public juce::UnitTest
{
public:
    ModifierSchedulerPlannedQueueTest() : juce::UnitTest ("ModifierScheduler Planned Queue", "LoopBreaker") {}

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

        beginTest ("Switch Part destinations stay explicit and advance through queue order");
        SessionSettings switchSettings;
        switchSettings.parts.numParts = 4;
        switchSettings.parts.activePart = 0;
        ModifierScheduler switchScheduler (switchSettings);
        switchScheduler.setRandomSeed (4321);
        switchScheduler.start();
        switchScheduler.forcePlannedModifier (0, ModifierType::SwitchPart);
        switchScheduler.forcePlannedModifier (1, ModifierType::SwitchPart);
        auto switchQueue = switchScheduler.getPlannedQueueSnapshot();
        expect (switchQueue[0].descriptor.plannedDestinationPart.has_value());
        expect (switchQueue[1].descriptor.plannedDestinationPart.has_value());
        expectEquals (*switchQueue[0].descriptor.plannedDestinationPart, 1);
        expectEquals (*switchQueue[1].descriptor.plannedDestinationPart, 2);

        switchSettings.parts.activePart = 2;
        switchScheduler.refreshPlannedPartDestinations();
        switchQueue = switchScheduler.getPlannedQueueSnapshot();
        expectEquals (*switchQueue[0].descriptor.plannedDestinationPart, 3);
        expectEquals (*switchQueue[1].descriptor.plannedDestinationPart, 0);

        beginTest ("Swap Stack uses exactly the live user selection");
        SessionSettings swapSettings;
        ModifierScheduler swapScheduler (swapSettings);
        swapScheduler.setAvailableTargetMask ((1u << 0u) | (1u << 2u) | (1u << 5u));
        swapScheduler.setUserSelectedBuffers ({ 0 });
        swapScheduler.setRandomSeed (2468);
        swapScheduler.start();
        swapScheduler.forceUpcomingModifier (ModifierType::SwapModifierStack);
        expect (swapScheduler.getPlannedQueueSnapshot().front().targets.isEmpty());

        swapScheduler.setUserSelectedBuffers ({ 5, 0, 2 });
        const auto swapFront = swapScheduler.getPlannedQueueSnapshot().front();
        expectEquals (swapFront.targets.size(), 3);
        expectEquals (swapFront.targets[0], 5);
        expectEquals (swapFront.targets[1], 0);
        expectEquals (swapFront.targets[2], 2);

        swapScheduler.setUserSelectedBuffers ({ 2, 5 });
        const auto lastMomentSwapFront = swapScheduler.getPlannedQueueSnapshot().front();
        expectEquals (lastMomentSwapFront.targets.size(), 2);
        expectEquals (lastMomentSwapFront.targets[0], 2);
        expectEquals (lastMomentSwapFront.targets[1], 5);

        CaptureListener swapCapture;
        swapScheduler.addListener (&swapCapture);
        swapScheduler.triggerNow();
        expectEquals (swapCapture.triggerCount, 1);
        expect (sameTargets (swapCapture.lastTargets, lastMomentSwapFront.targets));
        swapScheduler.removeListener (&swapCapture);

        beginTest ("Swap Stack has no target pips when fewer than two pads are available");
        SessionSettings insufficientSwapSettings;
        ModifierScheduler insufficientSwapScheduler (insufficientSwapSettings);
        insufficientSwapScheduler.setAvailableTargetMask (1u << 3u);
        insufficientSwapScheduler.setUserSelectedBuffers ({ 3 });
        insufficientSwapScheduler.start();
        insufficientSwapScheduler.forceUpcomingModifier (ModifierType::SwapModifierStack);
        expect (insufficientSwapScheduler.getPlannedQueueSnapshot().front().targets.isEmpty());

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
        scheduler.forceUpcomingModifier (ModifierType::Reverse);
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

        beginTest ("Probability changes rebuild every queue position");
        for (auto type : ModifierProbabilityManager::allModifierTypes())
            settingsA.modifierProbabilities.setWeight (type, 0.0f);
        settingsA.modifierProbabilities.setWeight (ModifierType::Reverse, 1.0f);
        schedulerA.refreshPlannedQueueForProbabilityChange();
        const auto afterProbabilityChange = schedulerA.getPlannedQueueSnapshot();
        expectEquals (static_cast<int> (afterProbabilityChange.size()),
                      ModifierScheduler::kPlannedQueueDepth);
        for (const auto& planned : afterProbabilityChange)
            expect (planned.descriptor.type == ModifierType::Reverse);

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

        beginTest ("Live probability changes clear and fully repopulate the queue");
        SessionSettings liveProbabilitySettings;
        ModifierScheduler liveProbabilityScheduler (liveProbabilitySettings);
        liveProbabilityScheduler.setRandomSeed (9753);
        liveProbabilityScheduler.start();
        expectEquals (static_cast<int> (liveProbabilityScheduler.getPlannedQueueSnapshot().size()),
                      ModifierScheduler::kPlannedQueueDepth);

        for (auto type : ModifierProbabilityManager::allModifierTypes())
            liveProbabilitySettings.modifierProbabilities.setWeight (type, 0.0f);
        liveProbabilityScheduler.refreshPlannedQueueForProbabilityChange();
        expect (liveProbabilityScheduler.getPlannedQueueSnapshot().empty());
        expect (! liveProbabilityScheduler.getUpcomingModifier().has_value());

        liveProbabilitySettings.modifierProbabilities.setWeight (ModifierType::Reverse, 1.0f);
        liveProbabilityScheduler.refreshPlannedQueueForProbabilityChange();
        const auto repopulatedQueue = liveProbabilityScheduler.getPlannedQueueSnapshot();
        expectEquals (static_cast<int> (repopulatedQueue.size()),
                      ModifierScheduler::kPlannedQueueDepth);
        for (const auto& planned : repopulatedQueue)
            expect (planned.descriptor.type == ModifierType::Reverse);

        beginTest ("Retired master filters cannot fill the scheduler queue");
        SessionSettings retiredSettings;
        for (auto type : ModifierProbabilityManager::allModifierTypes())
            retiredSettings.modifierProbabilities.setWeight (type, 0.0f);
        retiredSettings.modifierProbabilities.setWeight (ModifierType::MasterHighPassOn, 1.0f);
        retiredSettings.modifierProbabilities.setWeight (ModifierType::MasterLowPassOn, 1.0f);
        ModifierScheduler retiredScheduler (retiredSettings);
        retiredScheduler.start();
        expect (retiredScheduler.getPlannedQueueSnapshot().empty());
    }
};

static ModifierSchedulerPlannedQueueTest modifierSchedulerPlannedQueueTestInstance;

class MidiAssignableControlTest : public juce::UnitTest
{
public:
    MidiAssignableControlTest() : juce::UnitTest ("MIDI Assignable Controls", "LoopBreaker") {}

    struct ResizeCounter : juce::ComponentListener
    {
        void componentMovedOrResized (juce::Component&, bool wasMoved,
                                      bool wasResized) override
        {
            juce::ignoreUnused (wasMoved);
            if (wasResized)
                ++count;
        }

        int count = 0;
    };

    static juce::ComboBox* findLayoutCombo (juce::Component* parent)
    {
        if (auto* combo = dynamic_cast<juce::ComboBox*> (parent))
            if (combo->getNumItems() == 2
                && combo->getItemText (1) == "Portrait 9:16")
                return combo;

        for (int i = 0; i < parent->getNumChildComponents(); ++i)
            if (auto* result = findLayoutCombo (parent->getChildComponent (i)))
                return result;

        return nullptr;
    }

    static void processController (BufferTestAudioProcessor& processor, int cc, int value)
    {
        juce::AudioBuffer<float> audio (processor.getTotalNumOutputChannels(), 64);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 0);
        processor.processBlock (audio, midi);
    }

    static void processNoteOn (BufferTestAudioProcessor& processor, int note, float velocity = 1.0f)
    {
        juce::AudioBuffer<float> audio (processor.getTotalNumOutputChannels(), 64);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
        processor.processBlock (audio, midi);
    }

    void runTest() override
    {
        BufferTestAudioProcessor processor;
        processor.prepareToPlay (48000.0, 64);

        beginTest ("Probability actions learn notes and fire on Note On like pads");
        processor.setMidiLearnMode (
            true, BufferTestAudioProcessor::kProbabilityActionLearnIndexBase
                      + BufferTestAudioProcessor::kProbabilityActionZero);
        processNoteOn (processor, 70);
        expectEquals (processor.getAppState().settings.midiProbabilityActionNoteMap[0], 70);
        expect (! processor.isMidiLearnEnabled());

        processNoteOn (processor, 70);
        const auto firstType = ModifierProbabilityManager::allModifierTypes().front();
        const juce::String firstProbabilityId =
            "prob_" + juce::String (static_cast<int> (firstType));
        auto* firstProbability = processor.getAPVTS().getParameter (firstProbabilityId);
        expect (firstProbability != nullptr);
        if (firstProbability != nullptr)
            expectWithinAbsoluteError (firstProbability->getValue(), 0.0f, 0.001f);

        if (firstProbability != nullptr)
            firstProbability->setValueNotifyingHost (1.0f);
        processNoteOn (processor, 70, 0.25f);
        if (firstProbability != nullptr)
            expectWithinAbsoluteError (firstProbability->getValue(), 0.0f, 0.001f);

        beginTest ("Master volume learns a CC and follows its continuous value");
        processor.setMidiControlCCLearnTarget (BufferTestAudioProcessor::kMasterVolumeCCLearnTarget);
        processController (processor, 71, 64);
        expectEquals (processor.getAppState().settings.masterVolumeMidiCC, 71);
        processController (processor, 71, 127);
        if (auto* value = processor.getAPVTS().getRawParameterValue ("masterVolume"))
            expectWithinAbsoluteError (value->load(), 12.0f, 0.001f);
        else
            expect (false, "Master-volume parameter is missing");

        beginTest ("Selecting portrait layout performs one resize and keeps tabs visible");
        std::unique_ptr<juce::AudioProcessorEditor> layoutEditor (processor.createEditor());
        for (int i = 0; i < layoutEditor->getNumChildComponents(); ++i)
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (
                    layoutEditor->getChildComponent (i)))
                tabs->setCurrentTabIndex (2);

        auto* layoutCombo = findLayoutCombo (layoutEditor.get());
        expect (layoutCombo != nullptr);

        ResizeCounter resizeCounter;
        layoutEditor->addComponentListener (&resizeCounter);
        if (layoutCombo != nullptr)
            layoutCombo->setSelectedId (2, juce::sendNotificationSync);
        layoutEditor->removeComponentListener (&resizeCounter);

        expectEquals (layoutEditor->getWidth(), 540);
        expectEquals (layoutEditor->getHeight(), 960);
        expectEquals (resizeCounter.count, 1);

        bool dynamicTabBarVisible = false;
        for (int i = 0; i < layoutEditor->getNumChildComponents(); ++i)
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (
                    layoutEditor->getChildComponent (i)))
            {
                auto& tabBar = tabs->getTabbedButtonBar();
                auto* currentButton = tabBar.getTabButton (
                    tabBar.getCurrentTabIndex());
                dynamicTabBarVisible = tabBar.isVisible()
                                    && tabBar.getHeight() > 0
                                    && currentButton != nullptr
                                    && currentButton->isVisible()
                                    && ! currentButton->getBounds().isEmpty();
            }
        expect (dynamicTabBarVisible);
        layoutEditor.reset();

        beginTest ("MIDI mappings and portrait layout persist in plugin state");
        processor.getAppState().settings.windowLayoutMode = WindowLayoutMode::Portrait9x16;
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        BufferTestAudioProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        expectEquals (restored.getAppState().settings.midiProbabilityActionNoteMap[0], 70);
        expectEquals (restored.getAppState().settings.masterVolumeMidiCC, 71);
        expectEquals (static_cast<int> (restored.getAppState().settings.windowLayoutMode),
                      static_cast<int> (WindowLayoutMode::Portrait9x16));

        std::unique_ptr<juce::AudioProcessorEditor> portraitEditor (restored.createEditor());
        expectEquals (portraitEditor->getWidth(), 540);
        expectEquals (portraitEditor->getHeight(), 960);
        bool hasCornerResizer = false;
        bool hasVisibleTabBar = false;
        for (int i = 0; i < portraitEditor->getNumChildComponents(); ++i)
        {
            auto* child = portraitEditor->getChildComponent (i);
            hasCornerResizer = hasCornerResizer
                || dynamic_cast<juce::ResizableCornerComponent*> (child) != nullptr;
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (child))
                hasVisibleTabBar = tabs->getTabbedButtonBar().isVisible()
                                && tabs->getTabbedButtonBar().getHeight() > 0;
        }
        expect (! hasCornerResizer);
        expect (hasVisibleTabBar);

        std::function<UpcomingModifierDisplay* (juce::Component*)> findModifierDisplay;
        findModifierDisplay = [&findModifierDisplay] (juce::Component* parent)
            -> UpcomingModifierDisplay*
        {
            if (auto* display = dynamic_cast<UpcomingModifierDisplay*> (parent))
                return display;
            for (int i = 0; i < parent->getNumChildComponents(); ++i)
                if (auto* found = findModifierDisplay (parent->getChildComponent (i)))
                    return found;
            return nullptr;
        };

        auto* modifierDisplay = findModifierDisplay (portraitEditor.get());
        expect (modifierDisplay != nullptr);
        if (modifierDisplay != nullptr)
        {
            const auto baseBounds = modifierDisplay->getBounds();
            portraitEditor->setSize (1080, 1920);
            const auto scaledBounds = modifierDisplay->getBounds();
            expect (scaledBounds.getWidth() >= juce::roundToInt (baseBounds.getWidth() * 1.9f));
            expect (scaledBounds.getHeight() >= juce::roundToInt (baseBounds.getHeight() * 1.9f));
        }

        processor.releaseResources();
    }
};

static MidiAssignableControlTest midiAssignableControlTestInstance;

class ModifierSchedulerQuantizeTest : public juce::UnitTest {
public:
    ModifierSchedulerQuantizeTest() : juce::UnitTest("ModifierScheduler Quantization", "LoopBreaker") {}
    struct Capture : ModifierSchedulerListener
    {
        int triggerCount = 0;
        void modifierTriggered (const ModifierDescriptor&, const juce::Array<int>&) override
        {
            ++triggerCount;
        }
    };

    void runTest() override {
        beginTest("Quantized trigger snaps to expected beat grid");
        SessionSettings settings; // default: 120 BPM, 4/4, barsBetweenModifiers=4
        settings.barsBetweenModifiers = 1; // easier window
        ModifierScheduler scheduler(settings);
        scheduler.setRandomSeed(12345);
        Capture capture;
        scheduler.addListener (&capture);
        scheduler.start();
        // Enable quantization to beats (4 per bar)
        scheduler.setQuantizationEnabled(true);
        scheduler.setQuantizationSubdivision(4);
        // Simulate audio time advancing in small increments until just before trigger
        // We'll advance half a beat then ensure trigger not yet fired prematurely
        double secondsPerBeat = settings.getSecondsPerBeat();
        double step = secondsPerBeat / 8.0; // 32nd note increments
        double accumulated = 0.0;
        bool triggeredEarly = false;
        while (accumulated < secondsPerBeat * 0.49) {
            scheduler.updateTime(step);
            accumulated += step;
            if (capture.triggerCount > 0) {
                triggeredEarly = true; break;
            }
        }
        expect(!triggeredEarly, "Scheduler triggered before quantized boundary");

        // Advance to just after beat boundary; ensure trigger occurs
        for (int i = 0; i < 64 && capture.triggerCount == 0; ++i)
            scheduler.updateTime(step);
        expectEquals (capture.triggerCount, 1, "Scheduler did not trigger at the quantized boundary");
        // After trigger, progress should reset near 0
        expect(scheduler.getProgressToNextTrigger() < 0.2, "Progress did not reset after trigger");

        beginTest("Changing subdivision resnaps future trigger");
        scheduler.setQuantizationSubdivision(8); // finer grid
        double before = scheduler.getSecondsUntilNextTrigger();
        scheduler.setQuantizationSubdivision(16); // even finer
        double after = scheduler.getSecondsUntilNextTrigger();
        expect(after <= before + 0.5, "Resnap should not push trigger excessively far");
        scheduler.removeListener (&capture);
    }
};

static ModifierSchedulerQuantizeTest modifierSchedulerQuantizeTestInstance;

#include "Modifier.h"

class ModifierSchedulerVariantsTest : public juce::UnitTest {
public:
    ModifierSchedulerVariantsTest() : juce::UnitTest("ModifierScheduler Variants", "LoopBreaker") {}
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

        auto planOnly = [] (ModifierType type, int seed)
        {
            SessionSettings settings;
            for (auto candidate : ModifierProbabilityManager::allModifierTypes())
                settings.modifierProbabilities.setWeight (candidate, 0.0f);
            settings.modifierProbabilities.setWeight (type, 1.0f);

            ModifierScheduler scheduler (settings);
            scheduler.setRestrictToImplemented (true);
            scheduler.setRandomSeed (seed);
            scheduler.start();
            return scheduler.getUpcomingModifier();
        };

        beginTest ("Queue planning freezes Speed variant");
        const auto speed = planOnly (ModifierType::Speed, 42);
        expect (speed.has_value());
        if (speed.has_value())
        {
            expectEquals ((int) speed->type, (int) ModifierType::Speed);
            expect (speed->plannedSpeed.has_value());
        }

        beginTest ("Queue planning freezes Stretch variant");
        const auto stretch = planOnly (ModifierType::Stretch, 43);
        expect (stretch.has_value());
        if (stretch.has_value())
        {
            expectEquals ((int) stretch->type, (int) ModifierType::Stretch);
            expect (stretch->plannedStretch.has_value());
        }

        beginTest ("Queue planning freezes Beat Slice division");
        const auto slice = planOnly (ModifierType::BeatSliceRandom, 44);
        expect (slice.has_value());
        if (slice.has_value())
        {
            expectEquals ((int) slice->type, (int) ModifierType::BeatSliceRandom);
            expect (slice->plannedSliceDivision.isNotEmpty());
        }

        beginTest ("Queue planning freezes Volume Ramp timing");
        const auto volumeRamp = planOnly (ModifierType::BufferVolumeRampDown, 45);
        expect (volumeRamp.has_value());
        if (volumeRamp.has_value())
        {
            expectEquals ((int) volumeRamp->type, (int) ModifierType::BufferVolumeRampDown);
            expect (volumeRamp->plannedFxFadeBars.has_value());
            expect (volumeRamp->plannedVolumeHoldBars.has_value());
        }
    }
};

static ModifierSchedulerVariantsTest modifierSchedulerVariantsTestInstance;

// New test: ensure AppState handles multiple simultaneous targets without crashes.
class AppStateMultipleTargetsTest : public juce::UnitTest {
public:
    AppStateMultipleTargetsTest() : juce::UnitTest("AppState Multiple Targets Safety", "LoopBreaker") {}

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
    ModifierSchedulerEveryNBarsTest() : juce::UnitTest("ModifierScheduler Every N Bars", "LoopBreaker") {}
    
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
    SoundTouchSmokeTest() : juce::UnitTest("SoundTouch Smoke", "LoopBreaker") {}

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
