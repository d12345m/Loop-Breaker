/*
  ==============================================================================

    AudioBuffer.cpp
    
    Professional Audio Buffer Implementation

  ==============================================================================
*/

#include "AudioBuffer.h"
#include <vector>
#include <algorithm>

//==============================================================================
// AudioBuffer Implementation
//==============================================================================

AudioBuffer::AudioBuffer(int bufferIndex)
    : bufferIndex(bufferIndex)
{
    speedSmoother.reset(128); // Smooth parameter changes over ~3ms at 44.1kHz
    speedSmoother.setCurrentAndTargetValue(1.0);
}

void AudioBuffer::prepare(double sampleRate, int samplesPerBlockExpected)
{
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();
    hostSampleRate = sampleRate;
    speedSmoother.reset(sampleRate, 0.05); // 50ms smoothing time
    
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
        return;
    }

    const double stretch = stretchRatio.load();

    // Route through SoundTouch only when actually stretching.
    // Even at 1.0, SoundTouch adds buffering/CPU and can affect UI smoothness in Debug builds.
    if (std::abs(stretch - 1.0) > 1.0e-6)
    {
        processWithTimeStretch(outputBuffer);
    }
    else
    {
        // Smooth speed changes to avoid artifacts
        speedSmoother.setTargetValue(getEffectiveSpeed());
        processWithRepitching(outputBuffer);
    }
    
    // Check for state changes and notify listeners
    notifyPlaybackStateChanged();
    notifySliceChanged();
    notifyPositionChanged();
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
    stretcherPrimed = false; // Ensure the new primed flag resets
    stretcher.reset();
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
    setPlaying(true);
}

void AudioBuffer::stop()
{
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
        // Flush any buffered SoundTouch audio so we don't resume with stale data.
        stretcher.reset();
        stretcherPrepared = false;
        stretcherPrimed = false; // Ensure the new primed flag resets
    }
}

void AudioBuffer::setSpeed(double newSpeed)
{
    params.speed = newSpeed;
}

void AudioBuffer::setStretchRatio(double ratio)
{
    if (ratio <= 0.0)
        ratio = 1.0;

    // Clamp to reasonable range (matches TimeStretchSoundTouch)
    ratio = juce::jlimit(0.25, 4.0, ratio);

    stretchRatio.store(ratio);
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
    stretcherPrepared = false;
    stretcherPrimed = false;
    stretcher.reset();
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    previousSliceIndex = -1;
}

void AudioBuffer::processWithTimeStretch(juce::AudioBuffer<float>& outputBuffer)
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

    const double ratio = stretchRatio.load();
    // When time-stretching, SoundTouch changes the output tempo. We should NOT also scale
    // the source read speed by ratio (that would double-apply the effect).
    // We do, however, still apply host tempo-following so Stretch remains relative to the DAW tempo.
    const double direction = (params.speed < 0.0 ? -1.0 : 1.0);
    const double inputSpeed = direction * tempoMultiplier.load();

    // Ensure scratch buffers are appropriately sized without per-sample allocation.
    // We intentionally keep these buffers around; setSize may allocate but only when capacity grows.
    // For time-stretch we may need to feed more input than output; size input scratch accordingly.
    const double clampedRatio = juce::jlimit (0.25, 4.0, ratio);
    // Feed chunks proportional to the output block.  A small margin (64 frames) ensures we don't
    // starve SoundTouch on fractional boundaries, without generating the massive 300ms warmup that
    // was previously causing CPU spikes every processBlock call.
    const int marginFrames = 64;
    const int maxInputChunkFrames = juce::jmax (numOutputSamples, (int) std::ceil (numOutputSamples * clampedRatio) + marginFrames);

    // Prepare SoundTouch when needed.
    if (!stretcherPrepared || stretcherPreparedSampleRate != hostSampleRate || stretcherPreparedChannels != numChannels)
    {
        stretcher.prepare(hostSampleRate, numChannels);
        stretcher.setTempoRatio ((float) clampedRatio);
        stretcherPrepared = true;
        stretcherPreparedSampleRate = hostSampleRate;
        stretcherPreparedChannels = numChannels;
        stretcherPrimed = false;
    }
    else
    {
        // Ratio can change without channel/sr changing; keep tempo updated.
        stretcher.setTempoRatio ((float) clampedRatio);
    }

    // Ensure scratch buffers are large enough for priming too.
    const int stretcherLatency = juce::jmax (0, stretcher.getLatencySamples());
    {
        const int neededScratch = juce::jmax (maxInputChunkFrames, stretcherLatency + marginFrames);
        if (stretchInScratch.getNumChannels() != numChannels || stretchInScratch.getNumSamples() < neededScratch)
            stretchInScratch.setSize (numChannels, neededScratch, false, false, true);
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

    // ──── Prime SoundTouch on first use after a reset ────
    // Feed stretcherLatency worth of real audio and discard the output so the
    // first frames the main loop drains are fully warmed up (no initial crackle).
    if (! stretcherPrimed && stretcherLatency > 0)
    {
        const int primeSamples = juce::jmin (stretcherLatency + marginFrames, maxInputChunkFrames);
        const int filled = fillInputScratch (primeSamples);
        if (filled > 0)
        {
            std::vector<const float*> primeIn (numChannels, nullptr);
            for (int ch = 0; ch < numChannels; ++ch)
                primeIn[ch] = stretchInScratch.getReadPointer (ch);

            // Feed the audio; any output produced is latency artefact — discard it.
            stretcher.processNonInterleaved (primeIn.data(), filled, nullptr, 0, false,
                                            stretchInterleavedIn, stretchInterleavedOut);

            // Drain and throw away whatever SoundTouch buffered.
            if (stretcher.numSamplesAvailable() > 0)
            {
                // Use stretchInScratch as a throwaway drain target (it was just consumed).
                std::vector<float*> discardPtrs (numChannels, nullptr);
                for (int ch = 0; ch < numChannels; ++ch)
                    discardPtrs[ch] = stretchInScratch.getWritePointer (ch);

                stretcher.processNonInterleaved (nullptr, 0, discardPtrs.data(),
                                                stretcher.numSamplesAvailable(), false,
                                                stretchInterleavedIn, stretchInterleavedOut);
            }
        }
        stretcherPrimed = true;
    }

    int framesWritten = 0;
    int safetyIters = 0;
    std::vector<float*> outPtrs (numChannels, nullptr);
    std::vector<const float*> inPtrs (numChannels, nullptr);

    // Cap iterations: with proportional feed sizes we should fill the block in 2-4 iterations.
    // More than 8 means something is very wrong.
    while (framesWritten < numOutputSamples && safetyIters++ < 8)
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
        const int inputChunkFrames = juce::jlimit (1, maxInputChunkFrames,
                                                    (int) std::ceil (remaining * clampedRatio) + marginFrames);

        const int framesFilled = fillInputScratch (inputChunkFrames);
        if (framesFilled <= 0)
            break;

        for (int ch = 0; ch < numChannels; ++ch)
            inPtrs[ch] = stretchInScratch.getReadPointer (ch);

        const int produced = stretcher.processNonInterleaved (inPtrs.data(), framesFilled, outPtrs.data(), remaining, false,
                                                             stretchInterleavedIn, stretchInterleavedOut);
        if (produced > 0)
            framesWritten += produced;
    }

    // Warn in debug if we repeatedly failed to fill the block.
#if JUCE_DEBUG
    jassertquiet (framesWritten == numOutputSamples || safetyIters >= 8);
#endif

    // If we still underfilled, pad the tail to avoid hard zeros/crackles.
    if (framesWritten < numOutputSamples)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float last = framesWritten > 0 ? outputBuffer.getSample (ch, framesWritten - 1) : 0.0f;
            outputBuffer.copyFrom (ch, framesWritten, &last, numOutputSamples - framesWritten);
        }

        timeStretchUnderfills.fetch_add (1, std::memory_order_relaxed);
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
    
    // Process crossfade sample by sample
    const double effectiveSpeed = getEffectiveSpeed();
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
    {
        const juce::SpinLock::ScopedLockType sl(audioDataLock);
        audioData = nullptr;
    }
    repitchBuffer.setSize(0, 0);
    tempProcessingBuffer.setSize(0, 0);
    crossfadeBuffer.setSize(0, 0);
}
