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

    // Simple envelope describing a parameter change over musical bars
    struct EffectEnvelope
    {
        float start = 0.0f;
        float target = 0.0f;
        float durationBars = 0.0f; // 0 = instantaneous
        float progressBars = 0.0f; // accumulated
        enum class Curve { Linear } curve = Curve::Linear;

        bool isActive() const { return durationBars > 0.0f && progressBars < durationBars; }
        float current(float fallback) const
        {
            if (!isActive()) return fallback;
            float t = juce::jlimit(0.0f, 1.0f, progressBars / juce::jmax(0.0001f, durationBars));
            switch (curve) { case Curve::Linear: default: return start + (target - start) * t; }
        }
        void advance(float barsDelta) { progressBars += barsDelta; }
    };

    // Current parameter values (data-only for now)
    struct FxParams
    {
        float reverbWet = 0.0f;
        float delayFeedback = 0.0f;
        float lowPassCutoff = 20000.0f;
        float highPassCutoff = 20.0f;
        float tremoloDepth = 0.0f;
    };

    void reset()
    {
        chain.reset();
        params = FxParams{};
        reverbWetEnv = {};
        if (buffer) buffer->resetToDefaults();
    }

    // Envelope setters (expand as needed)
    void setReverbWetEnvelope(float start, float target, float durationBars)
    {
        reverbWetEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setDelayFeedbackEnvelope(float start, float target, float durationBars)
    {
        delayFeedbackEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setLowPassCutoffEnvelope(float start, float target, float durationBars)
    {
        lowPassCutoffEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setHighPassCutoffEnvelope(float start, float target, float durationBars)
    {
        highPassCutoffEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setTremoloDepthEnvelope(float start, float target, float durationBars)
    {
        tremoloDepthEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    // Advance envelopes by given bars; update current parameter values
    void advanceEnvelopes(float barsDelta)
    {
        // Reverb wet
        params.reverbWet = reverbWetEnv.isActive() ? reverbWetEnv.current(params.reverbWet) : params.reverbWet;
        if (reverbWetEnv.isActive()) reverbWetEnv.advance(barsDelta);
        // Delay feedback
        params.delayFeedback = delayFeedbackEnv.isActive() ? delayFeedbackEnv.current(params.delayFeedback) : params.delayFeedback;
        if (delayFeedbackEnv.isActive()) delayFeedbackEnv.advance(barsDelta);
        // LPF cutoff
        params.lowPassCutoff = lowPassCutoffEnv.isActive() ? lowPassCutoffEnv.current(params.lowPassCutoff) : params.lowPassCutoff;
        if (lowPassCutoffEnv.isActive()) lowPassCutoffEnv.advance(barsDelta);
        // HPF cutoff
        params.highPassCutoff = highPassCutoffEnv.isActive() ? highPassCutoffEnv.current(params.highPassCutoff) : params.highPassCutoff;
        if (highPassCutoffEnv.isActive()) highPassCutoffEnv.advance(barsDelta);
        // Tremolo depth
        params.tremoloDepth = tremoloDepthEnv.isActive() ? tremoloDepthEnv.current(params.tremoloDepth) : params.tremoloDepth;
        if (tremoloDepthEnv.isActive()) tremoloDepthEnv.advance(barsDelta);
    }

    const FxParams& getFxParams() const { return params; }

private:
    AudioBuffer* buffer = nullptr; // not owned
    EffectChainPlaceholder chain;
    FxParams params;
    EffectEnvelope reverbWetEnv;
    EffectEnvelope delayFeedbackEnv;
    EffectEnvelope lowPassCutoffEnv;
    EffectEnvelope highPassCutoffEnv;
    EffectEnvelope tremoloDepthEnv;
};
