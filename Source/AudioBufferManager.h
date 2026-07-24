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

struct ClipDetectorSystem;  // Forward declaration
struct PadProbeSet;         // Forward declaration
enum class NodeId;          // Forward declaration

//==============================================================================
/**
    Manager class for handling multiple AudioBuffer instances.
    Designed for use in sampler applications that need multiple simultaneous buffers.
*/
class AudioBufferManager : public AudioBufferListener
{
public:
    enum class AudioLoadIntent
    {
        UserReplacement,
        SessionRestore,
        MissingDataRecovery
    };

    static constexpr int MAX_BUFFERS = 8; // Match the MPC-style interface from DesignDoc
    
    AudioBufferManager();
    ~AudioBufferManager() override = default;
    
    //==============================================================================
    // Setup and lifecycle
    void prepare(double sampleRate, int samplesPerBlockExpected);
    void processBlock(juce::AudioBuffer<float>& outputBuffer);
    void processSingleBuffer(int bufferIndex, juce::AudioBuffer<float>& outputBuffer);
    void releaseResources();
    
    //==============================================================================
    
      // Optional per-buffer processing hook (e.g., FX). Called after each buffer renders into tempBuffer, before mix.
      void setPerBufferProcessor(std::function<void(int /*bufferIndex*/, juce::AudioBuffer<float>& /*tempBuffer*/, double /*sampleRate*/)> fn)
      {
        perBufferProcessor = std::move(fn);
      }

    // Clip detection system (optional, set from AppState)
    void setClipDetector(ClipDetectorSystem* detector) { clipDetector = detector; }
    // Buffer management
    AudioBuffer* getBuffer(int bufferIndex);
    const AudioBuffer* getBuffer(int bufferIndex) const;
    bool isValidBufferIndex(int bufferIndex) const;
    
    //==============================================================================
    // File loading
    bool loadAudioFile(int bufferIndex, const juce::File& file, juce::AudioFormatManager& formatManager);
    bool requestLoadAudioFile(int bufferIndex, const juce::File& file,
                              AudioLoadIntent intent = AudioLoadIntent::UserReplacement);
    // Applies completed background decode jobs.  When transportPlaying is true,
    // newly loaded buffers are flagged for musically-deferred start (they will
    // not begin playing until the next bar boundary or modifier trigger).
    // Returns the indices of buffers that received new audio data.
    juce::Array<int> applyPendingLoads(bool transportPlaying = false);
    void clearBuffer(int bufferIndex);

    /// Start any buffers that were loaded mid-transport and are waiting for a
    /// musically relevant cue (bar crossing or modifier trigger) to begin.
    void startBuffersAwaitingMusicalCue();

    /// Returns true if any buffer is flagged for musically-deferred start.
    bool hasBuffersAwaitingMusicalCue() const;
    void clearAllBuffers();
    
    //==============================================================================
    // Global controls
    void playAll();
    void stopAll();
    void resetAllBuffers();
    void restartAllLoadedBuffersToBeginning();
    // Scale playback speed for all buffers without overwriting their base speed.
    void setTempoMultiplier(double multiplier);
  // Parts: set playback start offset for all buffers (in samples)
  void setStartOffsetSamples(int64_t startOffsetSamples);
  // Parts: set end offset (absolute samples from start of file); 0 disables
  void setEndOffsetSamples(int64_t endOffsetSamples);
    void setMasterVolume(float volume) { masterVolume.store(volume); }
    float getMasterVolume() const { return masterVolume.load(); }
  double getHostSampleRate() const { return hostSampleRate; }
    
    //==============================================================================
    // Buffer queries
    int getNumLoadedBuffers() const;
    juce::Array<int> getLoadedBufferIndices() const;
    juce::Array<int> getPlayingBufferIndices() const;
    
    /// Returns true if background decode jobs are in-flight or pending application.
    bool hasAnyPendingLoads() const
    {
        return pendingFifo.getNumReady() > 0 || loaderPool.getNumJobs() > 0;
    }
    
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
      std::function<void(int, juce::AudioBuffer<float>&, double)> perBufferProcessor;
    ClipDetectorSystem* clipDetector = nullptr;  // not owned
    std::atomic<float> masterVolume { 1.0f };
    
    // Audio processing
    juce::AudioBuffer<float> mixBuffer;
    juce::AudioBuffer<float> tempBuffer;
  double hostSampleRate = 44100.0;
  std::atomic<double> resampleTargetRate { 0.0 }; // §4.2 target SR for load-time resampling
  int64_t globalStartOffsetSamples = 0; // applied at play() time
  int64_t globalEndOffsetSamples = 0;   // enforced during processing; 0 = disabled

    //==============================================================================
    // Background loading (no disk I/O on realtime thread)
    struct PendingLoadedBuffer
    {
      int bufferIndex = -1;
      AudioBuffer::LoadedAudioData::Ptr data;
      juce::String sourcePath;
      uint64_t generation = 0;
      AudioLoadIntent intent = AudioLoadIntent::UserReplacement;
      bool ok = false;
    };

    static constexpr int maxPendingLoads = 32;
    juce::ThreadPool loaderPool { 1 };
    juce::AbstractFifo pendingFifo { maxPendingLoads };
    std::array<PendingLoadedBuffer, (size_t) maxPendingLoads> pendingLoads;
    std::array<std::atomic<uint64_t>, MAX_BUFFERS> latestLoadGeneration {};

    void enqueuePendingLoad(PendingLoadedBuffer&& p);
    
    // Listeners
    juce::Array<AudioBufferListener*> listeners;
    
    //==============================================================================
    // Internal helpers
    void notifyListeners(std::function<void(AudioBufferListener*)> notification);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBufferManager)
};
