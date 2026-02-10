#pragma once

#include <JuceHeader.h>

// SoundTouch public headers live in ThirdParty/soundtouch/include.
// Xcode header search paths are patched to include that folder.
#include <SoundTouch.h>

// Minimal wrapper to make it easy to plug SoundTouch into our non-interleaved JUCE buffers.
// For now this is intentionally small: "get it working" + provide a clean seam for future modifiers.
class TimeStretchSoundTouch
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        jassert (sampleRate > 0.0);
        jassert (numChannels > 0);

        channels = numChannels;
        st.clear();
        st.setSampleRate ((uint) juce::jmax (1.0, sampleRate));
        st.setChannels ((uint) numChannels);
        st.setTempo (1.0f);
        st.setPitch (1.0f);
        st.setRate (1.0f);

        // Favor smoother output over minimal latency. These values are moderate and stable.
        // sequence: analysis window; seek: search window; overlap: crossfade between windows.
        st.setSetting (SETTING_SEQUENCE_MS, 60);
        st.setSetting (SETTING_SEEKWINDOW_MS, 30);
        st.setSetting (SETTING_OVERLAP_MS, 12);
    }

    void reset()
    {
        st.clear();
    }

    // ratio: 1.0 = original length, 0.5 = 2x longer, 2.0 = 2x shorter
    void setTempoRatio (float ratio)
    {
        st.setTempo (juce::jlimit (0.25f, 4.0f, ratio));
    }

    // Process non-interleaved JUCE buffers by interleaving into a scratch buffer.
    // - in/out are [channel][sample]
    // - scratch buffers are resized as needed but can be reused across calls
    // Returns number of output frames written to out.
    int processNonInterleaved (const float* const* input, int numInputSamples,
                              float* const* output, int maxOutputSamples,
                              bool flush,
                              juce::AudioBuffer<float>& interleavedIn,
                              juce::AudioBuffer<float>& interleavedOut)
    {
        jassert (channels > 0);
        jassert (output != nullptr);

        if (numInputSamples > 0)
            jassert (input != nullptr);

        if (numInputSamples > 0)
        {
            if (interleavedIn.getNumChannels() != 1 || interleavedIn.getNumSamples() < numInputSamples * channels)
                interleavedIn.setSize(1, numInputSamples * channels, false, false, true);

            auto* inInter = interleavedIn.getWritePointer(0);
            for (int i = 0; i < numInputSamples; ++i)
                for (int ch = 0; ch < channels; ++ch)
                    inInter[i * channels + ch] = input[ch][i];

            st.putSamples(inInter, (uint) numInputSamples);
        }

        if (flush)
            st.flush();

        if (interleavedOut.getNumChannels() != 1 || interleavedOut.getNumSamples() < maxOutputSamples * channels)
            interleavedOut.setSize(1, maxOutputSamples * channels, false, false, true);

        auto* outInter = interleavedOut.getWritePointer(0);
        const auto receivedFrames = (int) st.receiveSamples(outInter, (uint) maxOutputSamples);

        const int frames = juce::jlimit(0, maxOutputSamples, receivedFrames);
        for (int i = 0; i < frames; ++i)
            for (int ch = 0; ch < channels; ++ch)
                output[ch][i] = outInter[i * channels + ch];

        return frames;
    }

    // Convenience for the common mono case.
    int processMono (const float* input, int numInputSamples,
                     float* output, int maxOutputSamples,
                     bool flush)
    {
        const float* const inPtrs[1] { input };
        float* const outPtrs[1] { output };

        return processNonInterleaved ((numInputSamples > 0 ? inPtrs : nullptr), numInputSamples,
                                      outPtrs, maxOutputSamples,
                                      flush,
                                      scratchInterleavedIn,
                                      scratchInterleavedOut);
    }

    // Convenience overload that uses internal scratch buffers.
    int processNonInterleaved (const float* const* input, int numInputSamples,
                              float* const* output, int maxOutputSamples,
                              bool flush)
    {
        return processNonInterleaved (input, numInputSamples,
                                      output, maxOutputSamples,
                                      flush,
                                      scratchInterleavedIn,
                                      scratchInterleavedOut);
    }

private:
    soundtouch::SoundTouch st;
    int channels { 0 };

    juce::AudioBuffer<float> scratchInterleavedIn;
    juce::AudioBuffer<float> scratchInterleavedOut;
};
