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
    Debug statistics for tracking audio tearing and buffer issues.
    Only active in JUCE_DEBUG builds.
*/
struct TearingDebugStats
{
    // Counts of detected tearing conditions
    std::atomic<int> emptyOutputBuffers { 0 };       // Output buffer was completely empty
    std::atomic<int> partialUnderfills { 0 };        // SoundTouch didn't fill entire buffer
    std::atomic<int> discontinuities { 0 };          // Large sample value jumps detected (>0.3)
    std::atomic<int> mediumDiscontinuities { 0 };    // Medium sample value jumps (0.15-0.3)
    std::atomic<int> minorDiscontinuities { 0 };     // Minor sample value jumps (0.05-0.15)
    std::atomic<int> directionFlips { 0 };           // Playback direction changes
    std::atomic<int> sliceJumps { 0 };               // Slice-triggered position jumps
    std::atomic<int> modeTransitions { 0 };          // Repitch <-> stretch transitions
    std::atomic<int> soundTouchResets { 0 };         // SoundTouch pipeline resets
    std::atomic<int> zeroSampleRuns { 0 };           // Consecutive zero samples detected
    std::atomic<int> clippedSamples { 0 };           // Samples that exceeded +/-1.0
    std::atomic<int> nanOrInfSamples { 0 };          // NaN or Inf samples detected
    std::atomic<int> rmsJumps { 0 };                 // Sudden RMS level changes between blocks
    std::atomic<int> dcOffsetDrifts { 0 };           // DC offset drifting beyond threshold
    
    // Timing info
    std::atomic<double> lastTearingEventTime { 0.0 };
    std::atomic<double> lastPlayheadPos { 0.0 };
    
    void reset()
    {
        emptyOutputBuffers.store(0);
        partialUnderfills.store(0);
        discontinuities.store(0);
        mediumDiscontinuities.store(0);
        minorDiscontinuities.store(0);
        directionFlips.store(0);
        sliceJumps.store(0);
        modeTransitions.store(0);
        soundTouchResets.store(0);
        zeroSampleRuns.store(0);
        clippedSamples.store(0);
        nanOrInfSamples.store(0);
        rmsJumps.store(0);
        dcOffsetDrifts.store(0);
        lastTearingEventTime.store(0.0);
        lastPlayheadPos.store(0.0);
    }
    
    juce::String getSummary() const
    {
        return "TearingStats: empty=" + juce::String(emptyOutputBuffers.load())
             + " underfill=" + juce::String(partialUnderfills.load())
             + " discont=" + juce::String(discontinuities.load())
             + " medium=" + juce::String(mediumDiscontinuities.load())
             + " minor=" + juce::String(minorDiscontinuities.load())
             + " dirFlip=" + juce::String(directionFlips.load())
             + " sliceJump=" + juce::String(sliceJumps.load())
             + " modeTrans=" + juce::String(modeTransitions.load())
             + " stReset=" + juce::String(soundTouchResets.load())
             + " zeroRuns=" + juce::String(zeroSampleRuns.load())
             + " clipped=" + juce::String(clippedSamples.load())
             + " nanInf=" + juce::String(nanOrInfSamples.load())
             + " rmsJump=" + juce::String(rmsJumps.load())
             + " dcDrift=" + juce::String(dcOffsetDrifts.load());
    }
    
    int getTotalEvents() const
    {
        return emptyOutputBuffers.load() + partialUnderfills.load() + discontinuities.load()
             + mediumDiscontinuities.load() + minorDiscontinuities.load()
             + directionFlips.load() + sliceJumps.load() + modeTransitions.load()
             + soundTouchResets.load() + zeroSampleRuns.load() + clippedSamples.load()
             + nanOrInfSamples.load() + rmsJumps.load() + dcOffsetDrifts.load();
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
    // Debug statistics (only meaningful in debug builds)
    const TearingDebugStats& getTearingStats() const { return tearingStats; }
    void resetTearingStats() { tearingStats.reset(); }
    void setTearingDebugEnabled(bool enabled) { tearingDebugEnabled.store(enabled); }
    bool isTearingDebugEnabled() const { return tearingDebugEnabled.load(); }
    
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

    // §4.2  Musically-deferred start: when true, this buffer has freshly loaded
    // data but should not begin playing until the next bar boundary or modifier
    // trigger.
    bool isAwaitingMusicalStart() const { return awaitingMusicalStart.load(); }
    void setAwaitingMusicalStart(bool v)  { awaitingMusicalStart.store(v); }
    // Loop window controls
    void setLoopWindow(int64_t startSamples, int64_t endSamples);
    void clearLoopWindow();
    // Loop window queries
    bool isLoopWindowEnabled() const { return loopWindowEnabled.load(); }
    int64_t getLoopStartSamples() const { return loopStartSamples.load(); }
    int64_t getLoopEndSamples() const { return loopEndSamples.load(); }
    
    // Ping pong playback controls
    void setPingPongMode(bool enabled, double divisionBars = 0.25, double bpm = 120.0, double hostSampleRate = 44100.0);
    bool isPingPongModeEnabled() const { return pingPongEnabled.load(); }
    double getPingPongDivision() const { return pingPongDivision.load(); }
    
private:
    //==============================================================================
    // Core audio data — lock-free access via atomic raw pointer.
    // The retainer Ptrs prevent premature destruction: any reader that has
    // atomically loaded the raw pointer but hasn't yet bumped the ref count
    // is safe because the retainer keeps the object alive until the *next*
    // write (which installs a new retainer and rotates the old one into
    // previousAudioDataRetainer).
    std::atomic<LoadedAudioData*> atomicAudioData { nullptr };
    LoadedAudioData::Ptr audioDataRetainer;            // keeps current data alive
    LoadedAudioData::Ptr previousAudioDataRetainer;    // keeps previous data alive until next swap
    std::atomic<double> playheadPosition { 0.0 };
    juce::SmoothedValue<double> speedSmoother;
    juce::SmoothedValue<double> stretchSmoother;

    std::atomic<double> tempoMultiplier { 1.0 };
    std::atomic<double> stretchRatio { 1.0 }; // 1.0 = normal; <1 slower/longer; >1 faster/shorter
    std::atomic<double> pitchSemiTones { 0.0 }; // pitch shift in semitones; 0 = none

    double getEffectiveSpeed() const { return params.speed * tempoMultiplier.load(); }

    // T4: Smoother for speed magnitude when routed through SoundTouch rate/tempo.
    juce::SmoothedValue<double> speedMagSmoother;

    // Smoother for pitch semitones so that adding/stacking pitch shift doesn't
    // cause an abrupt jump in the tempo-compensation ratio sent to SoundTouch.
    juce::SmoothedValue<double> pitchSemiSmoother;

    // T8: Track the last direction used so we can detect direction flips and crossfade.
    double lastStretchDirection = 1.0;
    
    // Tearing debug
    TearingDebugStats tearingStats;
    std::atomic<bool> tearingDebugEnabled { true }; // Enable by default in debug builds
    float lastOutputSample[2] = { 0.0f, 0.0f };     // Track last sample for discontinuity detection
    int consecutiveZeroSamples = 0;                  // Counter for zero sample runs
    float lastBlockRms[2] = { 0.0f, 0.0f };         // Track RMS from previous block
    float lastBlockDcOffset[2] = { 0.0f, 0.0f };    // Track DC offset from previous block



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

private:
    // §4.2  Deferred musical start flag.
    std::atomic<bool> awaitingMusicalStart { false };

    // fillInputScratch fast path.  One interpolator per channel (stereo max).
    // These maintain internal filter state between consecutive calls so that
    // the output is continuous across feed-loop iterations.
    juce::LagrangeInterpolator blockResamplers[2];
    double blockResamplerExpectedPos = 0.0;
    bool   blockResamplersValid = false;
    
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
    
    // Ping pong playback mode: oscillates forward/backward at musical note divisions
    std::atomic<bool> pingPongEnabled { false };
    std::atomic<double> pingPongDivision { 0.25 };  // Musical division in bars (1.0=whole, 0.5=half, 0.25=quarter, etc.)
    std::atomic<double> pingPongPeriodSamples { 0.0 }; // Period in samples for one direction
    std::atomic<double> pingPongPhasePosition { 0.0 }; // Position within current ping pong cycle
    std::atomic<bool> pingPongGoingForward { true };
    
    // Notification helpers
    void notifyPlaybackStateChanged();
    void notifySliceChanged();
    void notifyPositionChanged();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBuffer)
};
