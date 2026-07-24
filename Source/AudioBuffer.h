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

#include "LockFreeBoundedQueue.h"
#include "TimeStretchSoundTouch.h"
#include "StretchQueueController.h"

//==============================================================================
/**
    Interface for receiving callbacks from AudioBuffer events
*/
class AudioBufferListener
{
public:
    virtual ~AudioBufferListener() = default;
    
    virtual void audioBufferPlaybackStarted (int) {}
    virtual void audioBufferPlaybackStopped (int) {}
    virtual void audioBufferSliceChanged (int, int) {}
    virtual void audioBufferPositionChanged (int, double) {}
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
    // Arp slice state
    bool arpSliceActive = false;
    bool arpSliceRepeaterMode = false;      // when true, refresh picks one slice and repeats it
    std::vector<int> arpSequence;           // fixed sequence of slice indices
    int arpSequencePos = 0;                  // current position within the sequence
    int arpRepeatBars = 2;                   // bars before picking a new sequence
    int arpCycleCount = 0;                   // how many full sequence cycles completed
    int arpTotalCyclesPerRefresh = 0;        // total cycles before refresh (derived from bars)
    
    // Reset to default values
    void reset()
    {
        speed = 1.0;
        isLooping = true;
        isPlaying = false;
        numSlices = 1;
        continuousRandomSlicing = false;
        crossfadeLengthMs = 20.0;
        arpSliceActive = false;
        arpSliceRepeaterMode = false;
        arpSequence.clear();
        arpSequencePos = 0;
        arpRepeatBars = 2;
        arpCycleCount = 0;
        arpTotalCyclesPerRefresh = 0;
    }
};

struct TimeStretchQueueTelemetry
{
    int readyOutputFrames = 0;
    int unprocessedInputFrames = 0;
    int targetOutputReserve = 0;
    int highOutputWatermark = 0;
    double outputPerInputRatio = 1.0;
    double estimatedOutputCreditFrames = 0.0;
    std::uint64_t totalInputFramesFed = 0;
    std::uint64_t totalOutputFramesReceived = 0;
    std::uint64_t totalUnderfills = 0;
};

enum class TransportTransitionReason : std::uint8_t
{
    UserSeek = 0,
    UserRestart,
    PartSwitch,
    PartLoad,
    PresetRecall,
    Reset,
    HostRestart,
    StartOffset,
    SliceTrigger,
    LoopWindowOnly
};

struct TransportTransitionTelemetry
{
    std::uint64_t lastIssuedGeneration = 0;
    std::uint64_t lastAcknowledgedGeneration = 0;
    std::uint64_t lastAppliedGeneration = 0;
    std::uint64_t droppedCommands = 0;
    TransportTransitionReason lastAppliedReason = TransportTransitionReason::UserSeek;
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
    
    // §12 Lookahead pre-priming counters
    std::atomic<int> lookaheadPreCrossfades { 0 };   // Successful pre-crossfades completed
    std::atomic<int> lookaheadMispredictions { 0 };  // Prediction didn't match actual jump
    std::atomic<int> lookaheadAborts { 0 };          // Aborted due to external trigger/speed change
    
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
        lookaheadPreCrossfades.store(0);
        lookaheadMispredictions.store(0);
        lookaheadAborts.store(0);
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
             + " dcDrift=" + juce::String(dcOffsetDrifts.load())
             + " laPreXF=" + juce::String(lookaheadPreCrossfades.load())
             + " laMispredict=" + juce::String(lookaheadMispredictions.load())
             + " laAbort=" + juce::String(lookaheadAborts.load());
    }
    
    int getTotalEvents() const
    {
        return emptyOutputBuffers.load() + partialUnderfills.load() + discontinuities.load()
             + mediumDiscontinuities.load() + minorDiscontinuities.load()
             + directionFlips.load() + sliceJumps.load() + modeTransitions.load()
             + soundTouchResets.load() + zeroSampleRuns.load() + clippedSamples.load()
             + nanOrInfSamples.load() + rmsJumps.load() + dcOffsetDrifts.load()
             + lookaheadPreCrossfades.load() + lookaheadMispredictions.load() + lookaheadAborts.load();
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
    void resetToBeginning(TransportTransitionReason reason = TransportTransitionReason::UserRestart);
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

    // Arp slice: play a fixed sequence of slices, refreshing after N bars
    void startArpSlicing(int totalSlices, int sequenceLength, int repeatBars);
    void stopArpSlicing();
    bool isInArpSliceMode() const { return params.arpSliceActive; }
    bool isInSliceRepeaterMode() const { return params.arpSliceActive && params.arpSliceRepeaterMode; }
    int  getArpSequenceLength() const { return (int) params.arpSequence.size(); }
    int  getArpRepeatBars() const { return params.arpRepeatBars; }
    int  getArpTotalSlices() const { return params.numSlices; }

    // Slice repeater: play one random slice N times, then pick another random slice
    void startSliceRepeater(int totalSlices, int repetitions);
    
    //==============================================================================
    // State queries
    bool hasAudioLoaded() const;
    bool isPlaying() const { return params.isPlaying; }
    bool isLooping() const { return params.isLooping; }
    double getSpeed() const { return atomicSpeed.load(std::memory_order_relaxed); }
    int getCurrentSlice() const;
    int getNumSlices() const { return params.numSlices; }
    bool isInContinuousRandomMode() const { return params.continuousRandomSlicing; }
    bool isInSlicingMode() const { return slicingModeActive.load(); }
#if JUCE_UNIT_TESTS
    void setRandomSeedForTesting(juce::int64 seed) noexcept
    {
        random.setSeed(seed);
    }
    bool isLookaheadTransitionActiveForTesting() const noexcept
    {
        return isInLookaheadCrossfade;
    }
    bool isLookaheadCancellationActiveForTesting() const noexcept
    {
        return lookaheadCrossfadeCancelling;
    }
#endif
    
    //==============================================================================
    // Debug statistics (only meaningful in debug builds)
    const TearingDebugStats& getTearingStats() const { return tearingStats; }
    void resetTearingStats() { tearingStats.reset(); }
    void setTearingDebugEnabled(bool enabled) { tearingDebugEnabled.store(enabled); }
    bool isTearingDebugEnabled() const { return tearingDebugEnabled.load(); }
    TimeStretchQueueTelemetry getTimeStretchQueueTelemetry() const;
    TransportTransitionTelemetry getTransportTransitionTelemetry() const;
    
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
    std::uint64_t requestPlayheadTransition(
        int64_t samples,
        TransportTransitionReason reason = TransportTransitionReason::UserSeek);
    std::uint64_t requestLoopAndPlayheadTransition(
        int64_t loopStart,
        int64_t loopEnd,
        int64_t targetSample,
        TransportTransitionReason reason);

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

    enum TransportCommandFlags : std::uint8_t
    {
        updatePositionFlag = 1u << 0,
        updateLoopWindowFlag = 1u << 1,
        triggerSliceFlag = 1u << 2,
        resetSlicingFlag = 1u << 3
    };

    struct TransportCommand
    {
        std::uint64_t generation = 0;
        std::uint64_t audioEpoch = 0;
        int64_t targetSample = 0;
        int64_t loopStart = 0;
        int64_t loopEnd = 0;
        std::int32_t sliceIndex = 0;
        TransportTransitionReason reason = TransportTransitionReason::UserSeek;
        std::uint8_t flags = 0;
        std::uint8_t loopEnabled = 0;
    };

    static constexpr std::size_t transportQueueCapacity = 128;
    LockFreeBoundedQueue<TransportCommand, transportQueueCapacity> transportCommands;
    std::atomic<std::uint64_t> nextTransportGeneration { 1 };
    std::atomic<std::uint64_t> audioDataEpoch { 1 };
    std::atomic<std::uint64_t> lastAcknowledgedTransportGeneration { 0 };
    std::atomic<std::uint64_t> lastAppliedTransportGeneration { 0 };
    std::atomic<std::uint64_t> droppedTransportCommands { 0 };
    std::atomic<std::uint8_t> lastAppliedTransportReason {
        static_cast<std::uint8_t> (TransportTransitionReason::UserSeek)
    };

    juce::SmoothedValue<double> speedSmoother;
    juce::SmoothedValue<double> stretchSmoother;

    std::atomic<double> tempoMultiplier { 1.0 };
    std::atomic<double> stretchRatio { 1.0 }; // 1.0 = normal; <1 slower/longer; >1 faster/shorter
    std::atomic<double> pitchSemiTones { 0.0 }; // pitch shift in semitones; 0 = none

    // Atomic mirror of params.speed.  params.speed is written by the message
    // thread (setSpeed) and by the audio thread (ping-pong flips); readers on
    // the audio thread (StretchSnapshot, getEffectiveSpeed) read this mirror
    // so they can never observe a torn double.  Writers must update BOTH.
    std::atomic<double> atomicSpeed { 1.0 };

    double getEffectiveSpeed() const { return atomicSpeed.load(std::memory_order_relaxed) * tempoMultiplier.load(); }

    // T4: Smoother for speed magnitude when routed through SoundTouch rate/tempo.
    juce::SmoothedValue<double> speedMagSmoother;

    // Smoother for pitch semitones so that adding/stacking pitch shift doesn't
    // cause an abrupt jump in the tempo-compensation ratio sent to SoundTouch.
    juce::SmoothedValue<double> pitchSemiSmoother;

    // T8: Track the last direction used so we can detect direction flips and crossfade.
    double lastStretchDirection = 1.0;
    
    // Tearing debug
    TearingDebugStats tearingStats;
   #if JUCE_IOS
    // Real-time DBG output from several pad workers can itself cause dropouts
    // on a device. The debug panel can still enable these probes explicitly.
    std::atomic<bool> tearingDebugEnabled { false };
   #else
    std::atomic<bool> tearingDebugEnabled { true };
   #endif
   #if JUCE_DEBUG
    float lastOutputSample[2] = { 0.0f, 0.0f };     // Track last sample for discontinuity detection
    int consecutiveZeroSamples = 0;                  // Counter for zero sample runs
    float lastBlockRms[2] = { 0.0f, 0.0f };         // Track RMS from previous block
    float lastBlockDcOffset[2] = { 0.0f, 0.0f };    // Track DC offset from previous block
   #endif
    int rmsBlankingBlocksLeft = 0;                    // Suppress RMS jump checks after slice transitions



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
        s.speed        = atomicSpeed.load(std::memory_order_relaxed); // torn-read-safe mirror of params.speed
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
    int stretchFadeInTotal = 0;     // total samples, retained so gain is global across callbacks
    std::atomic<bool> stretcherNeedsReset { false }; // deferred reset flag for thread safety
    bool lastBlockUsedStretch = false; // track mode transitions between repitch/stretch
    StretchQueueController stretchQueueController;
    std::atomic<int> stretchQueueReadyOutput { 0 };
    std::atomic<int> stretchQueueUnprocessedInput { 0 };
    std::atomic<int> stretchQueueTargetReserve { 0 };
    std::atomic<int> stretchQueueHighWatermark { 0 };
    std::atomic<double> stretchQueueOutputPerInput { 1.0 };
    std::atomic<double> stretchQueueOutputCredit { 0.0 };
    std::atomic<std::uint64_t> stretchQueueInputFramesFed { 0 };
    std::atomic<std::uint64_t> stretchQueueOutputFramesReceived { 0 };
    void resetStretchQueueState();
    void publishStretchQueueTelemetry (int targetOutputReserve,
                                       int highOutputWatermark);

    // Set by processWithTimeStretch when a live pipeline reset occurred. The
    // enclosing processBlock reconnects the new output to the exact last sample.
    bool stretcherResetThisBlock = false;

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
    // BUGFIX (stacked modifiers): each crossfade captures its length at start so
    // that in-flight crossfades are immune to crossfadeLengthSamples changing
    // between blocks (the stretch path used to temporarily mutate the member,
    // causing fade-gain jumps for crossfades spanning block boundaries).
    int activeCrossfadeLen = 882;
    bool isInCrossfade = false;
    int crossfadePosition = 0;
    double previousSlicePlayheadPos = 0.0;
    int previousSliceIndex = -1;
    juce::AudioBuffer<float> crossfadeBuffer;
    // Direction of the old branch. Direction pivots use ±1 so both branches
    // move away from the pivot; source boundaries use 0 to hold the last sample
    // that was actually rendered.
    double crossfadeOldDirection = 1.0;
    // When a §12 lookahead pre-crossfade correctly predicts the slice jump, the
    // input signal at the boundary already contains some new-slice audio. The
    // post-jump crossfade must continue from the actual terminal mix rather than
    // restarting at 0% old-slice audio.
    bool crossfadeIsLookaheadContinuation = false;
    float crossfadeInitialNewMix = 0.0f;

    // §12 Lookahead pre-priming: gradually blend next-slice audio into
    // SoundTouch's input *before* the slice boundary, so the OLA algorithm
    // sees a smooth spectral transition instead of a hard discontinuity.
    int   lookaheadNextSlice = -1;              // pre-computed next slice index (-1 = unknown)
    bool  isInLookaheadCrossfade = false;        // currently in pre-boundary crossfade
    int   lookaheadCrossfadePosition = 0;        // progress within lookahead crossfade
    int   lookaheadCrossfadeSamples = 0;         // total length of lookahead crossfade
    double lookaheadNextSliceReadPos = 0.0;       // read position in the next slice (advances with step)
    float lookaheadTerminalBlend = 0.0f;          // actual new-branch mix at the boundary
    bool  lookaheadCrossfadeCancelling = false;   // unwind after slicing is disabled
    float lookaheadCancellationInitialBlend = 0.0f;
    int   precomputedNextRandomSlice = -1;       // pre-rolled RNG for continuous random mode
    
    // DSP parameters
    double hostSampleRate = 44100.0;
    
    // Processing buffers
    juce::AudioBuffer<float> repitchBuffer;
    juce::AudioBuffer<float> tempProcessingBuffer;
    bool previousBlockValid = false;
    bool renderPositionHistoryValid = false;
    float lastRenderedOutputSample[2] = { 0.0f, 0.0f };
    float outputCorrectionAnchor[2] = { 0.0f, 0.0f };
    float outputCorrectionOffset[2] = { 0.0f, 0.0f };
    bool outputCorrectionPending = false;
    bool outputCorrectionActive = false;
    int outputCorrectionPosition = 0;
    int outputCorrectionLength = 0;
    bool underfillRecoveryPending = false;
    bool stretcherUnderfilledThisBlock = false;
    
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
    std::uint64_t enqueueTransportCommand(TransportCommand command) noexcept;
    void consumeTransportCommands(int fileLengthSamples);
    void publishLoopWindowState(bool enabled, int64_t startSamples, int64_t endSamples) noexcept;
    void resetRenderPositionStateDirect() noexcept;
    void armOutputEndpointCorrection(int fadeLength) noexcept;
    void applyOutputEndpointCorrection(juce::AudioBuffer<float>& outputBuffer);
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

    // §12 Lookahead pre-priming helpers
    int getNextSliceIndex(int currentSlice, int fileLengthSamples) const;
    double getDistanceToSliceBoundary(double currentPos, int currentSlice, int fileLengthSamples, double effectiveSpeed) const;
    void resetLookaheadState();

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
