/*
  ==============================================================================

    AudioBufferManager.cpp
    
    Audio Buffer Manager Implementation

  ==============================================================================
*/

#include "AudioBufferManager.h"

//==============================================================================
// AudioBufferManager Implementation
//==============================================================================

AudioBufferManager::AudioBufferManager()
{
    // Initialize all buffers
    for (int i = 0; i < MAX_BUFFERS; ++i)
    {
        buffers[i] = std::make_unique<AudioBuffer>(i);
        buffers[i]->addListener(this);
    }
}

void AudioBufferManager::prepare(double sampleRate, int samplesPerBlockExpected)
{
    // Prepare all buffers
    for (auto& buffer : buffers)
    {
        buffer->prepare(sampleRate, samplesPerBlockExpected);
    }
    
    // Prepare mixing buffers
    mixBuffer.setSize(2, samplesPerBlockExpected);
    tempBuffer.setSize(2, samplesPerBlockExpected);
}

void AudioBufferManager::processBlock(juce::AudioBuffer<float>& outputBuffer)
{
    const int numSamples = outputBuffer.getNumSamples();
    const int numChannels = outputBuffer.getNumChannels();
    
    // Clear the output buffer
    outputBuffer.clear();
    
    // Process and mix all loaded buffers
    for (auto& buffer : buffers)
    {
        if (buffer->hasAudioLoaded())
        {
            // Process the buffer into temp buffer
            tempBuffer.setSize(numChannels, numSamples, false, true, true);
            buffer->processBlock(tempBuffer);
            
            // Add to the mix
            for (int channel = 0; channel < numChannels; ++channel)
            {
                outputBuffer.addFrom(channel, 0, tempBuffer, channel, 0, numSamples);
            }
        }
    }
    
    // Apply master volume
    const float masterVol = masterVolume.load();
    if (masterVol != 1.0f)
    {
        outputBuffer.applyGain(masterVol);
    }
}

void AudioBufferManager::releaseResources()
{
    for (auto& buffer : buffers)
    {
        buffer->releaseResources();
    }
    
    mixBuffer.setSize(0, 0);
    tempBuffer.setSize(0, 0);
}

//==============================================================================
// Buffer Management
//==============================================================================

AudioBuffer* AudioBufferManager::getBuffer(int bufferIndex)
{
    if (isValidBufferIndex(bufferIndex))
        return buffers[bufferIndex].get();
    return nullptr;
}

const AudioBuffer* AudioBufferManager::getBuffer(int bufferIndex) const
{
    if (isValidBufferIndex(bufferIndex))
        return buffers[bufferIndex].get();
    return nullptr;
}

bool AudioBufferManager::isValidBufferIndex(int bufferIndex) const
{
    return bufferIndex >= 0 && bufferIndex < MAX_BUFFERS;
}

//==============================================================================
// File Loading
//==============================================================================

bool AudioBufferManager::loadAudioFile(int bufferIndex, const juce::File& file, juce::AudioFormatManager& formatManager)
{
    if (auto* buffer = getBuffer(bufferIndex))
    {
        return buffer->loadAudioFile(file, formatManager);
    }
    return false;
}

void AudioBufferManager::clearBuffer(int bufferIndex)
{
    if (auto* buffer = getBuffer(bufferIndex))
    {
        buffer->clearAudioData();
    }
}

void AudioBufferManager::clearAllBuffers()
{
    for (auto& buffer : buffers)
    {
        buffer->clearAudioData();
    }
}

//==============================================================================
// Global Controls
//==============================================================================

void AudioBufferManager::playAll()
{
    for (auto& buffer : buffers)
    {
        if (buffer->hasAudioLoaded())
        {
            buffer->play();
        }
    }
}

void AudioBufferManager::stopAll()
{
    for (auto& buffer : buffers)
    {
        buffer->stop();
    }
}

void AudioBufferManager::resetAllBuffers()
{
    for (auto& buffer : buffers)
    {
        buffer->resetToDefaults();
    }
}

//==============================================================================
// Buffer Queries
//==============================================================================

int AudioBufferManager::getNumLoadedBuffers() const
{
    int count = 0;
    for (const auto& buffer : buffers)
    {
        if (buffer->hasAudioLoaded())
            ++count;
    }
    return count;
}

juce::Array<int> AudioBufferManager::getLoadedBufferIndices() const
{
    juce::Array<int> indices;
    for (int i = 0; i < MAX_BUFFERS; ++i)
    {
        if (buffers[i]->hasAudioLoaded())
            indices.add(i);
    }
    return indices;
}

juce::Array<int> AudioBufferManager::getPlayingBufferIndices() const
{
    juce::Array<int> indices;
    for (int i = 0; i < MAX_BUFFERS; ++i)
    {
        if (buffers[i]->hasAudioLoaded() && buffers[i]->isPlaying())
            indices.add(i);
    }
    return indices;
}

//==============================================================================
// AudioBufferListener overrides (notifications from individual buffers)
//==============================================================================

void AudioBufferManager::audioBufferPlaybackStarted(int bufferIndex)
{
    notifyListeners([bufferIndex](AudioBufferListener* listener)
    {
        listener->audioBufferPlaybackStarted(bufferIndex);
    });
}

void AudioBufferManager::audioBufferPlaybackStopped(int bufferIndex)
{
    notifyListeners([bufferIndex](AudioBufferListener* listener)
    {
        listener->audioBufferPlaybackStopped(bufferIndex);
    });
}

void AudioBufferManager::audioBufferSliceChanged(int bufferIndex, int newSliceIndex)
{
    notifyListeners([bufferIndex, newSliceIndex](AudioBufferListener* listener)
    {
        listener->audioBufferSliceChanged(bufferIndex, newSliceIndex);
    });
}

void AudioBufferManager::audioBufferPositionChanged(int bufferIndex, double positionInSeconds)
{
    notifyListeners([bufferIndex, positionInSeconds](AudioBufferListener* listener)
    {
        listener->audioBufferPositionChanged(bufferIndex, positionInSeconds);
    });
}

//==============================================================================
// Listener Management
//==============================================================================

void AudioBufferManager::addListener(AudioBufferListener* listener)
{
    if (listener != nullptr)
        listeners.addIfNotAlreadyThere(listener);
}

void AudioBufferManager::removeListener(AudioBufferListener* listener)
{
    listeners.removeFirstMatchingValue(listener);
}

//==============================================================================
// Internal Helpers
//==============================================================================

void AudioBufferManager::notifyListeners(std::function<void(AudioBufferListener*)> notification)
{
    for (auto* listener : listeners)
    {
        notification(listener);
    }
}
