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

    // Processes a mono buffer for now. (We can extend to multichannel with interleave/deinterleave.)
    // Returns number of output samples written to out.
    int processMono (const float* input, int numInputSamples, float* output, int maxOutputSamples, bool flush)
    {
        jassert (channels == 1);
        if (numInputSamples > 0)
            st.putSamples (input, (uint) numInputSamples);

        if (flush)
            st.flush();

        const auto received = (int) st.receiveSamples (output, (uint) maxOutputSamples);
        return received;
    }

private:
    soundtouch::SoundTouch st;
    int channels { 0 };
};
