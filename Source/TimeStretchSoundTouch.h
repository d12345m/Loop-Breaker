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

        // Balance between latency and quality.
        // Too-small windows (e.g. 40/20/10) produce metallic artifacts.
        // Too-large windows (e.g. 80/30/12) add latency and CPU.
        // These values are a good middle ground for real-time playback.
        st.setSetting (SETTING_SEQUENCE_MS, 60);
        st.setSetting (SETTING_SEEKWINDOW_MS, 25);
        st.setSetting (SETTING_OVERLAP_MS, 12);
        // Quickseek trades quality for speed.  With the feed-loop optimizations
        // we have enough CPU headroom to keep it off for better audio quality.
        st.setSetting (SETTING_USE_QUICKSEEK, 0);
    }

    void reset()
    {
        st.clear();
    }

    // T9: Widen seek/overlap windows for reversed audio (worse auto-correlation).
    // Call after prepare() or after a direction change.  Forward = default params.
    void setWindowsForReverse (bool isReversed)
    {
        if (isReversed)
        {
            st.setSetting (SETTING_SEEKWINDOW_MS, 35);
            st.setSetting (SETTING_OVERLAP_MS, 18);
        }
        else
        {
            st.setSetting (SETTING_SEEKWINDOW_MS, 25);
            st.setSetting (SETTING_OVERLAP_MS, 12);
        }
    }

    // ratio: 1.0 = original length, 0.5 = 2x longer, 2.0 = 2x shorter
    // Range is wide to accommodate pitch-ratio compensation (see AudioBuffer.cpp).
    void setTempoRatio (float ratio)
    {
        st.setTempo (juce::jlimit (0.01f, 16.0f, ratio));
    }

    // ratio: 1.0 = normal rate, <1 slower (lower pitch), >1 faster (higher pitch)
    void setRateRatio (float ratio)
    {
        st.setRate (juce::jlimit (0.25f, 4.0f, ratio));
    }

    // Pitch shift in semitones. 0 = original pitch.
    void setPitchSemiTones (float semiTones)
    {
        st.setPitchSemiTones (juce::jlimit (-24.0f, 24.0f, semiTones));
    }

    // Expose SoundTouch's reported initial latency so callers can prime appropriately.
    int getLatencySamples() const
    {
        return (int) st.getSetting (SETTING_INITIAL_LATENCY);
    }

    // Number of output frames already available (no new input needed).
    int numSamplesAvailable() const
    {
        return (int) st.numSamples();
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
            const int totalIn = numInputSamples * channels;
            // Pre-allocated in AudioBuffer::prepare(); guard kept as safety net.
            if (interleavedIn.getNumChannels() != 1 || interleavedIn.getNumSamples() < totalIn)
            {
                jassertfalse; // interleavedIn should have been pre-allocated
                interleavedIn.setSize (1, totalIn, false, false, true);
            }

            auto* inInter = interleavedIn.getWritePointer (0);

            if (channels == 1)
            {
                juce::FloatVectorOperations::copy (inInter, input[0], numInputSamples);
            }
            else if (channels == 2)
            {
                const float* l = input[0];
                const float* r = input[1];
                for (int i = 0; i < numInputSamples; ++i)
                {
                    inInter[i * 2]     = l[i];
                    inInter[i * 2 + 1] = r[i];
                }
            }
            else
            {
                for (int i = 0; i < numInputSamples; ++i)
                    for (int ch = 0; ch < channels; ++ch)
                        inInter[i * channels + ch] = input[ch][i];
            }

            st.putSamples (inInter, (uint) numInputSamples);
        }

        if (flush)
            st.flush();

        if (maxOutputSamples <= 0 || output == nullptr)
            return 0;

        const int totalOut = maxOutputSamples * channels;
        // Pre-allocated in AudioBuffer::prepare(); guard kept as safety net.
        if (interleavedOut.getNumChannels() != 1 || interleavedOut.getNumSamples() < totalOut)
        {
            jassertfalse; // interleavedOut should have been pre-allocated
            interleavedOut.setSize (1, totalOut, false, false, true);
        }

        auto* outInter = interleavedOut.getWritePointer (0);
        const auto receivedFrames = (int) st.receiveSamples (outInter, (uint) maxOutputSamples);
        const int frames = juce::jlimit (0, maxOutputSamples, receivedFrames);

        if (channels == 1)
        {
            juce::FloatVectorOperations::copy (output[0], outInter, frames);
        }
        else if (channels == 2)
        {
            float* l = output[0];
            float* r = output[1];
            for (int i = 0; i < frames; ++i)
            {
                l[i] = outInter[i * 2];
                r[i] = outInter[i * 2 + 1];
            }
        }
        else
        {
            for (int i = 0; i < frames; ++i)
                for (int ch = 0; ch < channels; ++ch)
                    output[ch][i] = outInter[i * channels + ch];
        }

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
