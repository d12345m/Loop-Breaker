#include <JuceHeader.h>

#include "AudioBuffer.h"
#include "LockFreeBoundedQueue.h"
#include "StretchQueueController.h"

#include <array>
#include <atomic>
#include <cmath>
#include <thread>

#if JUCE_UNIT_TESTS

namespace
{
AudioBuffer::LoadedAudioData::Ptr makeSmoothAudio (int numSamples,
                                                   double sampleRate,
                                                   bool mismatchedEndpoints = false)
{
    auto data = AudioBuffer::LoadedAudioData::Ptr (new AudioBuffer::LoadedAudioData());
    data->sampleRate = sampleRate;
    data->fileName = "continuity-test";
    data->buffer.setSize (2, numSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double phase = static_cast<double> (sample) / sampleRate;
        data->buffer.setSample (0, sample, static_cast<float> (
            0.18 * std::sin (juce::MathConstants<double>::twoPi * 137.0 * phase)));
        data->buffer.setSample (1, sample, static_cast<float> (
            0.18 * std::sin (juce::MathConstants<double>::twoPi * 173.0 * phase)));
    }

    if (mismatchedEndpoints && numSamples >= 2)
    {
        data->buffer.setSample (0, 0, -0.6f);
        data->buffer.setSample (1, 0, 0.5f);
        data->buffer.setSample (0, numSamples - 1, 0.7f);
        data->buffer.setSample (1, numSamples - 1, -0.4f);
    }

    return data;
}

AudioBuffer::LoadedAudioData::Ptr makeConstantAudio (int numSamples,
                                                     double sampleRate,
                                                     float value)
{
    auto data = AudioBuffer::LoadedAudioData::Ptr (new AudioBuffer::LoadedAudioData());
    data->sampleRate = sampleRate;
    data->fileName = "constant-continuity-test";
    data->buffer.setSize (2, numSamples);

    for (int channel = 0; channel < data->buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill (
            data->buffer.getWritePointer (channel), value, numSamples);

    return data;
}

float accumulateMaximumDelta(const juce::AudioBuffer<float>& buffer,
                             float& previousSample,
                             bool& hasPreviousSample)
{
    float maximumDelta = 0.0f;
    const auto* samples = buffer.getReadPointer(0);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        if (hasPreviousSample)
            maximumDelta = juce::jmax(
                maximumDelta, std::abs(samples[sample] - previousSample));

        previousSample = samples[sample];
        hasPreviousSample = true;
    }

    return maximumDelta;
}

class AudioContinuityTests final : public juce::UnitTest
{
public:
    AudioContinuityTests()
        : juce::UnitTest ("Audio continuity and queue boundedness", "LoopBreaker")
    {
    }

    void runTest() override
    {
        testFractionalQueueCredit();
        testVariableCallbackLengths();
        testShortLoopBoundary();
        testSoundTouchQueueBoundedness();
        testStartupFadeProgress();
        testBoundedMpscQueue();
        testTransportPacketGenerationAndCoherence();
        testConcurrentTransportGenerationOrdering();
        testSeekBoundaryContinuity();
        testFullResetBoundaryContinuity();
        testModeTransitionEndpointContinuity();
        testSoundTouchRateCrossoverContinuity();
        testRepitchDirectionZeroCrossing();
        testSoundTouchDirectionPivotContinuity();
        testSoundTouchPingPongPivots();
        testSoundTouchHighStepSliceLookahead();
        testExitSlicingUnwindsLookahead();
        testSoundTouchUnderfillRecoveryContinuity();
    }

private:
    void testFractionalQueueCredit()
    {
        beginTest ("Fractional SoundTouch input debt does not accumulate");

        constexpr std::array<double, 8> inputPerOutputRatios {{
            0.25, 0.5, 0.8, 1.0, 1.3, 4.0 / 3.0, 2.0, 4.0
        }};
        constexpr std::array<int, 6> blockSizes {{ 32, 64, 128, 256, 512, 1024 }};
        constexpr int callbacks = 6000;
        constexpr int targetReserve = 5000;

        for (const double inputPerOutput : inputPerOutputRatios)
        {
            for (const int blockSize : blockSizes)
            {
                const double outputPerInput = 1.0 / inputPerOutput;
                StretchQueueController controller;
                controller.reset (outputPerInput);

                const int seedInput = static_cast<int> (
                    std::ceil (targetReserve / outputPerInput));
                controller.recordInputFrames (seedInput);

                for (int callback = 0; callback < callbacks; ++callback)
                {
                    const int input = controller.getInputFramesRequired (
                        blockSize, targetReserve, 8192);
                    controller.recordInputFrames (input);
                    controller.recordOutputFrames (blockSize);
                }

                const auto steadyInput = static_cast<double> (
                    controller.getTotalInputFrames() - static_cast<std::uint64_t> (seedInput));
                const double idealInput = callbacks * blockSize * inputPerOutput;

                expectWithinAbsoluteError (
                    steadyInput, idealInput, 1.0,
                    "Carried fractional credit should stay within one input frame of ideal");
                expect (controller.getOutputCreditFrames() >= targetReserve - outputPerInput);
                expect (controller.getOutputCreditFrames() <= targetReserve + outputPerInput);
            }
        }
    }

    void testVariableCallbackLengths()
    {
        beginTest ("AudioBuffer advances only the active callback length");

        constexpr double sampleRate = 48000.0;
        AudioBuffer player;
        player.prepare (sampleRate, 512);
        player.setLoadedAudioData (makeSmoothAudio (48000, sampleRate));
        player.setLooping (false);
        player.play();

        constexpr std::array<int, 7> callbackSizes {{ 512, 128, 512, 64, 256, 128, 512 }};
        int expectedPosition = 0;

        for (const int callbackSize : callbackSizes)
        {
            juce::AudioBuffer<float> output (2, callbackSize);
            player.processBlock (output);
            expectedPosition += callbackSize;
            expectWithinAbsoluteError (
                player.getPlayheadPositionInSamples(),
                static_cast<double> (expectedPosition),
                1.0e-9,
                "A short callback must not render the prepared scratch tail");
        }
    }

    void testShortLoopBoundary()
    {
        beginTest ("SoundTouch short-loop boundary remains bounded and in range");

        constexpr double sampleRate = 48000.0;
        constexpr int sourceLength = 97;
        AudioBuffer player;
        player.prepare (sampleRate, 512);
        player.setLoadedAudioData (
            makeSmoothAudio (sourceLength, sampleRate, true));
        player.setLooping (true);
        player.setStretchRatio (0.8);
        player.play();

        for (int callback = 0; callback < 80; ++callback)
        {
            juce::AudioBuffer<float> output (2, 64);
            player.processBlock (output);

            const double position = player.getPlayheadPositionInSamples();
            expect (position >= 0.0 && position < sourceLength,
                    "Looping SoundTouch playhead must remain inside the source");

            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                for (int sample = 0; sample < output.getNumSamples(); ++sample)
                    expect (std::isfinite (output.getSample (channel, sample)),
                            "Boundary render produced NaN/Inf");
        }
    }

    void testSoundTouchQueueBoundedness()
    {
        beginTest ("SoundTouch reserve has no positive long-run slope");

        constexpr double sampleRate = 48000.0;
        constexpr int callbacks = 6000;
        struct Case
        {
            double inputPerOutput;
            int blockSize;
        };
        constexpr std::array<Case, 2> cases {{ { 0.8, 64 }, { 1.3, 64 } }};

        for (const auto& testCase : cases)
        {
            const int sourceLength = static_cast<int> (
                std::ceil (callbacks * testCase.blockSize * testCase.inputPerOutput))
                                   + 100000;
            AudioBuffer player;
            player.prepare (sampleRate, testCase.blockSize);
            player.setLoadedAudioData (makeSmoothAudio (sourceLength, sampleRate));
            player.setLooping (false);
            player.setStretchRatio (testCase.inputPerOutput);
            player.play();

            int midpointReady = 0;
            for (int callback = 0; callback < callbacks; ++callback)
            {
                juce::AudioBuffer<float> output (2, testCase.blockSize);
                player.processBlock (output);

                if (callback == callbacks / 2)
                    midpointReady = player.getTimeStretchQueueTelemetry().readyOutputFrames;
            }

            const auto telemetry = player.getTimeStretchQueueTelemetry();
            expectEquals (
                telemetry.totalOutputFramesReceived,
                static_cast<std::uint64_t> (callbacks * testCase.blockSize),
                "SoundTouch should fill every requested host frame after priming");
            expect (telemetry.readyOutputFrames <= midpointReady + 2048,
                    "Ready-output depth grew materially during the second half");
            expect (telemetry.estimatedOutputCreditFrames
                        <= telemetry.highOutputWatermark + testCase.blockSize,
                    "Output-equivalent pipeline credit exceeded its documented bound");
            expectEquals (telemetry.totalUnderfills, std::uint64_t { 0 },
                          "Steady-state queue control should not underfill");
        }
    }

    void testStartupFadeProgress()
    {
        beginTest ("SoundTouch startup fade remains monotonic across callbacks");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (makeConstantAudio (100000, sampleRate, 0.25f));
        player.setLooping (false);
        player.setStretchRatio (0.8);
        player.play();

        float previousBlockEnd = 0.0f;
        for (int callback = 0; callback < 12; ++callback)
        {
            juce::AudioBuffer<float> output (2, blockSize);
            player.processBlock (output);

            const float blockStart = std::abs (output.getSample (0, 0));
            const float blockEnd = std::abs (output.getSample (0, blockSize - 1));
            if (callback > 0)
                expect (blockStart + 0.01f >= previousBlockEnd,
                        "Startup gain restarted near zero at a callback boundary");

            previousBlockEnd = blockEnd;
        }
    }

    void testBoundedMpscQueue()
    {
        beginTest ("Bounded command queue delivers MPSC traffic exactly once");

        struct QueueItem
        {
            int producer = 0;
            int sequence = 0;
        };

        constexpr int producerCount = 4;
        constexpr int itemsPerProducer = 200;
        constexpr int totalItems = producerCount * itemsPerProducer;
        LockFreeBoundedQueue<QueueItem, 1024> queue;
        std::atomic<bool> begin { false };
        std::array<std::thread, producerCount> producers;

        for (int producer = 0; producer < producerCount; ++producer)
        {
            producers[(size_t) producer] = std::thread (
                [producer, &queue, &begin]
                {
                    while (!begin.load (std::memory_order_acquire))
                        std::this_thread::yield();

                    for (int sequence = 0; sequence < itemsPerProducer; ++sequence)
                    {
                        const QueueItem item { producer, sequence };
                        while (!queue.tryEnqueue (item))
                            std::this_thread::yield();
                    }
                });
        }

        std::array<std::array<bool, itemsPerProducer>, producerCount> seen {};
        begin.store (true, std::memory_order_release);
        int received = 0;
        while (received < totalItems)
        {
            QueueItem item;
            if (!queue.tryDequeue (item))
            {
                std::this_thread::yield();
                continue;
            }

            expect (juce::isPositiveAndBelow (item.producer, producerCount));
            expect (juce::isPositiveAndBelow (item.sequence, itemsPerProducer));
            if (juce::isPositiveAndBelow (item.producer, producerCount)
                && juce::isPositiveAndBelow (item.sequence, itemsPerProducer))
            {
                expect (!seen[(size_t) item.producer][(size_t) item.sequence],
                        "A command was delivered more than once");
                seen[(size_t) item.producer][(size_t) item.sequence] = true;
            }
            ++received;
        }

        for (auto& producer : producers)
            producer.join();

        QueueItem extra;
        expect (!queue.tryDequeue (extra), "Queue should be empty after exact delivery");
    }

    void testTransportPacketGenerationAndCoherence()
    {
        beginTest ("Loop and playhead packet applies coherently once");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (makeSmoothAudio (48000, sampleRate));
        player.setLooping (true);
        player.play();

        juce::AudioBuffer<float> output (2, blockSize);
        player.processBlock (output);

        const auto generation = player.requestLoopAndPlayheadTransition (
            12000, 24000, 15000, TransportTransitionReason::PartSwitch);
        expect (generation > 0);

        player.processBlock (output);
        const auto afterFirstRender = player.getTransportTransitionTelemetry();
        expectEquals (afterFirstRender.lastAcknowledgedGeneration, generation);
        expectEquals (afterFirstRender.lastAppliedGeneration, generation);
        expectEquals (afterFirstRender.droppedCommands, std::uint64_t { 0 });
        expect (afterFirstRender.lastAppliedReason
                    == TransportTransitionReason::PartSwitch);
        expect (player.isLoopWindowEnabled());
        expectEquals (player.getLoopStartSamples(), int64_t { 12000 });
        expectEquals (player.getLoopEndSamples(), int64_t { 24000 });
        expectWithinAbsoluteError (
            player.getPlayheadPositionInSamples(), 15000.0 + blockSize, 1.0e-9);

        player.processBlock (output);
        const auto afterSecondRender = player.getTransportTransitionTelemetry();
        expectEquals (afterSecondRender.lastAppliedGeneration, generation,
                      "A consumed transition must not be applied twice");
        expectWithinAbsoluteError (
            player.getPlayheadPositionInSamples(), 15000.0 + 2 * blockSize, 1.0e-9);
    }

    void testConcurrentTransportGenerationOrdering()
    {
        beginTest ("Newest transport generation wins across producers");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        constexpr int producerCount = 4;
        constexpr int commandsPerProducer = 16;

        struct PublishedCommand
        {
            std::uint64_t generation = 0;
            int64_t target = 0;
        };

        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (makeSmoothAudio (100000, sampleRate));
        player.setLooping (false);
        player.play();

        juce::AudioBuffer<float> output (2, blockSize);
        player.processBlock (output);

        std::array<std::array<PublishedCommand, commandsPerProducer>,
                   producerCount> published {};
        std::atomic<bool> begin { false };
        std::array<std::thread, producerCount> producers;
        for (int producer = 0; producer < producerCount; ++producer)
        {
            producers[(size_t) producer] = std::thread (
                [producer, &player, &published, &begin]
                {
                    while (!begin.load (std::memory_order_acquire))
                        std::this_thread::yield();

                    for (int command = 0; command < commandsPerProducer; ++command)
                    {
                        const int64_t target =
                            1000 + producer * 1000 + command * 17;
                        const auto generation = player.requestPlayheadTransition (
                            target, TransportTransitionReason::UserSeek);
                        published[(size_t) producer][(size_t) command] = {
                            generation, target
                        };
                    }
                });
        }

        begin.store (true, std::memory_order_release);
        for (auto& producer : producers)
            producer.join();

        PublishedCommand newest;
        for (const auto& producer : published)
            for (const auto& command : producer)
                if (command.generation > newest.generation)
                    newest = command;

        expect (newest.generation > 0);
        player.processBlock (output);

        const auto telemetry = player.getTransportTransitionTelemetry();
        expectEquals (telemetry.lastAcknowledgedGeneration, newest.generation);
        expectEquals (telemetry.lastAppliedGeneration, newest.generation);
        expectEquals (telemetry.droppedCommands, std::uint64_t { 0 });
        expectWithinAbsoluteError (
            player.getPlayheadPositionInSamples(),
            static_cast<double> (newest.target + blockSize), 1.0e-9,
            "Enqueue scheduling must not let an older generation win");
    }

    void testSeekBoundaryContinuity()
    {
        beginTest ("Render-boundary seek crossfades from the actual old position");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        auto source = makeSmoothAudio (48000, sampleRate);

        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (source);
        player.setLooping (false);
        player.play();

        juce::AudioBuffer<float> before (2, blockSize);
        player.processBlock (before);
        const float oldTail = before.getSample (0, blockSize - 1);

        int target = 1000;
        for (int sample = 1000; sample < source->buffer.getNumSamples(); ++sample)
        {
            if (std::abs (source->buffer.getSample (0, sample) - oldTail) > 0.25f)
            {
                target = sample;
                break;
            }
        }
        expect (std::abs (source->buffer.getSample (0, target) - oldTail) > 0.25f,
                "Test source must expose a clearly discontinuous raw seek");

        const auto generation = player.requestPlayheadTransition (
            target, TransportTransitionReason::PresetRecall);
        juce::AudioBuffer<float> after (2, blockSize);
        player.processBlock (after);

        const float boundaryDelta =
            std::abs (after.getSample (0, 0) - oldTail);
        expect (boundaryDelta < 0.05f,
                "Seek packet should crossfade from the old render position");
        expectEquals (
            player.getTransportTransitionTelemetry().lastAppliedGeneration,
            generation);
    }

    void testFullResetBoundaryContinuity()
    {
        beginTest ("Full Reset reconnects cleared SoundTouch output");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        auto source = makeSmoothAudio (48000, sampleRate, true);

        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (source);
        player.setLooping (false);
        player.setStretchRatio (0.8);
        player.play();

        juce::AudioBuffer<float> before (2, blockSize);
        for (int callback = 0; callback < 40; ++callback)
            player.processBlock (before);

        const float oldTail = before.getSample (0, blockSize - 1);
        expect (std::abs (source->buffer.getSample (0, 0) - oldTail) > 0.2f,
                "Reset fixture must expose a discontinuous raw restart");

        player.resetToDefaults();
        player.play();
        juce::AudioBuffer<float> after (2, blockSize);
        player.processBlock (after);

        expect (std::abs (after.getSample (0, 0) - oldTail) < 0.01f,
                "Reset output correction must meet the prior audible endpoint");
        expect (player.getTransportTransitionTelemetry().lastAppliedReason
                    == TransportTransitionReason::Reset);
    }

    void testModeTransitionEndpointContinuity()
    {
        beginTest ("Mode transitions meet the exact prior audible endpoint");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (makeSmoothAudio (240000, sampleRate));
        player.setLooping (false);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        for (int callback = 0; callback < 40; ++callback)
            player.processBlock (block);

        float oldTail = block.getSample (0, blockSize - 1);
        player.setStretchRatio (0.8);
        player.processBlock (block);
        expect (std::abs (block.getSample (0, 0) - oldTail) < 0.001f,
                "Repitch-to-stretch must not replay an older output-tail sample");

        for (int callback = 0; callback < 80; ++callback)
            player.processBlock (block);

        oldTail = block.getSample (0, blockSize - 1);
        player.setStretchRatio (1.0);
        player.processBlock (block);
        expect (std::abs (block.getSample (0, 0) - oldTail) < 0.001f,
                "Stretch-to-repitch must meet the last SoundTouch output sample");
    }

    void testSoundTouchRateCrossoverContinuity()
    {
        beginTest ("SoundTouch rate automation crosses 1x without a click");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (
            makeConstantAudio (480000, sampleRate, 0.25f));
        player.setLooping (false);
        player.setStretchRatio (0.8);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        for (int callback = 0; callback < 80; ++callback)
            player.processBlock (block);

        float previousSample = block.getSample (0, blockSize - 1);
        bool hasPreviousSample = true;
        float maximumDelta = 0.0f;
        for (const double speed : { 0.5, 2.0, 0.5, 2.0 })
        {
            player.setSpeed (speed);
            for (int callback = 0; callback < 80; ++callback)
            {
                player.processBlock (block);
                maximumDelta = juce::jmax(
                    maximumDelta,
                    accumulateMaximumDelta(
                        block, previousSample, hasPreviousSample));
            }
        }

        expect (maximumDelta < 0.08f,
                "Crossing SoundTouch's 1x rate boundary exposed a discontinuity");
    }

    void testSoundTouchDirectionPivotContinuity()
    {
        beginTest ("SoundTouch reverse uses a pivot-centred source crossfade");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (makeSmoothAudio (480000, sampleRate));
        player.setLooping (false);
        player.setStretchRatio (0.8);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        for (int callback = 0; callback < 100; ++callback)
            player.processBlock (block);

        const double forwardPosition = player.getPlayheadPositionInSamples();
        float previousSample = block.getSample (0, blockSize - 1);
        bool hasPreviousSample = true;
        float maximumDelta = 0.0f;

        player.setSpeed (-1.0);
        for (int callback = 0; callback < 100; ++callback)
        {
            player.processBlock (block);
            maximumDelta = juce::jmax(
                maximumDelta,
                accumulateMaximumDelta(
                    block, previousSample, hasPreviousSample));
        }

        expect (player.getPlayheadPositionInSamples() < forwardPosition,
                "Reverse must advance the render-owned playhead backwards");
        expect (maximumDelta < 0.15f,
                "Direction pivot exposed an audible-scale sample discontinuity");
    }

    void testRepitchDirectionZeroCrossing()
    {
        beginTest ("Repitch direction smoothing renders through exact zero");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (
            makeConstantAudio (480000, sampleRate, 0.25f));
        player.setLooping (true);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        for (int callback = 0; callback < 20; ++callback)
            player.processBlock (block);

        float previousSample = block.getSample (0, blockSize - 1);
        bool hasPreviousSample = true;
        float maximumDelta = 0.0f;

        player.setSpeed (-1.0);
        for (int callback = 0; callback < 60; ++callback)
        {
            player.processBlock (block);
            maximumDelta = juce::jmax(
                maximumDelta,
                accumulateMaximumDelta(
                    block, previousSample, hasPreviousSample));
        }

        expect (maximumDelta < 0.01f,
                "The zero point in a signed speed ramp must not clear the block tail");
    }

    void testSoundTouchPingPongPivots()
    {
        beginTest ("SoundTouch ping-pong pivots inside the active input chunk");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (makeSmoothAudio (480000, sampleRate));
        player.setLooping (false);
        player.setStretchRatio (0.8);
        // 1/16 bar at 60 BPM is 12000 source samples per direction. This is
        // longer than one feed chunk, so the reversed state remains observable
        // at a callback boundary.
        player.setPingPongMode (true, 0.0625, 60.0, sampleRate);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        bool sawForward = false;
        bool sawReverse = false;
        bool hasPreviousSample = false;
        float previousSample = 0.0f;
        float maximumDelta = 0.0f;

        for (int callback = 0; callback < 240; ++callback)
        {
            player.processBlock (block);
            sawForward = sawForward || player.getSpeed() > 0.0;
            sawReverse = sawReverse || player.getSpeed() < 0.0;
            if (callback >= 40)
            {
                maximumDelta = juce::jmax(
                    maximumDelta,
                    accumulateMaximumDelta(
                        block, previousSample, hasPreviousSample));
            }
        }

        expect (sawForward && sawReverse,
                "Ping-pong must not be bypassed by SoundTouch's bulk-copy path");
        expect (maximumDelta < 0.15f,
                "Ping-pong pivot exposed an audible-scale sample discontinuity");
    }

    void testSoundTouchUnderfillRecoveryContinuity()
    {
        beginTest ("A recovered SoundTouch block reconnects after underfill");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (
            makeConstantAudio (1000, sampleRate, 0.25f));
        player.setLooping (false);
        player.setStretchRatio (0.8);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        player.processBlock (block);
        auto telemetry = player.getTimeStretchQueueTelemetry();
        expect (telemetry.totalUnderfills > 0,
                "The deliberately sub-latency source must force an underfill");

        player.setLooping (true);
        player.requestPlayheadTransition (
            0, TransportTransitionReason::UserRestart);
        player.play();

        bool recovered = false;
        for (int callback = 0; callback < 40 && !recovered; ++callback)
        {
            const float priorTail = block.getSample (0, blockSize - 1);
            const auto underfillsBefore =
                player.getTimeStretchQueueTelemetry().totalUnderfills;
            player.processBlock (block);
            const auto underfillsAfter =
                player.getTimeStretchQueueTelemetry().totalUnderfills;

            if (underfillsAfter == underfillsBefore)
            {
                recovered = true;
                expect (std::abs(block.getSample (0, 0) - priorTail) < 0.001f,
                        "First recovered sample must meet the underfill tail");
            }
        }

        expect (recovered,
                "Looping restart should recover from the forced underfill");
    }

    void testSoundTouchHighStepSliceLookahead()
    {
        beginTest ("SoundTouch lookahead remains continuous above 1x source step");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        for (juce::int64 seed = 1; seed <= 12; ++seed)
        {
            AudioBuffer player;
            player.prepare (sampleRate, blockSize);
            player.setLoadedAudioData (
                makeSmoothAudio (96000, sampleRate, true));
            player.setRandomSeedForTesting(seed);
            player.setLooping (true);
            player.setStretchRatio (0.8);
            player.setTempoMultiplier (4.0);
            player.startSliceRepeater (8, 8);
            player.play();

            juce::AudioBuffer<float> block (2, blockSize);
            bool hasPreviousSample = false;
            float previousSample = 0.0f;
            float maximumDelta = 0.0f;
            int maximumDeltaCallback = -1;
            double maximumDeltaPosition = 0.0;
            int maximumDeltaSlice = -1;

            for (int callback = 0; callback < 480; ++callback)
            {
                player.processBlock (block);
                if (callback >= 80)
                {
                    const float blockDelta = accumulateMaximumDelta(
                        block, previousSample, hasPreviousSample);
                    if (blockDelta > maximumDelta)
                    {
                        maximumDelta = blockDelta;
                        maximumDeltaCallback = callback;
                        maximumDeltaPosition =
                            player.getPlayheadPositionInSamples();
                        maximumDeltaSlice = player.getCurrentSlice();
                    }
                }

                for (int sample = 0; sample < blockSize; ++sample)
                    expect (std::isfinite(block.getSample(0, sample)),
                            "High-step lookahead produced NaN/Inf");
            }

            expect (maximumDelta < 0.15f,
                    "High-step slice handoff exposed an audible-scale discontinuity; seed="
                        + juce::String(seed)
                        + " max delta="
                        + juce::String(maximumDelta, 4)
                        + " callback="
                        + juce::String(maximumDeltaCallback)
                        + " playhead="
                        + juce::String(maximumDeltaPosition, 1)
                        + " slice="
                        + juce::String(maximumDeltaSlice));
            expectEquals (
                player.getTimeStretchQueueTelemetry().totalUnderfills,
                std::uint64_t { 0 },
                "High-step slicing should not starve the SoundTouch output queue");
        }
    }

    void testExitSlicingUnwindsLookahead()
    {
        beginTest ("Exiting slicing unwinds an active lookahead blend");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 64;
        AudioBuffer player;
        player.prepare (sampleRate, blockSize);
        player.setLoadedAudioData (
            makeSmoothAudio (96000, sampleRate));
        player.setRandomSeedForTesting(42);
        player.setLooping (true);
        player.setStretchRatio (0.8);
        player.setTempoMultiplier (4.0);
        player.startSliceRepeater (64, 8);
        player.play();

        juce::AudioBuffer<float> block (2, blockSize);
        bool lookaheadStarted = false;
        for (int callback = 0; callback < 80 && !lookaheadStarted; ++callback)
        {
            player.processBlock (block);
            lookaheadStarted =
                player.isLookaheadTransitionActiveForTesting();
        }
        expect (lookaheadStarted,
                "Fixture must enter a pre-boundary lookahead transition");

        float previousSample = block.getSample (0, blockSize - 1);
        bool hasPreviousSample = true;
        float maximumDelta = 0.0f;

        player.exitSlicingMode();
        player.processBlock (block);
        maximumDelta = juce::jmax(
            maximumDelta,
            accumulateMaximumDelta(
                block, previousSample, hasPreviousSample));
        expect (player.isLookaheadCancellationActiveForTesting(),
                "Slice exit must convert lookahead into a cancellation ramp");

        for (int callback = 0;
             callback < 500
                && player.isLookaheadTransitionActiveForTesting();
             ++callback)
        {
            player.processBlock (block);
            maximumDelta = juce::jmax(
                maximumDelta,
                accumulateMaximumDelta(
                    block, previousSample, hasPreviousSample));
        }

        expect (!player.isLookaheadTransitionActiveForTesting(),
                "Lookahead cancellation must complete instead of holding a stale mix");
        expect (maximumDelta < 0.15f,
                "Exiting slice mode exposed an audible-scale discontinuity; max delta="
                    + juce::String(maximumDelta, 4));
    }
};

AudioContinuityTests audioContinuityTests;
}

#endif
