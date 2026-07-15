/*
 ============================================================================== 
   ChannelStrip.h
   --------------------------------------------------------------------------
   Wraps an AudioBuffer plus a placeholder effect chain.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "AudioBuffer.h"
#include "NodeClipDetector.h"

struct EffectChainPlaceholder
{
    // Future: dsp::ProcessorChain<...>
    bool delayEnabled = false;
    bool reverbEnabled = false;
    bool lowPassEnabled = false;
    bool highPassEnabled = false;
    bool tremoloEnabled = false;
    bool chorusEnabled = false;
    bool autoPanEnabled = false;
    bool volumeRampEnabled = false;
    bool shLowPassEnabled = false;
    bool shHighPassEnabled = false;
    bool granularEnabled = false;

    void reset() { delayEnabled = reverbEnabled = lowPassEnabled = highPassEnabled = tremoloEnabled = chorusEnabled = autoPanEnabled = volumeRampEnabled = shLowPassEnabled = shHighPassEnabled = granularEnabled = false; }
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
            limiter.prepare(spec);
            limiter.setThreshold(-0.1f);  // -0.1dB to prevent clipping
            limiter.setRelease(100.0f);   // 100ms release
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
                shLowPass[ch].reset();
                shLowPass[ch].prepare(filterSpec);
                shHighPass[ch].reset();
                shHighPass[ch].prepare(filterSpec);
            }
            updateLowPassCoeffs(params.lowPassCutoff);
            updateHighPassCoeffs(params.highPassCutoff);
            updateDelayHighCutCoeffs(params.delayFeedbackHighCutHz);
            updateSHLowPassCoeffs(params.shLpfCutoff, params.shLpfQ);
            updateSHHighPassCoeffs(params.shHpfCutoff, params.shHpfQ);

            // §6.3  Pre-allocate ducking gains buffer so processDSP never
            // calls resize() on the audio thread.
            duckGains.resize((size_t) blockSize);
            tremoloGainTable.resize((size_t) blockSize);

            // (Re)allocate chorus delay buffer
            const int maxChorusDelaySamples = (int) std::ceil((kMaxChorusDelayMs / 1000.0) * juce::jmax(1.0, lastSampleRate));
            chorusDelayBufferSize = maxChorusDelaySamples + blockSize + 1;
            chorusDelayWritePos = 0;
            chorusDelayBuffer.setSize(2, chorusDelayBufferSize, false, false, true);
            chorusDelayBuffer.clear();

            // Granular capture buffer
            granular.prepare(sampleRate);

            // BUG 6: reset boundary-declick state on (re)prepare.
            declickFadeInTotal.store(0, std::memory_order_relaxed);
            declickFadeInProgress.store(0, std::memory_order_relaxed);
            declickStep[0] = declickStep[1] = 0.0f;
            declickLastOut[0] = declickLastOut[1] = 0.0f;
        }
    }

    void processDSP(juce::AudioBuffer<float>& tempBuffer)
    {
        processDSPChain(tempBuffer);

        // BUG 6 fix: boundary de-click runs at the very end of the FX chain so
        // it sees the final per-pad output (including any early-return path when
        // reverb is disabled).  See applyBoundaryDeclick() for details.
        applyBoundaryDeclick(tempBuffer);
    }

private:
    void processDSPChain(juce::AudioBuffer<float>& tempBuffer)
    {
        const int numSamples = tempBuffer.getNumSamples();
        const int numChannels = tempBuffer.getNumChannels();

        // NOTE (BUG 6 fix): the old "declick fade-in" here ramped the block from
        // SILENCE to unity, which itself created a full-amplitude step at the
        // block boundary (prev block ended at signal level, this one started at
        // ~0).  It has been replaced by a boundary-step correction applied after
        // the chain — see applyBoundaryDeclick().

        // Precompute per-sample ducking gains from incoming signal (before FX)
        if (params.duckingEnabled && numSamples > 0)
        {
            // Pre-allocated in prepareDSP(); guard kept as safety net.
            if ((int)duckGains.size() < numSamples)
                duckGains.resize((size_t) numSamples);
            // compute attack/release coeffs from sample rate and release param
            updateDuckingCoeffs(params.duckReleaseMs);
            for (int i = 0; i < numSamples; ++i)
            {
                float x = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    x += std::abs(tempBuffer.getSample(ch, i));
                x /= (float) juce::jmax(1, numChannels);
                if (x > duckEnv) duckEnv += duckAttackCoeff * (x - duckEnv);
                else             duckEnv += duckReleaseCoeff * (x - duckEnv);
                const float ref = 0.25f; // ~ -12 dB reference
                float activity = juce::jlimit(0.0f, 1.0f, duckEnv / ref);
                duckGains[(size_t)i] = 1.0f - (params.duckAmount * activity);
            }
        }

        // --- Insert filters and tremolo prior to reverb ---
        // §3.2A: Split into separate branch-free loops for vectorization.

        // --- High-pass filter (IIR, separate loop per channel) ---
        if (effects().highPassEnabled)
        {
            updateHighPassCoeffs(params.highPassCutoff);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] = highPass[ch].processSample(data[i]);
            }

            // Clip probe: after high-pass
            if (clipProbes) (*clipProbes)[NodeId::HighPass].inspect(tempBuffer, numSamples);
        }

        // --- Low-pass filter (IIR, separate loop per channel) ---
        if (effects().lowPassEnabled)
        {
            updateLowPassCoeffs(params.lowPassCutoff);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] = lowPass[ch].processSample(data[i]);
            }

            // Clip probe: after low-pass
            if (clipProbes) (*clipProbes)[NodeId::LowPass].inspect(tempBuffer, numSamples);
        }

        // --- S&H Low-pass filter (IIR with variable Q) ---
        if (effects().shLowPassEnabled)
        {
            updateSHLowPassCoeffs(params.shLpfCutoff, params.shLpfQ);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] = shLowPass[ch].processSample(data[i]);
            }
        }

        // --- S&H High-pass filter (IIR with variable Q) ---
        if (effects().shHighPassEnabled)
        {
            updateSHHighPassCoeffs(params.shHpfCutoff, params.shHpfQ);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] = shHighPass[ch].processSample(data[i]);
            }
        }

    // --- Delay Processing (post-S&H filters) ---
        if (effects().delayEnabled)
        {
            // Allocate delay line if needed
            if (delayBuffer.getNumChannels() != numChannels || delayBuffer.getNumSamples() != delayBufferSize)
            {
                delayBuffer.setSize(numChannels, delayBufferSize, false, false, true);
                delayBuffer.clear();
                delayWritePos = 0;
            }

            // Update delay feedback high cut if changed (cheap no-op when unchanged)
            updateDelayHighCutCoeffs(params.delayFeedbackHighCutHz);

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

            // Wow/Flutter precompute (in samples) and phase increments
            const float wowDepthSamples = params.wowFlutterEnabled ? (params.wowDepthMs * (float) (lastSampleRate * 0.001)) : 0.0f;
            const float flutterDepthSamples = params.wowFlutterEnabled ? (params.flutterDepthMs * (float) (lastSampleRate * 0.001)) : 0.0f;
            const float wowInc = (float)(2.0 * juce::MathConstants<double>::pi * juce::jmax(0.0f, params.wowRateHz) / juce::jmax(1.0, lastSampleRate));
            const float flutterInc = (float)(2.0 * juce::MathConstants<double>::pi * juce::jmax(0.0f, params.flutterRateHz) / juce::jmax(1.0, lastSampleRate));

            for (int i = 0; i < numSamples; ++i)
            {
                int writePos = delayWritePos;
                // Compute current modulation offset in samples (shared across channels)
                float modSamples = 0.0f;
                if (params.wowFlutterEnabled)
                    modSamples = wowDepthSamples * std::sin(wowPhase) + flutterDepthSamples * std::sin(flutterPhase);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* delayData = delayBuffer.getWritePointer(ch);
                    auto* oppDelayData = delayBuffer.getWritePointer(numChannels > 1 ? (1 - ch) : ch);
                    float inSample = tempBuffer.getSample(ch, i);
                    float delayedSum = 0.0f;
                    for (int tapIdx = 0; tapIdx < tapSamples.size(); ++tapIdx)
                    {
                        // Fractional read position with wow/flutter modulation
                        double readPosTap = (double) writePos - (double) tapSamples[tapIdx] - (double) modSamples;
                        while (readPosTap < 0.0) readPosTap += (double) delayBufferSize;
                        while (readPosTap >= (double) delayBufferSize) readPosTap -= (double) delayBufferSize;
                        int idx0 = (int) readPosTap;
                        int idx1 = idx0 + 1; if (idx1 >= delayBufferSize) idx1 = 0;
                        float frac = (float) (readPosTap - (double) idx0);
                        const float* src = (params.delayPingPong && numChannels > 1) ? oppDelayData : delayData;
                        float s = src[idx0] * (1.0f - frac) + src[idx1] * frac;
                        delayedSum += s;
                    }
                    float delayedAvg = delayedSum / (float) numTaps;
                    // High-cut only the feedback path to tame highs
                    float fbSample = delayedAvg;
                    fbSample = delayFbHighCut[ch].processSample(fbSample);
                    // §3.2D: Fast rational tanh approximation (~5x faster, audibly identical)
                    const float drive = juce::jmax(1.0f, params.delayFbDrive);
                    const float x = fbSample * drive;
                    const float x2 = x * x;
                    const float fbSat = x * (27.0f + x2) / (27.0f + 9.0f * x2);
                    // Single write using averaged delayed signal for feedback stability
                    delayData[writePos] = inSample + fbSat * fb;
                    const float duck = (params.duckingEnabled && !duckGains.empty() ? duckGains[(size_t)i] : 1.0f);
                    float out = inSample + delayedAvg * (wet * duck);
                    tempBuffer.setSample(ch, i, out);
                }
                // Advance modulation phases once per sample
                if (params.wowFlutterEnabled)
                {
                    wowPhase += wowInc;
                    flutterPhase += flutterInc;
                    if (wowPhase > juce::MathConstants<float>::twoPi) wowPhase -= juce::MathConstants<float>::twoPi;
                    if (flutterPhase > juce::MathConstants<float>::twoPi) flutterPhase -= juce::MathConstants<float>::twoPi;
                }
                if (++delayWritePos >= delayBufferSize) delayWritePos = 0;
            }

            // Clip probe: after delay
            if (clipProbes) (*clipProbes)[NodeId::Delay].inspect(tempBuffer, numSamples);
        }

        // --- Tremolo (vectorizable gain modulation) ---
        if (effects().tremoloEnabled && params.tremoloDepth > 0.0f && numSamples > 0)
        {
            const float depth = juce::jlimit(0.0f, 1.0f, params.tremoloDepth);
            const float phaseInc = (float)(2.0 * juce::MathConstants<double>::pi
                                           * juce::jmax(0.01f, params.tremoloRateHz)
                                           / juce::jmax(1.0, lastSampleRate));
            const float oneMinusDepth = 1.0f - depth;

            // Pre-compute per-sample gain table (shared across channels)
            if ((int)tremoloGainTable.size() < numSamples)
                tremoloGainTable.resize((size_t)numSamples);

            float phase = tremoloPhase;
            for (int i = 0; i < numSamples; ++i)
            {
                const float trem = 0.5f * (1.0f + std::sin(phase));
                tremoloGainTable[(size_t)i] = oneMinusDepth + depth * trem;
                phase += phaseInc;
                if (phase > juce::MathConstants<float>::twoPi)
                    phase -= juce::MathConstants<float>::twoPi;
            }
            tremoloPhase = phase;

            // Apply gain table to each channel (vectorizable multiply)
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply(tempBuffer.getWritePointer(ch),
                                                      tremoloGainTable.data(),
                                                      numSamples);

            // Clip probe: after tremolo
            if (clipProbes) (*clipProbes)[NodeId::Tremolo].inspect(tempBuffer, numSamples);
        }

        // --- Reverb Processing ---
        // --- Chorus Processing (modulated short delay) ---
        if (effects().chorusEnabled && chorusDelayBufferSize > 0)
        {
            // Ensure chorus buffer matches channel count
            if (chorusDelayBuffer.getNumChannels() != numChannels || chorusDelayBuffer.getNumSamples() != chorusDelayBufferSize)
            {
                chorusDelayBuffer.setSize(numChannels, chorusDelayBufferSize, false, false, true);
                chorusDelayBuffer.clear();
                chorusDelayWritePos = 0;
            }

            const float baseDelaySamples = (float)((params.chorusDelayMs / 1000.0) * juce::jmax(1.0, lastSampleRate));
            const float maxModSamples = (float)((params.chorusDepth * 5.0 / 1000.0) * juce::jmax(1.0, lastSampleRate)); // up to 5ms modulation
            const float lfoInc = (float)(2.0 * juce::MathConstants<double>::pi * juce::jmax(0.01f, params.chorusRateHz) / juce::jmax(1.0, lastSampleRate));
            const float wet = juce::jlimit(0.0f, 1.0f, params.chorusMix);
            const float dry = 1.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                int writePos = chorusDelayWritePos;
                // LFO modulation (sine)
                float lfoVal = std::sin(chorusLfoPhase);
                float modDelaySamples = baseDelaySamples + maxModSamples * lfoVal;
                modDelaySamples = juce::jlimit(0.5f, (float)(chorusDelayBufferSize - 2), modDelaySamples);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* chorusData = chorusDelayBuffer.getWritePointer(ch);
                    float inSample = tempBuffer.getSample(ch, i);

                    // Write dry sample to chorus delay buffer
                    chorusData[writePos] = inSample;

                    // Fractional read (linear interpolation)
                    double readPos = (double)writePos - (double)modDelaySamples;
                    while (readPos < 0.0) readPos += (double)chorusDelayBufferSize;
                    int idx0 = (int)readPos;
                    int idx1 = idx0 + 1; if (idx1 >= chorusDelayBufferSize) idx1 = 0;
                    float frac = (float)(readPos - (double)idx0);
                    float delayed = chorusData[idx0] * (1.0f - frac) + chorusData[idx1] * frac;

                    // Mix: dry + wet chorus signal
                    float out = inSample * dry + delayed * wet;
                    tempBuffer.setSample(ch, i, out);
                }

                // Advance LFO phase (per second voice; adding slight offset for stereo width)
                chorusLfoPhase += lfoInc;
                if (chorusLfoPhase > juce::MathConstants<float>::twoPi)
                    chorusLfoPhase -= juce::MathConstants<float>::twoPi;

                if (++chorusDelayWritePos >= chorusDelayBufferSize)
                    chorusDelayWritePos = 0;
            }

            // Clip probe: after chorus
            if (clipProbes) (*clipProbes)[NodeId::Chorus].inspect(tempBuffer, numSamples);
        }

        // --- Auto-Pan Processing (LFO-driven L/R gain) ---
        if (effects().autoPanEnabled && numChannels >= 2)
        {
            const float mix = juce::jlimit(0.0f, 1.0f, params.panMix);
            const float depth = juce::jlimit(0.0f, 1.0f, params.panDepth);
            const float lfoInc = (float)(2.0 * juce::MathConstants<double>::pi * juce::jmax(0.01f, params.panRateHz) / juce::jmax(1.0, lastSampleRate));

            auto* dataL = tempBuffer.getWritePointer(0);
            auto* dataR = tempBuffer.getWritePointer(1);

            for (int i = 0; i < numSamples; ++i)
            {
                // Sine LFO: positive half → right, negative half → left
                float lfoVal = std::sin(panLfoPhase); // -1..+1

                // Constant-power panning: 0.5 + 0.5*lfo gives 0..1 range
                float panPos = 0.5f + 0.5f * lfoVal * depth; // 0..1 (0=full L, 1=full R)

                // Constant-power gains using sin/cos quarter-cycle
                float gainL = std::cos(panPos * juce::MathConstants<float>::halfPi);
                float gainR = std::sin(panPos * juce::MathConstants<float>::halfPi);

                // Blend panned signal with dry (centre) signal
                float dryL = dataL[i];
                float dryR = dataR[i];
                float mono = (dryL + dryR) * 0.5f;
                float wetL = mono * gainL;
                float wetR = mono * gainR;

                dataL[i] = dryL * (1.0f - mix) + wetL * mix;
                dataR[i] = dryR * (1.0f - mix) + wetR * mix;

                panLfoPhase += lfoInc;
                if (panLfoPhase > juce::MathConstants<float>::twoPi)
                    panLfoPhase -= juce::MathConstants<float>::twoPi;
            }

            // Clip probe: after auto-pan
            if (clipProbes) (*clipProbes)[NodeId::AutoPan].inspect(tempBuffer, numSamples);
        }

        // ── Granular processing (Clouds-inspired) ──
        if (chain.granularEnabled && params.grainMix > 0.0001f)
        {
            granular.process(tempBuffer, numSamples,
                             params.grainDensityHz, params.grainSizeMs,
                             params.grainPitchSpread, params.grainMix,
                             params.grainTexture);
        }

        // Apply volume ramp gain (before reverb and limiter) if active
        if (chain.volumeRampEnabled && params.volumeGain < 0.9999f)
        {
            tempBuffer.applyGain(params.volumeGain);

            // Clip probe: after volume ramp
            if (clipProbes) (*clipProbes)[NodeId::VolumeRamp].inspect(tempBuffer, numSamples);
        }

        // Apply limiter before reverb to keep wet-path clean
        {
            juce::dsp::AudioBlock<float> preReverbBlock(tempBuffer);
            juce::dsp::ProcessContextReplacing<float> preReverbCtx(preReverbBlock);
            limiter.process(preReverbCtx);

            // Clip probe: after pre-reverb limiter
            if (clipProbes) (*clipProbes)[NodeId::PreReverbLimiter].inspect(tempBuffer, numSamples);
        }

        // --- Reverb Processing (continued) ---
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

        // Prepare wet buffer (delayed input) size without per-block allocation
        if (reverbWetBuffer.getNumChannels() != numChannels || reverbWetBuffer.getNumSamples() < numSamples)
        {
            // allocate only when channel count or block size grows; allow shrinking without realloc
            reverbWetBuffer.setSize(numChannels, numSamples, false, false, true);
        }
        reverbWetBuffer.clear();

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
                reverbWetBuffer.setSample(ch, i, delayed);
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

    juce::dsp::AudioBlock<float> wetBlock(reverbWetBuffer);
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
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] *= dryGain;
                const float duck = (params.duckingEnabled && !duckGains.empty() ? duckGains[(size_t)i] : 1.0f);
                data[i] += reverbWetBuffer.getSample(ch, i) * (wetGain * duck);
            }
        }

        // Clip probe: after reverb mix
        if (clipProbes) (*clipProbes)[NodeId::ReverbMix].inspect(tempBuffer, numSamples);

        // Apply limiter again post-reverb to catch any energy added by the wet mix
        juce::dsp::AudioBlock<float> block(tempBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        limiter.process(context);

        // Clip probe: after post-reverb limiter
        if (clipProbes) (*clipProbes)[NodeId::PostReverbLimiter].inspect(tempBuffer, numSamples);
    }

    /** BUG 6 fix: boundary de-click (swan-ramp style offset correction).

        When armed via requestDeclickFadeIn(), measures the step between the
        last sample of the previous block's output and the first sample of the
        current block, then adds a correction offset that starts at -step and
        decays to zero with a raised-cosine shape.  Unlike the old fade-from-
        silence, this preserves the new signal's waveform and introduces no
        amplitude drop — it only removes the boundary discontinuity caused by
        abrupt FX parameter swaps.

        Also continuously tracks the last output sample so the measurement is
        available whenever the declick is armed. */
    void applyBoundaryDeclick(juce::AudioBuffer<float>& tempBuffer)
    {
        const int numSamples = tempBuffer.getNumSamples();
        const int numChannels = juce::jmin(tempBuffer.getNumChannels(), 2);
        if (numSamples <= 0 || numChannels <= 0)
            return;

        const int fadeTotal = declickFadeInTotal.load(std::memory_order_acquire);
        if (fadeTotal > 0)
        {
            int progress = declickFadeInProgress.load(std::memory_order_relaxed);

            // On the first block of the correction, capture the boundary step.
            if (progress == 0)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    declickStep[ch] = tempBuffer.getReadPointer(ch)[0] - declickLastOut[ch];
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = tempBuffer.getWritePointer(ch);
                int p = progress;
                for (int i = 0; i < numSamples && p < fadeTotal; ++i, ++p)
                {
                    // Raised-cosine decay 1 → 0 (zero slope at both ends).
                    const float t = (float)(p + 1) / (float)(fadeTotal + 1);
                    const float w = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * t));
                    data[i] -= declickStep[ch] * w;
                }
            }

            progress = juce::jmin(fadeTotal, progress + numSamples);
            declickFadeInProgress.store(progress, std::memory_order_relaxed);
            if (progress >= fadeTotal)
                declickFadeInTotal.store(0, std::memory_order_relaxed);
        }

        // Track last output sample for the next boundary measurement.
        for (int ch = 0; ch < numChannels; ++ch)
            declickLastOut[ch] = tempBuffer.getReadPointer(ch)[numSamples - 1];
    }

public:
    void setAudioBuffer(AudioBuffer* b) { buffer = b; }
    AudioBuffer* getAudioBuffer() const { return buffer; }
    void setClipProbes(PadProbeSet* probes) { clipProbes = probes; }
    PadProbeSet* getClipProbes() const { return clipProbes; }
    EffectChainPlaceholder& effects() { return chain; }
    const EffectChainPlaceholder& effects() const { return chain; }

    /** Arm the boundary de-click for the next audio block.
        Call after abruptly changing FX params to mask discontinuities.

        BUG 6 fix: the old implementation faded the next block in from SILENCE,
        which itself created a full-amplitude step at the block boundary (the
        loudest click in the plugin).  The new implementation measures the step
        between the last output sample of the previous block and the first
        sample of the new block, then adds an offset correction that starts at
        exactly -step and decays to zero over `samples` samples with a
        raised-cosine shape.  The new signal's waveform is preserved; only the
        boundary discontinuity is removed.  Zero latency. */
    void requestDeclickFadeIn(int samples = 512)
    {
        declickFadeInProgress.store(0, std::memory_order_relaxed);
        declickFadeInTotal.store(samples, std::memory_order_release);
    }

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
        float delayFbDrive = 1.0f;      // feedback drive for saturation (1.0 = neutral)
    bool  duckingEnabled = true;    // enable ducking for delay/reverb wet (default ON)
    float duckAmount = 0.5f;        // 0..1 amount of ducking
    float duckReleaseMs = 250.0f;   // release time back to full wet
        // Wow/Flutter (tape-like delay time modulation)
        bool  wowFlutterEnabled = false;
        float wowDepthMs = 3.0f;
        float wowRateHz = 0.35f;
        float flutterDepthMs = 0.8f;
        float flutterRateHz = 6.0f;
        // Musical periods to preserve tempo-synced choices across BPM changes
        // If > 0, wowRateHz/flutterRateHz should be derived from these periods in bars
        float wowPeriodBars = 0.0f;      // e.g., 0.25 (quarter note), 0.5, 1.0, 2.0, 4.0
        float flutterPeriodBars = 0.0f;  // e.g., 1.0 (one bar), 0.5, 2.0
        float lowPassCutoff = 20000.0f;
        float highPassCutoff = 20.0f;
        float tremoloDepth = 0.0f;
        float tremoloRateHz = 4.0f; // default rate if not set from BPM
        // Chorus
        float chorusDepth = 0.5f;    // LFO modulation depth (0..1)
        float chorusRateHz = 1.0f;   // LFO rate
        float chorusMix = 0.5f;      // wet/dry mix (0..1)
        float chorusDelayMs = 7.0f;  // base delay for chorus modulation (ms)
        // Auto-pan
        float panRateHz = 2.0f;      // LFO rate (tempo-synced)
        float panDepth = 1.0f;       // 0..1 how far L/R the sweep goes
        float panMix = 0.5f;         // 0..1 wet/dry (0 = bypass)
        float panPeriodBars = 0.0f;  // musical period in bars (for BPM resync)
        float volumeGain = 1.0f;      // 0..1 overall channel gain (used by volume ramp modifier)
        // S&H filter parameters (persistent until reset)
        float shLpfCutoff = 4000.0f;  // current held low-pass cutoff (200..8000 Hz)
        float shLpfQ = 1.0f;          // current held low-pass Q (0.5..4.0)
        float shHpfCutoff = 200.0f;   // current held high-pass cutoff (60..800 Hz)
        float shHpfQ = 1.0f;          // current held high-pass Q (0.5..4.0)
        float shDivisionBars = 0.25f; // S&H rate: 0.0625=1/16, 0.125=1/8, 0.25=1/4
        // Granular (Clouds-inspired)
        float grainDensityHz = 4.0f;     // grains per second
        float grainSizeMs = 150.0f;      // grain length in ms
        float grainPitchSpread = 0.0f;   // pitch spread in semitones (quantized to octaves: 0, 12, 24)
        float grainMix = 0.0f;           // wet/dry 0..1
        float grainTexture = 0.3f;       // window shape 0=smooth..1=sharp
    };

    void reset()
    {
        chain.reset();
        params = FxParams{};
    reverbWetEnv = {};
    reverbPreDelayEnv = {};
    delayWetEnv = {};
    delayFeedbackEnv = {};
        lowPassCutoffEnv = {};
        highPassCutoffEnv = {};
        tremoloDepthEnv = {};
        chorusMixEnv = {};
        panMixEnv = {};
        volumeGainEnv = {};
        grainMixEnv = {};
        tempGranular = {};
        tempVolRamp = {};
        shPhaseBars = 0.0f;
        granular.reset();
        // BUG 6: reset boundary-declick state.
        declickFadeInTotal.store(0, std::memory_order_relaxed);
        declickFadeInProgress.store(0, std::memory_order_relaxed);
        declickStep[0] = declickStep[1] = 0.0f;
        declickLastOut[0] = declickLastOut[1] = 0.0f;
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

    void setDelayWetEnvelope(float start, float target, float durationBars)
    {
        delayWetEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
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

    void setChorusMixEnvelope(float start, float target, float durationBars)
    {
        chorusMixEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setPanMixEnvelope(float start, float target, float durationBars)
    {
        panMixEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setVolumeGainEnvelope(float start, float target, float durationBars)
    {
        volumeGainEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void setGrainMixEnvelope(float start, float target, float durationBars)
    {
        grainMixEnv = { start, target, durationBars, 0.0f, EffectEnvelope::Curve::Linear };
    }

    void startTemporaryGranular(float riseTarget, float riseDurationBars, float returnTarget, float returnDurationBars)
    {
        setGrainMixEnvelope(params.grainMix, riseTarget, juce::jmax(0.0f, riseDurationBars));
        tempGranular.active = true;
        tempGranular.fallingStarted = false;
        tempGranular.returnTarget = returnTarget;
        tempGranular.fallDurationBars = juce::jmax(0.0f, returnDurationBars);
    }

    // Temporary volume ramp: ramp down over rampDownBars, hold for holdBars, ramp back up over rampUpBars
    void startTemporaryVolumeRamp(float targetGain, float rampDownBars, float holdBars, float rampUpBars)
    {
        chain.volumeRampEnabled = true;
        tempVolRamp.active = true;
        tempVolRamp.holdingStarted = false;
        tempVolRamp.fallingStarted = false;
        tempVolRamp.holdTarget = juce::jlimit(0.0f, 1.0f, targetGain);
        tempVolRamp.holdBars = juce::jmax(0.0f, holdBars);
        tempVolRamp.rampUpBars = juce::jmax(0.0f, rampUpBars);
        tempVolRamp.returnTarget = params.volumeGain; // remember where we were
        // Start the ramp down
        setVolumeGainEnvelope(params.volumeGain, targetGain, juce::jmax(0.0f, rampDownBars));
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
    // Delay wet
    params.delayWet = delayWetEnv.isActive() ? delayWetEnv.current(params.delayWet) : params.delayWet;
    if (delayWetEnv.isActive()) delayWetEnv.advance(barsDelta);
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
        // If a temporary LPF action is active, schedule the return envelope once the rise completes
        if (tempLpf.active && !tempLpf.fallingStarted && !lowPassCutoffEnv.isActive())
        {
            setLowPassCutoffEnvelope(params.lowPassCutoff, tempLpf.returnTarget, tempLpf.fallDurationBars);
            tempLpf.fallingStarted = true;
        }
        // HPF cutoff
        params.highPassCutoff = highPassCutoffEnv.isActive() ? highPassCutoffEnv.current(params.highPassCutoff) : params.highPassCutoff;
        if (highPassCutoffEnv.isActive()) highPassCutoffEnv.advance(barsDelta);
        if (tempHpf.active && !tempHpf.fallingStarted && !highPassCutoffEnv.isActive())
        {
            setHighPassCutoffEnvelope(params.highPassCutoff, tempHpf.returnTarget, tempHpf.fallDurationBars);
            tempHpf.fallingStarted = true;
        }
        // Tremolo depth
        params.tremoloDepth = tremoloDepthEnv.isActive() ? tremoloDepthEnv.current(params.tremoloDepth) : params.tremoloDepth;
        if (tremoloDepthEnv.isActive()) tremoloDepthEnv.advance(barsDelta);
        // Chorus mix
        params.chorusMix = chorusMixEnv.isActive() ? chorusMixEnv.current(params.chorusMix) : params.chorusMix;
        if (chorusMixEnv.isActive()) chorusMixEnv.advance(barsDelta);
        // Auto-disable chorus when mix reaches zero and no active envelope
        if (chain.chorusEnabled && !chorusMixEnv.isActive() && params.chorusMix <= 0.0001f)
            chain.chorusEnabled = false;
        // Pan mix
        params.panMix = panMixEnv.isActive() ? panMixEnv.current(params.panMix) : params.panMix;
        if (panMixEnv.isActive()) panMixEnv.advance(barsDelta);
        // Auto-disable auto-pan when mix reaches zero and no active envelope
        if (chain.autoPanEnabled && !panMixEnv.isActive() && params.panMix <= 0.0001f)
            chain.autoPanEnabled = false;
        // Volume gain ramp
        params.volumeGain = volumeGainEnv.isActive() ? volumeGainEnv.current(params.volumeGain) : params.volumeGain;
        if (volumeGainEnv.isActive()) volumeGainEnv.advance(barsDelta);
        // Temp volume ramp state machine: ramp down -> hold -> ramp up
        if (tempVolRamp.active && !tempVolRamp.holdingStarted && !volumeGainEnv.isActive())
        {
            // Ramp down finished; begin hold phase (set an envelope from target to target over holdBars)
            tempVolRamp.holdingStarted = true;
            if (tempVolRamp.holdBars > 0.0f)
                setVolumeGainEnvelope(tempVolRamp.holdTarget, tempVolRamp.holdTarget, tempVolRamp.holdBars);
            // If holdBars==0, fall through immediately to fallingStarted check next time
        }
        if (tempVolRamp.active && tempVolRamp.holdingStarted && !tempVolRamp.fallingStarted && !volumeGainEnv.isActive())
        {
            // Hold finished; begin ramp up
            tempVolRamp.fallingStarted = true;
            setVolumeGainEnvelope(params.volumeGain, tempVolRamp.returnTarget, tempVolRamp.rampUpBars);
        }
        // Auto-disable volume ramp when complete
        if (chain.volumeRampEnabled && tempVolRamp.active && tempVolRamp.fallingStarted && !volumeGainEnv.isActive())
        {
            chain.volumeRampEnabled = false;
            tempVolRamp.active = false;
        }
        // Granular mix
        params.grainMix = grainMixEnv.isActive() ? grainMixEnv.current(params.grainMix) : params.grainMix;
        if (grainMixEnv.isActive()) grainMixEnv.advance(barsDelta);
        if (tempGranular.active && !tempGranular.fallingStarted && !grainMixEnv.isActive())
        {
            setGrainMixEnvelope(params.grainMix, tempGranular.returnTarget, tempGranular.fallDurationBars);
            tempGranular.fallingStarted = true;
        }
        // Auto-disable granular when mix reaches zero and no active envelope
        if (chain.granularEnabled && !grainMixEnv.isActive() && params.grainMix <= 0.0001f)
        {
            chain.granularEnabled = false;
            tempGranular = {};
        }
        // S&H filter: advance phase and trigger new random cutoff/Q at each division
        if (chain.shLowPassEnabled || chain.shHighPassEnabled)
        {
            shPhaseBars += barsDelta;
            const float div = juce::jmax(0.001f, params.shDivisionBars);
            if (shPhaseBars >= div)
            {
                shPhaseBars -= div;
                if (shPhaseBars >= div) shPhaseBars = 0.0f; // safety for large jumps
                if (chain.shLowPassEnabled)
                {
                    params.shLpfCutoff = 200.0f + shRng.nextFloat() * (8000.0f - 200.0f);
                    params.shLpfQ = 0.5f + shRng.nextFloat() * (4.0f - 0.5f);
                }
                if (chain.shHighPassEnabled)
                {
                    params.shHpfCutoff = 60.0f + shRng.nextFloat() * (800.0f - 60.0f);
                    params.shHpfQ = 0.5f + shRng.nextFloat() * (4.0f - 0.5f);
                }
            }
        }
    }

    const FxParams& getFxParams() const { return params; }
    FxParams& getMutableFxParams() { return params; }

private:
    AudioBuffer* buffer = nullptr; // not owned
    PadProbeSet* clipProbes = nullptr; // not owned; set from AppState
    EffectChainPlaceholder chain;
    FxParams params;
    EffectEnvelope reverbWetEnv;
    EffectEnvelope reverbPreDelayEnv;
    EffectEnvelope delayWetEnv;
    EffectEnvelope delayFeedbackEnv;
    EffectEnvelope lowPassCutoffEnv;
    EffectEnvelope highPassCutoffEnv;
    EffectEnvelope tremoloDepthEnv;
    EffectEnvelope chorusMixEnv;
    EffectEnvelope panMixEnv;
    EffectEnvelope volumeGainEnv;
    EffectEnvelope grainMixEnv;
    juce::dsp::Reverb reverb;
    juce::dsp::Limiter<float> limiter;
    bool reverbPrepared = false;
    double lastSampleRate = 0.0;
    int lastBlockSize = 0;
    // Circular pre-delay buffer
    juce::AudioBuffer<float> preDelayBuffer; // channels match current audio block
    juce::AudioBuffer<float> reverbWetBuffer; // reused wet buffer to avoid per-block allocation
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
    // S&H filters (separate from envelope-based filters; persistent until reset)
    juce::dsp::IIR::Filter<float> shLowPass[2];
    juce::dsp::IIR::Filter<float> shHighPass[2];
    float lastSHLpfCutoff = 0.0f;
    float lastSHLpfQ = 0.0f;
    float lastSHHpfCutoff = 0.0f;
    float lastSHHpfQ = 0.0f;
    float shPhaseBars = 0.0f;      // accumulated bars since last S&H trigger
    juce::Random shRng;             // per-strip RNG for S&H randomization
    float lastLowPassCutoff = 0.0f;
    float lastHighPassCutoff = 0.0f;
    float lastDelayHighCut = 0.0f;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float tremoloPhase = 0.0f;
    // Chorus DSP state
    juce::AudioBuffer<float> chorusDelayBuffer;
    int chorusDelayBufferSize = 0;
    int chorusDelayWritePos = 0;
    float chorusLfoPhase = 0.0f;
    static constexpr double kMaxChorusDelayMs = 30.0; // max modulated delay for chorus
    // Auto-pan LFO
    float panLfoPhase = 0.0f;
    // Declick boundary-step correction (masks discontinuities from abrupt
    // parameter swaps).  BUG 6 fix: replaces the old fade-from-silence ramp.
    std::atomic<int> declickFadeInTotal{0};
    std::atomic<int> declickFadeInProgress{0};
    float declickStep[2] = { 0.0f, 0.0f };      // measured boundary discontinuity
    float declickLastOut[2] = { 0.0f, 0.0f };   // last output sample per channel
    // Ducking envelope state
    float duckEnv = 0.0f;
    float duckAttackCoeff = 0.0f;
    float duckReleaseCoeff = 0.0f;
    std::vector<float> duckGains;          // reused per-block to avoid allocations
    std::vector<float> tremoloGainTable;   // §3.2A: pre-computed tremolo gains per block
    void updateDuckingCoeffs(float releaseMs)
    {
        releaseMs = juce::jlimit(5.0f, 2000.0f, releaseMs);
        const double fs = juce::jmax(1.0, lastSampleRate);
        const double attackMs = 5.0; // fixed fast attack
        duckAttackCoeff = (float)(1.0 - std::exp(-1.0 / (0.001 * attackMs * fs)));
        duckReleaseCoeff = (float)(1.0 - std::exp(-1.0 / (0.001 * releaseMs * fs)));
    }

    // Temporary filter helpers (for master LPF/HPF ramp up then down)
    struct TempFilterBurst { bool active = false; bool fallingStarted = false; float returnTarget = 0.0f; float fallDurationBars = 0.0f; };
    TempFilterBurst tempLpf;
    TempFilterBurst tempHpf;
    TempFilterBurst tempGranular;
    // Volume ramp state: ramp down -> hold -> ramp up
    struct TempVolRampState {
        bool active = false;
        bool holdingStarted = false;
        bool fallingStarted = false;
        float holdTarget = 0.0f;
        float holdBars = 0.0f;
        float rampUpBars = 0.0f;
        float returnTarget = 1.0f;
    } tempVolRamp;
public:
    void startTemporaryLowPass(float riseTarget, float riseDurationBars, float returnTarget, float returnDurationBars)
    {
        // Begin rise, then schedule fall via advanceEnvelopes when rise completes
        setLowPassCutoffEnvelope(params.lowPassCutoff, riseTarget, juce::jmax(0.0f, riseDurationBars));
        tempLpf.active = true;
        tempLpf.fallingStarted = false;
        tempLpf.returnTarget = returnTarget;
        tempLpf.fallDurationBars = juce::jmax(0.0f, returnDurationBars);
    }
    void startTemporaryHighPass(float riseTarget, float riseDurationBars, float returnTarget, float returnDurationBars)
    {
        setHighPassCutoffEnvelope(params.highPassCutoff, riseTarget, juce::jmax(0.0f, riseDurationBars));
        tempHpf.active = true;
        tempHpf.fallingStarted = false;
        tempHpf.returnTarget = returnTarget;
        tempHpf.fallDurationBars = juce::jmax(0.0f, returnDurationBars);
    }
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
    void updateSHLowPassCoeffs(float cutoff, float q)
    {
        cutoff = juce::jlimit(20.0f, 20000.0f, cutoff);
        q = juce::jlimit(0.1f, 10.0f, q);
        if (std::abs(cutoff - lastSHLpfCutoff) < 1.0f && std::abs(q - lastSHLpfQ) < 0.01f) return;
        auto coeff = juce::dsp::IIR::Coefficients<float>::makeLowPass(lastSampleRate, cutoff, q);
        for (int ch = 0; ch < 2; ++ch) *shLowPass[ch].coefficients = *coeff;
        lastSHLpfCutoff = cutoff;
        lastSHLpfQ = q;
    }
    void updateSHHighPassCoeffs(float cutoff, float q)
    {
        cutoff = juce::jlimit(20.0f, 20000.0f, cutoff);
        q = juce::jlimit(0.1f, 10.0f, q);
        if (std::abs(cutoff - lastSHHpfCutoff) < 1.0f && std::abs(q - lastSHHpfQ) < 0.01f) return;
        auto coeff = juce::dsp::IIR::Coefficients<float>::makeHighPass(lastSampleRate, cutoff, q);
        for (int ch = 0; ch < 2; ++ch) *shHighPass[ch].coefficients = *coeff;
        lastSHHpfCutoff = cutoff;
        lastSHHpfQ = q;
    }
    struct DubBurstState { bool active = false; bool fallingStarted = false; float fallTarget = 0.0f; float fallDurationBars = 0.0f; } dubBurst;

    // ── Granular processor (Clouds-inspired) ──
    struct GranularProcessor
    {
        static constexpr int kMaxGrains = 48;
        static constexpr int kCaptureMs = 2000;

        struct Grain
        {
            bool active = false;
            float readPos = 0.0f;
            float readInc = 1.0f;
            int remaining = 0;
            int total = 0;
            float panL = 1.0f;
            float panR = 1.0f;
        };

        juce::AudioBuffer<float> captureBuffer;
        int captureWritePos = 0;
        int captureBufSize = 0;
        Grain grains[kMaxGrains];
        float triggerAccum = 0.0f;
        juce::Random rng;
        double sampleRate = 44100.0;

        void prepare(double sr)
        {
            sampleRate = sr;
            captureBufSize = (int)(sr * kCaptureMs / 1000.0);
            if (captureBufSize < 64) captureBufSize = 64;
            captureBuffer.setSize(2, captureBufSize);
            captureBuffer.clear();
            captureWritePos = 0;
            for (auto& g : grains) g.active = false;
            triggerAccum = 0.0f;
        }

        void reset()
        {
            if (captureBufSize > 0) captureBuffer.clear();
            captureWritePos = 0;
            for (auto& g : grains) g.active = false;
            triggerAccum = 0.0f;
        }

        void process(juce::AudioBuffer<float>& audio, int numSamples,
                      float densityHz, float grainSizeMs, float pitchSpread,
                      float mix, float texture)
        {
            if (captureBufSize <= 0 || mix <= 0.0001f) return;
            const int numCh = juce::jmin(2, audio.getNumChannels());
            const float samplesPerTrigger = (float)(sampleRate / juce::jmax(0.5, (double)densityHz));
            const int grainLen = juce::jmax(64, (int)(grainSizeMs * 0.001 * sampleRate));

            for (int i = 0; i < numSamples; ++i)
            {
                // Capture input
                for (int ch = 0; ch < numCh; ++ch)
                    captureBuffer.setSample(ch, captureWritePos, audio.getSample(ch, i));
                captureWritePos = (captureWritePos + 1) % captureBufSize;

                // Trigger new grains
                triggerAccum += 1.0f;
                if (triggerAccum >= samplesPerTrigger)
                {
                    triggerAccum -= samplesPerTrigger;
                    spawnGrain(grainLen, pitchSpread);
                }

                // Sum active grains
                float wetL = 0.0f, wetR = 0.0f;
                for (auto& g : grains)
                {
                    if (!g.active) continue;
                    float progress = 1.0f - (float)g.remaining / (float)g.total;
                    float win = grainWindow(progress, texture);

                    int p0 = ((int)g.readPos) % captureBufSize;
                    if (p0 < 0) p0 += captureBufSize;
                    int p1 = (p0 + 1) % captureBufSize;
                    float frac = g.readPos - std::floor(g.readPos);

                    for (int ch = 0; ch < numCh; ++ch)
                    {
                        float s = captureBuffer.getSample(ch, p0)
                                + frac * (captureBuffer.getSample(ch, p1)
                                        - captureBuffer.getSample(ch, p0));
                        s *= win;
                        if (ch == 0) wetL += s * g.panL;
                        else         wetR += s * g.panR;
                    }
                    g.readPos += g.readInc;
                    if (g.readPos >= (float)captureBufSize) g.readPos -= (float)captureBufSize;
                    if (g.readPos < 0.0f) g.readPos += (float)captureBufSize;
                    if (--g.remaining <= 0) g.active = false;
                }

                // Soft-clip wet signal to prevent grain pileup blowout
                wetL = std::tanh(wetL);
                wetR = std::tanh(wetR);

                // Mix
                float dryL = audio.getSample(0, i);
                audio.setSample(0, i, dryL * (1.0f - mix) + wetL * mix);
                if (numCh > 1)
                {
                    float dryR = audio.getSample(1, i);
                    audio.setSample(1, i, dryR * (1.0f - mix) + wetR * mix);
                }
            }
        }

    private:
        void spawnGrain(int grainLen, float pitchSpread)
        {
            Grain* slot = nullptr;
            for (auto& g : grains)
                if (!g.active) { slot = &g; break; }
            if (!slot) return;

            int maxOff = juce::jmax(1, captureBufSize - grainLen * 2);
            int offset = rng.nextInt(maxOff);
            slot->active = true;
            slot->readPos = (float)((captureWritePos - offset - grainLen + captureBufSize * 2) % captureBufSize);
            slot->total = grainLen;
            slot->remaining = grainLen;
            // Quantize pitch to octave intervals (0, ±12, ±24 semitones) to stay in key
            int maxOctaves = (int)(pitchSpread / 12.0f);
            int octaveShift = maxOctaves > 0 ? (rng.nextInt(maxOctaves * 2 + 1) - maxOctaves) : 0;
            float semi = (float)(octaveShift * 12);
            slot->readInc = std::pow(2.0f, semi / 12.0f);
            float pan = rng.nextFloat();
            slot->panL = std::cos(pan * juce::MathConstants<float>::halfPi);
            slot->panR = std::sin(pan * juce::MathConstants<float>::halfPi);
        }

        static float grainWindow(float progress, float texture)
        {
            float hann = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * progress));
            float rect = juce::jmin(1.0f, progress * 20.0f) * juce::jmin(1.0f, (1.0f - progress) * 20.0f);
            return hann + texture * (rect - hann);
        }
    } granular;
};
