/*
  ==============================================================================

    AudioBuffer.cpp
    
    Professional Audio Buffer Implementation

  ==============================================================================
*/

#include "AudioBuffer.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

//==============================================================================
// Tearing Debug Helpers
//==============================================================================

namespace TearingDebug
{
    // Multi-level thresholds for detecting discontinuities of different severities
    constexpr float kMajorDiscontinuityThreshold = 0.3f;   // 30% of full scale - major tearing
    constexpr float kMediumDiscontinuityThreshold = 0.15f; // 15% of full scale - medium pops
    constexpr float kMinorDiscontinuityThreshold = 0.10f;  // 10% of full scale - minor clicks (increased from 0.05)
    
    // Threshold for consecutive zeros to be considered a "zero run"
    constexpr int kZeroRunThreshold = 64;  // ~1.5ms at 44.1kHz
    
    // RMS jump detection threshold (ratio between consecutive blocks)
    constexpr float kRmsJumpThreshold = 3.0f;  // 3x RMS change is suspicious
    
    // DC offset drift threshold — SoundTouch's overlap-add and RateTransposer
    // interpolation routinely shift block-level DC, especially with extreme rate
    // values (e.g. 0.25) where heavy interpolation is involved.  Keep this high
    // enough to avoid flooding the counter with false positives.
    constexpr float kDcOffsetThreshold = 0.40f;  // 40% DC drift (increased from 0.25)
    
    // Check if a sample value is valid (not NaN or Inf)
    inline bool isValidSample(float s)
    {
        return std::isfinite(s);
    }
    
    // Check if a sample is effectively zero
    inline bool isZero(float s)
    {
        return std::abs(s) < 1.0e-10f;
    }
    
    // Check for clipping (|sample| > 1.0)
    inline bool isClipped(float s)
    {
        return std::abs(s) > 1.0f;
    }
    
    enum class DiscontinuityLevel { None, Minor, Medium, Major };
    
    // Check for discontinuity between two consecutive samples with severity level
    inline DiscontinuityLevel getDiscontinuityLevel(float prev, float curr)
    {
        // Skip if either sample is invalid
        if (!isValidSample(prev) || !isValidSample(curr))
            return DiscontinuityLevel::None;
        // Skip if both are near zero (silence)
        if (isZero(prev) && isZero(curr))
            return DiscontinuityLevel::None;
            
        const float delta = std::abs(curr - prev);
        
        if (delta > kMajorDiscontinuityThreshold)
            return DiscontinuityLevel::Major;
        else if (delta > kMediumDiscontinuityThreshold)
            return DiscontinuityLevel::Medium;
        else if (delta > kMinorDiscontinuityThreshold)
            return DiscontinuityLevel::Minor;
        else
            return DiscontinuityLevel::None;
    }
    
    // Legacy function for backward compatibility
    inline bool isDiscontinuity(float prev, float curr)
    {
        return getDiscontinuityLevel(prev, curr) == DiscontinuityLevel::Major;
    }
    
    // Calculate RMS of a buffer
    inline float calculateRms(const float* data, int numSamples)
    {
        if (numSamples <= 0)
            return 0.0f;
            
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            if (isValidSample(data[i]))
                sum += data[i] * data[i];
        }
        return std::sqrt((float)(sum / numSamples));
    }
    
    // Calculate DC offset (average sample value)
    inline float calculateDcOffset(const float* data, int numSamples)
    {
        if (numSamples <= 0)
            return 0.0f;
            
        double sum = 0.0;
        int validCount = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            if (isValidSample(data[i]))
            {
                sum += data[i];
                validCount++;
            }
        }
        return validCount > 0 ? (float)(sum / validCount) : 0.0f;
    }
}

//==============================================================================
// AudioBuffer Implementation
//==============================================================================

AudioBuffer::AudioBuffer(int bufferIndex)
    : bufferIndex(bufferIndex)
{
    speedSmoother.reset(128); // Smooth parameter changes over ~3ms at 44.1kHz
    speedSmoother.setCurrentAndTargetValue(1.0);
    stretchSmoother.reset(128);
    stretchSmoother.setCurrentAndTargetValue(1.0);
    speedMagSmoother.reset(128);
    speedMagSmoother.setCurrentAndTargetValue(1.0);
    pitchSemiSmoother.reset(128);
    pitchSemiSmoother.setCurrentAndTargetValue(0.0);
}

void AudioBuffer::prepare(double sampleRate, int samplesPerBlockExpected)
{
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();
    hostSampleRate = sampleRate;
    speedSmoother.reset(sampleRate, 0.05); // 50ms smoothing time
    stretchSmoother.reset(sampleRate, 0.05);
    speedMagSmoother.reset(sampleRate, 0.05); // T4: 50ms smoothing for speed-mag through SoundTouch
    pitchSemiSmoother.reset(sampleRate, 0.05); // 50ms smoothing for pitch changes
    
    // Calculate crossfade length in samples
    crossfadeLengthSamples = static_cast<int>(params.crossfadeLengthMs * sampleRate / 1000.0);
    activeCrossfadeLen = crossfadeLengthSamples;
    crossfadeIsLookaheadContinuation = false;
    
    // Prepare buffers for processing
    repitchBuffer.setSize(2, samplesPerBlockExpected * 4); // Extra headroom for repitching
    tempProcessingBuffer.setSize(2, samplesPerBlockExpected * 4);
    crossfadeBuffer.setSize(2, crossfadeLengthSamples * 2); // Buffer for crossfading

    // §6.1  Pre-allocate SoundTouch scratch buffers at worst-case sizes so that
    // processWithTimeStretch never calls setSize (which may malloc) on the
    // audio thread.  Worst case: speed=4×, stretch=4×, pitch=±24 semitones
    // gives a totalTempoRatioForIO around 16×.  With the margin formula
    // (numOutput * ratio + 64 * ratio) a 4× safety multiplier is used.
    const int worstCaseInputFrames = samplesPerBlockExpected * 16 + 64 * 16;
    stretchInScratch.setSize(2, worstCaseInputFrames, false, false, true);

    // Interleaved buffers used inside TimeStretchSoundTouch::processNonInterleaved.
    // Input: worstCaseInputFrames * numChannels;  Output: samplesPerBlock * numChannels.
    stretchInterleavedIn.setSize(1, worstCaseInputFrames * 2, false, false, true);
    stretchInterleavedOut.setSize(1, samplesPerBlockExpected * 2, false, false, true);

    // Previous-block cache for mode-transition crossfades.
    previousBlockBuffer.setSize(2, samplesPerBlockExpected, false, false, true);

    // §10.3  Output-side crossfade ring buffer.  Sized at the maximum crossfade
    // length we'll use in SoundTouch mode (100ms gives headroom over the 50ms
    // minimum set in processWithTimeStretch).
    const int maxOutputCrossfadeSamples = (int)(sampleRate * 0.1) + 1;
    stretchOutputRing.setSize(2, maxOutputCrossfadeSamples, false, false, true);
    stretchOutputRingSize = maxOutputCrossfadeSamples;
    stretchOutputRingWritePos = 0;
    stretchOutputRingValidSamples = 0;
    stretchCrossfadeSnapshot.setSize(2, maxOutputCrossfadeSamples, false, false, true);
    stretchCrossfadeSnapshotLen = 0;
    stretchOutputCrossfadePending = false;
    stretchOutputCrossfadeActive = false;
    stretchOutputCrossfadePos = 0;
    stretchOutputCrossfadeLen = 0;

    // BUG 7 fix: multi-block transition fade buffer (max 100ms of old audio).
    transitionOldBuffer.setSize(2, maxOutputCrossfadeSamples, false, false, true);
    transitionFadeActive = false;
    transitionFadePos = 0;
    transitionFadeLen = 0;
    stretcherResetThisBlock = false;

    // §2.2A  Reset block-resampling interpolators.
    for (auto& r : blockResamplers)
        r.reset();
    blockResamplersValid = false;
}

void AudioBuffer::processBlock(juce::AudioBuffer<float>& outputBuffer)
{
    if (!hasAudioLoaded() || !params.isPlaying)
    {
        outputBuffer.clear();
        lastBlockUsedStretch = false;
        previousBlockValid = false;
        // BUG 7: cancel any in-flight transition fade and invalidate the output
        // ring — after silence there is nothing meaningful to fade from.
        transitionFadeActive = false;
        stretchOutputRingValidSamples = 0;
        return;
    }

    // T3: Take a single coordinated snapshot of all stretch-related parameters
    // so the mode decision and parameter values are always consistent within
    // this audio block.
    const auto snap = takeStretchSnapshot();
    const bool useStretcher = snap.useStretcher();

    // Route through SoundTouch when stretching or when pitch shifting is active.
    // Even at 1.0, SoundTouch adds buffering/CPU and can affect UI smoothness in Debug builds.
    if (useStretcher)
    {
        processWithTimeStretch(outputBuffer, snap);
    }
    else
    {
        // When transitioning from stretch → repitch, snap the speed smoother to
        // the current value so we don't ramp from a stale value that was frozen
        // while SoundTouch was handling speed internally.
        if (lastBlockUsedStretch)
            speedSmoother.setCurrentAndTargetValue(snap.speed * snap.tempoMult);
        else
            speedSmoother.setTargetValue(snap.speed * snap.tempoMult);
        processWithRepitching(outputBuffer);
    }

    // When transitioning between repitch <-> stretch mode, crossfade the tail
    // of the previous block into the head of the current block to avoid a hard edge.
    const bool transitioned = (useStretcher != lastBlockUsedStretch);
#if JUCE_DEBUG
    if (transitioned)
    {
        juce::String modifiers;
        if (std::abs(snap.pitchSemis) > 0.01)
            modifiers += " [PITCH]";
        if (std::abs(snap.stretchRatio - 1.0) > 0.01)
            modifiers += " [STRETCH]";
        if (std::abs(snap.speed - 1.0) > 0.01)
            modifiers += " [SPEED]";
        if (snap.speed < 0.0)
            modifiers += " [REVERSE]";
        if (slicingModeActive.load())
            modifiers += " [SLICING]";
            
        DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] mode transition: "
             + juce::String(lastBlockUsedStretch ? "stretch" : "repitch") + " -> "
             + juce::String(useStretcher ? "stretch" : "repitch")
             + " speed=" + juce::String(snap.speed) + " stretch=" + juce::String(snap.stretchRatio)
             + " pitch=" + juce::String(snap.pitchSemis)
             + " modifiers:" + modifiers);
        if (tearingDebugEnabled.load())
            tearingStats.modeTransitions.fetch_add(1, std::memory_order_relaxed);
    }
#endif
    // BUG 7 fix: multi-block transition crossfade.
    // The fade is no longer clamped to a single host block — at 64/128-sample
    // buffers the intended 20–80ms fade previously couldn't fit and left a
    // residual click.  Old (pre-transition) audio is captured from the output
    // ring buffer (fed at the end of every block in BOTH modes) and blended in
    // across as many blocks as needed.
    //
    // Two triggers share one unified fade (no double-crossfading):
    //  - mode transition (repitch <-> stretch)
    //  - SoundTouch pipeline reset while already in stretch mode (old T8 path)
    const bool resetOccurred = stretcherResetThisBlock;
    stretcherResetThisBlock = false;
    if ((transitioned || resetOccurred) && previousBlockValid)
    {
        // The crossfade must be long enough to cover SoundTouch's settle time,
        // which is the period during which its overlap-add algorithm may produce
        // audible splice artifacts.  The settle time scales with the combined
        // consumption ratio (rate × tempo × pitch-compensation).
        const double speedDev = juce::jmax(snap.speedMag(), 1.0);
        const double ratioVal = juce::jmax(snap.stretchRatio, 1.0);
        const double pitchDev = snap.usePitch() ? std::pow(2.0, std::abs(snap.pitchSemis) / 12.0) : 1.0;
        // Mirrors the priming settleMs formula but with wider headroom:
        //   base 20ms × ratio-factor × speed-factor × pitch-factor
        const double crossfadeMs = juce::jlimit(20.0, 80.0,
                                                 20.0 * juce::jmax(1.0, ratioVal)
                                                      * juce::jmax(1.0, speedDev)
                                                      * juce::jmax(1.0, pitchDev));
        beginTransitionFade(juce::jmax(1, (int)(hostSampleRate * crossfadeMs / 1000.0)));
    }

    // Apply any in-flight transition fade (may span multiple blocks).
    if (transitionFadeActive)
        applyTransitionFade(outputBuffer);

    // Feed the output ring so future transitions have pre-transition audio to
    // fade from.  Written in BOTH playback modes (repitch used to skip this).
    writeToStretchOutputRing(outputBuffer, outputBuffer.getNumSamples());

    lastBlockUsedStretch = useStretcher;

    // Cache this block for potential transition crossfade next time.
    // Pre-allocated in prepare(); guard kept as safety net.
    if (previousBlockBuffer.getNumChannels() != outputBuffer.getNumChannels()
        || previousBlockBuffer.getNumSamples() < outputBuffer.getNumSamples())
    {
        jassertfalse; // previousBlockBuffer should have been pre-allocated in prepare()
        previousBlockBuffer.setSize(outputBuffer.getNumChannels(),
                                    outputBuffer.getNumSamples(),
                                    false, false, true);
    }
    for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
        previousBlockBuffer.copyFrom(ch, 0, outputBuffer, ch, 0, outputBuffer.getNumSamples());
    previousBlockNumSamples = outputBuffer.getNumSamples();
    previousBlockValid = true;
    
    //==============================================================================
    // TEARING DEBUG: Validate output buffer for potential audio issues
    //==============================================================================
#if JUCE_DEBUG
    if (tearingDebugEnabled.load())
    {
        // Decrement RMS blanking counter (set by slice transitions in stretch mode)
        if (rmsBlankingBlocksLeft > 0)
            --rmsBlankingBlocksLeft;

        const int numSamples = outputBuffer.getNumSamples();
        const int numChannels = outputBuffer.getNumChannels();
        
        // Check for completely empty buffer
        bool bufferIsEmpty = true;
        for (int ch = 0; ch < numChannels && bufferIsEmpty; ++ch)
        {
            const float* data = outputBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples && bufferIsEmpty; ++i)
            {
                if (!TearingDebug::isZero(data[i]))
                    bufferIsEmpty = false;
            }
        }
        
        if (bufferIsEmpty)
        {
            tearingStats.emptyOutputBuffers.fetch_add(1, std::memory_order_relaxed);
            tearingStats.lastTearingEventTime.store(juce::Time::getMillisecondCounterHiRes());
            DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING: Empty output buffer detected! "
                + "pos=" + juce::String(playheadPosition.load(), 0)
                + " speed=" + juce::String(snap.speed)
                + " stretch=" + juce::String(snap.stretchRatio)
                + " pitch=" + juce::String(snap.pitchSemis)
                + " mode=" + (snap.useStretcher() ? "stretch" : "repitch"));
        }
        
        // Check each channel for issues
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = outputBuffer.getReadPointer(ch);
            float lastValid = (ch < 2) ? lastOutputSample[ch] : 0.0f;
            
            // Calculate RMS and DC offset for this block
            const float currentRms = TearingDebug::calculateRms(data, numSamples);
            const float currentDc = TearingDebug::calculateDcOffset(data, numSamples);
            
            // Check for RMS jumps (only for first 2 channels to avoid spam)
            // Suppress during slice-transition blanking period — SoundTouch's
            // latency means the output takes several blocks to reflect new
            // content, causing legitimate RMS changes that aren't tearing.
            if (ch < 2 && lastBlockRms[ch] > 1.0e-6f && rmsBlankingBlocksLeft <= 0)
            {
                const float rmsRatio = juce::jmax(currentRms / lastBlockRms[ch], 
                                                   lastBlockRms[ch] / currentRms);
                if (rmsRatio > TearingDebug::kRmsJumpThreshold)
                {
                    tearingStats.rmsJumps.fetch_add(1, std::memory_order_relaxed);
                    tearingStats.lastTearingEventTime.store(juce::Time::getMillisecondCounterHiRes());
                    DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING: RMS jump ch="
                        + juce::String(ch) + " prev=" + juce::String(lastBlockRms[ch], 4)
                        + " curr=" + juce::String(currentRms, 4) + " ratio=" + juce::String(rmsRatio, 2));
                }
            }
            
            // Check for DC offset drift
            if (ch < 2)
            {
                const float dcDrift = std::abs(currentDc - lastBlockDcOffset[ch]);
                if (dcDrift > TearingDebug::kDcOffsetThreshold)
                {
                    tearingStats.dcOffsetDrifts.fetch_add(1, std::memory_order_relaxed);
                    DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING: DC offset drift ch="
                        + juce::String(ch) + " prev=" + juce::String(lastBlockDcOffset[ch], 4)
                        + " curr=" + juce::String(currentDc, 4) + " drift=" + juce::String(dcDrift, 4));
                }
                
                // Store for next block
                lastBlockRms[ch] = currentRms;
                lastBlockDcOffset[ch] = currentDc;
            }
            
            for (int i = 0; i < numSamples; ++i)
            {
                const float sample = data[i];
                
                // Check for NaN/Inf
                if (!TearingDebug::isValidSample(sample))
                {
                    tearingStats.nanOrInfSamples.fetch_add(1, std::memory_order_relaxed);
                    tearingStats.lastTearingEventTime.store(juce::Time::getMillisecondCounterHiRes());
                    DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING: NaN/Inf sample at ch="
                        + juce::String(ch) + " idx=" + juce::String(i));
                    continue;
                }
                
                // Check for clipping
                if (TearingDebug::isClipped(sample))
                {
                    tearingStats.clippedSamples.fetch_add(1, std::memory_order_relaxed);
                }
                
                // Check for discontinuity with multiple severity levels
                if (i > 0 || (ch < 2 && std::abs(lastOutputSample[ch]) > 1.0e-10f))
                {
                    const auto level = TearingDebug::getDiscontinuityLevel(lastValid, sample);
                    if (level != TearingDebug::DiscontinuityLevel::None)
                    {
                        tearingStats.lastTearingEventTime.store(juce::Time::getMillisecondCounterHiRes());
                        const float delta = std::abs(sample - lastValid);
                        
                        switch (level)
                        {
                            case TearingDebug::DiscontinuityLevel::Major:
                                tearingStats.discontinuities.fetch_add(1, std::memory_order_relaxed);
                                DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING [MAJOR]: Discontinuity ch="
                                    + juce::String(ch) + " idx=" + juce::String(i)
                                    + " prev=" + juce::String(lastValid, 4) + " curr=" + juce::String(sample, 4)
                                    + " delta=" + juce::String(delta, 4));
                                break;
                            case TearingDebug::DiscontinuityLevel::Medium:
                                tearingStats.mediumDiscontinuities.fetch_add(1, std::memory_order_relaxed);
                                DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING [MEDIUM]: Discontinuity ch="
                                    + juce::String(ch) + " idx=" + juce::String(i)
                                    + " prev=" + juce::String(lastValid, 4) + " curr=" + juce::String(sample, 4)
                                    + " delta=" + juce::String(delta, 4));
                                break;
                            case TearingDebug::DiscontinuityLevel::Minor:
                                tearingStats.minorDiscontinuities.fetch_add(1, std::memory_order_relaxed);
                                DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING [MINOR]: Discontinuity ch="
                                    + juce::String(ch) + " idx=" + juce::String(i)
                                    + " prev=" + juce::String(lastValid, 4) + " curr=" + juce::String(sample, 4)
                                    + " delta=" + juce::String(delta, 4));
                                break;
                            default:
                                break;
                        }
                    }
                }
                
                // Track consecutive zeros
                if (TearingDebug::isZero(sample))
                {
                    if (ch == 0)
                        consecutiveZeroSamples++;
                }
                else
                {
                    if (ch == 0 && consecutiveZeroSamples >= TearingDebug::kZeroRunThreshold)
                    {
                        tearingStats.zeroSampleRuns.fetch_add(1, std::memory_order_relaxed);
                        DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING: Zero run of "
                            + juce::String(consecutiveZeroSamples) + " samples ended at idx=" + juce::String(i));
                    }
                    if (ch == 0)
                        consecutiveZeroSamples = 0;
                }
                
                lastValid = sample;
            }
            
            // Store last sample for next block discontinuity check
            if (ch < 2)
                lastOutputSample[ch] = data[numSamples - 1];
        }
        
        // Update playhead tracking
        tearingStats.lastPlayheadPos.store(playheadPosition.load());
    }
#endif
    
    // Check for state changes and notify listeners
    notifyPlaybackStateChanged();
    notifySliceChanged();
    notifyPositionChanged();
}

void AudioBuffer::setPitchSemiTones(double semiTones)
{
    // Clamp to a musically useful range to keep SoundTouch stable.
    semiTones = juce::jlimit(-24.0, 24.0, semiTones);

    const double oldPitch = pitchSemiTones.load();
    pitchSemiTones.store(semiTones);

    const bool oldPitchActive = std::abs(oldPitch) > 1.0e-6;
    const bool newPitchActive = std::abs(semiTones) > 1.0e-6;
    const bool stretchActive  = std::abs(stretchRatio.load() - 1.0) > 1.0e-6;

    // Only reset SoundTouch when OVERALL stretcher usage toggles on/off.
    // When the stretcher is already running, SoundTouch picks up parameter
    // changes (pitch, tempo, rate) on the next processing chunk via its
    // overlap-add algorithm — no pipeline flush required.  Flushing while
    // running destroys buffered audio continuity and causes audible clicks,
    // especially when stacking pitch modifiers (e.g. multiple oct+).
    const bool oldUse = (stretchActive || oldPitchActive);
    const bool newUse = (stretchActive || newPitchActive);
    if (oldUse != newUse)
    {
        stretcherNeedsReset.store(true);
    }
    // No else-if reset for pitch changes while already running.
    // The pitchSemiSmoother in processWithTimeStretch ramps the value
    // smoothly, and SoundTouch handles the transition natively.
}

AudioBuffer::LoadedAudioData::Ptr AudioBuffer::getAudioDataSnapshot() const
{
    // Lock-free: acquire-load the raw pointer, then wrap in a Ptr which
    // atomically bumps the ref count.  Safe because audioDataRetainer /
    // previousAudioDataRetainer guarantee the object is still alive.
    return LoadedAudioData::Ptr (atomicAudioData.load (std::memory_order_acquire));
}

bool AudioBuffer::hasAudioLoaded() const
{
    auto data = getAudioDataSnapshot();
    return data != nullptr && data->buffer.getNumSamples() > 0;
}

void AudioBuffer::processWithRepitching(juce::AudioBuffer<float>& outputBuffer)
{
    const int numOutputSamples = outputBuffer.getNumSamples();

    auto data = getAudioDataSnapshot();
    if (data == nullptr || data->buffer.getNumSamples() <= 0 || data->sampleRate <= 0.0)
    {
        outputBuffer.clear();
        return;
    }

    const auto& sourceBuffer = data->buffer;
    const int fileLengthSamples = sourceBuffer.getNumSamples();
    const double fileSampleRate = data->sampleRate;
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), sourceBuffer.getNumChannels());
    const double srRatio = fileSampleRate / hostSampleRate;

    outputBuffer.clear();

    // §5.2  Snapshot ALL atomic state into locals once before the processing
    // loop.  Only write back playheadPosition (and ping-pong state) once after
    // the loop completes.
    double currentPos               = playheadPosition.load();
    const bool slicingOn            = slicingModeActive.load();
    bool sliceTrig                  = sliceTriggered.load();
    const int tgtSlice              = targetSlice.load();
    int activeSlice                 = currentActiveSlice.load();
    const bool loopWinOn            = loopWindowEnabled.load();
    const int64_t loopStart         = loopWinOn ? loopStartSamples.load() : 0;
    const int64_t loopEnd           = loopWinOn ? loopEndSamples.load()   : 0;
    const bool ppEnabled            = pingPongEnabled.load();
    const double ppPeriod           = ppEnabled ? pingPongPeriodSamples.load() : 0.0;
    double ppPhase                  = ppEnabled ? pingPongPhasePosition.load() : 0.0;
    bool ppGoingForward             = ppEnabled ? pingPongGoingForward.load() : true;

    // §7: Block-based repitch processing — process in chunks between boundary
    // events.  Between boundaries the playhead advances linearly and the inner
    // interpolation loop is branch-free and auto-vectorizable.

    int outputPos = 0;

    while (outputPos < numOutputSamples)
    {
        // --- Read current speed ---
        const bool smoothing = speedSmoother.isSmoothing();
        const double speed = smoothing ? speedSmoother.getNextValue()
                                       : speedSmoother.getCurrentValue();

        if (speed == 0.0)
            break;

        const double step = speed * srRatio;

        // ==================================================================
        // Event handling at current position
        // ==================================================================

        // Ping-pong direction flip (check phase accumulated from previous chunk)
        if (ppEnabled && ppPeriod > 0.0 && ppPhase >= ppPeriod)
        {
            ppPhase = std::fmod(ppPhase, ppPeriod);
            ppGoingForward = !ppGoingForward;
            if (ppGoingForward)
            {
                params.speed = std::abs(params.speed);
                atomicSpeed.store(params.speed, std::memory_order_relaxed);
                speedSmoother.setCurrentAndTargetValue(params.speed);
            }
            else
            {
                params.speed = -std::abs(params.speed);
                atomicSpeed.store(params.speed, std::memory_order_relaxed);
                speedSmoother.setCurrentAndTargetValue(params.speed);
            }
            continue; // re-evaluate with new direction
        }

        // Slice trigger
        if (slicingOn && sliceTrig)
        {
            const double spd = getEffectiveSpeed();
            double newPos = (spd >= 0.0)
                ? getSliceStartPosition(tgtSlice, fileLengthSamples)
                : getSliceEndPosition(tgtSlice, fileLengthSamples) - 1.0;
            previousSliceIndex = activeSlice;
            previousSlicePlayheadPos = currentPos;
            isInCrossfade = true;
            crossfadePosition = 0;
            activeCrossfadeLen = crossfadeLengthSamples;
            crossfadeIsLookaheadContinuation = false;
            activeSlice = tgtSlice;
            currentPos = newPos;
            sliceTrig = false;
        }

        // Slice boundary (continuous random or looping within slice)
        if (slicingOn)
        {
            const double sliceStart = getSliceStartPosition(activeSlice, fileLengthSamples);
            const double sliceEnd   = getSliceEndPosition(activeSlice, fileLengthSamples);
            const double spd = getEffectiveSpeed();

            if (params.continuousRandomSlicing)
            {
                bool atBoundary = (spd > 0.0 && currentPos >= sliceEnd - 1.0)
                               || (spd < 0.0 && currentPos <= sliceStart);
                if (atBoundary)
                {
                    const int nextSlice = random.nextInt(params.numSlices);
                    double newPos = (spd > 0.0)
                        ? getSliceStartPosition(nextSlice, fileLengthSamples)
                        : getSliceEndPosition(nextSlice, fileLengthSamples) - 1.0;
                    previousSliceIndex = activeSlice;
                    previousSlicePlayheadPos = currentPos;
                    isInCrossfade = true;
                    crossfadePosition = 0;
                    activeCrossfadeLen = crossfadeLengthSamples;
                    crossfadeIsLookaheadContinuation = false;
                    activeSlice = nextSlice;
                    currentPos = newPos;
                }
            }
            else if (params.arpSliceActive && !params.arpSequence.empty())
            {
                bool atBoundary = (spd > 0.0 && currentPos >= sliceEnd - 1.0)
                               || (spd < 0.0 && currentPos <= sliceStart);
                if (atBoundary)
                {
                    // Advance position in the arp sequence
                    params.arpSequencePos++;
                    if (params.arpSequencePos >= (int)params.arpSequence.size())
                    {
                        params.arpSequencePos = 0;
                        params.arpCycleCount++;
                        // Refresh the sequence after N full cycles
                        if (params.arpCycleCount >= params.arpTotalCyclesPerRefresh)
                        {
                            params.arpCycleCount = 0;
                            int seqLen = (int)params.arpSequence.size();
                            params.arpSequence.clear();
                            if (params.arpSliceRepeaterMode)
                            {
                                // Repeater: pick one random slice and fill entire sequence
                                int rs = random.nextInt(params.numSlices);
                                for (int si = 0; si < seqLen; ++si)
                                    params.arpSequence.push_back(rs);
                            }
                            else
                            {
                                for (int si = 0; si < seqLen; ++si)
                                    params.arpSequence.push_back(random.nextInt(params.numSlices));
                            }
                        }
                    }
                    const int nextSlice = params.arpSequence[(size_t)params.arpSequencePos];
                    double newPos = (spd > 0.0)
                        ? getSliceStartPosition(nextSlice, fileLengthSamples)
                        : getSliceEndPosition(nextSlice, fileLengthSamples) - 1.0;
                    previousSliceIndex = activeSlice;
                    previousSlicePlayheadPos = currentPos;
                    isInCrossfade = true;
                    crossfadePosition = 0;
                    activeCrossfadeLen = crossfadeLengthSamples;
                    crossfadeIsLookaheadContinuation = false;
                    activeSlice = nextSlice;
                    currentPos = newPos;
                }
            }
            else
            {
                // BUGFIX (BUG 4): plain slice-loop wrap used to teleport the playhead
                // with no crossfade, producing a click every loop cycle.  Start a
                // boundary crossfade just like the loop-window path does.
                if (spd > 0.0 && currentPos >= sliceEnd - 1.0)
                {
                    if (params.isLooping)
                    {
                        previousSlicePlayheadPos = currentPos;
                        previousSliceIndex = -1;
                        isInCrossfade = true;
                        crossfadePosition = 0;
                        activeCrossfadeLen = crossfadeLengthSamples;
                        crossfadeIsLookaheadContinuation = false;
                        currentPos = sliceStart;
                    }
                    else { params.isPlaying = false; break; }
                }
                else if (spd < 0.0 && currentPos <= sliceStart)
                {
                    if (params.isLooping)
                    {
                        previousSlicePlayheadPos = currentPos;
                        previousSliceIndex = -1;
                        isInCrossfade = true;
                        crossfadePosition = 0;
                        activeCrossfadeLen = crossfadeLengthSamples;
                        crossfadeIsLookaheadContinuation = false;
                        currentPos = sliceEnd - 1.0;
                    }
                    else { params.isPlaying = false; break; }
                }
            }
        }

        // File boundary (only when NOT in slicing mode)
        // BUGFIX (BUG 3): file-boundary wrap now starts a crossfade instead of a
        // hard teleport — this was a known click source in repitch mode
        // (DECLICK_REPORT §1.3).  Mirrors the loop-window wrap pattern below.
        if (!slicingOn)
        {
            if (speed > 0.0)
            {
                if (currentPos >= fileLengthSamples - 1)
                {
                    if (params.isLooping)
                    {
                        previousSlicePlayheadPos = currentPos;
                        previousSliceIndex = -1;
                        isInCrossfade = true;
                        crossfadePosition = 0;
                        activeCrossfadeLen = crossfadeLengthSamples;
                        crossfadeIsLookaheadContinuation = false;
                        currentPos = 0.0;
                    }
                    else { params.isPlaying = false; break; }
                }
            }
            else if (speed < 0.0)
            {
                if (currentPos <= 0.0)
                {
                    if (params.isLooping)
                    {
                        previousSlicePlayheadPos = currentPos;
                        previousSliceIndex = -1;
                        isInCrossfade = true;
                        crossfadePosition = 0;
                        activeCrossfadeLen = crossfadeLengthSamples;
                        crossfadeIsLookaheadContinuation = false;
                        currentPos = static_cast<double>(fileLengthSamples - 1);
                    }
                    else { params.isPlaying = false; break; }
                }
            }
        }

        // Loop window enforcement (inline startBoundaryCrossfade to use local currentPos)
        if (loopWinOn)
        {
            if (speed >= 0.0)
            {
                if (currentPos >= (double) loopEnd)
                {
                    previousSlicePlayheadPos = currentPos;
                    previousSliceIndex = -1;
                    isInCrossfade = true;
                    crossfadePosition = 0;
                    activeCrossfadeLen = crossfadeLengthSamples;
                    crossfadeIsLookaheadContinuation = false;
                    currentPos = (double) loopStart;
                }
                else if (currentPos < (double) loopStart)
                    currentPos = (double) loopStart;
            }
            else
            {
                if (currentPos < (double) loopStart)
                {
                    previousSlicePlayheadPos = currentPos;
                    previousSliceIndex = -1;
                    isInCrossfade = true;
                    crossfadePosition = 0;
                    activeCrossfadeLen = crossfadeLengthSamples;
                    crossfadeIsLookaheadContinuation = false;
                    currentPos = (double) loopEnd;
                }
                else if (currentPos > (double) loopEnd)
                    currentPos = (double) loopEnd;
            }
        }

        // Clamp to file bounds
        currentPos = juce::jlimit(0.0, static_cast<double>(fileLengthSamples - 1), currentPos);

        // ==================================================================
        // Compute safe chunk size until the next boundary event
        // ==================================================================
        int remaining = numOutputSamples - outputPos;
        int chunkSize = remaining;

        // When speed smoother is active, fall back to per-sample processing
        if (smoothing)
        {
            chunkSize = 1;
        }
        else if (chunkSize > 1)
        {
            // Determine the valid playback range for the current position
            double rangeStart, rangeEnd;
            if (slicingOn)
            {
                rangeStart = getSliceStartPosition(activeSlice, fileLengthSamples);
                rangeEnd   = getSliceEndPosition(activeSlice, fileLengthSamples) - 1.0;
            }
            else
            {
                rangeStart = 0.0;
                rangeEnd   = static_cast<double>(fileLengthSamples - 1);
            }

            // Narrow by loop window
            if (loopWinOn)
            {
                rangeStart = std::max(rangeStart, (double) loopStart);
                rangeEnd   = std::min(rangeEnd,   (double) loopEnd);
            }

            // Distance to spatial boundary (in output samples)
            if (step > 0.0)
            {
                double dist = (rangeEnd - currentPos) / step;
                chunkSize = juce::jmin(chunkSize, juce::jmax(1, static_cast<int>(dist)));
            }
            else if (step < 0.0)
            {
                double dist = (currentPos - rangeStart) / (-step);
                chunkSize = juce::jmin(chunkSize, juce::jmax(1, static_cast<int>(dist)));
            }

            // Distance to ping-pong flip
            if (ppEnabled && ppPeriod > 0.0)
            {
                double ppDist = (ppPeriod - ppPhase) / std::abs(step);
                chunkSize = juce::jmin(chunkSize, juce::jmax(1, static_cast<int>(ppDist)));
            }
        }

        // ==================================================================
        // §7: Tight, branch-free interpolation loop for this chunk
        // ==================================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = sourceBuffer.getReadPointer(ch);
            float* dst = outputBuffer.getWritePointer(ch) + outputPos;
            double pos = currentPos;

            for (int i = 0; i < chunkSize; ++i)
            {
                const int p1 = static_cast<int>(pos);
                const int p2 = juce::jmin(p1 + 1, fileLengthSamples - 1);
                const float frac = static_cast<float>(pos - static_cast<double>(p1));
                dst[i] = src[p1] + frac * (src[p2] - src[p1]);
                pos += step;
            }
        }

        // Apply crossfade overlay if active
        if (isInCrossfade)
            applyCrossfadeToSliceTransition(sourceBuffer, fileLengthSamples,
                                           fileSampleRate, outputBuffer,
                                           outputPos, chunkSize);

        // Advance playhead and ping-pong phase
        currentPos += step * chunkSize;

        if (ppEnabled && ppPeriod > 0.0)
            ppPhase += std::abs(step) * chunkSize;

        outputPos += chunkSize;
    }

    // §5.2  Write back all state atomics once after the loop.
    playheadPosition.store(currentPos);
    currentActiveSlice.store(activeSlice);
    sliceTriggered.store(sliceTrig);
    if (ppEnabled)
    {
        pingPongPhasePosition.store(ppPhase);
        pingPongGoingForward.store(ppGoingForward);
    }
}

//==============================================================================
// Loop window controls
//==============================================================================
void AudioBuffer::setLoopWindow(int64_t startSamples, int64_t endSamples)
{
    if (endSamples <= startSamples)
    {
        loopWindowEnabled.store(false);
        loopStartSamples.store(0);
        loopEndSamples.store(0);
        return;
    }
    loopWindowEnabled.store(true);
    loopStartSamples.store(juce::jmax<int64_t>(0, startSamples));
    loopEndSamples.store(juce::jmax<int64_t>(0, endSamples));
}

void AudioBuffer::clearLoopWindow()
{
    loopWindowEnabled.store(false);
}

bool AudioBuffer::loadAudioFile(const juce::File& file, juce::AudioFormatManager& formatManager)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    
    if (reader == nullptr)
        return false;

    auto newData = new LoadedAudioData();
    newData->fileName = file.getFileNameWithoutExtension();

    const int fileLengthSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    const double fileSR = reader->sampleRate;

    // §4.2  Resample to host SR at load time if the rates differ, eliminating
    // continuous per-sample ratio math during playback.
    if (hostSampleRate > 0.0
        && fileSR > 0.0
        && std::abs (fileSR - hostSampleRate) > 0.5)
    {
        juce::AudioBuffer<float> nativeBuf (numChannels, fileLengthSamples);
        reader->read (&nativeBuf, 0, fileLengthSamples, 0, true, true);

        const double ratio = fileSR / hostSampleRate;
        // Allocate with +4 padding so the Lagrange interpolator has headroom
        // at the tail end of the input.  We trim to the correct musical length
        // after processing.
        const int usableLength = juce::jmax (1, (int) std::round ((double) fileLengthSamples / ratio));
        const int allocLength  = usableLength + 4;
        newData->buffer.setSize (numChannels, allocLength);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::LagrangeInterpolator interp;
            // process() returns the number of INPUT samples consumed (not output).
            // It always writes exactly allocLength output samples.
            interp.process (
                ratio,
                nativeBuf.getReadPointer (ch),
                newData->buffer.getWritePointer (ch),
                allocLength,
                fileLengthSamples,
                0
            );
        }

        // Trim to the musically-correct length so the loop boundary is precise.
        newData->buffer.setSize (numChannels, usableLength, true);
        newData->sampleRate = hostSampleRate;
    }
    else
    {
        newData->sampleRate = fileSR;
        newData->buffer.setSize (numChannels, fileLengthSamples);
        reader->read (&newData->buffer, 0, fileLengthSamples, 0, true, true);
    }

    setLoadedAudioData(newData);
    return true;
}

void AudioBuffer::setLoadedAudioData(LoadedAudioData::Ptr newData)
{
    // Rotate retainers so the previous data stays alive long enough for any
    // concurrent reader that loaded the raw pointer but hasn't yet bumped
    // its ref count.
    previousAudioDataRetainer = audioDataRetainer;
    audioDataRetainer = newData;
    atomicAudioData.store (newData.get(), std::memory_order_release);

    // Reset playback position and transient state for a clean start.
    playheadPosition.store(0.0);
    clearLoopWindow();
    slicingModeActive.store(false);
    sliceTriggered.store(false);
    currentActiveSlice.store(0);
    targetSlice.store(0);
    params.continuousRandomSlicing = false;

    isInCrossfade = false;
    crossfadePosition = 0;
    activeCrossfadeLen = crossfadeLengthSamples;
    crossfadeIsLookaheadContinuation = false;
    previousSliceIndex = -1;

    // BUG 7: cancel transition fade — new audio data invalidates old-tail capture.
    transitionFadeActive = false;
    stretchOutputRingValidSamples = 0;

    // Reset time-stretch state for a clean start.
    stretchRatio.store(1.0);
    stretcherPrepared = false;
    stretcherPrimed = false;
    stretcherNeedsReset.store(false);
    lastBlockUsedStretch = false;
    stretchFadeInRemaining = 0;
    stretcher.reset();
    stretchSmoother.setCurrentAndTargetValue(1.0);
    speedMagSmoother.setCurrentAndTargetValue(1.0);
    lastStretchDirection = 1.0;
    previousBlockValid = false;
    resetCrossfadePending = false;
}

void AudioBuffer::clearAudioData()
{
    previousAudioDataRetainer = audioDataRetainer;
    audioDataRetainer = nullptr;
    atomicAudioData.store (nullptr, std::memory_order_release);
    playheadPosition.store(0.0);
    clearLoopWindow();
    resetToDefaults();
}

//==============================================================================
// Transport Controls
//==============================================================================

void AudioBuffer::play()
{
#if JUCE_DEBUG
    // Reset tearing stats when starting playback for a fresh tracking session
    if (tearingDebugEnabled.load() && !params.isPlaying)
    {
        DBG("[AudioBuffer " + juce::String(bufferIndex) + "] Starting playback - resetting tearing stats");
        tearingStats.reset();
        consecutiveZeroSamples = 0;
        lastOutputSample[0] = 0.0f;
        lastOutputSample[1] = 0.0f;
        lastBlockRms[0] = 0.0f;
        lastBlockRms[1] = 0.0f;
        lastBlockDcOffset[0] = 0.0f;
        lastBlockDcOffset[1] = 0.0f;
    }
#endif
    setPlaying(true);
}

void AudioBuffer::stop()
{
#if JUCE_DEBUG
    // Print tearing stats summary when stopping
    if (tearingDebugEnabled.load() && params.isPlaying)
    {
        const int totalEvents = tearingStats.getTotalEvents();
        if (totalEvents > 0)
        {
            DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING SUMMARY: " + tearingStats.getSummary());
        }
        else
        {
            DBG("[AudioBuffer " + juce::String(bufferIndex) + "] Stopping playback - no tearing events detected");
        }
    }
#endif
    setPlaying(false);
}

void AudioBuffer::pause()
{
    setPlaying(false);
}

void AudioBuffer::setPlaying(bool shouldPlay)
{
    params.isPlaying = shouldPlay;

    if (! shouldPlay)
    {
        // Signal the audio thread to flush SoundTouch so we don't resume with stale data.
        // Use a flag rather than touching SoundTouch directly for thread safety.
        stretcherNeedsReset.store(true);
    }
}

void AudioBuffer::setSpeed(double newSpeed)
{
    params.speed = newSpeed;
    atomicSpeed.store(newSpeed, std::memory_order_relaxed); // keep torn-read-safe mirror in sync
    // Direction changes are handled smoothly in processWithTimeStretch via
    // setWindowsForReverse(); no SoundTouch flush is needed. The source-level
    // crossfade and SoundTouch's overlap-add handle the transition naturally.
}

void AudioBuffer::setStretchRatio(double ratio)
{
    if (ratio <= 0.0)
        ratio = 1.0;

    // Clamp to reasonable range (matches TimeStretchSoundTouch)
    ratio = juce::jlimit(0.25, 4.0, ratio);

    const double oldRatio = stretchRatio.load();
    stretchRatio.store(ratio);

    // When transitioning between stretch and non-stretch modes, signal the audio
    // thread to clear SoundTouch's internal buffers so stale data doesn't cause
    // a volume spike or burst of old audio on the first output block.
    const bool wasStretching = std::abs(oldRatio - 1.0) > 1.0e-6;
    const bool nowStretching = std::abs(ratio  - 1.0) > 1.0e-6;
    const bool pitchActive = std::abs(pitchSemiTones.load()) > 1.0e-6;

    // Only reset when OVERALL stretcher usage toggles on/off.
    // Account for pitch-active state: if pitch is keeping the stretcher alive,
    // toggling stretch on/off doesn't change overall usage and shouldn't
    // flush SoundTouch's pipeline.
    const bool oldOverallUse = (wasStretching || pitchActive);
    const bool newOverallUse = (nowStretching || pitchActive);
    if (oldOverallUse != newOverallUse)
    {
        stretcherNeedsReset.store(true);
    }
    // No else-if reset for ratio changes while already running.
    // SoundTouch handles parameter changes natively via per-chunk processing.
    // The stretchSmoother ramps the value smoothly.
}

void AudioBuffer::setLooping(bool shouldLoop)
{
    params.isLooping = shouldLoop;
}

void AudioBuffer::resetToBeginning()
{
    playheadPosition.store(0.0);
    currentActiveSlice.store(0);
    slicingModeActive.store(false);
    sliceTriggered.store(false);
    params.continuousRandomSlicing = false;
}

void AudioBuffer::resetToDefaults()
{
    resetToBeginning();
    params.reset();
    atomicSpeed.store(params.speed, std::memory_order_relaxed);

    stretchRatio.store(1.0);
    pitchSemiTones.store(0.0);
    stretcherPrepared = false;
    stretcherPrimed = false;
    stretcherNeedsReset.store(false);
    lastBlockUsedStretch = false;
    stretcher.reset();
    speedSmoother.setCurrentAndTargetValue(1.0);
    stretchSmoother.setCurrentAndTargetValue(1.0);
    speedMagSmoother.setCurrentAndTargetValue(1.0);
    pitchSemiSmoother.setCurrentAndTargetValue(0.0);
    lastStretchDirection = 1.0;
    previousBlockValid = false;
    resetCrossfadePending = false;
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    activeCrossfadeLen = crossfadeLengthSamples;
    crossfadeIsLookaheadContinuation = false;
    previousSliceIndex = -1;

    // §12: Reset lookahead state.
    resetLookaheadState();

    // §10.3  Reset output-side crossfade state.
    stretchOutputRingWritePos = 0;
    stretchOutputRingValidSamples = 0;
    stretchCrossfadeSnapshotLen = 0;
    stretchOutputCrossfadePending = false;
    stretchOutputCrossfadeActive = false;
    stretchOutputCrossfadePos = 0;
    stretchOutputCrossfadeLen = 0;

    // BUG 7: reset multi-block transition fade state.
    transitionFadeActive = false;
    transitionFadePos = 0;
    transitionFadeLen = 0;
    stretcherResetThisBlock = false;
    
    // Reset ping pong state
    pingPongEnabled.store(false);
    pingPongDivision.store(0.25);
    pingPongPeriodSamples.store(0.0);
    pingPongPhasePosition.store(0.0);
    pingPongGoingForward.store(true);
}

void AudioBuffer::processWithTimeStretch(juce::AudioBuffer<float>& outputBuffer, const StretchSnapshot& snap)
{
    const int numOutputSamples = outputBuffer.getNumSamples();

    auto data = getAudioDataSnapshot();
    if (data == nullptr || data->buffer.getNumSamples() <= 0 || data->sampleRate <= 0.0)
    {
        outputBuffer.clear();
        return;
    }

    const auto& sourceBuffer = data->buffer;
    const int fileLengthSamples = sourceBuffer.getNumSamples();
    const double fileSampleRate = data->sampleRate;
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), sourceBuffer.getNumChannels());
    if (numChannels <= 0)
    {
        outputBuffer.clear();
        return;
    }

    // Use a longer crossfade when SoundTouch is active so that source-level
    // transitions (slice jumps, loop boundaries, direction changes) span SoundTouch's
    // overlap-add window. This avoids the need to flush SoundTouch's pipeline.
    // NOTE: this temporary raise is safe for in-flight crossfades because every
    // crossfade captures its own length (activeCrossfadeLen) at start — the
    // member value only affects crossfades STARTED while it is raised (BUG 2 fix).
    const int savedCrossfadeLengthSamples = crossfadeLengthSamples;
    crossfadeLengthSamples = juce::jmax(crossfadeLengthSamples,
                                        (int)(hostSampleRate * 0.05)); // 50ms minimum

    // T3: Use the coordinated snapshot instead of independent atomic reads.
    const double ratio      = snap.stretchRatio;
    const double pitchSemis = snap.pitchSemis;
    const bool pitchActive  = snap.usePitch();
    // When time-stretching, SoundTouch changes the output tempo. We should NOT also scale
    // the source read speed by ratio (that would double-apply the effect).
    // We do, however, still apply host tempo-following so Stretch remains relative to the DAW tempo.
    const double direction  = snap.direction();
    const double inputSpeed = direction * snap.tempoMult;

    // If pitch shifting is active, keep tempo/speed independent of pitch by expressing speed
    // via SoundTouch tempo (NOT rate). Rate is only used when pitch shifting is inactive.
    const double speedMag = snap.speedMag();
    const bool useRate = (!pitchActive) && (std::abs(speedMag - 1.0) > 1.0e-6);

    // Handle deferred reset (e.g., from setStretchRatio mode transition, stop, or direction flip).
    // Always touch the SoundTouch instance from the audio thread only.
    const bool didReset = stretcherNeedsReset.exchange(false);
    if (didReset)
    {
        stretcher.reset();
        stretcherPrepared = false;
        stretcherPrimed = false;
        stretchFadeInRemaining = 0;
        // §12: Reset lookahead state on pipeline reset.
        resetLookaheadState();
        // §2.2A  Invalidate block-resampler state on pipeline resets so the
        // interpolators don't carry stale filter history across a seek/jump.
        blockResamplersValid = false;
    }

    // Ensure scratch buffers are appropriately sized without per-sample allocation.
    // We intentionally keep these buffers around; setSize may allocate but only when capacity grows.
    // For time-stretch we may need to feed more input than output; size input scratch accordingly.
    const double clampedRatio = juce::jlimit (0.25, 4.0, ratio);
    // Smooth stretch ratio changes to avoid abrupt tempo jumps.
    // However, on a reset (mode transition), snap to target immediately to avoid
    // underruns from the mismatch between the priming calculation and actual tempo.
    stretchSmoother.setTargetValue(clampedRatio);
    if (didReset)
        stretchSmoother.setCurrentAndTargetValue(clampedRatio);
    const double smoothedRatio = stretchSmoother.getNextValue();
    if (numOutputSamples > 1)
        stretchSmoother.skip(numOutputSamples - 1);

    // T4: Smooth speed magnitude changes to avoid abrupt rate/tempo jumps in SoundTouch.
    // On reset, snap to target to match the stretchSmoother fix above.
    speedMagSmoother.setTargetValue (speedMag > 0.0 ? speedMag : 1.0);
    if (didReset)
        speedMagSmoother.setCurrentAndTargetValue(speedMag > 0.0 ? speedMag : 1.0);
    const double smoothedSpeedMag = speedMagSmoother.getNextValue();
    if (numOutputSamples > 1)
        speedMagSmoother.skip (numOutputSamples - 1);

    // Smooth pitch semitones to avoid abrupt tempo-compensation jumps when
    // adding/stacking pitch modifiers (e.g. oct+ on top of stretch + speed).
    // On reset (mode transition), snap to target so priming calculations match.
    pitchSemiSmoother.setTargetValue (pitchSemis);
    if (didReset)
        pitchSemiSmoother.setCurrentAndTargetValue (pitchSemis);
    const double smoothedPitchSemis = pitchSemiSmoother.getNextValue();
    if (numOutputSamples > 1)
        pitchSemiSmoother.skip (numOutputSamples - 1);

    const double clampedRate = juce::jlimit (0.25, 4.0, smoothedSpeedMag);
    // SoundTouch internally divides virtualTempo by virtualPitch to get effective tempo:
    //   effective_tempo = virtualTempo / virtualPitch
    // and sets effective_rate = virtualRate * virtualPitch.
    // This internal division IS how SoundTouch preserves playback speed during pitch
    // shifting.  We must NOT pre-multiply the tempo we send by the pitch ratio —
    // doing so would cancel the compensation and cause the audio to speed up/slow
    // down whenever a pitch modifier is active.
    //
    // When pitch is active, speed magnitude is routed through tempo (not rate) so
    // that SoundTouch can keep pitch and speed independent.
    const double tempoRatioForSt = pitchActive ? (smoothedRatio * clampedRate) : smoothedRatio;
    const double totalTempoRatioForIO = tempoRatioForSt * (useRate ? clampedRate : 1.0);
    // Feed chunks proportional to the output block.  The margin scales with the effective
    // consumption ratio so we don't starve SoundTouch when rate/tempo/pitch are high.
    const int marginFrames = juce::jmax (64, (int) std::ceil (64.0 * totalTempoRatioForIO));
    const int maxInputChunkFrames = juce::jmax (numOutputSamples, (int) std::ceil (numOutputSamples * totalTempoRatioForIO) + marginFrames);

    // T10: Log mode transitions for debugging.
#if JUCE_DEBUG
    if (didReset)
    {
        juce::String modifiers;
        if (std::abs(snap.pitchSemis) > 0.01)
            modifiers += " [PITCH]";
        if (std::abs(snap.stretchRatio - 1.0) > 0.01)
            modifiers += " [STRETCH]";
        if (std::abs(snap.speed - 1.0) > 0.01)
            modifiers += " [SPEED]";
        if (direction < 0.0)
            modifiers += " [REVERSE]";
        if (slicingModeActive.load())
            modifiers += " [SLICING]";
        
        DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] SoundTouch RESET  speed=" + juce::String(snap.speed)
             + " stretch=" + juce::String(snap.stretchRatio) + " pitch=" + juce::String(snap.pitchSemis)
             + " dir=" + juce::String(direction) + " pos=" + juce::String(playheadPosition.load())
             + " modifiers:" + modifiers);
        if (tearingDebugEnabled.load())
            tearingStats.soundTouchResets.fetch_add(1, std::memory_order_relaxed);
    }
#endif

    // Prepare SoundTouch when needed.
    if (!stretcherPrepared || stretcherPreparedSampleRate != hostSampleRate || stretcherPreparedChannels != numChannels)
    {
        stretcher.prepare(hostSampleRate, numChannels);
        // T9: Tune windows for reversed audio on initial prepare.
        stretcher.setWindowsForReverse (direction < 0.0);
        stretcher.setTempoRatio ((float) tempoRatioForSt);
        stretcher.setRateRatio ((float) (useRate ? clampedRate : 1.0));
        stretcher.setPitchSemiTones ((float) smoothedPitchSemis);
        stretcherPrepared = true;
        stretcherPreparedSampleRate = hostSampleRate;
        stretcherPreparedChannels = numChannels;
        stretcherPrimed = false;
        stretchFadeInRemaining = 0;
        lastStretchDirection = direction;
    }
    else
    {
        // Ratio can change without channel/sr changing; keep tempo updated.
        stretcher.setTempoRatio ((float) tempoRatioForSt);
        stretcher.setRateRatio ((float) (useRate ? clampedRate : 1.0));
        stretcher.setPitchSemiTones ((float) smoothedPitchSemis);

        // T9: When the direction changes, re-tune SoundTouch's overlap windows.
        if (direction != lastStretchDirection)
        {
            stretcher.setWindowsForReverse (direction < 0.0);
            // §12: Direction flip invalidates the lookahead — boundary is now on the opposite side.
            if (isInLookaheadCrossfade)
            {
                isInLookaheadCrossfade = false;
                lookaheadNextSlice = -1;
#if JUCE_DEBUG
                if (tearingDebugEnabled.load())
                    tearingStats.lookaheadAborts.fetch_add(1, std::memory_order_relaxed);
#endif
            }
#if JUCE_DEBUG
            DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] direction flip " + juce::String(lastStretchDirection) 
                 + "->" + juce::String(direction) + " speed=" + juce::String(snap.speed)
                 + " stretch=" + juce::String(snap.stretchRatio) + " pos=" + juce::String(playheadPosition.load()));
            if (tearingDebugEnabled.load())
                tearingStats.directionFlips.fetch_add(1, std::memory_order_relaxed);
#endif
            lastStretchDirection = direction;
        }
    }

    // Ensure scratch buffers are appropriately sized.
    // These are pre-allocated in prepare() at worst-case sizes.  The guard is
    // kept as a safety net but should never trigger in normal operation.
    const int stretcherLatency = juce::jmax (0, stretcher.getLatencySamples());
    if (stretchInScratch.getNumChannels() != numChannels || stretchInScratch.getNumSamples() < maxInputChunkFrames)
    {
        jassertfalse; // scratch buffer should have been pre-allocated in prepare()
        stretchInScratch.setSize (numChannels, maxInputChunkFrames, false, false, true);
    }

    // Fill / pull in a loop so we always output a full block.
    // Key idea: try to drain existing SoundTouch output first; if insufficient, feed more input and retry.
    // This avoids the periodic underruns (silence tail) that sound like crackles.
    outputBuffer.clear();

    auto fillInputScratch = [&] (int framesToGenerate) -> int
    {
        jassert (framesToGenerate >= 0);
        if (framesToGenerate == 0)
            return 0;

        // Fast path: common case is forward playback at 1x, same file/host sample rate, no slicing/window/crossfade.
        // In that case we can bulk-copy samples instead of per-sample interpolation.
        const double currentPos0 = playheadPosition.load();
        const bool canBulkCopy = (inputSpeed > 0.0
                                  && std::abs (inputSpeed - 1.0) < 1.0e-9
                                  && std::abs (fileSampleRate - hostSampleRate) < 1.0e-9
                                  && ! loopWindowEnabled.load()
                                  && ! slicingModeActive.load()
                                  && ! isInCrossfade
                                  && std::abs (currentPos0 - std::floor (currentPos0)) < 1.0e-9);

        if (canBulkCopy)
        {
            const int startSample = (int) currentPos0;
            if (startSample >= 0 && startSample < fileLengthSamples)
            {
                int framesCopied = 0;
                int pos = startSample;

                while (framesCopied < framesToGenerate)
                {
                    const int available = fileLengthSamples - pos;
                    if (available <= 0)
                    {
                        if (params.isLooping)
                            pos = 0;
                        else
                        {
                            params.isPlaying = false;
                            break;
                        }
                    }

                    const int chunk = juce::jmin (framesToGenerate - framesCopied, available);
                    for (int ch = 0; ch < numChannels; ++ch)
                        stretchInScratch.copyFrom (ch, framesCopied, sourceBuffer, ch, pos, chunk);

                    framesCopied += chunk;
                    pos += chunk;
                }

                playheadPosition.store ((double) (startSample + framesCopied));
                // Bulk-copy bypasses the interpolator, so invalidate its state.
                blockResamplersValid = false;
                return framesCopied;
            }
        }

        // §2.2A  Extended fast path: block-based resampling via LagrangeInterpolator.
        // Handles the common case where speed≠1.0 or fileSR≠hostSR but no special
        // playback features (slicing, loop window, crossfade, ping-pong) are active.
        // This replaces the expensive per-sample interpolation loop with JUCE's
        // optimised block-processing interpolator.
        {
            const double blockStep = inputSpeed * (fileSampleRate / hostSampleRate);
            const bool canBlockResample = (inputSpeed > 0.0
                                           && blockStep > 0.0
                                           && ! loopWindowEnabled.load()
                                           && ! slicingModeActive.load()
                                           && ! isInCrossfade
                                           && ! pingPongEnabled.load());

            if (canBlockResample)
            {
                const int startPos = (int) currentPos0;
                if (startPos >= 0 && startPos < fileLengthSamples)
                {
                    // The Lagrange interpolator uses a 4-point kernel and needs
                    // a few extra samples beyond the nominal consumption.
                    const int inputNeeded = (int) std::ceil (framesToGenerate * blockStep) + 8;
                    const int availableInput = fileLengthSamples - startPos;

                    if (availableInput >= inputNeeded)
                    {
                        // Reset interpolators if internal filter state is stale
                        // (playhead jumped, first call, or previous call used the
                        // per-sample path).
                        if (! blockResamplersValid
                            || std::abs (currentPos0 - blockResamplerExpectedPos) > 1.0e-6)
                        {
                            for (int ch = 0; ch < numChannels; ++ch)
                                blockResamplers[ch].reset();
                        }

                        int totalInputUsed = 0;
                        for (int ch = 0; ch < numChannels; ++ch)
                        {
                            const int used = blockResamplers[ch].process (
                                blockStep,
                                sourceBuffer.getReadPointer (ch, startPos),
                                stretchInScratch.getWritePointer (ch),
                                framesToGenerate,
                                availableInput,
                                0 /* no wrap */
                            );
                            totalInputUsed = juce::jmax (totalInputUsed, used);
                        }

                        const double newPos = currentPos0 + (double) totalInputUsed;
                        playheadPosition.store (newPos);
                        blockResamplerExpectedPos = newPos;
                        blockResamplersValid = true;
                        return framesToGenerate;
                    }
                    // Not enough contiguous samples (near end of file).
                    // Fall through to per-sample path for boundary handling.
                }
            }

            // Falling through to per-sample path invalidates the interpolator.
            blockResamplersValid = false;
        }

        // Zero the region we are going to fill.
        stretchInScratch.clear (0, framesToGenerate);

        // Use local playhead while generating to avoid per-sample atomic traffic.
        double currentPos = playheadPosition.load();
        const double step = inputSpeed * (fileSampleRate / hostSampleRate);

        auto startBoundaryCrossfadeLocal = [&] (double newPlayheadPos)
        {
            previousSlicePlayheadPos = currentPos;
            previousSliceIndex = -1;
            isInCrossfade = true;
            crossfadePosition = 0;
            activeCrossfadeLen = crossfadeLengthSamples;
            crossfadeIsLookaheadContinuation = false;
            currentPos = newPlayheadPos;
        };

        // Inline slice handling using the local playhead.
        // §5.2  All slice/ping-pong atomic state is snapshotted into locals
        // below (before the per-sample loop) so the inner loop is free of
        // atomic traffic.
        bool localSliceTrig = sliceTriggered.load();
        const int  localTargetSlice = targetSlice.load();
        int  localActiveSlice = currentActiveSlice.load();
        const bool slicingOn = slicingModeActive.load();

        auto handleSlicePlaybackLocal = [&] ()
        {
            if (! slicingOn)
                return;

            // Check if we need to jump to a new slice
            if (localSliceTrig)
            {
                const int slice = localTargetSlice;
                const double speed = getEffectiveSpeed();

                double newPos = 0.0;
                if (speed >= 0.0)
                    newPos = getSliceStartPosition (slice, fileLengthSamples);
                else
                    newPos = getSliceEndPosition (slice, fileLengthSamples) - 1.0;

                // Inline startSliceCrossfade using locals.
                // §11 revised: No SoundTouch pipeline flush on slice jumps.
                // The per-sample loop applies an input-side crossfade (§10.3
                // revised) so SoundTouch receives smooth audio at transitions.
                // §12: External trigger always aborts any active lookahead.
                if (isInLookaheadCrossfade)
                {
                    isInLookaheadCrossfade = false;
                    lookaheadNextSlice = -1;
#if JUCE_DEBUG
                    if (tearingDebugEnabled.load())
                        tearingStats.lookaheadAborts.fetch_add(1, std::memory_order_relaxed);
#endif
                }
                previousSliceIndex = localActiveSlice;
                previousSlicePlayheadPos = currentPos;
                isInCrossfade = true;
                crossfadePosition = 0;
                activeCrossfadeLen = crossfadeLengthSamples;
                crossfadeIsLookaheadContinuation = false;
                rmsBlankingBlocksLeft = 6;
                localActiveSlice = slice;
                currentPos = newPos;
                localSliceTrig = false;
                return;
            }

            // Handle continuous random mode
            if (params.continuousRandomSlicing)
            {
                const double sliceStart = getSliceStartPosition (localActiveSlice, fileLengthSamples);
                const double sliceEnd   = getSliceEndPosition (localActiveSlice, fileLengthSamples);

                bool shouldTriggerNext = false;
                const double speed = getEffectiveSpeed();

                if (speed > 0.0)
                    shouldTriggerNext = (currentPos >= sliceEnd - 1.0);
                else if (speed < 0.0)
                    shouldTriggerNext = (currentPos <= sliceStart);

                if (shouldTriggerNext)
                {
                    // §12: Use pre-computed next slice if available; otherwise roll the RNG.
                    const int nextSlice = (precomputedNextRandomSlice >= 0)
                                            ? precomputedNextRandomSlice
                                            : random.nextInt(params.numSlices);

                    double newPos = 0.0;
                    if (speed > 0.0)
                        newPos = getSliceStartPosition (nextSlice, fileLengthSamples);
                    else
                        newPos = getSliceEndPosition (nextSlice, fileLengthSamples) - 1.0;

                    // §12: Coordinate with lookahead pre-crossfade.
                    const bool lookaheadPredictionCorrect =
                        isInLookaheadCrossfade && (lookaheadNextSlice == nextSlice);
                    if (isInLookaheadCrossfade && !lookaheadPredictionCorrect)
                    {
                        // Misprediction — abort and fall back to full post-jump crossfade
                        isInLookaheadCrossfade = false;
#if JUCE_DEBUG
                        if (tearingDebugEnabled.load())
                            tearingStats.lookaheadMispredictions.fetch_add(1, std::memory_order_relaxed);
#endif
                    }
                    // The boundary consumes the lookahead state (it may still be
                    // "holding" at 50% — see BUG 5b hold fix).
                    isInLookaheadCrossfade = false;

                    previousSliceIndex = localActiveSlice;
                    previousSlicePlayheadPos = currentPos;
                    isInCrossfade = true;
                    crossfadePosition = 0;
                    // §12 + BUG 5 fix: when the prediction was correct, the input at
                    // the boundary is already a 50/50 blend from the lookahead ramp.
                    // Use a half-length crossfade that CONTINUES from 50% blend
                    // (see crossfadeIsLookaheadContinuation in the blend code) so the
                    // mix is continuous through the boundary sample.
                    // BUG 2 fix: capture into activeCrossfadeLen instead of mutating
                    // the shared member.
                    activeCrossfadeLen = lookaheadPredictionCorrect
                        ? juce::jmax(64, crossfadeLengthSamples / 2)
                        : crossfadeLengthSamples;
                    crossfadeIsLookaheadContinuation = lookaheadPredictionCorrect;
                    rmsBlankingBlocksLeft = 6;
                    localActiveSlice = nextSlice;
                    // BUG 5c: continue reading the new slice from where the lookahead
                    // ramp was already reading it — restarting at the slice start would
                    // rewind the (already audible at 50%) new-slice content, creating a
                    // content discontinuity at the boundary sample.
                    currentPos = lookaheadPredictionCorrect
                        ? juce::jlimit(0.0, (double) (fileLengthSamples - 1), lookaheadNextSliceReadPos)
                        : newPos;

                    // §12: Pre-compute the *following* random slice for the next lookahead cycle.
                    precomputedNextRandomSlice = random.nextInt(params.numSlices);
                    lookaheadNextSlice = -1;
                }
            }
            else if (params.arpSliceActive && !params.arpSequence.empty())
            {
                const double sliceStart = getSliceStartPosition (localActiveSlice, fileLengthSamples);
                const double sliceEnd   = getSliceEndPosition (localActiveSlice, fileLengthSamples);

                bool shouldTriggerNext = false;
                const double speed = getEffectiveSpeed();

                if (speed > 0.0)
                    shouldTriggerNext = (currentPos >= sliceEnd - 1.0);
                else if (speed < 0.0)
                    shouldTriggerNext = (currentPos <= sliceStart);

                if (shouldTriggerNext)
                {
                    params.arpSequencePos++;
                    if (params.arpSequencePos >= (int)params.arpSequence.size())
                    {
                        params.arpSequencePos = 0;
                        params.arpCycleCount++;
                        if (params.arpCycleCount >= params.arpTotalCyclesPerRefresh)
                        {
                            params.arpCycleCount = 0;
                            int seqLen = (int)params.arpSequence.size();
                            params.arpSequence.clear();
                            if (params.arpSliceRepeaterMode)
                            {
                                int rs = random.nextInt(params.numSlices);
                                for (int si = 0; si < seqLen; ++si)
                                    params.arpSequence.push_back(rs);
                            }
                            else
                            {
                                for (int si = 0; si < seqLen; ++si)
                                    params.arpSequence.push_back(random.nextInt(params.numSlices));
                            }
                        }
                    }
                    const int nextSlice = params.arpSequence[(size_t)params.arpSequencePos];

                    double newPos = 0.0;
                    if (speed > 0.0)
                        newPos = getSliceStartPosition (nextSlice, fileLengthSamples);
                    else
                        newPos = getSliceEndPosition (nextSlice, fileLengthSamples) - 1.0;

                    // §12: Coordinate with lookahead pre-crossfade.
                    const bool lookaheadPredictionCorrect =
                        isInLookaheadCrossfade && (lookaheadNextSlice == nextSlice);
                    if (isInLookaheadCrossfade && !lookaheadPredictionCorrect)
                    {
                        isInLookaheadCrossfade = false;
#if JUCE_DEBUG
                        if (tearingDebugEnabled.load())
                            tearingStats.lookaheadMispredictions.fetch_add(1, std::memory_order_relaxed);
#endif
                    }
                    // The boundary consumes the lookahead state (may be holding at 50%).
                    isInLookaheadCrossfade = false;

                    previousSliceIndex = localActiveSlice;
                    previousSlicePlayheadPos = currentPos;
                    isInCrossfade = true;
                    crossfadePosition = 0;
                    // §12 + BUG 5 fix: continue from the 50% lookahead blend (see above).
                    activeCrossfadeLen = lookaheadPredictionCorrect
                        ? juce::jmax(64, crossfadeLengthSamples / 2)
                        : crossfadeLengthSamples;
                    crossfadeIsLookaheadContinuation = lookaheadPredictionCorrect;
                    rmsBlankingBlocksLeft = 6;
                    localActiveSlice = nextSlice;
                    // BUG 5c: continue from the lookahead read position (see above).
                    currentPos = lookaheadPredictionCorrect
                        ? juce::jlimit(0.0, (double) (fileLengthSamples - 1), lookaheadNextSliceReadPos)
                        : newPos;
                    lookaheadNextSlice = -1;
                }
            }
            else
            {
                const double sliceStart = getSliceStartPosition (localActiveSlice, fileLengthSamples);
                const double sliceEnd   = getSliceEndPosition (localActiveSlice, fileLengthSamples);
                const double speed = getEffectiveSpeed();

                // BUGFIX (BUG 4): slice-loop wrap now starts a crossfade instead of
                // teleporting the playhead (was a click on every loop cycle).
                if (speed > 0.0 && currentPos >= sliceEnd - 1.0)
                {
                    if (params.isLooping)
                        startBoundaryCrossfadeLocal (sliceStart);
                    else
                        params.isPlaying = false;
                }
                else if (speed < 0.0 && currentPos <= sliceStart)
                {
                    if (params.isLooping)
                        startBoundaryCrossfadeLocal (sliceEnd - 1.0);
                    else
                        params.isPlaying = false;
                }
            }
        };

        const float* const* sourceRead = sourceBuffer.getArrayOfReadPointers();
        float* const* scratchWrite = stretchInScratch.getArrayOfWritePointers();

        const bool loopWindowOn = loopWindowEnabled.load();
        const int64_t loopStart = loopWindowOn ? loopStartSamples.load() : 0;
        const int64_t loopEnd   = loopWindowOn ? loopEndSamples.load()   : 0;

        // §5.2  Snapshot ping-pong state into locals before the per-sample loop.
        const bool ppEnabled   = pingPongEnabled.load();
        const double ppPeriod  = ppEnabled ? pingPongPeriodSamples.load() : 0.0;
        double ppPhase         = ppEnabled ? pingPongPhasePosition.load() : 0.0;
        bool ppGoingForward    = ppEnabled ? pingPongGoingForward.load()  : true;

        // §12  Lookahead pre-priming: compute the lookahead distance from
        // SoundTouch's internal latency.  Only active when slicing AND stretcher
        // are both in use — the repitch path doesn't benefit from this.
        const bool lookaheadEnabled = slicingOn && snap.useStretcher();
        const int lookaheadSamples = lookaheadEnabled
            ? juce::jlimit(512, 4096, stretcher.getLatencySamples())
            : 0;

        // §12  Lambda: manage the lookahead pre-crossfade state.
        // Called per-sample before the main interpolation to check whether we
        // are approaching a slice boundary and should begin blending in the
        // next slice's audio.
        auto handleLookaheadLocal = [&] ()
        {
            if (! lookaheadEnabled)
                return;

            const double effectiveSpeed = getEffectiveSpeed();

            // --- Already in a lookahead crossfade: nothing more to check ---
            if (isInLookaheadCrossfade)
                return;

            // --- Check if we're inside the lookahead zone ---
            const double distToBoundary = getDistanceToSliceBoundary(
                currentPos, localActiveSlice, fileLengthSamples, effectiveSpeed);

            // Convert from source-domain samples to the fillInputScratch domain.
            // step = inputSpeed * (fileSampleRate / hostSampleRate), so distance
            // in "fill iterations" ≈ distToBoundary / |step|.  But since lookaheadSamples
            // is already in host-rate samples, we compare directly with the
            // source-domain distance (which is what fills the SoundTouch FIFO).
            if (distToBoundary > (double) lookaheadSamples || distToBoundary <= 0.0)
                return; // not in the zone yet, or already past boundary

            // --- Determine the next slice ---
            const int nextSlice = getNextSliceIndex(localActiveSlice, fileLengthSamples);
            if (nextSlice < 0)
                return; // unpredictable — skip lookahead for this transition

            // --- Start the lookahead crossfade ---
            lookaheadNextSlice = nextSlice;
            isInLookaheadCrossfade = true;
            lookaheadCrossfadePosition = 0;
            lookaheadCrossfadeSamples = juce::jlimit(1, lookaheadSamples,
                                                      (int) distToBoundary);

            // Compute the read position in the next slice.
            if (effectiveSpeed >= 0.0)
                lookaheadNextSliceReadPos = getSliceStartPosition(nextSlice, fileLengthSamples);
            else
                lookaheadNextSliceReadPos = getSliceEndPosition(nextSlice, fileLengthSamples) - 1.0;

#if JUCE_DEBUG
            DBG("[AudioBuffer " + juce::String(bufferIndex) + "] §12 LOOKAHEAD START: "
                "nextSlice=" + juce::String(nextSlice) +
                " dist=" + juce::String(distToBoundary, 0) +
                " xfadeLen=" + juce::String(lookaheadCrossfadeSamples));
#endif
        };

        int framesFilled = 0;
        for (int sample = 0; sample < framesToGenerate; ++sample)
        {
            // Keep slice behavior responsive using local snapshot.
            if (slicingOn || localSliceTrig)
                handleSlicePlaybackLocal();

            // §12  Check whether to start a lookahead pre-crossfade.
            if (lookaheadEnabled)
                handleLookaheadLocal();

            // If no custom loop window is active, respect file boundaries + looping.
            if (! loopWindowOn && ! slicingOn)
            {
                if (step > 0.0)
                {
                    if (currentPos >= fileLengthSamples - 1)
                    {
                        if (params.isLooping)
                            startBoundaryCrossfadeLocal (0.0);
                        else
                        {
                            params.isPlaying = false;
                            break;
                        }
                    }
                }
                else if (step < 0.0)
                {
                    if (currentPos <= 0.0)
                    {
                        if (params.isLooping)
                            startBoundaryCrossfadeLocal ((double) (fileLengthSamples - 1));
                        else
                        {
                            params.isPlaying = false;
                            break;
                        }
                    }
                }
            }

            // Enforce loop window boundaries.
            if (loopWindowOn)
            {
                if (step >= 0.0)
                {
                    if (currentPos >= (double) loopEnd)
                        startBoundaryCrossfadeLocal ((double) loopStart);
                    else if (currentPos < (double) loopStart)
                        currentPos = (double) loopStart;
                }
                else
                {
                    if (currentPos < (double) loopStart)
                        startBoundaryCrossfadeLocal ((double) loopEnd);
                    else if (currentPos > (double) loopEnd)
                        currentPos = (double) loopEnd;
                }
            }
            
            // Ping pong mode using locals (no per-sample atomic traffic)
            if (ppEnabled && ppPeriod > 0.0)
            {
                ppPhase += std::abs(step);
                
                if (ppPhase >= ppPeriod)
                {
                    ppPhase = std::fmod(ppPhase, ppPeriod);
                    
                    if (ppGoingForward)
                    {
                        ppGoingForward = false;
                        params.speed = -std::abs(params.speed);
                        atomicSpeed.store(params.speed, std::memory_order_relaxed);
                    }
                    else
                    {
                        ppGoingForward = true;
                        params.speed = std::abs(params.speed);
                        atomicSpeed.store(params.speed, std::memory_order_relaxed);
                    }
                }
            }

            currentPos = juce::jlimit (0.0, static_cast<double> (fileLengthSamples - 1), currentPos);
            const int pos1 = (int) currentPos;
            const int pos2 = juce::jmin (pos1 + 1, fileLengthSamples - 1);
            const float frac = (float) (currentPos - (double) pos1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float s1 = sourceRead[ch][pos1];
                const float s2 = sourceRead[ch][pos2];
                scratchWrite[ch][sample] = s1 + frac * (s2 - s1);
            }

            if (isInCrossfade)
            {
                // §10.3 revised: Apply input-side crossfade so SoundTouch
                // receives a smooth transition instead of a hard discontinuity.
                // The old §10.3 deferred to output-side crossfade, but that
                // can't undo artifacts SoundTouch bakes in from processing
                // a discontinuous input.  Feeding crossfaded (smooth) audio
                // lets SoundTouch's overlap-add algorithm work normally.
                // BUG 2 fix: progress is computed against the length captured at
                // crossfade start (activeCrossfadeLen), immune to member changes.
                const int fadeLenTotal = juce::jmax(1, activeCrossfadeLen);
                const double fadeProgress = static_cast<double>(crossfadePosition)
                                          / static_cast<double>(fadeLenTotal);

                float fadeIn, fadeOut;
                if (crossfadeIsLookaheadContinuation)
                {
                    // BUG 5 fix: the lookahead ramp already blended 0→50% of the
                    // new slice by the boundary.  Continue linearly from 50/50 to
                    // 100/0 so the mix is continuous through the boundary sample.
                    const float mix = 0.5f + 0.5f * static_cast<float>(fadeProgress); // 0.5→1.0
                    fadeIn  = mix;
                    fadeOut = 1.0f - mix;
                }
                else
                {
                    fadeIn  = static_cast<float>(std::sin(fadeProgress * juce::MathConstants<double>::halfPi));
                    fadeOut = static_cast<float>(std::cos(fadeProgress * juce::MathConstants<double>::halfPi));
                }

                // Read from the old (pre-jump) position, advancing it in step.
                const double prevPos = previousSlicePlayheadPos + crossfadePosition * step;
                const double clampedPrevPos = juce::jlimit(0.0, static_cast<double>(fileLengthSamples - 1), prevPos);
                const int prevP1 = static_cast<int>(clampedPrevPos);
                const int prevP2 = juce::jmin(prevP1 + 1, fileLengthSamples - 1);
                const float prevFrac = static_cast<float>(clampedPrevPos - static_cast<double>(prevP1));

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float prevSample = sourceRead[ch][prevP1]
                                           + prevFrac * (sourceRead[ch][prevP2] - sourceRead[ch][prevP1]);
                    scratchWrite[ch][sample] = scratchWrite[ch][sample] * fadeIn + prevSample * fadeOut;
                }

                crossfadePosition++;
                if (crossfadePosition >= fadeLenTotal)
                {
                    isInCrossfade = false;
                    crossfadePosition = 0;
                    previousSliceIndex = -1;
                    crossfadeIsLookaheadContinuation = false;
                }
            }

            // §12  Lookahead pre-crossfade: blend next-slice audio into SoundTouch's
            // input *before* the boundary so the OLA algorithm transitions smoothly.
            // The blend ramps from 0% to 50% of the next slice by the time the
            // boundary arrives — the post-jump crossfade handles the remaining 50%.
            if (isInLookaheadCrossfade)
            {
                // BUGFIX (BUG 5b): clamp progress at 1.0 and HOLD the 50% blend when
                // the ramp completes before the boundary arrives (happens whenever
                // |step| < 1).  Previously the blend snapped from 50% straight back
                // to 0% on completion — itself an audible click — and the boundary
                // jump then restarted a full crossfade from 0%.
                const double progress = juce::jmin(1.0,
                    static_cast<double>(lookaheadCrossfadePosition)
                                      / static_cast<double>(lookaheadCrossfadeSamples));
                // Ramp from 0 → 0.5 using a smooth (sinusoidal) curve
                const float blendFactor = static_cast<float>(
                    0.5 * std::sin(progress * juce::MathConstants<double>::halfPi));

                // Read from the next slice's position
                const double nextPos = juce::jlimit(0.0, static_cast<double>(fileLengthSamples - 1),
                                                     lookaheadNextSliceReadPos);
                const int nextP1 = static_cast<int>(nextPos);
                const int nextP2 = juce::jmin(nextP1 + 1, fileLengthSamples - 1);
                const float nextFrac = static_cast<float>(nextPos - static_cast<double>(nextP1));

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float nextSample = sourceRead[ch][nextP1]
                                           + nextFrac * (sourceRead[ch][nextP2] - sourceRead[ch][nextP1]);
                    scratchWrite[ch][sample] = scratchWrite[ch][sample] * (1.0f - blendFactor)
                                             + nextSample * blendFactor;
                }

                lookaheadNextSliceReadPos += step;
                lookaheadCrossfadePosition++;

                // BUGFIX (BUG 5b): when the ramp completes before the boundary,
                // HOLD the 50% blend (isInLookaheadCrossfade stays true; progress
                // is clamped above) instead of ending it — ending snapped the
                // blend from 50% back to 0%, an audible click.  The slice-jump
                // handler consumes/clears the state at the actual boundary.
#if JUCE_DEBUG
                if (lookaheadCrossfadePosition == lookaheadCrossfadeSamples
                    && tearingDebugEnabled.load())
                    tearingStats.lookaheadPreCrossfades.fetch_add(1, std::memory_order_relaxed);
#endif
            }

            currentPos += step;
            ++framesFilled;
        }

        // §5.2  Write back all snapshotted state once after the loop.
        playheadPosition.store (currentPos);
        currentActiveSlice.store (localActiveSlice);
        sliceTriggered.store (localSliceTrig);
        if (ppEnabled)
        {
            pingPongPhasePosition.store (ppPhase);
            pingPongGoingForward.store (ppGoingForward);
        }
        return framesFilled;
    };

    // On the very first block after a reset, schedule a fade-in to mask any
    // startup transient from SoundTouch's settling overlap-add algorithm.
    // For extreme rate/tempo combos (e.g. rate=0.25 + tempo=2.0), the TDStretch
    // takes longer to find good splice matches, so scale the fade-in accordingly.
    if (! stretcherPrimed)
    {
        stretcherPrimed = true;
        const double settleMs = juce::jlimit (20.0, 40.0,
                                              20.0 * juce::jmax (1.0, clampedRatio)
                                                   * juce::jmax (1.0, 1.0 / juce::jmax (0.25, clampedRate)));
        stretchFadeInRemaining = juce::jmax (1, (int) (hostSampleRate * settleMs / 1000.0));

        // T2 + §10.2: Pre-prime SoundTouch with enough input to fill its internal
        // latency pipeline.  Without this, the first 1–2 output blocks after a
        // reset are partially empty (underruns), which sounds like crackles or
        // brief silence.
        //
        // getLatencySamples() sums TDStretch + RateTransposer latencies, but
        // the units are mixed when rate != 1.0:
        //   - TDStretch reports in its INPUT samples (= RateTransposer output)
        //   - RateTransposer reports in raw input samples
        // When rate < 1.0: RateTransposer EXPANDS input (e.g. rate=0.25 → 4x),
        //   so TDStretch's reported latency in RateTransposer-output-samples
        //   corresponds to far fewer raw input samples.  rawLatency overestimates.
        // When rate > 1.0: RateTransposer COMPRESSES input, so TDStretch needs
        //   more raw input.  rawLatency underestimates.
        //
        // §10.2: Over-prime by 2× the estimated amount.  The extra CPU cost is
        // one-time (per reset) and eliminates the audible crackle that occurred
        // when the previous 1.5× scaling left SoundTouch's overlap-add algorithm
        // without enough context to find good splice matches at startup.
        const int rawLatency = juce::jmax (0, stretcher.getLatencySamples());
        const double primeScale = juce::jmax (totalTempoRatioForIO * 2.0, 2.0);
        const int primeSamples = juce::jmax (rawLatency,
                                             (int) std::ceil (rawLatency * primeScale));
        if (primeSamples > 0)
        {
            // Ensure scratch is large enough for the priming chunk.
            // Pre-allocated in prepare(); guard kept as safety net.
            if (stretchInScratch.getNumChannels() != numChannels
                || stretchInScratch.getNumSamples() < primeSamples)
            {
                jassertfalse; // scratch buffer should have been pre-allocated in prepare()
                stretchInScratch.setSize (numChannels, primeSamples, false, false, true);
            }

            const int primed = fillInputScratch (primeSamples);
            if (primed > 0)
            {
                // §6.2  Use stack-allocated array instead of std::vector to avoid
                // heap allocation on the audio thread (stereo is the max channel count).
                std::array<const float*, 2> primePtrs {};
                for (int ch = 0; ch < numChannels; ++ch)
                    primePtrs[(size_t) ch] = stretchInScratch.getReadPointer (ch);

                // Feed to SoundTouch but don't try to drain output yet — this just
                // fills the internal pipeline so the drain loop below won't starve.
                stretcher.processNonInterleaved (primePtrs.data(), primed,
                                                 nullptr, 0, false,
                                                 stretchInterleavedIn, stretchInterleavedOut);
            }
        }
    }

    int framesWritten = 0;
    int safetyIters = 0;
    // §6.2  Stack-allocated arrays instead of std::vector — avoids heap
    // allocation on every audio block (stereo is the max channel count).
    std::array<float*, 2> outPtrs {};
    std::array<const float*, 2> inPtrs {};

    // Cap iterations: with proportional feed sizes we should fill the block in 2-4 iterations
    // in steady state.  During initial warmup or at high effective ratios (e.g. speed=2 + oct+)
    // more iterations may be needed to fill SoundTouch's internal pipeline.
    while (framesWritten < numOutputSamples && safetyIters++ < 24)
    {
        const int remaining = numOutputSamples - framesWritten;
        for (int ch = 0; ch < numChannels; ++ch)
            outPtrs[ch] = outputBuffer.getWritePointer (ch) + framesWritten;

        // 1) Drain any already-buffered output without consuming input.
        //    Skip the drain call entirely if SoundTouch has nothing ready (avoids interleave overhead).
        if (stretcher.numSamplesAvailable() > 0)
        {
            const int drained = stretcher.processNonInterleaved (nullptr, 0, outPtrs.data(), remaining, false,
                                                                stretchInterleavedIn, stretchInterleavedOut);
            if (drained > 0)
            {
                framesWritten += drained;
                continue;
            }
        }

        // 2) Feed more input and try again.
        //    Feed just enough to produce `remaining` output frames, plus a small margin.
        //    Use totalTempoRatioForIO (not just the stretch ratio) because when pitch
        //    shifting is active, the speed magnitude is routed through SoundTouch's
        //    tempo control, so it consumes more input per output frame.
        const int inputChunkFrames = juce::jlimit (1, maxInputChunkFrames,
                                                    (int) std::ceil (remaining * totalTempoRatioForIO) + marginFrames);

        const int framesFilled = fillInputScratch (inputChunkFrames);
        if (framesFilled <= 0)
        {
#if JUCE_DEBUG
            DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] fillInputScratch returned 0, breaking. remaining=" 
                 + juce::String(remaining) + " inputChunkFrames=" + juce::String(inputChunkFrames));
#endif
            break;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            inPtrs[ch] = stretchInScratch.getReadPointer (ch);

        const int produced = stretcher.processNonInterleaved (inPtrs.data(), framesFilled, outPtrs.data(), remaining, false,
                                                             stretchInterleavedIn, stretchInterleavedOut);
#if JUCE_DEBUG
        if (produced == 0 && framesFilled > 0)
        {
            DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] SoundTouch produced 0 from " + juce::String(framesFilled) 
                 + " input frames. avail=" + juce::String(stretcher.numSamplesAvailable()));
        }
#endif
        if (produced > 0)
            framesWritten += produced;
    }

    // §10.3  Output-side crossfade (legacy safety net).
    // With input-side crossfading now active, this path is rarely triggered.
    // Kept as a fallback in case other transition types flag it in the future.
    if (stretchOutputCrossfadePending)
    {
        snapshotStretchOutputRing();

        stretchOutputCrossfadeLen = crossfadeLengthSamples;
        stretchOutputCrossfadePos = 0;
        stretchOutputCrossfadeActive = (stretchCrossfadeSnapshotLen > 0);
        stretchOutputCrossfadePending = false;
    }

    // Apply any active output-side crossfade to the current block.
    if (stretchOutputCrossfadeActive && framesWritten > 0)
        applyStretchOutputCrossfade(outputBuffer, framesWritten);

    // NOTE (BUG 7): the output ring is now written once at the end of
    // processBlock (after the unified transition fade), in both playback modes.

    // Apply fade-in if this is the first block after priming.
    //
    // When a crossfade will run (mode transition or param-change reset), the
    // crossfade blends the clean previous-block audio over SoundTouch's startup
    // transient, so we do NOT need a fade-from-silence here — that would fight
    // the crossfade and cause a volume dip that can sound like a click.
    //
    // When NO crossfade will run (e.g. playback just started, or previous block
    // was invalid), the full settle-time fade-in is needed to ramp up from
    // silence and mask SoundTouch's startup artifacts.
    const bool modeTransitionCrossfadeWillRun = (lastBlockUsedStretch == false) && previousBlockValid;
    const bool paramChangeCrossfadeWillRun = didReset && previousBlockValid;
    if (modeTransitionCrossfadeWillRun || paramChangeCrossfadeWillRun)
    {
        // Skip the fade-in entirely; the crossfade handles the transition.
        stretchFadeInRemaining = 0;
    }
    // Apply fade-in (either the full settle-time fade or the shortened brief
    // fade set above). This ramps SoundTouch's first output from silence,
    // suppressing any startup transient before the crossfade blends it in.
    if (stretchFadeInRemaining > 0 && framesWritten > 0)
    {
        const int fadeLen = juce::jmin (stretchFadeInRemaining, framesWritten);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = outputBuffer.getWritePointer (ch);
            for (int i = 0; i < fadeLen; ++i)
            {
                const float g = (float) (i + 1) / (float) fadeLen;
                out[i] *= g;
            }
        }
        stretchFadeInRemaining -= fadeLen;
    }

    // T8 (revised for BUG 7): When a SoundTouch reset occurred this block
    // (direction flip, param change while already in stretch mode), signal
    // processBlock to run the unified multi-block transition fade.  The fade
    // itself now lives in processBlock so it can span multiple host blocks
    // (the old single-block blend couldn't fit 20–80ms at small buffer sizes)
    // and can't double-fire with the mode-transition crossfade.
    if (didReset && lastBlockUsedStretch)
        stretcherResetThisBlock = true;

    // Clamp output to +/-1.0 to prevent SoundTouch's overlap-add from producing
    // peaks that cause DAW metering to clip and stay in the red.
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::clip (outputBuffer.getWritePointer (ch),
                                           outputBuffer.getReadPointer (ch),
                                           -1.0f, 1.0f, framesWritten);

    // Restore normal crossfade length (was increased for SoundTouch mode above).
    crossfadeLengthSamples = savedCrossfadeLengthSamples;

    // Warn in debug if we repeatedly failed to fill the block.
#if JUCE_DEBUG
    jassertquiet (framesWritten == numOutputSamples || safetyIters >= 24);
#endif

    // If we still underfilled, fade the tail to zero instead of holding the last
    // sample value.  The old constant-fill approach created DC offset that caused
    // DAW metering to stay in the red and crackles at block boundaries.
    if (framesWritten < numOutputSamples)
    {
        const int tail = numOutputSamples - framesWritten;
        const int fadeOut = juce::jmin (tail, juce::jmax (1, (int) (hostSampleRate * 0.002))); // 2ms

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = outputBuffer.getWritePointer (ch);
            const float last = framesWritten > 0 ? out[framesWritten - 1] : 0.0f;

            // Short fade from last valid sample to zero.
            for (int i = 0; i < fadeOut; ++i)
                out[framesWritten + i] = last * (1.0f - (float) (i + 1) / (float) (fadeOut + 1));

            // Zero the remainder of the block.
            if (framesWritten + fadeOut < numOutputSamples)
                juce::FloatVectorOperations::clear (out + framesWritten + fadeOut,
                                                    numOutputSamples - framesWritten - fadeOut);
        }

        timeStretchUnderfills.fetch_add (1, std::memory_order_relaxed);
#if JUCE_DEBUG
        // T10: Log underruns.
        DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] SoundTouch UNDERRUN: wrote "
             + juce::String(framesWritten) + "/" + juce::String(numOutputSamples)
             + " iters=" + juce::String(safetyIters)
             + " stretch=" + juce::String(snap.stretchRatio) + " speed=" + juce::String(snap.speed)
             + " avail=" + juce::String(stretcher.numSamplesAvailable())
             + " latency=" + juce::String(stretcher.getLatencySamples()));
        if (tearingDebugEnabled.load())
            tearingStats.partialUnderfills.fetch_add(1, std::memory_order_relaxed);
#endif
    }
}

//==============================================================================
// Slicing Functionality
//==============================================================================

void AudioBuffer::setNumSlices(int numSlices)
{
    params.numSlices = juce::jmax(1, numSlices);
}

void AudioBuffer::triggerSlice(int sliceIndex)
{
    if (sliceIndex >= 0 && sliceIndex < params.numSlices)
    {
        targetSlice.store(sliceIndex);
        currentActiveSlice.store(sliceIndex);
        sliceTriggered.store(true);
        slicingModeActive.store(true);
    }
}

void AudioBuffer::triggerRandomSlice()
{
    if (params.numSlices > 1)
    {
        int randomSlice = random.nextInt(params.numSlices);
        triggerSlice(randomSlice);
        params.continuousRandomSlicing = true;
        // §12: Pre-compute the next random slice for lookahead.
        precomputedNextRandomSlice = random.nextInt(params.numSlices);
    }
}

void AudioBuffer::startContinuousRandomSlicing()
{
    if (params.numSlices > 1)
    {
        // Cancel arp slicing if active — the two modes are mutually exclusive
        params.arpSliceActive = false;
        params.arpSliceRepeaterMode = false;
        params.arpSequence.clear();

        triggerRandomSlice();
        params.continuousRandomSlicing = true;
        // §12: triggerRandomSlice already pre-computes, but ensure it's set.
        if (precomputedNextRandomSlice < 0)
            precomputedNextRandomSlice = random.nextInt(params.numSlices);
    }
}

void AudioBuffer::stopContinuousRandomSlicing()
{
    params.continuousRandomSlicing = false;
}

void AudioBuffer::startArpSlicing(int totalSlices, int sequenceLength, int repeatBars)
{
    // Set up the slice grid
    int slices = juce::jlimit(4, 64, totalSlices);
    setNumSlices(slices);

    // Build a random sequence of `sequenceLength` slice indices
    sequenceLength = juce::jlimit(1, 8, sequenceLength);
    repeatBars = juce::jmax(1, repeatBars);

    params.arpSequence.clear();
    for (int i = 0; i < sequenceLength; ++i)
        params.arpSequence.push_back(random.nextInt(slices));

    params.arpSequencePos = 0;
    params.arpRepeatBars = repeatBars;
    params.arpCycleCount = 0;
    // How many full sequence cycles fit in repeatBars worth of slices?
    // Each slice plays for (bufferDuration / numSlices) seconds.
    // One full sequence cycle = sequenceLength slices.
    // Total slices in repeatBars bars = repeatBars * numSlices (whole buffer = 1 bar approx).
    // Actually, let's just count: the sequence repeats, and after repeatBars full cycles
    // through the sequence, we pick a new sequence.
    params.arpTotalCyclesPerRefresh = juce::jmax(1, repeatBars);
    params.arpSliceActive = true;
    params.arpSliceRepeaterMode = false;   // normal arp mode
    params.continuousRandomSlicing = false; // disable random mode

    // Trigger the first slice in the sequence
    if (!params.arpSequence.empty())
        triggerSlice(params.arpSequence[0]);

    slicingModeActive.store(true);
}

void AudioBuffer::stopArpSlicing()
{
    params.arpSliceActive = false;
    params.arpSequence.clear();
    params.arpSequencePos = 0;
    params.arpCycleCount = 0;
}

void AudioBuffer::startSliceRepeater(int totalSlices, int repetitions)
{
    // Set up the slice grid
    int slices = juce::jlimit(4, 64, totalSlices);
    setNumSlices(slices);

    repetitions = juce::jlimit(4, 32, repetitions);

    // Build a sequence that repeats one random slice 'repetitions' times.
    // When the sequence ends, arp refresh logic picks a new random sequence
    // of the same length — effectively repeating a new random slice.
    int chosenSlice = random.nextInt(slices);
    params.arpSequence.clear();
    for (int i = 0; i < repetitions; ++i)
        params.arpSequence.push_back(chosenSlice);

    params.arpSequencePos = 0;
    params.arpRepeatBars = 1;           // refresh after every full pass through the sequence
    params.arpCycleCount = 0;
    params.arpTotalCyclesPerRefresh = 1; // pick a new slice after one full cycle
    params.arpSliceActive = true;
    params.arpSliceRepeaterMode = true;    // repeater mode: single-slice refresh
    params.continuousRandomSlicing = false;

    // Trigger the first slice
    triggerSlice(chosenSlice);
    slicingModeActive.store(true);
}

void AudioBuffer::exitSlicingMode()
{
    slicingModeActive.store(false);
    params.continuousRandomSlicing = false;
    params.arpSliceActive = false;
    params.arpSliceRepeaterMode = false;
    params.arpSequence.clear();
    params.arpSequencePos = 0;
    params.arpCycleCount = 0;
    sliceTriggered.store(false);
    currentActiveSlice.store(0);
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    activeCrossfadeLen = crossfadeLengthSamples;
    crossfadeIsLookaheadContinuation = false;
    previousSliceIndex = -1;

    // §12: Reset lookahead state.
    resetLookaheadState();
}

int AudioBuffer::getCurrentSlice() const
{
    auto data = getAudioDataSnapshot();
    const int fileLengthSamples = (data != nullptr ? data->buffer.getNumSamples() : 0);

    if (params.numSlices <= 1 || fileLengthSamples <= 0)
        return 0;
    
    // If we're in slicing mode, return the active slice rather than calculating from position
    if (slicingModeActive.load())
    {
        return currentActiveSlice.load();
    }
    
    // Otherwise, calculate from playhead position within the active range
    // Active range = loop window if enabled, else full file
    const bool windowOn = loopWindowEnabled.load();
    const int64_t start = windowOn ? loopStartSamples.load() : 0;
    const int64_t end   = windowOn ? loopEndSamples.load()   : (int64_t) fileLengthSamples;
    const int64_t length = juce::jmax<int64_t>(1, end - start);
    double currentPos = playheadPosition.load();
    double rel = juce::jlimit(0.0, (double)(length - 1), currentPos - (double) start);
    double sliceSize = static_cast<double>(length) / params.numSlices;
    return juce::jlimit(0, params.numSlices - 1, static_cast<int>(rel / sliceSize));
}

//==============================================================================
// Timing and Position
//==============================================================================

double AudioBuffer::getPlayheadPositionInSeconds() const
{
    auto data = getAudioDataSnapshot();
    const double fileSampleRate = (data != nullptr ? data->sampleRate : 0.0);

    if (fileSampleRate <= 0.0)
        return 0.0;
    return playheadPosition.load() / fileSampleRate;
}

double AudioBuffer::getDurationInSeconds() const
{
    auto data = getAudioDataSnapshot();
    const double fileSampleRate = (data != nullptr ? data->sampleRate : 0.0);
    const int fileLengthSamples = (data != nullptr ? data->buffer.getNumSamples() : 0);

    if (fileSampleRate <= 0.0)
        return 0.0;
    return static_cast<double>(fileLengthSamples) / fileSampleRate;
}

int AudioBuffer::getDurationInSamples() const
{
    auto data = getAudioDataSnapshot();
    return data != nullptr ? data->buffer.getNumSamples() : 0;
}

double AudioBuffer::getFileSampleRate() const
{
    auto data = getAudioDataSnapshot();
    return data != nullptr ? data->sampleRate : 0.0;
}

juce::String AudioBuffer::getLoadedFileName() const
{
    auto data = getAudioDataSnapshot();
    return data != nullptr ? data->fileName : juce::String();
}

//==============================================================================
// Parameters
//==============================================================================

void AudioBuffer::setParams(const AudioBufferParams& newParams)
{
    params = newParams;
    atomicSpeed.store(params.speed, std::memory_order_relaxed);
    
    // Update crossfade length if it changed
    if (hostSampleRate > 0.0)
    {
        crossfadeLengthSamples = static_cast<int>(params.crossfadeLengthMs * hostSampleRate / 1000.0);
    }
}

//==============================================================================
// Listener Management
//==============================================================================

void AudioBuffer::addListener(AudioBufferListener* listener)
{
    if (listener != nullptr)
        listeners.addIfNotAlreadyThere(listener);
}

void AudioBuffer::removeListener(AudioBufferListener* listener)
{
    listeners.removeFirstMatchingValue(listener);
}

//==============================================================================
// Internal Processing Methods
//==============================================================================

double AudioBuffer::getSliceStartPosition(int sliceIndex, int fileLengthSamples) const
{
    if (sliceIndex < 0 || sliceIndex >= params.numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    // Use active range: loop window if enabled, else the full file
    const bool windowOn = loopWindowEnabled.load();
    const int64_t start = windowOn ? loopStartSamples.load() : 0;
    const int64_t end   = windowOn ? loopEndSamples.load()   : (int64_t) fileLengthSamples;
    const int64_t length = juce::jmax<int64_t>(1, end - start);
    double sliceSize = static_cast<double>(length) / params.numSlices;
    int64_t offsetWithin = (int64_t) juce::jlimit(0.0, (double)(length - 1), sliceIndex * sliceSize);
    int64_t targetSample = start + offsetWithin;
    
    return static_cast<double>(juce::jlimit<int64_t>(0, (int64_t)fileLengthSamples - 1, targetSample));
}

double AudioBuffer::getSliceEndPosition(int sliceIndex, int fileLengthSamples) const
{
    if (sliceIndex < 0 || sliceIndex >= params.numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    // Use active range: loop window if enabled, else the full file
    const bool windowOn = loopWindowEnabled.load();
    const int64_t start = windowOn ? loopStartSamples.load() : 0;
    const int64_t end   = windowOn ? loopEndSamples.load()   : (int64_t) fileLengthSamples;
    const int64_t length = juce::jmax<int64_t>(1, end - start);
    double sliceSize = static_cast<double>(length) / params.numSlices;
    int64_t offsetWithin = (int64_t) juce::jlimit(0.0, (double)length, (sliceIndex + 1) * sliceSize);
    int64_t targetSample = start + offsetWithin;
    
    return static_cast<double>(juce::jlimit<int64_t>(0, (int64_t)fileLengthSamples, targetSample));
}

//==============================================================================
// §12 Lookahead pre-priming helpers
//==============================================================================

int AudioBuffer::getNextSliceIndex(int currentSlice, int fileLengthSamples) const
{
    (void) fileLengthSamples; // may be used in future for validation

    // Arp / slice repeater mode — sequence is pre-built and deterministic
    if (params.arpSliceActive && !params.arpSequence.empty())
    {
        const int nextSeqPos = params.arpSequencePos + 1;

        if (nextSeqPos >= (int) params.arpSequence.size())
        {
            // We're about to wrap around the sequence.
            // If also at refresh boundary, the sequence will be regenerated — unpredictable.
            if ((params.arpCycleCount + 1) >= params.arpTotalCyclesPerRefresh)
                return -1; // new sequence will be randomised, can't predict

            // Otherwise we just wrap to the start of the same sequence.
            return params.arpSequence[0];
        }

        return params.arpSequence[(size_t) nextSeqPos];
    }

    // Continuous random slicing — use pre-computed next slice if available
    if (params.continuousRandomSlicing)
    {
        return precomputedNextRandomSlice; // may be -1 if not yet pre-computed
    }

    // External trigger or single-slice mode — can't predict
    return -1;
}

double AudioBuffer::getDistanceToSliceBoundary(double currentPos, int currentSlice,
                                                int fileLengthSamples, double effectiveSpeed) const
{
    if (fileLengthSamples <= 0 || params.numSlices <= 1)
        return 1.0e12; // effectively infinite — no boundary

    const double sliceStart = getSliceStartPosition(currentSlice, fileLengthSamples);
    const double sliceEnd   = getSliceEndPosition(currentSlice, fileLengthSamples);

    if (effectiveSpeed >= 0.0)
    {
        // Forward: boundary is at sliceEnd - 1.0
        return (sliceEnd - 1.0) - currentPos;
    }
    else
    {
        // Reverse: boundary is at sliceStart
        return currentPos - sliceStart;
    }
}

void AudioBuffer::resetLookaheadState()
{
    isInLookaheadCrossfade = false;
    lookaheadCrossfadePosition = 0;
    lookaheadCrossfadeSamples = 0;
    lookaheadNextSlice = -1;
    lookaheadNextSliceReadPos = 0.0;
    precomputedNextRandomSlice = -1;
}

void AudioBuffer::startSliceCrossfade(int newSliceIndex, double newPlayheadPos)
{
    // Store previous slice information for crossfading
    previousSliceIndex = currentActiveSlice.load();
    previousSlicePlayheadPos = playheadPosition.load();
    
    // Start the crossfade
    isInCrossfade = true;
    crossfadePosition = 0;
    activeCrossfadeLen = crossfadeLengthSamples;
    crossfadeIsLookaheadContinuation = false;
    
    // Update to new slice
    currentActiveSlice.store(newSliceIndex);
    playheadPosition.store(newPlayheadPos);
    
#if JUCE_DEBUG
    // Track slice jumps for tearing debug
    if (tearingDebugEnabled.load())
    {
        tearingStats.sliceJumps.fetch_add(1, std::memory_order_relaxed);
        DBG("[AudioBuffer " + juce::String(bufferIndex) + "] SLICE JUMP: from slice " 
            + juce::String(previousSliceIndex) + " to " + juce::String(newSliceIndex)
            + " pos=" + juce::String(previousSlicePlayheadPos, 0) + "->" + juce::String(newPlayheadPos, 0));
    }
#endif
}

void AudioBuffer::applyCrossfadeToSliceTransition(const juce::AudioBuffer<float>& sourceBuffer,
                                                  int fileLengthSamples,
                                                  double fileSampleRate,
                                                  juce::AudioBuffer<float>& outputBuffer,
                                                  int startSample,
                                                  int numSamples)
{
    if (!isInCrossfade)
        return;
        
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), sourceBuffer.getNumChannels());
    // BUGFIX (BUG 2): use the length captured when THIS crossfade started, so a
    // change to crossfadeLengthSamples between blocks can't warp the fade curve.
    const int fadeLenTotal = juce::jmax(1, activeCrossfadeLen);
    const int remainingCrossfadeSamples = fadeLenTotal - crossfadePosition;
    const int samplesToProcess = juce::jmin(numSamples, remainingCrossfadeSamples);
    
    if (samplesToProcess <= 0)
    {
        isInCrossfade = false;
        return;
    }
    
    // Process crossfade sample by sample.
    // T7: In stretch mode the source-read advancement is direction * tempoMult,
    // regardless of whether speed magnitude is routed through SoundTouch's
    // rate or tempo knob (SoundTouch handles that internally). Using
    // getEffectiveSpeed() here would be wrong because it includes the speed
    // magnitude which SoundTouch already accounts for.
    const double stretch = stretchRatio.load();
    const double pitchST = pitchSemiTones.load();
    const bool stretcherActive = (std::abs(stretch - 1.0) > 1.0e-6) || (std::abs(pitchST) > 1.0e-6);
    const double effectiveSpeed = stretcherActive
        ? ((atomicSpeed.load(std::memory_order_relaxed) < 0.0 ? -1.0 : 1.0) * tempoMultiplier.load())
        : getEffectiveSpeed();
    for (int sample = 0; sample < samplesToProcess; ++sample)
    {
        const double fadeProgress = static_cast<double>(crossfadePosition + sample) / static_cast<double>(fadeLenTotal);
        
        // Use equal-power crossfade curve
        const float fadeOut = static_cast<float>(std::cos(fadeProgress * juce::MathConstants<double>::halfPi));
        const float fadeIn = static_cast<float>(std::sin(fadeProgress * juce::MathConstants<double>::halfPi));
        
        // Get sample from previous slice position with proper speed compensation.
        // BUGFIX: advance from the TOTAL crossfade progress (crossfadePosition + sample),
        // not the chunk-local sample index.  Using the chunk-local index caused the
        // fade-out audio to restart from the original jump position on every chunk /
        // block, producing an audible stutter when a crossfade spanned chunk boundaries.
        // Clamp instead of bounds-skip: at a file-boundary wrap the old position
        // runs past the file edge; holding the edge sample keeps the fade-out
        // smooth rather than silently dropping the old-signal contribution
        // (which produced a step in the blend).  Matches the stretch path.
        const double prevPosRaw = previousSlicePlayheadPos
                                + (crossfadePosition + sample) * effectiveSpeed * (fileSampleRate / hostSampleRate);
        const double prevPos = juce::jlimit(0.0, static_cast<double>(fileLengthSamples - 1), prevPosRaw);
        const int prevSampleIndex = static_cast<int>(prevPos);
        const int prevSampleIndex2 = juce::jmin(prevSampleIndex + 1, fileLengthSamples - 1);
        const double prevFraction = prevPos - prevSampleIndex;
        
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                // Linear interpolation for previous slice
                const float* readPtr = sourceBuffer.getReadPointer(channel);
                const float prevSample1 = readPtr[prevSampleIndex];
                const float prevSample2 = readPtr[prevSampleIndex2];
                const float prevInterpolatedSample = prevSample1 + static_cast<float>(prevFraction) * (prevSample2 - prevSample1);
                
                // Apply equal-power crossfade
                float* writePtr = outputBuffer.getWritePointer(channel);
                const int outputIndex = startSample + sample;
                writePtr[outputIndex] = writePtr[outputIndex] * fadeIn + prevInterpolatedSample * fadeOut;
            }
        }
    }
    
    crossfadePosition += samplesToProcess;
    
    // End crossfade if we've processed all samples
    if (crossfadePosition >= fadeLenTotal)
    {
        isInCrossfade = false;
        crossfadePosition = 0;
        previousSliceIndex = -1;
    }
}

void AudioBuffer::startBoundaryCrossfade(double newPlayheadPos)
{
    // Use previous position for crossfade; mark previousSliceIndex as -1 (boundary), but allow crossfade to run.
    previousSlicePlayheadPos = playheadPosition.load();
    previousSliceIndex = -1;
    isInCrossfade = true;
    crossfadePosition = 0;
    activeCrossfadeLen = crossfadeLengthSamples;
    crossfadeIsLookaheadContinuation = false;
    playheadPosition.store(newPlayheadPos);
}

//==============================================================================
// BUG 7 fix: multi-block transition crossfade helpers.
//==============================================================================

void AudioBuffer::beginTransitionFade(int desiredFadeSamples)
{
    // Snapshot the most recent pre-transition output from the ring buffer.
    // The ring is written at the END of every processBlock, so at transition-
    // detection time it contains only OLD (pre-transition) audio.
    const int available = stretchOutputRingValidSamples;
    const int len = juce::jmin(desiredFadeSamples, available,
                               transitionOldBuffer.getNumSamples());
    if (len <= 0 || stretchOutputRingSize <= 0)
    {
        transitionFadeActive = false;
        return;
    }

    const int numCh = juce::jmin(transitionOldBuffer.getNumChannels(),
                                 stretchOutputRing.getNumChannels());
    // Oldest of the captured span sits at (writePos - len + ringSize) % ringSize.
    const int readStart = (stretchOutputRingWritePos - len + stretchOutputRingSize) % stretchOutputRingSize;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* ringPtr = stretchOutputRing.getReadPointer(ch);
        float* dst = transitionOldBuffer.getWritePointer(ch);
        const int firstChunk = juce::jmin(len, stretchOutputRingSize - readStart);
        juce::FloatVectorOperations::copy(dst, ringPtr + readStart, firstChunk);
        if (firstChunk < len)
            juce::FloatVectorOperations::copy(dst + firstChunk, ringPtr, len - firstChunk);
    }

    transitionFadeLen = len;
    transitionFadePos = 0;
    transitionFadeActive = true;
}

void AudioBuffer::applyTransitionFade(juce::AudioBuffer<float>& buffer)
{
    if (!transitionFadeActive || transitionFadeLen <= 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int remaining = transitionFadeLen - transitionFadePos;
    const int n = juce::jmin(numSamples, remaining);
    const int numCh = juce::jmin(buffer.getNumChannels(),
                                 transitionOldBuffer.getNumChannels());

    for (int i = 0; i < n; ++i)
    {
        const double progress = static_cast<double>(transitionFadePos + i)
                              / static_cast<double>(transitionFadeLen);
        const float fadeIn  = static_cast<float>(std::sin(progress * juce::MathConstants<double>::halfPi));
        const float fadeOut = static_cast<float>(std::cos(progress * juce::MathConstants<double>::halfPi));

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer(ch);
            const float oldSample = transitionOldBuffer.getReadPointer(ch)[transitionFadePos + i];
            out[i] = out[i] * fadeIn + oldSample * fadeOut;
        }
    }

    transitionFadePos += n;
    if (transitionFadePos >= transitionFadeLen)
    {
        transitionFadeActive = false;
        transitionFadePos = 0;
        transitionFadeLen = 0;
    }
}

//==============================================================================
// §10.3  Output-side crossfade helpers for SoundTouch mode.
//==============================================================================

void AudioBuffer::writeToStretchOutputRing(const juce::AudioBuffer<float>& src, int numSamples)
{
    if (stretchOutputRingSize <= 0 || numSamples <= 0)
        return;

    const int numCh = juce::jmin(src.getNumChannels(), stretchOutputRing.getNumChannels());

    // If the write exceeds the ring capacity, only keep the last ringSize samples.
    const int writeStart = (numSamples > stretchOutputRingSize)
                           ? (numSamples - stretchOutputRingSize) : 0;
    const int samplesToWrite = numSamples - writeStart;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* srcPtr = src.getReadPointer(ch) + writeStart;
        float* ringPtr = stretchOutputRing.getWritePointer(ch);

        const int firstChunk = juce::jmin(samplesToWrite, stretchOutputRingSize - stretchOutputRingWritePos);
        juce::FloatVectorOperations::copy(ringPtr + stretchOutputRingWritePos, srcPtr, firstChunk);

        if (firstChunk < samplesToWrite)
            juce::FloatVectorOperations::copy(ringPtr, srcPtr + firstChunk, samplesToWrite - firstChunk);
    }

    stretchOutputRingWritePos = (stretchOutputRingWritePos + samplesToWrite) % stretchOutputRingSize;
    stretchOutputRingValidSamples = juce::jmin(stretchOutputRingValidSamples + samplesToWrite, stretchOutputRingSize);
}

void AudioBuffer::snapshotStretchOutputRing()
{
    // Copy the valid portion of the ring buffer into the flat snapshot buffer.
    // The snapshot represents the most recent stretchOutputRingValidSamples of
    // SoundTouch output, arranged oldest→newest so that index 0 is the oldest.
    const int len = stretchOutputRingValidSamples;
    if (len <= 0 || stretchOutputRingSize <= 0)
    {
        stretchCrossfadeSnapshotLen = 0;
        return;
    }

    const int numCh = stretchOutputRing.getNumChannels();
    // Read position: writePos points to the next write, so the oldest sample is
    // at (writePos - validSamples + ringSize) % ringSize.
    const int readStart = (stretchOutputRingWritePos - len + stretchOutputRingSize) % stretchOutputRingSize;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* ringPtr = stretchOutputRing.getReadPointer(ch);
        float* snapPtr = stretchCrossfadeSnapshot.getWritePointer(ch);

        const int firstChunk = juce::jmin(len, stretchOutputRingSize - readStart);
        juce::FloatVectorOperations::copy(snapPtr, ringPtr + readStart, firstChunk);

        if (firstChunk < len)
            juce::FloatVectorOperations::copy(snapPtr + firstChunk, ringPtr, len - firstChunk);
    }

    stretchCrossfadeSnapshotLen = len;
}

void AudioBuffer::applyStretchOutputCrossfade(juce::AudioBuffer<float>& outputBuffer, int numSamples)
{
    if (!stretchOutputCrossfadeActive || stretchOutputCrossfadeLen <= 0)
        return;

    const int numCh = juce::jmin(outputBuffer.getNumChannels(),
                                  stretchCrossfadeSnapshot.getNumChannels());

    // The snapshot is laid out oldest→newest.  We want the crossfade to use the
    // TAIL of the snapshot (the most recent old output) as the fade-out signal.
    // The snapshot was taken at transition time, so its tail is the last old
    // audio before the transition.
    //
    // snapReadStart = where in the snapshot to start reading for this crossfade
    // position.  As the crossfade progresses across blocks, we advance through
    // the tail of the snapshot.
    const int snapTailStart = juce::jmax(0, stretchCrossfadeSnapshotLen - stretchOutputCrossfadeLen);

    const int remaining = stretchOutputCrossfadeLen - stretchOutputCrossfadePos;
    const int samplesToProcess = juce::jmin(numSamples, remaining);

    for (int i = 0; i < samplesToProcess; ++i)
    {
        const int fadeIdx = stretchOutputCrossfadePos + i;
        const double fadeProgress = static_cast<double>(fadeIdx) / static_cast<double>(stretchOutputCrossfadeLen);

        // Equal-power crossfade curve (same as the input-side crossfade).
        const float fadeOut = static_cast<float>(std::cos(fadeProgress * juce::MathConstants<double>::halfPi));
        const float fadeIn  = static_cast<float>(std::sin(fadeProgress * juce::MathConstants<double>::halfPi));

        const int snapIdx = snapTailStart + fadeIdx;
        if (snapIdx >= 0 && snapIdx < stretchCrossfadeSnapshotLen)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float oldSample = stretchCrossfadeSnapshot.getReadPointer(ch)[snapIdx];
                float* out = outputBuffer.getWritePointer(ch);
                out[i] = out[i] * fadeIn + oldSample * fadeOut;
            }
        }
        // If snapshot doesn't cover this position, just let the new signal through
        // (fadeIn is approaching 1.0 anyway at this point).
    }

    stretchOutputCrossfadePos += samplesToProcess;
    if (stretchOutputCrossfadePos >= stretchOutputCrossfadeLen)
    {
        stretchOutputCrossfadeActive = false;
        stretchOutputCrossfadePos = 0;
        stretchOutputCrossfadeLen = 0;
    }
}

void AudioBuffer::handleSlicePlayback(double& currentPos, int fileLengthSamples)
{
    if (!slicingModeActive.load())
        return;
    
    // Check if we need to jump to a new slice
    if (sliceTriggered.load())
    {
        int slice = targetSlice.load();
        double speed = getEffectiveSpeed();
        
        // Calculate new position based on playback direction
        double newPos;
        if (speed >= 0.0)
        {
            newPos = getSliceStartPosition(slice, fileLengthSamples);
        }
        else
        {
            newPos = getSliceEndPosition(slice, fileLengthSamples) - 1.0;
        }
        
        // Start crossfade transition to new slice
        startSliceCrossfade(slice, newPos);
        currentPos = newPos;
        sliceTriggered.store(false);
        return;
    }
    
    // Handle continuous random mode
    if (params.continuousRandomSlicing)
    {
        int activeSlice = currentActiveSlice.load();
        double sliceStart = getSliceStartPosition(activeSlice, fileLengthSamples);
        double sliceEnd = getSliceEndPosition(activeSlice, fileLengthSamples);
        
        // Check if we've reached the end/beginning of the current slice
        bool shouldTriggerNext = false;
        double speed = getEffectiveSpeed();
        
        if (speed > 0.0) // Forward playback
        {
            shouldTriggerNext = (currentPos >= sliceEnd - 1.0);
        }
        else if (speed < 0.0) // Reverse playback
        {
            shouldTriggerNext = (currentPos <= sliceStart);
        }
        
        if (shouldTriggerNext)
        {
            // Trigger a new random slice with crossfading
            int nextSlice = random.nextInt(params.numSlices);
            
            // Calculate new position based on playback direction
            double newPos;
            if (speed > 0.0)
            {
                newPos = getSliceStartPosition(nextSlice, fileLengthSamples);
            }
            else
            {
                newPos = getSliceEndPosition(nextSlice, fileLengthSamples) - 1.0;
            }
            
            // Start crossfade transition to new random slice
            startSliceCrossfade(nextSlice, newPos);
            currentPos = newPos;
            return;
        }
    }
    else if (params.arpSliceActive && !params.arpSequence.empty())
    {
        int activeSlice = currentActiveSlice.load();
        double sliceStart = getSliceStartPosition(activeSlice, fileLengthSamples);
        double sliceEnd = getSliceEndPosition(activeSlice, fileLengthSamples);
        
        bool shouldTriggerNext = false;
        double speed = getEffectiveSpeed();
        
        if (speed > 0.0)
            shouldTriggerNext = (currentPos >= sliceEnd - 1.0);
        else if (speed < 0.0)
            shouldTriggerNext = (currentPos <= sliceStart);
        
        if (shouldTriggerNext)
        {
            params.arpSequencePos++;
            if (params.arpSequencePos >= (int)params.arpSequence.size())
            {
                params.arpSequencePos = 0;
                params.arpCycleCount++;
                if (params.arpCycleCount >= params.arpTotalCyclesPerRefresh)
                {
                    params.arpCycleCount = 0;
                    int seqLen = (int)params.arpSequence.size();
                    params.arpSequence.clear();
                    for (int si = 0; si < seqLen; ++si)
                        params.arpSequence.push_back(random.nextInt(params.numSlices));
                }
            }
            const int nextSlice = params.arpSequence[(size_t)params.arpSequencePos];
            
            double newPos;
            if (speed > 0.0)
                newPos = getSliceStartPosition(nextSlice, fileLengthSamples);
            else
                newPos = getSliceEndPosition(nextSlice, fileLengthSamples) - 1.0;
            
            startSliceCrossfade(nextSlice, newPos);
            currentPos = newPos;
            return;
        }
    }
    else
    {
        // Non-continuous slice mode - check if we've reached the end of the current slice
        int activeSlice = currentActiveSlice.load();
        double sliceStart = getSliceStartPosition(activeSlice, fileLengthSamples);
        double sliceEnd = getSliceEndPosition(activeSlice, fileLengthSamples);
        double speed = getEffectiveSpeed();
        
        if (speed > 0.0 && currentPos >= sliceEnd - 1.0)
        {
            if (params.isLooping)
            {
                // Loop back to the start of the current slice
                currentPos = sliceStart;
                playheadPosition.store(currentPos);
            }
            else
            {
                params.isPlaying = false;
            }
        }
        else if (speed < 0.0 && currentPos <= sliceStart)
        {
            if (params.isLooping)
            {
                // Loop back to the end of the current slice
                currentPos = sliceEnd - 1.0;
                playheadPosition.store(currentPos);
            }
            else
            {
                params.isPlaying = false;
            }
        }
    }
}

//==============================================================================
// Notification Helpers
//==============================================================================

void AudioBuffer::notifyPlaybackStateChanged()
{
    if (previousIsPlaying != params.isPlaying)
    {
        previousIsPlaying = params.isPlaying;
        
        for (auto* listener : listeners)
        {
            if (params.isPlaying)
                listener->audioBufferPlaybackStarted(bufferIndex);
            else
                listener->audioBufferPlaybackStopped(bufferIndex);
        }
    }
}

void AudioBuffer::notifySliceChanged()
{
    int currentSlice = getCurrentSlice();
    if (previousSlice != currentSlice)
    {
        previousSlice = currentSlice;
        
        for (auto* listener : listeners)
        {
            listener->audioBufferSliceChanged(bufferIndex, currentSlice);
        }
    }
}

void AudioBuffer::notifyPositionChanged()
{
    double currentPosition = getPlayheadPositionInSeconds();
    // Only notify if position changed by more than 0.01 seconds to avoid spam
    if (std::abs(currentPosition - previousPosition) > 0.01)
    {
        previousPosition = currentPosition;
        
        for (auto* listener : listeners)
        {
            listener->audioBufferPositionChanged(bufferIndex, currentPosition);
        }
    }
}

void AudioBuffer::releaseResources()
{
    // NOTE: Do NOT clear audioData here! The loaded audio should persist across
    // bus configuration changes and host re-preparation cycles. Only clear
    // processing buffers which will be re-allocated in prepare().
    repitchBuffer.setSize(0, 0);
    tempProcessingBuffer.setSize(0, 0);
    crossfadeBuffer.setSize(0, 0);
    previousBlockBuffer.setSize(0, 0);
    previousBlockValid = false;
    resetCrossfadePending = false;
}

//==============================================================================
// Ping pong playback mode
//==============================================================================
void AudioBuffer::setPingPongMode(bool enabled, double divisionBars, double bpm, double sampleRate)
{
    // When enabling ping pong, cancel reverse mode (absolute value of speed)
    if (enabled && params.speed < 0.0)
    {
        params.speed = std::abs(params.speed);
        atomicSpeed.store(params.speed, std::memory_order_relaxed);
    }
    
    pingPongEnabled.store(enabled);
    pingPongDivision.store(juce::jlimit(0.0625, 4.0, divisionBars)); // 1/16 to 4 bars
    
    if (enabled)
    {
        // Calculate period in samples for one direction
        // divisionBars is how long to go in one direction before reversing
        const double secondsPerBar = (60.0 / bpm) * 4.0; // 4 beats per bar
        const double periodSeconds = divisionBars * secondsPerBar;
        pingPongPeriodSamples.store(periodSeconds * sampleRate);
        
        // Start going forward at beginning of cycle
        pingPongGoingForward.store(true);
        pingPongPhasePosition.store(0.0);
        
        // Make sure speed is positive
        if (params.speed < 0.0)
        {
            params.speed = std::abs(params.speed);
            atomicSpeed.store(params.speed, std::memory_order_relaxed);
        }
    }
}
