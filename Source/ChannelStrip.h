/*
 ============================================================================== 
   ChannelStrip.h
   --------------------------------------------------------------------------
   Wraps an AudioBuffer plus a placeholder effect chain.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "AudioBuffer.h"

struct EffectChainPlaceholder
{
    // Future: dsp::ProcessorChain<...>
    bool delayEnabled = false;
    bool reverbEnabled = false;
    bool lowPassEnabled = false;
    bool highPassEnabled = false;
    bool tremoloEnabled = false;

    void reset() { delayEnabled = reverbEnabled = lowPassEnabled = highPassEnabled = tremoloEnabled = false; }
};

class ChannelStrip
{
public:
    explicit ChannelStrip(AudioBuffer* bufferPtr = nullptr) : buffer(bufferPtr) {}

    void setAudioBuffer(AudioBuffer* b) { buffer = b; }
    AudioBuffer* getAudioBuffer() const { return buffer; }
    EffectChainPlaceholder& effects() { return chain; }
    const EffectChainPlaceholder& effects() const { return chain; }

    void reset()
    {
        chain.reset();
        if (buffer) buffer->resetToDefaults();
    }

private:
    AudioBuffer* buffer = nullptr; // not owned
    EffectChainPlaceholder chain;
};
