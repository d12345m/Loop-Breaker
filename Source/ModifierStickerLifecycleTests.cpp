#if JUCE_UNIT_TESTS

#include <JuceHeader.h>

#include "AppState.h"
#include "ModifierStickerOverlay.h"

#include <cmath>

namespace
{
using StickerMask = ModifierStickerOverlay::Mask;

StickerMask bitFor (ModifierType type)
{
    return ModifierStickerOverlay::bitForType (type);
}

ModifierDescriptor descriptorFor (ModifierType type)
{
    ModifierDescriptor descriptor;
    descriptor.type = type;
    return descriptor;
}

juce::Array<int> targetsFor (std::initializer_list<int> indices)
{
    juce::Array<int> targets;
    for (const int index : indices)
        targets.add (index);
    return targets;
}

void loadSyntheticAudio (AppState& app, int padIndex)
{
    auto data = AudioBuffer::LoadedAudioData::Ptr (
        new AudioBuffer::LoadedAudioData());
    data->buffer.setSize (2, 2048);
    data->buffer.clear();
    data->sampleRate = 48000.0;
    data->fileName = "modifier-sticker-lifecycle-test.wav";

    if (auto* buffer = app.bufferManager.getBuffer (padIndex))
        buffer->setLoadedAudioData (data);
}

void advanceBars (AppState& app, double bars)
{
    app.advanceFxEnvelopes (app.settings.getSecondsPerBar() * bars);
}

bool maskContains (StickerMask mask, ModifierType type)
{
    return (mask & bitFor (type)) != 0;
}

class ModifierStickerLifecycleTest : public juce::UnitTest
{
public:
    ModifierStickerLifecycleTest()
        : juce::UnitTest ("Modifier Sticker Lifecycle", "LoopBreaker")
    {
    }

    void runTest() override
    {
        testPersistentEffects();
        testSwapMovesStickersAndTemporaryController();
        testReset();
        testNeutralTransforms();
        testTemporaryVolumeRamp();
        testDubBurstRestoresDelay();
        testGranularBurstRestoresGranular();
    }

private:
    void testPersistentEffects()
    {
        beginTest ("Persistent reverb and tremolo publish their sticker bits");

        AppState app;
        loadSyntheticAudio (app, 0);

        const auto target = targetsFor ({ 0 });
        auto reverb = descriptorFor (ModifierType::BufferReverbOn);
        reverb.plannedWet = 0.6;
        reverb.plannedFxFadeBars = 0.0;
        app.modifierTriggered (reverb, target);

        app.modifierTriggered (
            descriptorFor (ModifierType::BufferTremolo), target);

        const auto mask = app.getActiveModifierStickerMask (0);
        expect (maskContains (mask, ModifierType::BufferReverbOn),
                "Reverb should remain visible while enabled");
        expect (maskContains (mask, ModifierType::BufferTremolo),
                "Tremolo should remain visible while enabled");
    }

    void testSwapMovesStickersAndTemporaryController()
    {
        beginTest ("Swap Stack rotates stickers and their in-flight controller state");

        AppState app;
        loadSyntheticAudio (app, 0);
        loadSyntheticAudio (app, 1);

        const auto pad0 = targetsFor ({ 0 });
        const auto pad1 = targetsFor ({ 1 });

        auto reverb = descriptorFor (ModifierType::BufferReverbOn);
        reverb.plannedWet = 0.5;
        reverb.plannedFxFadeBars = 0.0;
        app.modifierTriggered (reverb, pad0);
        app.modifierTriggered (
            descriptorFor (ModifierType::BufferTremolo), pad1);

        auto volumeRamp = descriptorFor (ModifierType::BufferVolumeRampDown);
        volumeRamp.plannedFxFadeBars = 0.05;
        volumeRamp.plannedVolumeHoldBars = 0.05;
        app.modifierTriggered (volumeRamp, pad0);

        const auto before0 = app.getActiveModifierStickerMask (0);
        const auto before1 = app.getActiveModifierStickerMask (1);
        expect (maskContains (before0, ModifierType::BufferReverbOn));
        expect (maskContains (before0, ModifierType::BufferVolumeRampDown));
        expect (maskContains (before1, ModifierType::BufferTremolo));

        app.modifierTriggered (
            descriptorFor (ModifierType::SwapModifierStack),
            targetsFor ({ 0, 1 }));

        const auto after0 = app.getActiveModifierStickerMask (0);
        const auto after1 = app.getActiveModifierStickerMask (1);
        expect (maskContains (after0, ModifierType::BufferTremolo),
                "Pad 0 should receive pad 1's Tremolo sticker");
        expect (! maskContains (after0, ModifierType::BufferReverbOn));
        expect (! maskContains (after0, ModifierType::BufferVolumeRampDown));
        expect (maskContains (after1, ModifierType::BufferReverbOn),
                "Pad 1 should receive pad 0's Reverb sticker");
        expect (maskContains (after1, ModifierType::BufferVolumeRampDown),
                "The active Volume Ramp sticker should rotate with its stack");
        expect (! maskContains (after1, ModifierType::BufferTremolo));

        advanceBars (app, 0.05);
        advanceBars (app, 0.05);
        advanceBars (app, 0.05);

        const auto settled0 = app.getActiveModifierStickerMask (0);
        const auto settled1 = app.getActiveModifierStickerMask (1);
        expect (maskContains (settled0, ModifierType::BufferTremolo));
        expect (maskContains (settled1, ModifierType::BufferReverbOn));
        expect (! maskContains (settled1, ModifierType::BufferVolumeRampDown),
                "The moved Volume Ramp controller should expire on pad 1");
        expectWithinAbsoluteError (
            app.channelStrips[1]->getFxParams().volumeGain, 1.0f, 0.0001f);
    }

    void testReset()
    {
        beginTest ("Reset clears every active sticker on the targeted pad");

        AppState app;
        loadSyntheticAudio (app, 0);
        const auto target = targetsFor ({ 0 });

        auto reverb = descriptorFor (ModifierType::BufferReverbOn);
        reverb.plannedFxFadeBars = 0.0;
        app.modifierTriggered (reverb, target);
        app.modifierTriggered (
            descriptorFor (ModifierType::BufferTremolo), target);
        app.modifierTriggered (descriptorFor (ModifierType::Reverse), target);

        expect (app.getActiveModifierStickerMask (0) != 0);
        app.modifierTriggered (descriptorFor (ModifierType::ResetAll), target);
        expect (app.getActiveModifierStickerMask (0) == 0,
                "Reset should leave the pad with no active modifier stickers");
        advanceBars (app, 8.0);
        expect (app.getActiveModifierStickerMask (0) == 0,
                "Cancelled controllers must not resurrect stickers after Reset");
    }

    void testNeutralTransforms()
    {
        beginTest ("Transform stickers disappear when state returns to neutral");

        AppState app;
        loadSyntheticAudio (app, 0);
        const auto target = targetsFor ({ 0 });

        const auto reverse = descriptorFor (ModifierType::Reverse);
        app.modifierTriggered (reverse, target);
        expect (maskContains (app.getActiveModifierStickerMask (0),
                              ModifierType::Reverse));
        app.modifierTriggered (reverse, target);
        expect (! maskContains (app.getActiveModifierStickerMask (0),
                                ModifierType::Reverse),
                "Toggling Reverse twice should remove its sticker");

        auto speed = descriptorFor (ModifierType::Speed);
        speed.plannedSpeed = 2.0;
        app.modifierTriggered (speed, target);
        expect (maskContains (app.getActiveModifierStickerMask (0),
                              ModifierType::Speed));
        speed.plannedSpeed = 1.0;
        app.modifierTriggered (speed, target);
        expect (! maskContains (app.getActiveModifierStickerMask (0),
                                ModifierType::Speed),
                "Returning Speed to 1x should remove its sticker");

        app.modifierTriggered (
            descriptorFor (ModifierType::PitchUpOctave), target);
        expect (maskContains (app.getActiveModifierStickerMask (0),
                              ModifierType::PitchUpOctave));
        app.modifierTriggered (
            descriptorFor (ModifierType::PitchDownOctave), target);
        const auto neutralPitchMask = app.getActiveModifierStickerMask (0);
        expect (! maskContains (neutralPitchMask, ModifierType::PitchUpOctave));
        expect (! maskContains (neutralPitchMask, ModifierType::PitchDownOctave),
                "Opposite octave moves should cancel to no pitch sticker");
    }

    void testTemporaryVolumeRamp()
    {
        beginTest ("Volume Ramp sticker appears and expires with its envelope");

        AppState app;
        loadSyntheticAudio (app, 0);

        auto volumeRamp = descriptorFor (ModifierType::BufferVolumeRampDown);
        volumeRamp.plannedFxFadeBars = 0.05;
        volumeRamp.plannedVolumeHoldBars = 0.05;
        app.modifierTriggered (volumeRamp, targetsFor ({ 0 }));

        expect (maskContains (app.getActiveModifierStickerMask (0),
                              ModifierType::BufferVolumeRampDown));

        advanceBars (app, 0.05);
        advanceBars (app, 0.05);
        advanceBars (app, 0.05);

        expect (! maskContains (app.getActiveModifierStickerMask (0),
                                ModifierType::BufferVolumeRampDown),
                "Volume Ramp should remove its sticker after ramp-up");
        expectWithinAbsoluteError (
            app.channelStrips[0]->getFxParams().volumeGain, 1.0f, 0.0001f);
    }

    void testDubBurstRestoresDelay()
    {
        beginTest ("Dub Burst expires and restores the persistent Delay sticker");

        AppState app;
        loadSyntheticAudio (app, 0);
        const auto target = targetsFor ({ 0 });

        auto delay = descriptorFor (ModifierType::BufferDelayOn);
        delay.plannedDelayWet = 0.37;
        delay.plannedDelayFeedback = 0.42;
        delay.plannedDelayPingPong = false;
        delay.plannedFxFadeBars = 0.0;
        app.modifierTriggered (delay, target);

        const auto& baseline = app.channelStrips[0]->getFxParams();
        const float baselineWet = baseline.delayWet;
        const float baselineFeedback = baseline.delayFeedback;
        const bool baselinePingPong = baseline.delayPingPong;

        auto dubBurst = descriptorFor (ModifierType::BufferDelayDubBurst);
        dubBurst.plannedDelayWet = 0.8;
        dubBurst.plannedDelayFeedback = 0.88;
        app.modifierTriggered (dubBurst, target);

        const auto burstMask = app.getActiveModifierStickerMask (0);
        expect (maskContains (burstMask, ModifierType::BufferDelayOn),
                "Persistent Delay should remain represented under the burst");
        expect (maskContains (burstMask, ModifierType::BufferDelayDubBurst));

        advanceBars (app, 0.5);
        advanceBars (app, 4.0);

        const auto settledMask = app.getActiveModifierStickerMask (0);
        expect (maskContains (settledMask, ModifierType::BufferDelayOn));
        expect (! maskContains (settledMask,
                                ModifierType::BufferDelayDubBurst),
                "Dub Burst should remove only its temporary sticker");

        const auto& restored = app.channelStrips[0]->getFxParams();
        expectWithinAbsoluteError (
            restored.delayWet, baselineWet, 0.0001f);
        expectWithinAbsoluteError (
            restored.delayFeedback, baselineFeedback, 0.0001f);
        expect (restored.delayPingPong == baselinePingPong);

        loadSyntheticAudio (app, 1);
        app.modifierTriggered (dubBurst, targetsFor ({ 1 }));
        expect (maskContains (app.getActiveModifierStickerMask (1),
                              ModifierType::BufferDelayDubBurst));
        expect (! maskContains (app.getActiveModifierStickerMask (1),
                                ModifierType::BufferDelayOn),
                "A standalone burst should not masquerade as persistent Delay");
        advanceBars (app, 0.5);
        advanceBars (app, 4.0);
        expect (app.getActiveModifierStickerMask (1) == 0,
                "A standalone Dub Burst should leave no Delay stickers behind");

        loadSyntheticAudio (app, 2);
        app.modifierTriggered (dubBurst, targetsFor ({ 2 }));
        advanceBars (app, 0.25);
        auto replacementDelay = descriptorFor (ModifierType::BufferDelayOn);
        replacementDelay.plannedDelayWet = 0.29;
        replacementDelay.plannedDelayFeedback = 0.36;
        replacementDelay.plannedFxFadeBars = 0.0;
        app.modifierTriggered (replacementDelay, targetsFor ({ 2 }));
        advanceBars (app, 8.0);
        const auto replacementDelayMask =
            app.getActiveModifierStickerMask (2);
        expect (maskContains (replacementDelayMask,
                              ModifierType::BufferDelayOn));
        expect (! maskContains (replacementDelayMask,
                                ModifierType::BufferDelayDubBurst),
                "A persistent Delay should cancel an in-flight Dub Burst");
        expectWithinAbsoluteError (
            app.channelStrips[2]->getFxParams().delayWet, 0.29f, 0.0001f);
        expectWithinAbsoluteError (
            app.channelStrips[2]->getFxParams().delayFeedback, 0.36f, 0.0001f);
    }

    void testGranularBurstRestoresGranular()
    {
        beginTest ("Granular Burst expires and restores persistent Granular state");

        AppState app;
        loadSyntheticAudio (app, 0);
        const auto target = targetsFor ({ 0 });

        auto granular = descriptorFor (ModifierType::BufferGranularOn);
        granular.plannedGrainMix = 0.41;
        granular.plannedGrainDensityHz = 6.0;
        granular.plannedGrainSizeMs = 210.0;
        granular.plannedGrainPitchSpread = 12.0;
        granular.plannedGrainTexture = 0.22;
        granular.plannedFxFadeBars = 0.0;
        app.modifierTriggered (granular, target);

        const auto& baseline = app.channelStrips[0]->getFxParams();
        const float baselineMix = baseline.grainMix;
        const float baselineDensity = baseline.grainDensityHz;
        const float baselineSize = baseline.grainSizeMs;
        const float baselinePitch = baseline.grainPitchSpread;
        const float baselineTexture = baseline.grainTexture;

        auto burst = descriptorFor (ModifierType::BufferGranularMomentary);
        burst.plannedGrainMix = 0.9;
        burst.plannedGrainDensityHz = 18.0;
        burst.plannedGrainSizeMs = 80.0;
        burst.plannedGrainPitchSpread = 24.0;
        burst.plannedGrainTexture = 0.8;
        burst.plannedFxFadeBars = 0.1;
        app.modifierTriggered (burst, target);

        const auto burstMask = app.getActiveModifierStickerMask (0);
        expect (maskContains (burstMask, ModifierType::BufferGranularOn),
                "Persistent Granular should remain represented under the burst");
        expect (maskContains (burstMask,
                              ModifierType::BufferGranularMomentary));

        advanceBars (app, 0.05);
        advanceBars (app, 0.05);

        const auto settledMask = app.getActiveModifierStickerMask (0);
        expect (maskContains (settledMask, ModifierType::BufferGranularOn));
        expect (! maskContains (settledMask,
                                ModifierType::BufferGranularMomentary),
                "Granular Burst should remove only its temporary sticker");

        const auto& restored = app.channelStrips[0]->getFxParams();
        expectWithinAbsoluteError (restored.grainMix, baselineMix, 0.0001f);
        expectWithinAbsoluteError (
            restored.grainDensityHz, baselineDensity, 0.0001f);
        expectWithinAbsoluteError (
            restored.grainSizeMs, baselineSize, 0.0001f);
        expectWithinAbsoluteError (
            restored.grainPitchSpread, baselinePitch, 0.0001f);
        expectWithinAbsoluteError (
            restored.grainTexture, baselineTexture, 0.0001f);

        loadSyntheticAudio (app, 1);
        app.modifierTriggered (burst, targetsFor ({ 1 }));
        expect (maskContains (app.getActiveModifierStickerMask (1),
                              ModifierType::BufferGranularMomentary));
        expect (! maskContains (app.getActiveModifierStickerMask (1),
                                ModifierType::BufferGranularOn),
                "A standalone burst should not masquerade as persistent Granular");
        advanceBars (app, 0.05);
        advanceBars (app, 0.05);
        expect (app.getActiveModifierStickerMask (1) == 0,
                "A standalone Granular Burst should leave no stickers behind");

        loadSyntheticAudio (app, 2);
        app.modifierTriggered (burst, targetsFor ({ 2 }));
        advanceBars (app, 0.025);
        auto replacementGranular =
            descriptorFor (ModifierType::BufferGranularOn);
        replacementGranular.plannedGrainMix = 0.33;
        replacementGranular.plannedGrainDensityHz = 7.0;
        replacementGranular.plannedGrainSizeMs = 240.0;
        replacementGranular.plannedGrainPitchSpread = 12.0;
        replacementGranular.plannedGrainTexture = 0.18;
        replacementGranular.plannedFxFadeBars = 0.0;
        app.modifierTriggered (replacementGranular, targetsFor ({ 2 }));
        advanceBars (app, 1.0);
        const auto replacementGranularMask =
            app.getActiveModifierStickerMask (2);
        expect (maskContains (replacementGranularMask,
                              ModifierType::BufferGranularOn));
        expect (! maskContains (replacementGranularMask,
                                ModifierType::BufferGranularMomentary),
                "Persistent Granular should cancel an in-flight burst");
        expectWithinAbsoluteError (
            app.channelStrips[2]->getFxParams().grainMix, 0.33f, 0.0001f);
    }
};

static ModifierStickerLifecycleTest modifierStickerLifecycleTestInstance;
}

#endif
