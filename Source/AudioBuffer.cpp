/*
  ==============================================================================

    AudioBuffer.cpp
    
    Professional Audio Buffer Implementation

  ==============================================================================
*/

#include "AudioBuffer.h"

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
    
    // Smooth speed changes to avoid artifacts
    speedSmoother.setTargetValue(params.speed);
    
    // Process with repitching (speed and pitch change together)
    processWithRepitching(outputBuffer);
    
    // Check for state changes and notify listeners
    notifyPlaybackStateChanged();
    notifySliceChanged();
    notifyPositionChanged();
}

void AudioBuffer::processWithRepitching(juce::AudioBuffer<float>& outputBuffer)
{
    const int numOutputSamples = outputBuffer.getNumSamples();
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), audioFileBuffer.getNumChannels());
    
    outputBuffer.clear();
    
    for (int sample = 0; sample < numOutputSamples; ++sample)
    {
        const double speed = speedSmoother.getNextValue();
        double currentPos = playheadPosition.load();
        
        // Handle slice-based playback first
        handleSlicePlayback(currentPos);
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
            const float sample1 = audioFileBuffer.getSample(channel, pos1);
            const float sample2 = audioFileBuffer.getSample(channel, pos2);
            const float interpolatedSample = sample1 + static_cast<float>(fraction) * (sample2 - sample1);
            
            outputBuffer.setSample(channel, sample, interpolatedSample);
        }
        
        // Apply crossfading for slice transitions (if active)
        if (isInCrossfade)
        {
            applyCrossfadeToSliceTransition(outputBuffer, sample, 1);
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
    
    fileSampleRate = reader->sampleRate;
    fileLengthSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    
    // Load entire file into memory for reliable playback
    audioFileBuffer.setSize(numChannels, fileLengthSamples);
    reader->read(&audioFileBuffer, 0, fileLengthSamples, 0, true, true);
    
    // Store filename for reference
    loadedFileName = file.getFileNameWithoutExtension();
    
    // Reset playback position
    playheadPosition.store(0.0);
    
    return true;
}

void AudioBuffer::clearAudioData()
{
    audioFileBuffer.setSize(0, 0);
    loadedFileName.clear();
    playheadPosition.store(0.0);
    fileLengthSamples = 0;
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
}

void AudioBuffer::setSpeed(double newSpeed)
{
    params.speed = newSpeed;
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
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    previousSliceIndex = -1;
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
    if (params.numSlices <= 1 || fileLengthSamples <= 0)
        return 0;
    
    // If we're in slicing mode, return the active slice rather than calculating from position
    if (slicingModeActive.load())
    {
        return currentActiveSlice.load();
    }
    
    // Otherwise, calculate from playhead position for display purposes
    double currentPos = playheadPosition.load();
    double sliceSize = static_cast<double>(fileLengthSamples) / params.numSlices;
    return juce::jlimit(0, params.numSlices - 1, static_cast<int>(currentPos / sliceSize));
}

//==============================================================================
// Timing and Position
//==============================================================================

double AudioBuffer::getPlayheadPositionInSeconds() const
{
    if (fileSampleRate <= 0.0)
        return 0.0;
    return playheadPosition.load() / fileSampleRate;
}

double AudioBuffer::getDurationInSeconds() const
{
    if (fileSampleRate <= 0.0)
        return 0.0;
    return static_cast<double>(fileLengthSamples) / fileSampleRate;
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

double AudioBuffer::getSliceStartPosition(int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= params.numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    double sliceSize = static_cast<double>(fileLengthSamples) / params.numSlices;
    int targetSample = static_cast<int>(sliceIndex * sliceSize);
    
    return static_cast<double>(targetSample);
}

double AudioBuffer::getSliceEndPosition(int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= params.numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    double sliceSize = static_cast<double>(fileLengthSamples) / params.numSlices;
    int targetSample = static_cast<int>((sliceIndex + 1) * sliceSize);
    
    return static_cast<double>(targetSample);
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

void AudioBuffer::applyCrossfadeToSliceTransition(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isInCrossfade)
        return;
        
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), audioFileBuffer.getNumChannels());
    const int remainingCrossfadeSamples = crossfadeLengthSamples - crossfadePosition;
    const int samplesToProcess = juce::jmin(numSamples, remainingCrossfadeSamples);
    
    if (samplesToProcess <= 0)
    {
        isInCrossfade = false;
        return;
    }
    
    // Process crossfade sample by sample
    for (int sample = 0; sample < samplesToProcess; ++sample)
    {
        const double fadeProgress = static_cast<double>(crossfadePosition + sample) / static_cast<double>(crossfadeLengthSamples);
        
        // Use equal-power crossfade curve
        const float fadeOut = static_cast<float>(std::cos(fadeProgress * juce::MathConstants<double>::halfPi));
        const float fadeIn = static_cast<float>(std::sin(fadeProgress * juce::MathConstants<double>::halfPi));
        
        // Get sample from previous slice position with proper speed compensation
        const double prevPos = previousSlicePlayheadPos + sample * params.speed * (fileSampleRate / hostSampleRate);
        const int prevSampleIndex = static_cast<int>(prevPos);
        const double prevFraction = prevPos - prevSampleIndex;
        
        if (prevSampleIndex >= 0 && prevSampleIndex < fileLengthSamples - 1)
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                // Linear interpolation for previous slice
                const float* readPtr = audioFileBuffer.getReadPointer(channel);
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

void AudioBuffer::handleSlicePlayback(double& currentPos)
{
    if (!slicingModeActive.load())
        return;
    
    // Check if we need to jump to a new slice
    if (sliceTriggered.load())
    {
        int slice = targetSlice.load();
        double speed = params.speed;
        
        // Calculate new position based on playback direction
        double newPos;
        if (speed >= 0.0)
        {
            newPos = getSliceStartPosition(slice);
        }
        else
        {
            newPos = getSliceEndPosition(slice) - 1.0;
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
        double sliceStart = getSliceStartPosition(activeSlice);
        double sliceEnd = getSliceEndPosition(activeSlice);
        
        // Check if we've reached the end/beginning of the current slice
        bool shouldTriggerNext = false;
        double speed = params.speed;
        
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
                newPos = getSliceStartPosition(nextSlice);
            }
            else
            {
                newPos = getSliceEndPosition(nextSlice) - 1.0;
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
        double sliceStart = getSliceStartPosition(activeSlice);
        double sliceEnd = getSliceEndPosition(activeSlice);
        double speed = params.speed;
        
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
    audioFileBuffer.setSize(0, 0);
    repitchBuffer.setSize(0, 0);
    tempProcessingBuffer.setSize(0, 0);
    crossfadeBuffer.setSize(0, 0);
}
