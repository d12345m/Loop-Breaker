#if JUCE_UNIT_TESTS

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "AudioBuffer.h"

namespace
{
constexpr double kTestDurationSeconds = 4.0;
constexpr float kTestSignalAmplitude = 0.30f;

AudioBuffer::LoadedAudioData::Ptr makeSineSource(double sampleRate)
{
    auto source = new AudioBuffer::LoadedAudioData();
    const int numSamples = (int) std::ceil(kTestDurationSeconds * sampleRate);
    source->buffer.setSize(2, numSamples);
    source->sampleRate = sampleRate;
    source->fileName = "click-detection-sine";

    for (int i = 0; i < numSamples; ++i)
    {
        // Two non-harmonically-related tones make channel-specific errors and
        // accidental mono assumptions visible in the rendered output.
        const float left = kTestSignalAmplitude * std::sin(2.0 * juce::MathConstants<double>::pi * 211.0 * i / sampleRate);
        const float right = kTestSignalAmplitude * std::sin(2.0 * juce::MathConstants<double>::pi * 317.0 * i / sampleRate);
        source->buffer.setSample(0, i, left);
        source->buffer.setSample(1, i, right);
    }

    return source;
}

struct ClickDetectionResult
{
    int majorDiscontinuities = 0;
    int mediumDiscontinuities = 0;
    int nanOrInfSamples = 0;
};

ClickDetectionResult renderWorstCaseSequence(double sampleRate, int blockSize)
{
    AudioBuffer buffer;
    buffer.prepare(sampleRate, blockSize);
    buffer.setLoadedAudioData(makeSineSource(sampleRate));
    buffer.setLooping(true);
    buffer.setSpeed(1.0);
    buffer.play();

    juce::AudioBuffer<float> output(2, blockSize);

    // Warm the normal repitch path before measuring scripted transitions.
    for (int i = 0; i < 8; ++i)
        buffer.processBlock(output);
    buffer.resetTearingStats();

    constexpr int kRenderBlocks = 240;
    for (int block = 0; block < kRenderBlocks; ++block)
    {
        // Exercise the problematic stacked sequence at deterministic times.
        if (block == 8)
        {
            buffer.setNumSlices(16);
            buffer.triggerSlice(7);
        }
        else if (block == 24)
        {
            buffer.setStretchRatio(0.5);
            buffer.setPitchSemiTones(7.0);
        }
        else if (block == 56)
        {
            buffer.triggerSlice(2);
        }
        else if (block == 80)
        {
            buffer.setSpeed(-1.5);
        }
        else if (block == 112)
        {
            buffer.setPingPongMode(true, 0.0625, 120.0, sampleRate);
        }
        else if (block == 144)
        {
            buffer.triggerSlice(13);
        }
        else if (block == 176)
        {
            buffer.setPingPongMode(false);
            buffer.setSpeed(1.0);
            buffer.setPitchSemiTones(0.0);
            buffer.setStretchRatio(1.0);
        }
        else if (block > 176 && (block % 12) == 0)
        {
            buffer.triggerSlice((block / 12) % 16);
        }

        buffer.processBlock(output);

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            for (int sample = 0; sample < output.getNumSamples(); ++sample)
                jassert(std::isfinite(output.getSample(ch, sample)));
    }

    const auto& stats = buffer.getTearingStats();
    return {
        stats.discontinuities.load(),
        stats.mediumDiscontinuities.load(),
        stats.nanOrInfSamples.load()
    };
}
}

class AudioBufferClickDetectionTest : public juce::UnitTest
{
public:
    AudioBufferClickDetectionTest() : juce::UnitTest("AudioBuffer Click Detection") {}

    void runTest() override
    {
        constexpr std::array<int, 4> blockSizes { 64, 128, 512, 1024 };
        constexpr std::array<double, 2> sampleRates { 44100.0, 48000.0 };

        for (const auto sampleRate : sampleRates)
        {
            for (const auto blockSize : blockSizes)
            {
                beginTest("Stacked modifier render at " + juce::String(sampleRate, 0)
                          + " Hz / " + juce::String(blockSize) + " samples");
                const auto result = renderWorstCaseSequence(sampleRate, blockSize);

                expectEquals(result.nanOrInfSamples, 0, "The render must not produce invalid samples");
                expectEquals(result.majorDiscontinuities, 0,
                             "Major discontinuities must be removed by the structural fades and de-clicker");
                expect(result.mediumDiscontinuities <= 8,
                       "Medium discontinuity budget exceeded: " + juce::String(result.mediumDiscontinuities));
            }
        }
    }
};

static AudioBufferClickDetectionTest audioBufferClickDetectionTestInstance;

#endif // JUCE_UNIT_TESTS
