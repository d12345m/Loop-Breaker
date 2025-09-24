/*
  ==============================================================================

    AudioBufferManager.h
    
    Audio Buffer Manager for Multiple Buffer Management
    
    This component manages multiple audio buffers and provides:
    - Centralized buffer management for up to N buffers
    - Synchronized parameter changes across buffers
    - Master audio processing and mixing
    - Clean interface for integration into sampler applications

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "AudioBuffer.h"
#include <array>
#include <memory>

//==============================================================================
/**
    Manager class for handling multiple AudioBuffer instances.
    Designed for use in sampler applications that need multiple simultaneous buffers.
*/
class AudioBufferManager : public AudioBufferListener
{
public:
    static constexpr int MAX_BUFFERS = 8; // Match the MPC-style interface from DesignDoc
    
    AudioBufferManager();
    ~AudioBufferManager() = default;
    
    //==============================================================================
    // Setup and lifecycle
    void prepare(double sampleRate, int samplesPerBlockExpected);
    void processBlock(juce::AudioBuffer<float>& outputBuffer);
    void releaseResources();
    
    //==============================================================================
    // Buffer management
    AudioBuffer* getBuffer(int bufferIndex);
    const AudioBuffer* getBuffer(int bufferIndex) const;
    bool isValidBufferIndex(int bufferIndex) const;
    
    //==============================================================================
    // File loading
    bool loadAudioFile(int bufferIndex, const juce::File& file, juce::AudioFormatManager& formatManager);
    void clearBuffer(int bufferIndex);
    void clearAllBuffers();
    
    //==============================================================================
    // Global controls
    void playAll();
    void stopAll();
    void resetAllBuffers();
    void setMasterVolume(float volume) { masterVolume.store(volume); }
    float getMasterVolume() const { return masterVolume.load(); }
    
    //==============================================================================
    // Buffer queries
    int getNumLoadedBuffers() const;
    juce::Array<int> getLoadedBufferIndices() const;
    juce::Array<int> getPlayingBufferIndices() const;
    
    //==============================================================================
    // AudioBufferListener overrides (for receiving notifications from individual buffers)
    void audioBufferPlaybackStarted(int bufferIndex) override;
    void audioBufferPlaybackStopped(int bufferIndex) override;
    void audioBufferSliceChanged(int bufferIndex, int newSliceIndex) override;
    void audioBufferPositionChanged(int bufferIndex, double positionInSeconds) override;
    
    //==============================================================================
    // Listener management for external components
    void addListener(AudioBufferListener* listener);
    void removeListener(AudioBufferListener* listener);
    
private:
    //==============================================================================
    // Buffer storage
    std::array<std::unique_ptr<AudioBuffer>, MAX_BUFFERS> buffers;
    
    // Master controls
    std::atomic<float> masterVolume { 1.0f };
    
    // Audio processing
    juce::AudioBuffer<float> mixBuffer;
    juce::AudioBuffer<float> tempBuffer;
    
    // Listeners
    juce::Array<AudioBufferListener*> listeners;
    
    //==============================================================================
    // Internal helpers
    void notifyListeners(std::function<void(AudioBufferListener*)> notification);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBufferManager)
};
