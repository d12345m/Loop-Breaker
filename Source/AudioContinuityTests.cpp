#include <JuceHeader.h>

#include "AudioBuffer.h"
#include "StretchQueueController.h"

#include <array>
#include <cmath>

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
};

AudioContinuityTests audioContinuityTests;
}

#endif
