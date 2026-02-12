/*
  ==============================================================================

    AudioBuffer.cpp
    
    Professional Audio Buffer Implementation

  ==============================================================================
*/

#include "AudioBuffer.h"
#include <vector>
#include <algorithm>
#include <cmath>

//==============================================================================
// Tearing Debug Helpers
//==============================================================================

namespace TearingDebug
{
    // Threshold for detecting discontinuities (sample-to-sample jump)
    constexpr float kDiscontinuityThreshold = 0.3f;  // 30% of full scale
    
    // Threshold for consecutive zeros to be considered a "zero run"
    constexpr int kZeroRunThreshold = 64;  // ~1.5ms at 44.1kHz
    
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
    
    // Check for discontinuity between two consecutive samples
    inline bool isDiscontinuity(float prev, float curr)
    {
        // Skip if either sample is invalid
        if (!isValidSample(prev) || !isValidSample(curr))
            return false;
        // Skip if both are near zero (silence)
        if (isZero(prev) && isZero(curr))
            return false;
        return std::abs(curr - prev) > kDiscontinuityThreshold;
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
}

void AudioBuffer::prepare(double sampleRate, int samplesPerBlockExpected)
{
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();
    hostSampleRate = sampleRate;
    speedSmoother.reset(sampleRate, 0.05); // 50ms smoothing time
    stretchSmoother.reset(sampleRate, 0.05);
    speedMagSmoother.reset(sampleRate, 0.05); // T4: 50ms smoothing for speed-mag through SoundTouch
    
    // Calculate crossfade length in samples
    crossfadeLengthSamples = static_cast<int>(params.crossfadeLengthMs * sampleRate / 1000.0);
    
    // Prepare buffers for processing
    repitchBuffer.setSize(2, samplesPerBlockExpected * 4); // Extra headroom for repitching
    tempProcessingBuffer.setSize(2, samplesPerBlockExpected * 4);
    crossfadeBuffer.setSize(2, crossfadeLengthSamples * 2); // Buffer for crossfading
}

void AudioBuffer::processBlock(juce::AudioBuffer<float>& outputBuffer)
{
    if (!hasAudioLoaded() || !params.isPlaying)
    {
        outputBuffer.clear();
        lastBlockUsedStretch = false;
        previousBlockValid = false;
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
        // Smooth speed changes to avoid artifacts
        speedSmoother.setTargetValue(snap.speed * snap.tempoMult);
        processWithRepitching(outputBuffer);
    }

    // When transitioning between repitch <-> stretch mode, crossfade the tail
    // of the previous block into the head of the current block to avoid a hard edge.
    const bool transitioned = (useStretcher != lastBlockUsedStretch);
#if JUCE_DEBUG
    if (transitioned)
    {
        DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] mode transition: "
             + juce::String(lastBlockUsedStretch ? "stretch" : "repitch") + " -> "
             + juce::String(useStretcher ? "stretch" : "repitch")
             + " speed=" + juce::String(snap.speed) + " stretch=" + juce::String(snap.stretchRatio));
        if (tearingDebugEnabled.load())
            tearingStats.modeTransitions.fetch_add(1, std::memory_order_relaxed);
    }
#endif
    if (transitioned && previousBlockValid)
    {
        const int curSamples = outputBuffer.getNumSamples();
        const int prevSamples = previousBlockNumSamples;
        const int fadeSamples = juce::jmin(curSamples,
                                           juce::jmin(prevSamples,
                                                     juce::jmax(1, (int)(hostSampleRate * 0.01)))); // 10ms

        if (fadeSamples > 0
            && previousBlockBuffer.getNumChannels() >= outputBuffer.getNumChannels())
        {
            const int prevStart = juce::jmax(0, prevSamples - fadeSamples);
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            {
                const float* prev = previousBlockBuffer.getReadPointer(ch);
                float* curr = outputBuffer.getWritePointer(ch);
                for (int i = 0; i < fadeSamples; ++i)
                {
                    const float fadeIn  = (float)(i + 1) / (float)(fadeSamples + 1);
                    const float fadeOut = 1.0f - fadeIn;
                    curr[i] = prev[prevStart + i] * fadeOut + curr[i] * fadeIn;
                }
            }
        }
    }
    lastBlockUsedStretch = useStretcher;

    // Cache this block for potential transition crossfade next time.
    if (previousBlockBuffer.getNumChannels() != outputBuffer.getNumChannels()
        || previousBlockBuffer.getNumSamples() < outputBuffer.getNumSamples())
    {
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
                + " stretch=" + juce::String(snap.stretchRatio));
        }
        
        // Check each channel for issues
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = outputBuffer.getReadPointer(ch);
            float lastValid = (ch < 2) ? lastOutputSample[ch] : 0.0f;
            
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
                
                // Check for discontinuity (only check after first sample has valid lastValid)
                if (i > 0 || (ch < 2 && std::abs(lastOutputSample[ch]) > 1.0e-10f))
                {
                    if (TearingDebug::isDiscontinuity(lastValid, sample))
                    {
                        tearingStats.discontinuities.fetch_add(1, std::memory_order_relaxed);
                        tearingStats.lastTearingEventTime.store(juce::Time::getMillisecondCounterHiRes());
                        DBG("[AudioBuffer " + juce::String(bufferIndex) + "] TEARING: Discontinuity ch=" 
                            + juce::String(ch) + " idx=" + juce::String(i)
                            + " prev=" + juce::String(lastValid, 4) + " curr=" + juce::String(sample, 4)
                            + " delta=" + juce::String(std::abs(sample - lastValid), 4));
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

    const bool oldPitchActive = std::abs(oldPitch - 0.0) > 1.0e-6;
    const bool newPitchActive = std::abs(semiTones - 0.0) > 1.0e-6;
    const bool stretchActive = std::abs(stretchRatio.load() - 1.0) > 1.0e-6;

    // Reset SoundTouch only when toggling overall "stretcher usage" on/off.
    const bool oldUse = (stretchActive || oldPitchActive);
    const bool newUse = (stretchActive || newPitchActive);
    if (oldUse != newUse)
        stretcherNeedsReset.store(true);
}

AudioBuffer::LoadedAudioData::Ptr AudioBuffer::getAudioDataSnapshot() const
{
    const juce::SpinLock::ScopedLockType sl(audioDataLock);
    return audioData;
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
    
    outputBuffer.clear();
    
    for (int sample = 0; sample < numOutputSamples; ++sample)
    {
        const double speed = speedSmoother.getNextValue();
        double currentPos = playheadPosition.load();
        
        // Handle slice-based playback first
        handleSlicePlayback(currentPos, fileLengthSamples);
        currentPos = playheadPosition.load(); // Get updated position after slice handling
        
        // Only handle normal boundary conditions if not in slicing mode
        if (!slicingModeActive.load())
        {
            // Handle looping and boundaries for normal playback
            if (speed > 0.0) // Forward playback
            {
                if (currentPos >= fileLengthSamples - 1)
                {
                    if (params.isLooping)
                    {
                        currentPos = 0.0;
                    }
                    else
                    {
                        params.isPlaying = false;
                        break;
                    }
                }
            }
            else if (speed < 0.0) // Reverse playback
            {
                if (currentPos <= 0.0)
                {
                    if (params.isLooping)
                    {
                        currentPos = static_cast<double>(fileLengthSamples - 1);
                    }
                    else
                    {
                        params.isPlaying = false;
                        break;
                    }
                }
            }
            else // Speed == 0, paused
            {
                break;
            }
        }
        else if (speed == 0.0) // Paused in slicing mode
        {
            break;
        }
        
        // Ensure position is within bounds (and loop within custom window if enabled)
        if (loopWindowEnabled.load())
        {
            const int64_t start = loopStartSamples.load();
            const int64_t end   = loopEndSamples.load();
            if (speed >= 0.0)
            {
                if (currentPos >= (double) end)
                {
                    // Smooth wrap: start a short boundary crossfade
                    startBoundaryCrossfade((double) start);
                    currentPos = (double) start;
                }
                else if (currentPos < (double) start)
                    currentPos = (double) start;
            }
            else // reverse
            {
                if (currentPos < (double) start)
                {
                    startBoundaryCrossfade((double) end);
                    currentPos = (double) end;
                }
                else if (currentPos > (double) end)
                    currentPos = (double) end;
            }
        }
        // Clamp to file bounds as a safety
        currentPos = juce::jlimit(0.0, static_cast<double>(fileLengthSamples - 1), currentPos);
        
        // High-quality interpolation
        const int pos1 = static_cast<int>(currentPos);
        const int pos2 = juce::jmin(pos1 + 1, fileLengthSamples - 1);
        const double fraction = currentPos - pos1;
        
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float sample1 = sourceBuffer.getSample(channel, pos1);
            const float sample2 = sourceBuffer.getSample(channel, pos2);
            const float interpolatedSample = sample1 + static_cast<float>(fraction) * (sample2 - sample1);
            
            outputBuffer.setSample(channel, sample, interpolatedSample);
        }
        
        // Apply crossfading for slice transitions (if active)
        if (isInCrossfade)
        {
            applyCrossfadeToSliceTransition(sourceBuffer, fileLengthSamples, fileSampleRate, outputBuffer, sample, 1);
        }
        
        // Advance playhead
        currentPos += speed * (fileSampleRate / hostSampleRate);
        playheadPosition.store(currentPos);
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
    newData->sampleRate = reader->sampleRate;
    newData->fileName = file.getFileNameWithoutExtension();

    const int fileLengthSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);

    // Load entire file into memory (on the calling thread).
    newData->buffer.setSize(numChannels, fileLengthSamples);
    reader->read(&newData->buffer, 0, fileLengthSamples, 0, true, true);

    setLoadedAudioData(newData);
    return true;
}

void AudioBuffer::setLoadedAudioData(LoadedAudioData::Ptr newData)
{
    {
        const juce::SpinLock::ScopedLockType sl(audioDataLock);
        audioData = newData;
    }

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
    previousSliceIndex = -1;

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
    {
        const juce::SpinLock::ScopedLockType sl(audioDataLock);
        audioData = nullptr;
    }
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

    if (wasStretching != nowStretching)
        stretcherNeedsReset.store(true);
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

    stretchRatio.store(1.0);
    pitchSemiTones.store(0.0);
    stretcherPrepared = false;
    stretcherPrimed = false;
    stretcherNeedsReset.store(false);
    lastBlockUsedStretch = false;
    stretcher.reset();
    stretchSmoother.setCurrentAndTargetValue(1.0);
    speedMagSmoother.setCurrentAndTargetValue(1.0);
    lastStretchDirection = 1.0;
    previousBlockValid = false;
    resetCrossfadePending = false;
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    previousSliceIndex = -1;
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

    const double clampedRate = juce::jlimit (0.25, 4.0, smoothedSpeedMag);
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
        DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] SoundTouch RESET  speed=" + juce::String(snap.speed)
             + " stretch=" + juce::String(snap.stretchRatio) + " pitch=" + juce::String(snap.pitchSemis)
             + " dir=" + juce::String(direction) + " pos=" + juce::String(playheadPosition.load()));
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
        stretcher.setPitchSemiTones ((float) pitchSemis);
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
        stretcher.setPitchSemiTones ((float) pitchSemis);

        // T9: When the direction changes, re-tune SoundTouch's overlap windows.
        if (direction != lastStretchDirection)
        {
            stretcher.setWindowsForReverse (direction < 0.0);
#if JUCE_DEBUG
            DBG ("[AudioBuffer " + juce::String(bufferIndex) + "] direction flip " + juce::String(lastStretchDirection) + "->" + juce::String(direction));
            if (tearingDebugEnabled.load())
                tearingStats.directionFlips.fetch_add(1, std::memory_order_relaxed);
#endif
            lastStretchDirection = direction;
        }
    }

    // Ensure scratch buffers are appropriately sized.
    const int stretcherLatency = juce::jmax (0, stretcher.getLatencySamples());
    if (stretchInScratch.getNumChannels() != numChannels || stretchInScratch.getNumSamples() < maxInputChunkFrames)
        stretchInScratch.setSize (numChannels, maxInputChunkFrames, false, false, true);

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
                return framesCopied;
            }
        }

        // Zero the region we are going to fill.
        stretchInScratch.clear (0, framesToGenerate);

        // Use local playhead while generating to avoid per-sample atomic traffic.
        double currentPos = playheadPosition.load();
        const double step = inputSpeed * (fileSampleRate / hostSampleRate);

        // Local crossfade helpers (avoid touching playhead atomics).
        auto startSliceCrossfadeLocal = [&] (int newSliceIndex, double newPlayheadPos)
        {
            previousSliceIndex = currentActiveSlice.load();
            previousSlicePlayheadPos = currentPos;
            isInCrossfade = true;
            crossfadePosition = 0;

            currentActiveSlice.store (newSliceIndex);
            currentPos = newPlayheadPos;
        };

        auto startBoundaryCrossfadeLocal = [&] (double newPlayheadPos)
        {
            previousSlicePlayheadPos = currentPos;
            previousSliceIndex = -1;
            isInCrossfade = true;
            crossfadePosition = 0;
            currentPos = newPlayheadPos;
        };

        // Inline slice handling using the local playhead.
        auto handleSlicePlaybackLocal = [&] ()
        {
            if (! slicingModeActive.load())
                return;

            // Check if we need to jump to a new slice
            if (sliceTriggered.load())
            {
                const int slice = targetSlice.load();
                const double speed = getEffectiveSpeed();

                double newPos = 0.0;
                if (speed >= 0.0)
                    newPos = getSliceStartPosition (slice, fileLengthSamples);
                else
                    newPos = getSliceEndPosition (slice, fileLengthSamples) - 1.0;

                startSliceCrossfadeLocal (slice, newPos);
                sliceTriggered.store (false);
                return;
            }

            // Handle continuous random mode
            if (params.continuousRandomSlicing)
            {
                const int activeSlice = currentActiveSlice.load();
                const double sliceStart = getSliceStartPosition (activeSlice, fileLengthSamples);
                const double sliceEnd   = getSliceEndPosition (activeSlice, fileLengthSamples);

                bool shouldTriggerNext = false;
                const double speed = getEffectiveSpeed();

                if (speed > 0.0)
                    shouldTriggerNext = (currentPos >= sliceEnd - 1.0);
                else if (speed < 0.0)
                    shouldTriggerNext = (currentPos <= sliceStart);

                if (shouldTriggerNext)
                {
                    const int nextSlice = random.nextInt (params.numSlices);

                    double newPos = 0.0;
                    if (speed > 0.0)
                        newPos = getSliceStartPosition (nextSlice, fileLengthSamples);
                    else
                        newPos = getSliceEndPosition (nextSlice, fileLengthSamples) - 1.0;

                    startSliceCrossfadeLocal (nextSlice, newPos);
                }
            }
            else
            {
                const int activeSlice = currentActiveSlice.load();
                const double sliceStart = getSliceStartPosition (activeSlice, fileLengthSamples);
                const double sliceEnd   = getSliceEndPosition (activeSlice, fileLengthSamples);
                const double speed = getEffectiveSpeed();

                if (speed > 0.0 && currentPos >= sliceEnd - 1.0)
                {
                    if (params.isLooping)
                        currentPos = sliceStart;
                    else
                        params.isPlaying = false;
                }
                else if (speed < 0.0 && currentPos <= sliceStart)
                {
                    if (params.isLooping)
                        currentPos = sliceEnd - 1.0;
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

        const bool slicingOn = slicingModeActive.load();

        int framesFilled = 0;
        for (int sample = 0; sample < framesToGenerate; ++sample)
        {
            // Keep slice behavior responsive (checks atomics, but avoids playhead atomics).
            if (slicingOn || sliceTriggered.load())
                handleSlicePlaybackLocal();

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
                applyCrossfadeToSliceTransition (sourceBuffer, fileLengthSamples, fileSampleRate, stretchInScratch, sample, 1);

            currentPos += step;
            ++framesFilled;
        }

        playheadPosition.store (currentPos);
        return framesFilled;
    };

    // On the very first block after a reset, schedule a short fade-in (~20ms)
    // to mask any startup transient from SoundTouch's empty internal buffers.
    if (! stretcherPrimed)
    {
        stretcherPrimed = true;
        stretchFadeInRemaining = juce::jmax (1, (int) (hostSampleRate * 0.02));

        // T2: Pre-prime SoundTouch with enough input to fill its internal latency
        // pipeline.  Without this, the first 1–2 output blocks after a reset are
        // partially empty (underruns), which sounds like crackles or brief silence.
        //
        // getLatencySamples() underestimates when rate/tempo/pitch are > 1 because
        // it doesn't fully account for the rate transposer's input consumption.
        // Scale by the effective consumption ratio so the TDStretch + RateTransposer
        // pipeline is fully warm on the first drain.
        const int rawLatency = juce::jmax (0, stretcher.getLatencySamples());
        const int primeSamples = juce::jmax (rawLatency,
                                             (int) std::ceil (rawLatency * totalTempoRatioForIO));
        if (primeSamples > 0)
        {
            // Ensure scratch is large enough for the priming chunk.
            if (stretchInScratch.getNumChannels() != numChannels
                || stretchInScratch.getNumSamples() < primeSamples)
            {
                stretchInScratch.setSize (numChannels, primeSamples, false, false, true);
            }

            const int primed = fillInputScratch (primeSamples);
            if (primed > 0)
            {
                std::vector<const float*> primePtrs ((size_t) numChannels, nullptr);
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
    std::vector<float*> outPtrs (numChannels, nullptr);
    std::vector<const float*> inPtrs (numChannels, nullptr);

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

    // Apply fade-in if this is the first block after priming — but only when
    // there is NO mode-transition crossfade about to run in processBlock.
    // The processBlock crossfade already blends the previous repitch block into
    // this stretch block; applying a fade-from-silence here would fight with it
    // and cause a volume dip / click at the transition point.
    const bool modeTransitionCrossfadeWillRun = (lastBlockUsedStretch == false) && previousBlockValid;
    if (modeTransitionCrossfadeWillRun)
    {
        // Skip the fade-in entirely; processBlock's crossfade handles the blend.
        stretchFadeInRemaining = 0;
    }
    else if (stretchFadeInRemaining > 0 && framesWritten > 0)
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

    // T8: When the playback direction changed this block (detected via T1 reset +
    // direction != lastStretchDirection from previous block), crossfade the tail
    // of the previous block into the head of the current block.
    // The mode-transition crossfade in processBlock handles repitch<->stretch,
    // but direction flips within the stretch path need their own crossfade.
    if (didReset && previousBlockValid
        && previousBlockBuffer.getNumChannels() >= outputBuffer.getNumChannels())
    {
        const int curSamples = framesWritten;
        const int prevSamples = previousBlockNumSamples;
        const int fadeSamples = juce::jmin (curSamples,
                                            juce::jmin (prevSamples,
                                                        juce::jmax (1, (int) (hostSampleRate * 0.01)))); // 10ms
        if (fadeSamples > 0)
        {
            const int prevStart = juce::jmax (0, prevSamples - fadeSamples);
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            {
                const float* prev = previousBlockBuffer.getReadPointer (ch);
                float* curr = outputBuffer.getWritePointer (ch);
                for (int i = 0; i < fadeSamples; ++i)
                {
                    const float fadeIn  = (float) (i + 1) / (float) (fadeSamples + 1);
                    const float fadeOut = 1.0f - fadeIn;
                    curr[i] = prev[prevStart + i] * fadeOut + curr[i] * fadeIn;
                }
            }
        }
    }

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
    }
}

void AudioBuffer::startContinuousRandomSlicing()
{
    if (params.numSlices > 1)
    {
        triggerRandomSlice();
        params.continuousRandomSlicing = true;
    }
}

void AudioBuffer::stopContinuousRandomSlicing()
{
    params.continuousRandomSlicing = false;
}

void AudioBuffer::exitSlicingMode()
{
    slicingModeActive.store(false);
    params.continuousRandomSlicing = false;
    sliceTriggered.store(false);
    currentActiveSlice.store(0);
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    previousSliceIndex = -1;
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

void AudioBuffer::startSliceCrossfade(int newSliceIndex, double newPlayheadPos)
{
    // Store previous slice information for crossfading
    previousSliceIndex = currentActiveSlice.load();
    previousSlicePlayheadPos = playheadPosition.load();
    
    // Start the crossfade
    isInCrossfade = true;
    crossfadePosition = 0;
    
    // Update to new slice
    currentActiveSlice.store(newSliceIndex);
    playheadPosition.store(newPlayheadPos);
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
    const int remainingCrossfadeSamples = crossfadeLengthSamples - crossfadePosition;
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
        ? ((params.speed < 0.0 ? -1.0 : 1.0) * tempoMultiplier.load())
        : getEffectiveSpeed();
    for (int sample = 0; sample < samplesToProcess; ++sample)
    {
        const double fadeProgress = static_cast<double>(crossfadePosition + sample) / static_cast<double>(crossfadeLengthSamples);
        
        // Use equal-power crossfade curve
        const float fadeOut = static_cast<float>(std::cos(fadeProgress * juce::MathConstants<double>::halfPi));
        const float fadeIn = static_cast<float>(std::sin(fadeProgress * juce::MathConstants<double>::halfPi));
        
        // Get sample from previous slice position with proper speed compensation
        const double prevPos = previousSlicePlayheadPos + sample * effectiveSpeed * (fileSampleRate / hostSampleRate);
        const int prevSampleIndex = static_cast<int>(prevPos);
        const double prevFraction = prevPos - prevSampleIndex;
        
        if (prevSampleIndex >= 0 && prevSampleIndex < fileLengthSamples - 1)
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                // Linear interpolation for previous slice
                const float* readPtr = sourceBuffer.getReadPointer(channel);
                const float prevSample1 = readPtr[prevSampleIndex];
                const float prevSample2 = readPtr[prevSampleIndex + 1];
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
    if (crossfadePosition >= crossfadeLengthSamples)
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
    playheadPosition.store(newPlayheadPos);
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
