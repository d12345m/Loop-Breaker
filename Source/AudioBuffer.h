/*
  ==============================================================================

    AudioBuffer.h
    
    Professional Audio Buffer Component
    
    This component provides:
    - High-quality repitching for speed changes
    - Seamless reverse playback with crossfading
    - Buffer slicing functionality
    - Thread-safe operation
    - Clean interface for integration into larger projects

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <functional>

//==============================================================================
/**
    Interface for receiving callbacks from AudioBuffer events
*/
class AudioBufferListener
{
public:
    virtual ~AudioBufferListener() = default;
    
    virtual void audioBufferPlaybackStarted(int bufferIndex) {}
    virtual void audioBufferPlaybackStopped(int bufferIndex) {}
    virtual void audioBufferSliceChanged(int bufferIndex, int newSliceIndex) {}
    virtual void audioBufferPositionChanged(int bufferIndex, double positionInSeconds) {}
};

//==============================================================================
/**
    Parameters structure for configuring buffer behavior
*/
struct AudioBufferParams
{
    double speed = 1.0;
    bool isLooping = true;
    bool isPlaying = false;
    int numSlices = 1;
    bool continuousRandomSlicing = false;
    double crossfadeLengthMs = 20.0;
    
    // Reset to default values
    void reset()
    {
        speed = 1.0;
        isLooping = true;
        isPlaying = false;
        numSlices = 1;
        continuousRandomSlicing = false;
        crossfadeLengthMs = 20.0;
    }
};

//==============================================================================
/**
    A professional audio buffer that handles variable speed playback
    with proper repitching, reverse playback, and seamless looping.
    
    This class is designed to be used as a component within larger audio applications.
*/
class AudioBuffer
{
public:
    AudioBuffer(int bufferIndex = 0);
    ~AudioBuffer() = default;
    
    //==============================================================================
    // Setup and lifecycle
    void prepare(double sampleRate, int samplesPerBlockExpected);
    void processBlock(juce::AudioBuffer<float>& outputBuffer);
    void releaseResources();
    
    //==============================================================================
    // Audio file loading
    bool loadAudioFile(const juce::File& file, juce::AudioFormatManager& formatManager);
    void clearAudioData();
    
    //==============================================================================
    // Transport controls
    void play();
    void stop();
    void pause();
    void setPlaying(bool shouldPlay);
    void setSpeed(double newSpeed);
    void setLooping(bool shouldLoop);
    void resetToBeginning();
    void resetToDefaults();
    
    //==============================================================================
    // Slicing functionality
    void setNumSlices(int numSlices);
    void triggerSlice(int sliceIndex);
    void triggerRandomSlice();
    void startContinuousRandomSlicing();
    void stopContinuousRandomSlicing();
    void exitSlicingMode();
    
    //==============================================================================
    // State queries
    bool hasAudioLoaded() const { return audioFileBuffer.getNumSamples() > 0; }
    bool isPlaying() const { return params.isPlaying; }
    bool isLooping() const { return params.isLooping; }
    double getSpeed() const { return params.speed; }
    int getCurrentSlice() const;
    int getNumSlices() const { return params.numSlices; }
    bool isInContinuousRandomMode() const { return params.continuousRandomSlicing; }
    bool isInSlicingMode() const { return slicingModeActive.load(); }
    
    //==============================================================================
    // Timing and position
    double getPlayheadPositionInSeconds() const;
    double getPlayheadPositionInSamples() const { return playheadPosition.load(); }
    double getDurationInSeconds() const;
    int getDurationInSamples() const { return fileLengthSamples; }
    double getFileSampleRate() const { return fileSampleRate; }
    
    //==============================================================================
    // Parameters
    const AudioBufferParams& getParams() const { return params; }
    void setParams(const AudioBufferParams& newParams);
    
    //==============================================================================
    // Listener management
    void addListener(AudioBufferListener* listener);
    void removeListener(AudioBufferListener* listener);
    
    //==============================================================================
    // Utility
    int getBufferIndex() const { return bufferIndex; }
    juce::String getLoadedFileName() const { return loadedFileName; }
    void setPlayheadSamples(int64_t samples) { playheadPosition.store((double) juce::jmax<int64_t>(0, samples)); }
    // Loop window controls
    void setLoopWindow(int64_t startSamples, int64_t endSamples);
    void clearLoopWindow();
    // Loop window queries
    bool isLoopWindowEnabled() const { return loopWindowEnabled.load(); }
    int64_t getLoopStartSamples() const { return loopStartSamples.load(); }
    int64_t getLoopEndSamples() const { return loopEndSamples.load(); }
    
private:
    //==============================================================================
    // Core audio data
    juce::AudioBuffer<float> audioFileBuffer;
    std::atomic<double> playheadPosition { 0.0 };
    juce::SmoothedValue<double> speedSmoother;
    
    // Parameters
    AudioBufferParams params;
    
    // Buffer identification
    int bufferIndex;
    juce::String loadedFileName;
    
    // State management
    std::atomic<bool> slicingModeActive { false };
    std::atomic<int> targetSlice { 0 };
    std::atomic<int> currentActiveSlice { 0 };
    std::atomic<bool> sliceTriggered { false };
    juce::Random random;
    
    // Crossfading for smooth slice transitions
    int crossfadeLengthSamples = 882;
    bool isInCrossfade = false;
    int crossfadePosition = 0;
    double previousSlicePlayheadPos = 0.0;
    int previousSliceIndex = -1;
    juce::AudioBuffer<float> crossfadeBuffer;
    
    // DSP parameters
    double fileSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    int fileLengthSamples = 0;
    
    // Processing buffers
    juce::AudioBuffer<float> repitchBuffer;
    juce::AudioBuffer<float> tempProcessingBuffer;
    
    // Listeners
    juce::Array<AudioBufferListener*> listeners;
    
    // Previous state for change detection
    bool previousIsPlaying = false;
    int previousSlice = 0;
    double previousPosition = 0.0;
    
    //==============================================================================
    // Internal processing methods
    void processWithRepitching(juce::AudioBuffer<float>& outputBuffer);
    void handleSlicePlayback(double& currentPos);
    void applyCrossfadeToSliceTransition(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    void startSliceCrossfade(int newSliceIndex, double newPlayheadPos);
    void startBoundaryCrossfade(double newPlayheadPos);
    double getSliceStartPosition(int sliceIndex) const;
    double getSliceEndPosition(int sliceIndex) const;

    // Optional per-buffer loop window in absolute samples (file rate). When enabled, playback wraps within [loopStart, loopEnd).
    std::atomic<bool> loopWindowEnabled { false };
    std::atomic<int64_t> loopStartSamples { 0 };
    std::atomic<int64_t> loopEndSamples { 0 };
    
    // Notification helpers
    void notifyPlaybackStateChanged();
    void notifySliceChanged();
    void notifyPositionChanged();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBuffer)
};
