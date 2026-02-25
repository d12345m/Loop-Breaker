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
    pendingLoads.fill({});

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
    
    // Prepare mixing buffers with extra headroom so processBlock never re-allocates.
    // Some hosts may occasionally deliver slightly larger blocks than advertised.
    const int headroom = samplesPerBlockExpected + 64;
    mixBuffer.setSize(2, headroom, false, false, true);
    tempBuffer.setSize(2, headroom, false, false, true);
    hostSampleRate = sampleRate;
}

void AudioBufferManager::processBlock(juce::AudioBuffer<float>& outputBuffer)
{
    const int numSamples = outputBuffer.getNumSamples();
    const int numChannels = outputBuffer.getNumChannels();
    
    // Clear the output buffer
    outputBuffer.clear();
    
    // Process and mix all loaded buffers
    for (int i = 0; i < (int)buffers.size(); ++i)
    {
        auto& buffer = buffers[i];
        if (buffer->hasAudioLoaded())
        {
            // Process the buffer into temp buffer without per-block allocation
            // Pre-allocated in prepare(); guard kept as safety net.
            if (tempBuffer.getNumChannels() != numChannels || tempBuffer.getNumSamples() < numSamples)
            {
                jassertfalse; // tempBuffer should have been pre-allocated in prepare()
                tempBuffer.setSize(numChannels, numSamples, false, false, true);
            }
            tempBuffer.clear();
            buffer->processBlock(tempBuffer);
            // Per-buffer processing hook (FX)
            if (perBufferProcessor)
                perBufferProcessor(i, tempBuffer, hostSampleRate);
            
            // Apply -12dB reduction per channel (gain = 0.251189 for -12dB)
            tempBuffer.applyGain(0.251189f);
            
            // Add to the mix
            for (int channel = 0; channel < numChannels; ++channel)
            {
                outputBuffer.addFrom(channel, 0, tempBuffer, channel, 0, numSamples);
            }

            // Per-buffer loop windows are enforced inside AudioBuffer; no global enforcement needed here.
        }
    }
    
    // Apply master volume
    const float masterVol = masterVolume.load();
    if (masterVol != 1.0f)
    {
        outputBuffer.applyGain(masterVol);
    }
}

void AudioBufferManager::processSingleBuffer(int bufferIndex, juce::AudioBuffer<float>& outputBuffer)
{
    outputBuffer.clear();

    if (! isValidBufferIndex(bufferIndex))
        return;

    auto& buffer = buffers[(size_t) bufferIndex];
    if (buffer == nullptr || ! buffer->hasAudioLoaded())
        return;

    buffer->processBlock(outputBuffer);

    if (perBufferProcessor)
        perBufferProcessor(bufferIndex, outputBuffer, hostSampleRate);

    // Apply -12dB reduction per channel (gain = 0.251189 for -12dB)
    outputBuffer.applyGain(0.251189f);

    const float masterVol = masterVolume.load();
    if (masterVol != 1.0f)
        outputBuffer.applyGain(masterVol);

}

void AudioBufferManager::releaseResources()
{
    // Ensure no background jobs are still decoding.
    loaderPool.removeAllJobs(true, 5000);

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

namespace
{
AudioBuffer::LoadedAudioData::Ptr decodeFileToLoadedData(const juce::File& file)
{
    if (!file.existsAsFile())
        return nullptr;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr)
        return nullptr;

    const int lengthSamples = (int) reader->lengthInSamples;
    const int channels = (int) reader->numChannels;
    if (lengthSamples <= 0 || channels <= 0)
        return nullptr;

    auto data = new AudioBuffer::LoadedAudioData();
    data->sampleRate = reader->sampleRate;
    data->fileName = file.getFileNameWithoutExtension();
    data->buffer.setSize(channels, lengthSamples);
    reader->read(&data->buffer, 0, lengthSamples, 0, true, true);
    return data;
}
}

void AudioBufferManager::enqueuePendingLoad(PendingLoadedBuffer&& p)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 <= 0)
    {
        juce::Logger::writeToLog("Sample loader: pending queue full; dropping load for pad "
                                 + juce::String(p.bufferIndex + 1) + " (" + p.sourcePath + ")");
        return;
    }

    pendingLoads[(size_t) start1] = std::move(p);
    pendingFifo.finishedWrite(1);
}

bool AudioBufferManager::requestLoadAudioFile(int bufferIndex, const juce::File& file)
{
    if (!isValidBufferIndex(bufferIndex) || !file.existsAsFile())
        return false;

    struct LoadJob final : public juce::ThreadPoolJob
    {
        LoadJob(AudioBufferManager& mgr, int idx, juce::File f)
            : juce::ThreadPoolJob("Load sample")
            , manager(mgr)
            , bufferIndex(idx)
            , file(std::move(f))
        {
        }

        JobStatus runJob() override
        {
            PendingLoadedBuffer p;
            p.bufferIndex = bufferIndex;
            p.sourcePath = file.getFullPathName();
            p.data = decodeFileToLoadedData(file);
            p.ok = (p.data != nullptr);
            manager.enqueuePendingLoad(std::move(p));
            return jobHasFinished;
        }

        AudioBufferManager& manager;
        int bufferIndex;
        juce::File file;
    };

    loaderPool.addJob(new LoadJob(*this, bufferIndex, file), true);
    return true;
}

int AudioBufferManager::applyPendingLoads()
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    const int ready = pendingFifo.getNumReady();
    if (ready <= 0)
        return 0;

    pendingFifo.prepareToRead(ready, start1, size1, start2, size2);

    int appliedCount = 0;

    auto applyRange = [this, &appliedCount](int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            auto p = std::move(pendingLoads[(size_t) (start + i)]);
            pendingLoads[(size_t) (start + i)] = {};

            if (!isValidBufferIndex(p.bufferIndex))
                continue;

            ++appliedCount;

            if (!p.ok || p.data == nullptr)
            {
                juce::Logger::writeToLog("Sample loader: failed to decode pad "
                                         + juce::String(p.bufferIndex + 1) + " from " + p.sourcePath);
                clearBuffer(p.bufferIndex);
                continue;
            }

            if (auto* buffer = getBuffer(p.bufferIndex))
                buffer->setLoadedAudioData(p.data);
        }
    };

    if (size1 > 0) applyRange(start1, size1);
    if (size2 > 0) applyRange(start2, size2);

    pendingFifo.finishedRead(size1 + size2);

    return appliedCount;
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
            // Apply global start offset (if any) before starting playback
            if (globalStartOffsetSamples > 0)
            {
                // Clamp within buffer duration
                auto duration = (int64_t) buffer->getDurationInSamples();
                auto start = juce::jmin(globalStartOffsetSamples, duration > 0 ? (duration - 1) : (int64_t)0);
                buffer->setPlayheadSamples(start);
            }
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

void AudioBufferManager::restartAllLoadedBuffersToBeginning()
{
    for (auto& buffer : buffers)
    {
        if (buffer->hasAudioLoaded())
            buffer->resetToBeginning();
    }
}

void AudioBufferManager::setTempoMultiplier(double multiplier)
{
    // This is safe to call from the audio thread: it only writes atomics.
    const double m = (multiplier > 0.0 ? multiplier : 1.0);
    for (auto& buffer : buffers)
    {
        if (buffer != nullptr)
            buffer->setTempoMultiplier(m);
    }
}

void AudioBufferManager::setStartOffsetSamples(int64_t startOffsetSamples)
{
    globalStartOffsetSamples = juce::jmax<int64_t>(0, startOffsetSamples);
}

void AudioBufferManager::setEndOffsetSamples(int64_t endOffsetSamples)
{
    globalEndOffsetSamples = juce::jmax<int64_t>(0, endOffsetSamples);
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
