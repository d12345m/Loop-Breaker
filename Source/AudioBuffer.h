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

#include "TimeStretchSoundTouch.h"

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
    struct LoadedAudioData : public juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<LoadedAudioData>;

        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        juce::String fileName;
    };

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

    // Swap in already-decoded audio data (safe for audio thread; no disk I/O)
    void setLoadedAudioData(LoadedAudioData::Ptr newData);
    
    //==============================================================================
    // Transport controls
    void play();
    void stop();
    void pause();
    void setPlaying(bool shouldPlay);
    void setSpeed(double newSpeed);
    // Multiplies the base speed (params.speed). Used for host-tempo-following behavior.
    void setTempoMultiplier(double multiplier) { tempoMultiplier.store(multiplier); }
    double getTempoMultiplier() const { return tempoMultiplier.load(); }
    void setLooping(bool shouldLoop);
    void resetToBeginning();
    void resetToDefaults();

    // Time-stretch (tempo only, no pitch). 1.0 = normal.
    void setStretchRatio(double ratio);
    double getStretchRatio() const { return stretchRatio.load(); }

    // Pitch shift (semitones). 0 = no shift. Implemented via SoundTouch when non-zero.
    void setPitchSemiTones(double semiTones);
    double getPitchSemiTones() const { return pitchSemiTones.load(); }
    
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
    bool hasAudioLoaded() const;
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
    int getDurationInSamples() const;
    double getFileSampleRate() const;
    
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
    juce::String getLoadedFileName() const;
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
    LoadedAudioData::Ptr audioData;
    mutable juce::SpinLock audioDataLock;
    std::atomic<double> playheadPosition { 0.0 };
    juce::SmoothedValue<double> speedSmoother;
    juce::SmoothedValue<double> stretchSmoother;

    std::atomic<double> tempoMultiplier { 1.0 };
    std::atomic<double> stretchRatio { 1.0 }; // 1.0 = normal; <1 slower/longer; >1 faster/shorter
    std::atomic<double> pitchSemiTones { 0.0 }; // pitch shift in semitones; 0 = none

    double getEffectiveSpeed() const { return params.speed * tempoMultiplier.load(); }

    // Coordinated snapshot of stretch-related parameters.
    // Read once at the top of processBlock so the mode decision and all parameter
    // values are consistent within a single audio callback.
    struct StretchSnapshot
    {
        double speed        = 1.0;   // params.speed
        double stretchRatio = 1.0;
        double pitchSemis   = 0.0;
        double tempoMult    = 1.0;

        bool useStretch  () const { return std::abs(stretchRatio - 1.0) > 1.0e-6; }
        bool usePitch    () const { return std::abs(pitchSemis)        > 1.0e-6; }
        bool useStretcher() const { return useStretch() || usePitch(); }
        double direction () const { return speed < 0.0 ? -1.0 : 1.0; }
        double speedMag  () const { return std::abs(speed); }
    };

    StretchSnapshot takeStretchSnapshot() const
    {
        StretchSnapshot s;
        s.speed        = params.speed;
        s.stretchRatio = stretchRatio.load();
        s.pitchSemis   = pitchSemiTones.load();
        s.tempoMult    = tempoMultiplier.load();
        return s;
    }

    // Time-stretch engine + scratch buffers (used when stretchRatio != 1.0)
    TimeStretchSoundTouch stretcher;
    bool stretcherPrepared = false;
    bool stretcherPrimed = false;
    double stretcherPreparedSampleRate = 0.0;
    int stretcherPreparedChannels = 0;
    juce::AudioBuffer<float> stretchInScratch;
    juce::AudioBuffer<float> stretchOutScratch;
    juce::AudioBuffer<float> stretchInterleavedIn;
    juce::AudioBuffer<float> stretchInterleavedOut;
    std::atomic<int> timeStretchUnderfills { 0 };
    int stretchFadeInRemaining = 0; // samples of fade-in left to apply on first stretch block
    std::atomic<bool> stretcherNeedsReset { false }; // deferred reset flag for thread safety
    bool lastBlockUsedStretch = false; // track mode transitions between repitch/stretch
    
    // Parameters
    AudioBufferParams params;
    
    // Buffer identification
    int bufferIndex;
    
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
    double hostSampleRate = 44100.0;
    
    // Processing buffers
    juce::AudioBuffer<float> repitchBuffer;
    juce::AudioBuffer<float> tempProcessingBuffer;
    // Previous block cache for mode-transition crossfades
    juce::AudioBuffer<float> previousBlockBuffer;
    int previousBlockNumSamples = 0;
    bool previousBlockValid = false;
    bool resetCrossfadePending = false;
    
    // Listeners
    juce::Array<AudioBufferListener*> listeners;
    
    // Previous state for change detection
    bool previousIsPlaying = false;
    int previousSlice = 0;
    double previousPosition = 0.0;
    
    //==============================================================================
    // Internal processing methods
    void processWithRepitching(juce::AudioBuffer<float>& outputBuffer);
    void processWithTimeStretch(juce::AudioBuffer<float>& outputBuffer, const StretchSnapshot& snap);
    void handleSlicePlayback(double& currentPos, int fileLengthSamples);
    void applyCrossfadeToSliceTransition(const juce::AudioBuffer<float>& sourceBuffer,
                                         int fileLengthSamples,
                                         double fileSampleRate,
                                         juce::AudioBuffer<float>& outputBuffer,
                                         int startSample,
                                         int numSamples);
    void startSliceCrossfade(int newSliceIndex, double newPlayheadPos);
    void startBoundaryCrossfade(double newPlayheadPos);
    double getSliceStartPosition(int sliceIndex, int fileLengthSamples) const;
    double getSliceEndPosition(int sliceIndex, int fileLengthSamples) const;

    LoadedAudioData::Ptr getAudioDataSnapshot() const;

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
