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

            // (Re)allocate circular pre-delay buffer: size = maxDelaySamples + blockSize + 1
            const int maxDelaySamples = (int) std::ceil((kMaxPreDelayMs / 1000.0) * juce::jmax(1.0, lastSampleRate));
            preDelayBufferSize = maxDelaySamples + blockSize + 1; // +1 guard for modular arithmetic
            preDelayWritePos = 0;
            preDelayBuffer.setSize(2, preDelayBufferSize, false, false, true);
            preDelayBuffer.clear();

            // (Re)allocate delay line buffer similarly (max delay + blockSize + 1)
            const int maxMainDelaySamples = (int) std::ceil((kMaxDelayMs / 1000.0) * juce::jmax(1.0, lastSampleRate));
            delayBufferSize = maxMainDelaySamples + blockSize + 1; // +1 guard
            delayWritePos = 0;
            delayBuffer.setSize(2, delayBufferSize, false, false, true);
            delayBuffer.clear();

            // Prepare filters
            juce::dsp::ProcessSpec filterSpec { sampleRate, (juce::uint32) blockSize, 2 };
            for (int ch = 0; ch < 2; ++ch)
            {
                lowPass[ch].reset();
                lowPass[ch].prepare(filterSpec);
                highPass[ch].reset();
                highPass[ch].prepare(filterSpec);
                delayFbHighCut[ch].reset();
                delayFbHighCut[ch].prepare(filterSpec);
            }
            updateLowPassCoeffs(params.lowPassCutoff);
            updateHighPassCoeffs(params.highPassCutoff);
            updateDelayHighCutCoeffs(params.delayFeedbackHighCutHz);
        }
    }

    void processDSP(juce::AudioBuffer<float>& tempBuffer)
    {
        const int numSamples = tempBuffer.getNumSamples();
        const int numChannels = tempBuffer.getNumChannels();

    // --- Delay Processing (pre-reverb) ---
        if (effects().delayEnabled)
        {
            // Allocate delay line if needed
            if (delayBuffer.getNumChannels() != numChannels || delayBuffer.getNumSamples() != delayBufferSize)
            {
                delayBuffer.setSize(numChannels, delayBufferSize, false, false, true);
                delayBuffer.clear();
                delayWritePos = 0;
            }

            // Compute delay samples from params (clamped)
            juce::Array<int> tapSamples;
            if (delayTapTimesMs.isEmpty())
            {
                int single = (int) std::round((params.delayTimeMs / 1000.0) * juce::jmax(1.0, lastSampleRate));
                single = juce::jlimit(1, delayBufferSize - 1, single);
                tapSamples.add(single);
            }
            else
            {
                for (auto tMs : delayTapTimesMs)
                {
                    int s = (int) std::round((tMs / 1000.0f) * juce::jmax(1.0, lastSampleRate));
                    s = juce::jlimit(1, delayBufferSize - 1, s);
                    tapSamples.addIfNotAlreadyThere(s);
                }
            }
            float fb = juce::jlimit(0.0f, 0.95f, params.delayFeedback); // safety limit
            float wet = juce::jlimit(0.0f, 1.0f, params.delayWet);
            int numTaps = tapSamples.size();
            if (numTaps < 1) numTaps = 1;

            for (int i = 0; i < numSamples; ++i)
            {
                int writePos = delayWritePos;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* delayData = delayBuffer.getWritePointer(ch);
                    auto* oppDelayData = delayBuffer.getWritePointer(numChannels > 1 ? (1 - ch) : ch);
                    float inSample = tempBuffer.getSample(ch, i);
                    float delayedSum = 0.0f;
                    for (int tapIdx = 0; tapIdx < tapSamples.size(); ++tapIdx)
                    {
                        int readPosTap = writePos - tapSamples[tapIdx];
                        if (readPosTap < 0) readPosTap += delayBufferSize;
                        // For ping-pong, read from opposite channel buffer
                        delayedSum += (params.delayPingPong && numChannels > 1 ? oppDelayData[readPosTap] : delayData[readPosTap]);
                    }
                    float delayedAvg = delayedSum / (float) numTaps;
                    // High-cut only the feedback path to tame highs
                    float fbSample = delayedAvg;
                    fbSample = delayFbHighCut[ch].processSample(fbSample);
                    // Single write using averaged delayed signal for feedback stability
                    delayData[writePos] = inSample + fbSample * fb;
                    float out = inSample + delayedAvg * wet;
                    tempBuffer.setSample(ch, i, out);
                }
                if (++delayWritePos >= delayBufferSize) delayWritePos = 0;
            }
        }

        // --- Insert filters and tremolo prior to reverb ---
        // Update filter coefficients if cutoffs changed significantly
        if (effects().lowPassEnabled)
            updateLowPassCoeffs(params.lowPassCutoff);
        if (effects().highPassEnabled)
            updateHighPassCoeffs(params.highPassCutoff);

        if (effects().lowPassEnabled || effects().highPassEnabled || effects().tremoloEnabled)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    float s = data[i];
                    if (effects().highPassEnabled) s = highPass[ch].processSample(s);
                    if (effects().lowPassEnabled)  s = lowPass[ch].processSample(s);
                    if (effects().tremoloEnabled && params.tremoloDepth > 0.0f)
                    {
                        float trem = 0.5f * (1.0f + std::sin(tremoloPhase)); // 0..1
                        float depth = juce::jlimit(0.0f, 1.0f, params.tremoloDepth);
                        float gain = (1.0f - depth) + depth * trem; // crossfade to tremolo
                        s *= gain;
                        // advance phase once per sample (common to channels); update after ch loop first channel
                    }
                    data[i] = s;
                    if (ch == 0 && effects().tremoloEnabled && params.tremoloDepth > 0.0f)
                    {
                        float inc = (float)(2.0 * juce::MathConstants<double>::pi * juce::jmax(0.01f, params.tremoloRateHz) / juce::jmax(1.0, lastSampleRate));
                        tremoloPhase += inc;
                        if (tremoloPhase > juce::MathConstants<float>::twoPi) tremoloPhase -= juce::MathConstants<float>::twoPi;
                    }
                }
            }
        }

        // --- Reverb Processing ---
        if (!effects().reverbEnabled)
            return; // skip reverb portion if disabled (delay may still have processed)

        // Ensure pre-delay buffer matches channel count (reallocate if channel count changed)
        if (preDelayBuffer.getNumChannels() != numChannels)
        {
            juce::AudioBuffer<float> newBuf(numChannels, preDelayBufferSize);
            newBuf.clear();
            // copy existing channel data where possible (up to min channels)
            const int copyCh = juce::jmin(preDelayBuffer.getNumChannels(), newBuf.getNumChannels());
            for (int ch = 0; ch < copyCh; ++ch)
                newBuf.copyFrom(ch, 0, preDelayBuffer, ch, 0, preDelayBufferSize);
            preDelayBuffer = std::move(newBuf);
        }

        // Prepare wet buffer (delayed input) size
        juce::AudioBuffer<float> wetBuffer(numChannels, numSamples);
        wetBuffer.clear();

        // Compute desired pre-delay samples from params (circular buffer based, continuous across blocks)
        const int delaySamples = (int) juce::jlimit(0.0, (kMaxPreDelayMs / 1000.0) * lastSampleRate,
                                                  (params.reverbPreDelayMs / 1000.0) * juce::jmax(1.0, lastSampleRate));

        // Circular buffer read/write (advance position once per sample for all channels to avoid inter-channel drift)
        const int bufferSize = preDelayBufferSize;
        for (int i = 0; i < numSamples; ++i)
        {
            int writePos = preDelayWritePos;
            int readPos = writePos - delaySamples;
            if (readPos < 0) readPos += bufferSize;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* delayData = preDelayBuffer.getWritePointer(ch);
                const float inSample = tempBuffer.getSample(ch, i);
                delayData[writePos] = inSample;
                const float delayed = delaySamples > 0 ? delayData[readPos] : inSample;
                wetBuffer.setSample(ch, i, delayed);
            }
            if (++preDelayWritePos >= bufferSize) preDelayWritePos = 0;
        }

        // Reverb parameters: wet-only processing
        juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.80f; // slightly smaller to reduce metallic density
    rp.damping  = 0.40f; // more damping to tame highs
        rp.width    = 1.0f;
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        reverb.setParameters(rp);

        juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
        juce::dsp::ProcessContextReplacing<float> wetCtx(wetBlock);
        reverb.process(wetCtx);

        // Perceptual scaling of wet contribution
        float linearWet = juce::jlimit(0.0f, 1.0f, params.reverbWet);
        float wetGain = std::pow(linearWet, 0.85f);
        float dryGain = 1.0f - (wetGain * 0.3f);
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
        float delayFeedback = 0.0f;    // 0..~0.9
        float delayTimeMs = 400.0f;     // default ~quarter note at 150 BPM (placeholder)
        float delayWet = 0.35f;         // mix level for delay repeats
        bool  delayPingPong = false;    // ping-pong cross feedback
        float delayFeedbackHighCutHz = 6000.0f; // high-cut inside feedback loop
        float lowPassCutoff = 20000.0f;
        float highPassCutoff = 20.0f;
        float tremoloDepth = 0.0f;
        float tremoloRateHz = 4.0f; // default rate if not set from BPM
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
        // If a dub burst is active, and the rise finished, start the fall envelope exactly once
        if (dubBurst.active && !dubBurst.fallingStarted && !delayFeedbackEnv.isActive())
        {
            setDelayFeedbackEnvelope(params.delayFeedback, dubBurst.fallTarget, dubBurst.fallDurationBars);
            dubBurst.fallingStarted = true;
        }
        // Auto-disable delay when feedback essentially zero and no active envelope
        if (chain.delayEnabled && !delayFeedbackEnv.isActive() && params.delayFeedback <= 0.0001f)
        {
            chain.delayEnabled = false;
            dubBurst.active = false;
            dubBurst.fallingStarted = false;
        }
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
    FxParams& getMutableFxParams() { return params; }

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
    // Circular pre-delay buffer
    juce::AudioBuffer<float> preDelayBuffer; // channels match current audio block
    int preDelayBufferSize = 0;
    int preDelayWritePos = 0;
    static constexpr double kMaxPreDelayMs = 60.0; // matches envelope clamp
    // Delay line
    juce::AudioBuffer<float> delayBuffer;
    int delayBufferSize = 0; // allocated in prepare
    int delayWritePos = 0;
    static constexpr double kMaxDelayMs = 2000.0; // 2 seconds max
    // Multi-tap support: user may select multiple divisions (times in ms)
    juce::Array<float> delayTapTimesMs; // if empty -> single params.delayTimeMs
public:
    void setDelayTapTimesMs(const juce::Array<float>& times)
    {
        delayTapTimesMs.clear();
        for (auto t : times)
        {
            t = juce::jlimit(1.0f, (float) kMaxDelayMs, t);
            delayTapTimesMs.add(t);
        }
    }
    // Dub-style burst controller
    void startDubDelayBurst(float /*riseTarget*/, float /*riseDurationBars*/, float fallTarget, float fallDurationBars)
    {
        dubBurst.active = true;
        dubBurst.fallingStarted = false;
        dubBurst.fallTarget = juce::jlimit(0.0f, 0.95f, fallTarget);
        dubBurst.fallDurationBars = juce::jmax(0.0f, fallDurationBars);
    }
private:
    // Filters
    juce::dsp::IIR::Filter<float> lowPass[2];
    juce::dsp::IIR::Filter<float> highPass[2];
    juce::dsp::IIR::Filter<float> delayFbHighCut[2];
    float lastLowPassCutoff = -1.0f;
    float lastHighPassCutoff = -1.0f;
    float lastDelayHighCut = -1.0f;
    float tremoloPhase = 0.0f;
    void updateLowPassCoeffs(float cutoff)
    {
        cutoff = juce::jlimit(20.0f, 20000.0f, cutoff);
        if (std::abs(cutoff - lastLowPassCutoff) < 1.0f) return;
        auto coeff = juce::dsp::IIR::Coefficients<float>::makeLowPass(lastSampleRate, cutoff);
        for (int ch = 0; ch < 2; ++ch) *lowPass[ch].coefficients = *coeff;
        lastLowPassCutoff = cutoff;
    }
    void updateHighPassCoeffs(float cutoff)
    {
        cutoff = juce::jlimit(20.0f, 1000.0f, cutoff);
        if (std::abs(cutoff - lastHighPassCutoff) < 1.0f) return;
        auto coeff = juce::dsp::IIR::Coefficients<float>::makeHighPass(lastSampleRate, cutoff);
        for (int ch = 0; ch < 2; ++ch) *highPass[ch].coefficients = *coeff;
        lastHighPassCutoff = cutoff;
    }
    void updateDelayHighCutCoeffs(float cutoff)
    {
        cutoff = juce::jlimit(1000.0f, 12000.0f, cutoff);
        if (std::abs(cutoff - lastDelayHighCut) < 1.0f) return;
        auto coeff = juce::dsp::IIR::Coefficients<float>::makeLowPass(lastSampleRate, cutoff);
        for (int ch = 0; ch < 2; ++ch) *delayFbHighCut[ch].coefficients = *coeff;
        lastDelayHighCut = cutoff;
    }
    struct DubBurstState { bool active = false; bool fallingStarted = false; float fallTarget = 0.0f; float fallDurationBars = 0.0f; } dubBurst;
};
