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
    // DSP: simple reverb processor (per-strip). Prepare once and process temp buffers.
    void prepareDSP(double sampleRate, int blockSize)
    {
        // Prepare once or when configuration changes; do NOT reset every audio block
        if (!reverbPrepared || lastSampleRate != sampleRate || lastBlockSize != blockSize)
        {
            juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 2 };
            reverb.reset();
            reverb.prepare(spec);
            reverbPrepared = true;
            lastSampleRate = sampleRate;
            lastBlockSize = blockSize;
        }
    }

    void processDSP(juce::AudioBuffer<float>& tempBuffer)
    {
        if (!effects().reverbEnabled) return;
        const int numSamples = tempBuffer.getNumSamples();
        const int numChannels = tempBuffer.getNumChannels();

        // Build a wet-only path by processing a delayed copy through the reverb
        juce::AudioBuffer<float> wetBuffer(numChannels, numSamples);
        wetBuffer.makeCopyOf(tempBuffer);

        // Apply simple pre-delay (intra-block shift) only if it fits inside current block
        const int delaySamples = (int) std::round((params.reverbPreDelayMs / 1000.0) * juce::jmax(1.0, lastSampleRate));
        if (delaySamples > 0 && delaySamples < numSamples)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = wetBuffer.getWritePointer(ch);
                // Shift from end backward to avoid overwrite
                for (int i = numSamples - 1; i >= delaySamples; --i)
                    data[i] = data[i - delaySamples];
                // Zero the newly exposed leading region
                juce::FloatVectorOperations::clear(data, delaySamples);
            }
        }
        // If delaySamples >= numSamples we skip (avoids full zeroing that killed reverb input)

        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = 0.95f;  // very large room for longer tail
        rp.damping  = 0.20f;  // less damping for longer decay
        rp.width    = 1.0f;
        rp.wetLevel = 1.0f;   // wet-only output
        rp.dryLevel = 0.0f;   // keep dry at unity in original buffer
        reverb.setParameters(rp);

        juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
        juce::dsp::ProcessContextReplacing<float> wetCtx(wetBlock);
        reverb.process(wetCtx);

        // Perceptual scaling: make low percentages less dominant, high more noticeable
        float linearWet = juce::jlimit(0.0f, 1.0f, params.reverbWet);
        // Map: 0.25->0.20, 0.50->0.45, 0.75->0.75, 1.0->1.0 approximately using a gentle curve
        float wetGain = std::pow(linearWet, 0.85f); // exponent < 1 lifts higher values
        // Slight dry attenuation at high wet to reveal tail without killing presence
        float dryGain = 1.0f - (wetGain * 0.3f); // up to -0.3 at full wet
        dryGain = juce::jlimit(0.7f, 1.0f, dryGain);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = tempBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) data[i] *= dryGain;
            tempBuffer.addFrom(ch, 0, wetBuffer, ch, 0, numSamples, wetGain);
        }
    }

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
        float reverbPreDelayMs = 0.0f; // pre-delay applied to reverb wet path only
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
        reverbPreDelayEnv = {};
        delayFeedbackEnv = {};
        lowPassCutoffEnv = {};
        highPassCutoffEnv = {};
        tremoloDepthEnv = {};
        if (buffer) buffer->resetToDefaults();
    }

    // Envelope setters (expand as needed)
    void setReverbWetEnvelope(float start, float target, float durationBars)
    {
        reverbWetEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setReverbPreDelayEnvelope(float startMs, float targetMs, float durationBars)
    {
        // Clamp to sensible range 0..60 ms
        startMs = juce::jlimit(0.0f, 60.0f, startMs);
        targetMs = juce::jlimit(0.0f, 60.0f, targetMs);
        reverbPreDelayEnv = { startMs, targetMs, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
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
        // PreDelay
        params.reverbPreDelayMs = reverbPreDelayEnv.isActive() ? reverbPreDelayEnv.current(params.reverbPreDelayMs) : params.reverbPreDelayMs;
        if (reverbPreDelayEnv.isActive()) reverbPreDelayEnv.advance(barsDelta);
        // Auto-disable reverb when wet reaches zero and no active envelope
        if (chain.reverbEnabled && !reverbWetEnv.isActive() && params.reverbWet <= 0.0001f)
            chain.reverbEnabled = false;
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
    EffectEnvelope reverbPreDelayEnv;
    EffectEnvelope delayFeedbackEnv;
    EffectEnvelope lowPassCutoffEnv;
    EffectEnvelope highPassCutoffEnv;
    EffectEnvelope tremoloDepthEnv;
    juce::dsp::Reverb reverb;
    bool reverbPrepared = false;
    double lastSampleRate = 0.0;
    int lastBlockSize = 0;
};
