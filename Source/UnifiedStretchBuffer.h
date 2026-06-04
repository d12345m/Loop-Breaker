/*
  ==============================================================================

    UnifiedStretchBuffer.h

    A pull-based WSOLA time-stretch + pitch-shift engine that reads directly
    from a JUCE AudioBuffer<float> source (LoadedAudioData) without maintaining
    a secondary internal audio FIFO.

    Design goals:
    - No internal audio FIFO — every output sample is pulled on-demand from the
      source buffer at the exact position the playhead says.
    - Slice-boundary-aware correlation — the OLA seek window is clamped to the
      active slice region, so the algorithm never reads across a slice boundary
      and cross-slice OLA mismatch is structurally impossible.
    - Drop-in replacement for processWithTimeStretch(): the caller manages the
      playhead; readBlock() advances it in-place.
    - Pitch shift is a post-WSOLA resampling stage (no second SoundTouch instance).

    Algorithm overview (one readBlock() call):
    1. If stretch == 1.0 and pitch == 0: fast-path bulk copy via LagrangeInterpolator.
    2. Otherwise, iterate WSOLA sequences until numSamples output are filled:
       a. Compute nominalSkip = (seekWindowLength - overlapLength) * tempoRatio
       b. Read (seekWindowLength + seekLength) source frames via readSourceFrames()
       c. Find best OLA position via cross-correlation, clamped to [sliceStart, sliceEnd]
       d. OLA-blend with raised-cosine window (pMidBuffer), emit seekWindowLength-2*overlapLength frames
       e. Copy new tail to pMidBuffer; advance playhead by nominalSkip
    3. Pitch stage: if pitchSemiTones != 0, resample WSOLA output via LagrangeInterpolator
       at ratio = pow(2, semitones/12).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstring>
#include <cmath>

//==============================================================================
class UnifiedStretchBuffer
{
public:
    // The source audio data type — mirrors AudioBuffer::LoadedAudioData
    // but we only need buffer + sampleRate, so we accept them directly.

    UnifiedStretchBuffer() = default;
    ~UnifiedStretchBuffer() = default;

    //==========================================================================
    // Setup

    /// Call once (or when host SR / channel count changes) before readBlock().
    void prepare (double hostSampleRate, int numChannels, int maxOutputBlockSize);

    /// Reset internal WSOLA state (overlap buffer, pitch resampler) without
    /// changing parameters.  Call when the playhead jumps discontinuously or
    /// when parameters change enough to require a clean start.
    void reset();

    //==========================================================================
    // Parameters (can be changed between readBlock() calls)

    /// Time stretch ratio: 1.0 = original length, 0.5 = 2× slower, 2.0 = 2× faster.
    void setStretchRatio (double ratio);

    /// Pitch shift in semitones. 0 = no shift.
    void setPitchSemiTones (double semitones);

    /// Speed multiplier applied to the source read rate (not stretch).
    /// Combined with stretch: inputStep = speedMag / stretchRatio.
    void setSpeedMagnitude (double speedMag);

    /// Playback direction: +1.0 forward, -1.0 reverse.
    void setDirection (double dir);

    /// Set the active slice bounds in source samples.  Pass 0 / fileLengthSamples
    /// to span the entire file.  Clamped internally; call before readBlock().
    void setSliceBounds (double startSample, double endSample, bool isLooping);

    /// Remove slice bounds (revert to whole-file).
    void clearSliceBounds (int fileLengthSamples);

    //==========================================================================
    // Query

    int getLatencySamples() const;

    bool isIdentityMode() const; // true when no processing is needed (fast-path)

    //==========================================================================
    // Processing

    /// Pull numSamples frames of output from srcBuffer.
    /// playheadPos is read/written in source-file sample units.
    /// srcBuffer must have at least numChannels channels.
    /// Returns the number of samples actually written (may be < numSamples at
    /// file/slice end when looping is off).
    int readBlock (const juce::AudioBuffer<float>& srcBuffer,
                   double srcSampleRate,
                   double& playheadPos,
                   juce::AudioBuffer<float>& outputBuffer,
                   int numSamples);

private:
    //==========================================================================
    // Configuration
    double sampleRate   = 44100.0;
    int    channels     = 2;
    int    maxBlockSize = 512;

    // Parameters (smoothed on readBlock entry)
    double stretchRatio = 1.0;
    double pitchSemis   = 0.0;
    double speedMag     = 1.0;
    double direction    = 1.0;

    juce::SmoothedValue<double> stretchSmoother;
    juce::SmoothedValue<double> pitchSmoother;
    juce::SmoothedValue<double> speedSmoother;

    // Slice bounds
    double sliceStart   = 0.0;
    double sliceEnd     = 0.0;    // 0 means "end of file"
    bool   sliceBoundsActive = false;
    bool   sliceLooping = true;

    //==========================================================================
    // WSOLA state
    static constexpr int kSequenceMs   = 60;
    static constexpr int kSeekWindowMs = 25;
    static constexpr int kOverlapMs    = 12;

    int sequenceLength  = 0;   // samples
    int seekWindowLength= 0;   // samples
    int seekLength      = 0;   // samples (seek window in samples)
    int overlapLength   = 0;   // samples

    double nominalSkip  = 0.0; // fractional skip per sequence
    double skipFract    = 0.0; // accumulated fractional part of skip

    bool   isBeginning  = true;

    // The "mid buffer" — tail of the last output sequence, used for OLA blending.
    // Interleaved: [ch0_s0, ch1_s0, ch0_s1, ch1_s1, ...]  (matches TDStretch convention)
    std::vector<float> midBuffer;   // size = channels * overlapLength

    // Scratch buffer for reading source frames (interleaved).
    // Size = channels * (seekWindowLength + seekLength)
    std::vector<float> srcScratchInterleaved;

    // WSOLA output carry buffer (non-interleaved, 2 ch max).
    // Data valid in [wsolaOutReadPos, wsolaOutWritePos).
    juce::AudioBuffer<float> wsolaOut;
    int wsolaOutWritePos = 0;
    int wsolaOutReadPos  = 0;

    //==========================================================================
    // Pitch shift state
    std::array<juce::LagrangeInterpolator, 2> pitchResamplers;
    bool pitchResamplersValid = false;

    // Pitch resampling input accumulator (non-interleaved).
    // Filled by WSOLA, consumed by pitch resampler.
    juce::AudioBuffer<float> pitchIn;
    int pitchInWritePos  = 0;
    int pitchInReadPos   = 0;

    //==========================================================================
    // Internal helpers

    void recalcParams();

    // Read srcSamples source frames (non-interleaved) starting at srcPos
    // into srcScratchInterleaved (interleaved).  Handles forward/reverse,
    // loop boundary, and slice clamping.
    // Returns the number of frames read.
    int readSourceFrames (const juce::AudioBuffer<float>& src,
                          int fileLengthSamples,
                          double startPos,
                          int numFrames,
                          int outCh);

    // Read a single source sample at fractional position pos (Lagrange 4-point).
    // Handles boundary (returns 0 at file edges).
    float readSourceSample (const juce::AudioBuffer<float>& src,
                            int ch,
                            double pos,
                            int fileLengthSamples) const noexcept;

    // Cross-correlation of refPos against midBuffer over overlapLength frames (interleaved).
    // Returns the best offset within [0, seekLength).
    // seekStart and seekEnd allow clamping for slice-boundary safety.
    int seekBestOverlapPosition (const float* refPos,
                                 int seekStart,
                                 int seekEnd) const noexcept;

    double calcCrossCorr (const float* refPos, const float* comparePos) const noexcept;

    // OLA-blend midBuffer with the sequence at pInput[offset] into pOutput.
    // pOutput receives overlapLength interleaved frames.
    void overlapAdd (float* pOutput,
                     const float* pInput,
                     int offset) const noexcept;

    // Fill a full WSOLA output block by pulling from srcBuffer.
    // playheadPos is advanced.  Returns frames written to wsolaOut.
    int runWsola (const juce::AudioBuffer<float>& srcBuffer,
                  double srcSampleRate,
                  double& playheadPos,
                  int framesNeeded);

    // Fast-path: identity copy (no stretch, no pitch) with speed scaling only.
    int runFastPath (const juce::AudioBuffer<float>& srcBuffer,
                     double srcSampleRate,
                     double& playheadPos,
                     juce::AudioBuffer<float>& outputBuffer,
                     int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnifiedStretchBuffer)
};
