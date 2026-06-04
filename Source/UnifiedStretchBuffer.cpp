/*
  ==============================================================================

    UnifiedStretchBuffer.cpp

    See UnifiedStretchBuffer.h for design notes.

  ==============================================================================
*/

#include "UnifiedStretchBuffer.h"
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>

//==============================================================================
// prepare
//==============================================================================

void UnifiedStretchBuffer::prepare (double hostSampleRate, int numCh, int maxOutputBlockSize)
{
    sampleRate   = hostSampleRate;
    channels     = juce::jlimit (1, 2, numCh);
    maxBlockSize = maxOutputBlockSize;

    stretchSmoother.reset (sampleRate, 0.05);
    stretchSmoother.setCurrentAndTargetValue (stretchRatio);
    pitchSmoother.reset (sampleRate, 0.05);
    pitchSmoother.setCurrentAndTargetValue (pitchSemis);
    speedSmoother.reset (sampleRate, 0.05);
    speedSmoother.setCurrentAndTargetValue (speedMag);

    recalcParams();

    // WSOLA mid buffer
    midBuffer.assign ((size_t)(channels * overlapLength), 0.0f);

    // Source scratch: seekLength + sequenceLength interleaved frames
    const int srcScratchFrames = seekLength + sequenceLength + 16;
    srcScratchInterleaved.assign ((size_t)(channels * srcScratchFrames), 0.0f);

    // WSOLA carry buffer: holds ~3 full sequences so there is always headroom
    const int carrySize = sequenceLength * 3 + maxOutputBlockSize + 256;
    wsolaOut.setSize (channels, carrySize, false, true, true);
    wsolaOutWritePos = 0;
    wsolaOutReadPos  = 0;

    // Pitch resampler input buffer — sized for worst-case pitch-down (need more input than output)
    const int pitchInSize = maxOutputBlockSize * 4 + 64;
    pitchIn.setSize (channels, pitchInSize, false, true, true);
    pitchInWritePos  = 0;
    pitchInReadPos   = 0;

    for (auto& r : pitchResamplers)
        r.reset();
    pitchResamplersValid = false;

    isBeginning = true;
    skipFract   = 0.0;
}

//==============================================================================
// reset
//==============================================================================

void UnifiedStretchBuffer::reset()
{
    if (midBuffer.size() > 0)
        std::fill (midBuffer.begin(), midBuffer.end(), 0.0f);

    wsolaOutWritePos     = 0;
    wsolaOutReadPos      = 0;
    pitchInWritePos      = 0;
    pitchInReadPos       = 0;
    isBeginning          = true;
    skipFract            = 0.0;

    for (auto& r : pitchResamplers)
        r.reset();
    pitchResamplersValid = false;

    stretchSmoother.setCurrentAndTargetValue (stretchRatio);
    pitchSmoother.setCurrentAndTargetValue (pitchSemis);
    speedSmoother.setCurrentAndTargetValue (speedMag);
}

//==============================================================================
// Parameter setters
//==============================================================================

void UnifiedStretchBuffer::setStretchRatio (double ratio)
{
    stretchRatio = juce::jlimit (0.1, 8.0, ratio);
    stretchSmoother.setTargetValue (stretchRatio);
}

void UnifiedStretchBuffer::setPitchSemiTones (double semitones)
{
    pitchSemis = juce::jlimit (-24.0, 24.0, semitones);
    pitchSmoother.setTargetValue (pitchSemis);
}

void UnifiedStretchBuffer::setSpeedMagnitude (double mag)
{
    speedMag = juce::jlimit (0.05, 8.0, juce::jmax (1e-9, std::abs (mag)));
    speedSmoother.setTargetValue (speedMag);
}

void UnifiedStretchBuffer::setDirection (double dir)
{
    direction = (dir >= 0.0) ? 1.0 : -1.0;
}

void UnifiedStretchBuffer::setSliceBounds (double startSample, double endSample, bool isLooping)
{
    sliceStart       = startSample;
    sliceEnd         = endSample;
    sliceBoundsActive = (endSample > startSample);
    sliceLooping     = isLooping;
}

void UnifiedStretchBuffer::clearSliceBounds (int fileLengthSamples)
{
    sliceStart        = 0.0;
    sliceEnd          = (double) fileLengthSamples;
    sliceBoundsActive = false;
    sliceLooping      = true;
}

//==============================================================================
// recalcParams — derive all length-in-samples values from ms constants + SR
//==============================================================================

void UnifiedStretchBuffer::recalcParams()
{
    overlapLength    = juce::jmax (16, (int)(sampleRate * kOverlapMs    / 1000.0));
    seekWindowLength = juce::jmax (overlapLength * 2,
                                   (int)(sampleRate * kSeekWindowMs / 1000.0));
    seekLength       = juce::jmax (1,
                                   (int)(sampleRate * kSeekWindowMs / 1000.0));
    sequenceLength   = juce::jmax (seekWindowLength,
                                   (int)(sampleRate * kSequenceMs   / 1000.0));

    // nominalSkip retained for getLatencySamples() only
    nominalSkip = (double) sequenceLength;
}

//==============================================================================
// getLatencySamples
//==============================================================================

int UnifiedStretchBuffer::getLatencySamples() const
{
    return sequenceLength + seekWindowLength;
}

bool UnifiedStretchBuffer::isIdentityMode() const
{
    return (std::abs (stretchRatio - 1.0) < 1e-6 &&
            std::abs (pitchSemis)         < 1e-6 &&
            std::abs (speedMag - 1.0)     < 1e-6);
}

//==============================================================================
// readSourceSample — Lagrange 4-point interpolation from source buffer
//==============================================================================

float UnifiedStretchBuffer::readSourceSample (const juce::AudioBuffer<float>& src,
                                               int ch,
                                               double pos,
                                               int fileLengthSamples) const noexcept
{
    const int n = fileLengthSamples;
    if (n <= 0 || ch >= src.getNumChannels()) return 0.0f;

    const float* d = src.getReadPointer (ch);
    const int    i = (int) std::floor (pos);
    const float  f = (float)(pos - std::floor (pos));

    // 4-point Lagrange: gather 4 samples with boundary clamping
    auto S = [&](int idx) -> float
    {
        return d[juce::jlimit (0, n - 1, idx)];
    };

    const float s0 = S (i - 1);
    const float s1 = S (i    );
    const float s2 = S (i + 1);
    const float s3 = S (i + 2);

    // Standard 4-point Lagrange
    const float c0 = s1;
    const float c1 = (s2 - s0) * 0.5f;
    const float c2 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    const float c3 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;

    return c0 + f * (c1 + f * (c2 + f * c3));
}

//==============================================================================
// seekBestOverlapPosition — cross-correlation search within [seekStart, seekEnd)
//==============================================================================

double UnifiedStretchBuffer::calcCrossCorr (const float* refPos,
                                             const float* comparePos) const noexcept
{
    double corr = 0.0;
    double norm = 0.0;
    for (int i = 0; i < channels * overlapLength; ++i)
    {
        corr += (double)(refPos[i] * comparePos[i]);
        norm += (double)(refPos[i] * refPos[i]) + (double)(comparePos[i] * comparePos[i]);
    }
    return (norm > 1e-12) ? (corr / norm) : 0.0;
}

int UnifiedStretchBuffer::seekBestOverlapPosition (const float* refPos,
                                                    int seekStart,
                                                    int seekEnd) const noexcept
{
    const int end = juce::jmax (seekStart + 1, juce::jmin (seekEnd, seekLength));

    int    bestOffs = seekStart;
    double bestCorr = calcCrossCorr (refPos + (size_t)(channels * seekStart),
                                     midBuffer.data());
    bestCorr = (bestCorr + 0.1) * 0.75;

    for (int i = seekStart + 1; i < end; ++i)
    {
        const double corr = calcCrossCorr (refPos + (size_t)(channels * i),
                                            midBuffer.data());
        // Heuristic: slightly favour mid-range positions (mirrors TDStretch)
        const double tmp  = (double)(2 * i - end) / (double)(end > 1 ? end : 1);
        const double adj  = (corr + 0.1) * (1.0 - 0.25 * tmp * tmp);
        if (adj > bestCorr)
        {
            bestCorr = adj;
            bestOffs = i;
        }
    }

    return bestOffs;
}

//==============================================================================
// overlapAdd — OLA blend of midBuffer with pInput[offset] into pOutput
//==============================================================================

void UnifiedStretchBuffer::overlapAdd (float* pOutput,
                                       const float* pInput,
                                       int offset) const noexcept
{
    const float* inp = pInput + (size_t)(channels * offset);
    const int    len  = overlapLength;

    for (int i = 0; i < len; ++i)
    {
        // Raised-cosine cross-fade (same as TDStretch linear fade for now)
        const float fadeIn  = (float)(i + 1) / (float)(len + 1);
        const float fadeOut = 1.0f - fadeIn;
        for (int ch = 0; ch < channels; ++ch)
        {
            const int idx = i * channels + ch;
            pOutput[idx]  = inp[idx] * fadeIn + midBuffer[(size_t)(i * channels + ch)] * fadeOut;
        }
    }
}

//==============================================================================
// runWsola — runs ONE complete WSOLA sequence and appends to the carry buffer.
// wsolaOutWritePos is advanced by the number of frames produced.
// Returns frames produced (sequenceLength, or fewer at content end).
//==============================================================================

int UnifiedStretchBuffer::runWsola (const juce::AudioBuffer<float>& srcBuffer,
                                     double srcSampleRate,
                                     double& playheadPos,
                                     int /*framesNeeded*/)
{
    const int fileLengthSamples = srcBuffer.getNumSamples();
    if (fileLengthSamples <= 0) return 0;

    const double smoothedStretch = juce::jlimit (0.1, 8.0, stretchSmoother.getCurrentValue());
    const double smoothedSpeed   = juce::jlimit (0.05, 8.0, speedSmoother.getCurrentValue());
    const double srcRatio        = srcSampleRate / sampleRate;
    const double srcStep         = direction * srcRatio * smoothedSpeed;
    const bool   goingForward    = (direction >= 0.0);

    const double activeStart = sliceBoundsActive ? sliceStart : 0.0;
    const double activeEnd   = sliceBoundsActive ? sliceEnd   : (double) fileLengthSamples;

    // Source frames to advance per output sequence
    const double srcAdvancePerSeq = (double) sequenceLength / smoothedStretch * std::abs (srcStep);

    // Scratch: seekLength + sequenceLength interleaved source frames
    const int scratchFrames = seekLength + sequenceLength;
    if ((int) srcScratchInterleaved.size() < scratchFrames * channels)
        srcScratchInterleaved.assign ((size_t)(scratchFrames * channels), 0.0f);

    // Boundary check
    const bool atEnd = goingForward ? (playheadPos >= activeEnd - 1.0)
                                    : (playheadPos <= activeStart);
    if (atEnd)
    {
        if (sliceLooping)
            playheadPos = goingForward ? activeStart : (activeEnd - 1.0);
        else
            return 0;
    }

    // Fill scratch from source via Lagrange interpolation
    {
        double rp = playheadPos;
        for (int f = 0; f < scratchFrames; ++f)
        {
            const double safeRp = juce::jlimit (activeStart, activeEnd - 1.0, rp);
            for (int ch = 0; ch < channels; ++ch)
                srcScratchInterleaved[(size_t)(f * channels + ch)] =
                    readSourceSample (srcBuffer, ch, safeRp, fileLengthSamples);
            rp += srcStep;
        }
    }

    // Find best overlap position in scratch[0..seekLength)
    const int offset = isBeginning ? (isBeginning = false, 0)
                                   : seekBestOverlapPosition (srcScratchInterleaved.data(), 0, seekLength);

    // Ensure carry buffer has room for one more sequence
    if (wsolaOut.getNumSamples() < wsolaOutWritePos + sequenceLength + 16)
    {
        // Compact first: shift valid data to front
        const int avail = wsolaOutWritePos - wsolaOutReadPos;
        if (avail > 0 && wsolaOutReadPos > 0)
            for (int ch = 0; ch < channels; ++ch)
                std::memmove (wsolaOut.getWritePointer (ch),
                              wsolaOut.getReadPointer (ch) + wsolaOutReadPos,
                              (size_t) avail * sizeof (float));
        wsolaOutWritePos = avail;
        wsolaOutReadPos  = 0;

        if (wsolaOut.getNumSamples() < wsolaOutWritePos + sequenceLength + 16)
            wsolaOut.setSize (channels, wsolaOutWritePos + sequenceLength * 2 + 256, true, true, true);
    }

    float* const* wOut = wsolaOut.getArrayOfWritePointers();

    // OLA blend [0..overlapLength): fade from midBuffer to scratch[offset..]
    for (int i = 0; i < overlapLength; ++i)
    {
        const float fadeIn  = (float)(i + 1) / (float)(overlapLength + 1);
        const float fadeOut = 1.0f - fadeIn;
        const int   si      = (offset + i) * channels;
        const int   mi      = i * channels;
        for (int ch = 0; ch < channels; ++ch)
            wOut[ch][wsolaOutWritePos + i] =
                srcScratchInterleaved[(size_t)(si + ch)] * fadeIn
                + midBuffer[(size_t)(mi + ch)] * fadeOut;
    }

    // Direct copy [overlapLength..sequenceLength)
    const int copyStart = offset + overlapLength;
    const int copyAvail = juce::jmax (0, scratchFrames - copyStart);
    const int copyLen   = juce::jmin (sequenceLength - overlapLength, copyAvail);
    for (int i = 0; i < copyLen; ++i)
    {
        const int si = (copyStart + i) * channels;
        for (int ch = 0; ch < channels; ++ch)
            wOut[ch][wsolaOutWritePos + overlapLength + i] =
                srcScratchInterleaved[(size_t)(si + ch)];
    }

    const int produced = overlapLength + copyLen;

    // Update midBuffer with the tail of this sequence
    {
        const int tailStart = offset + sequenceLength - overlapLength;
        const int tailAvail = juce::jmax (0, scratchFrames - tailStart);
        const int tailLen   = juce::jmin (overlapLength, tailAvail);
        if (tailLen > 0)
            std::memcpy (midBuffer.data(),
                         srcScratchInterleaved.data() + (size_t)(tailStart * channels),
                         (size_t)(tailLen * channels) * sizeof (float));
        if (tailLen < overlapLength)
            std::fill (midBuffer.begin() + tailLen * channels, midBuffer.end(), 0.0f);
    }

    wsolaOutWritePos += produced;

    // Advance playhead by one full sequence worth of source samples
    playheadPos += goingForward ? srcAdvancePerSeq : -srcAdvancePerSeq;

    if (goingForward && playheadPos >= activeEnd)
    {
        if (sliceLooping)
            playheadPos = activeStart + std::fmod (playheadPos - activeStart, activeEnd - activeStart);
        else
            playheadPos = activeEnd;
    }
    else if (!goingForward && playheadPos <= activeStart)
    {
        if (sliceLooping)
            playheadPos = activeEnd - std::fmod (activeEnd - playheadPos, activeEnd - activeStart);
        else
            playheadPos = activeStart;
    }

    return produced;
}

//==============================================================================
// runFastPath — identity copy with optional speed scaling via LagrangeInterpolator
//==============================================================================

int UnifiedStretchBuffer::runFastPath (const juce::AudioBuffer<float>& srcBuffer,
                                        double srcSampleRate,
                                        double& playheadPos,
                                        juce::AudioBuffer<float>& outputBuffer,
                                        int numSamples)
{
    const int fileLengthSamples = srcBuffer.getNumSamples();
    if (fileLengthSamples <= 0) return 0;

    const double activeStart = sliceBoundsActive ? sliceStart : 0.0;
    const double activeEnd   = sliceBoundsActive ? sliceEnd   : (double) fileLengthSamples;

    const double srcRatio     = srcSampleRate / sampleRate;
    const double smoothedSpeed = juce::jlimit (0.05, 8.0, speedSmoother.getNextValue());
    if (numSamples > 1)
        speedSmoother.skip (numSamples - 1);

    const double srcStep = direction * srcRatio * smoothedSpeed;
    const bool   isForward = (srcStep >= 0.0);

    // If the speed is exactly 1.0 and forward and same SR: bulk memcpy
    const bool canMemcpy = (std::abs (srcStep - 1.0) < 1e-9
                             && std::abs (srcSampleRate - sampleRate) < 1.0);

    if (canMemcpy)
    {
        int written = 0;
        int pos = (int) playheadPos;
        while (written < numSamples)
        {
            if (pos < 0 || pos >= fileLengthSamples)
            {
                if (sliceLooping)
                    pos = (pos < 0) ? (int)(activeEnd - 1.0) : (int) activeStart;
                else
                    break;
            }
            const int available = isForward ? (fileLengthSamples - pos) : (pos + 1);
            if (available <= 0) break;
            const int chunk = juce::jmin (numSamples - written, available);
            for (int ch = 0; ch < channels && ch < outputBuffer.getNumChannels(); ++ch)
            {
                outputBuffer.copyFrom (ch, written, srcBuffer, ch, pos, chunk);
            }
            written += chunk;
            pos     += chunk;
        }
        playheadPos = (double) pos;
        return written;
    }

    // Variable speed / reverse: use Lagrange interpolation per sample
    int written = 0;
    double rp = playheadPos;

    for (int i = 0; i < numSamples; ++i)
    {
        // Boundary handling
        if (isForward && rp >= activeEnd - 1.0)
        {
            if (sliceLooping)
                rp = activeStart;
            else
                break;
        }
        else if (!isForward && rp <= activeStart)
        {
            if (sliceLooping)
                rp = activeEnd - 1.0;
            else
                break;
        }

        for (int ch = 0; ch < channels && ch < outputBuffer.getNumChannels(); ++ch)
            outputBuffer.getWritePointer (ch)[i] = readSourceSample (srcBuffer, ch, rp, fileLengthSamples);

        rp     += srcStep;
        written = i + 1;
    }

    playheadPos = rp;
    return written;
}

//==============================================================================
// readBlock — public entry point
//==============================================================================

int UnifiedStretchBuffer::readBlock (const juce::AudioBuffer<float>& srcBuffer,
                                      double srcSampleRate,
                                      double& playheadPos,
                                      juce::AudioBuffer<float>& outputBuffer,
                                      int numSamples)
{
    const int numOut = juce::jmin (numSamples, outputBuffer.getNumSamples());
    if (numOut <= 0)
        return 0;

    outputBuffer.clear();

    // Advance smoothers by one sample to get the per-block representative value.
    // (The smoothers are advanced further inside runWsola/runFastPath.)
    const double smoothedStretch = stretchSmoother.getNextValue();
    const double smoothedPitch   = pitchSmoother.getNextValue();
    const bool   needStretch     = std::abs (smoothedStretch - 1.0) > 1e-5;
    const bool   needPitch       = std::abs (smoothedPitch)         > 1e-5;
    const bool   needSpeed       = std::abs (speedMag - 1.0)        > 1e-5
                                   || std::abs (srcSampleRate - sampleRate) > 1.0;

    // Fast path: no processing needed
    if (!needStretch && !needPitch && !needSpeed && direction >= 0.0)
    {
        // Still need to reset smoothers to avoid stale state
        stretchSmoother.skip (numOut - 1);
        pitchSmoother.skip   (numOut - 1);
        speedSmoother.skip   (numOut - 1);
        return runFastPath (srcBuffer, srcSampleRate, playheadPos, outputBuffer, numOut);
    }

    if (!needStretch && !needPitch)
    {
        // Speed/direction only — fast path with speed scaling
        stretchSmoother.skip (numOut - 1);
        pitchSmoother.skip   (numOut - 1);
        return runFastPath (srcBuffer, srcSampleRate, playheadPos, outputBuffer, numOut);
    }

    // WSOLA path (with optional pitch shift post-stage)
    // ---------------------------------------------------

    // How many WSOLA output frames do we need before pitch resampling?
    const double pitchRatio = std::pow (2.0, smoothedPitch / 12.0);  // >1 = pitch up
    const int wsolaNeeded   = needPitch
        ? (int) std::ceil ((double) numOut * pitchRatio) + 8
        : numOut;

    // Advance smoothers by block size (runWsola reads getCurrentValue() not getNextValue())
    stretchSmoother.skip (numOut);
    speedSmoother.skip   (numOut);

    // Compact carry buffer if the read pointer has advanced far enough
    {
        const int avail = wsolaOutWritePos - wsolaOutReadPos;
        if (wsolaOutReadPos > sequenceLength && avail >= 0)
        {
            if (avail > 0)
                for (int ch = 0; ch < channels; ++ch)
                    std::memmove (wsolaOut.getWritePointer (ch),
                                  wsolaOut.getReadPointer (ch) + wsolaOutReadPos,
                                  (size_t) avail * sizeof (float));
            wsolaOutWritePos = avail;
            wsolaOutReadPos  = 0;
        }
    }

    // Fill carry buffer with WSOLA sequences until we have enough
    while (wsolaOutWritePos - wsolaOutReadPos < wsolaNeeded)
    {
        const int produced = runWsola (srcBuffer, srcSampleRate, playheadPos, sequenceLength);
        if (produced <= 0)
            break;
    }

    const int wsolaAvail  = wsolaOutWritePos - wsolaOutReadPos;
    const int wsolaFilled = juce::jmin (wsolaAvail, wsolaNeeded);
    if (wsolaFilled <= 0)
        return 0;

    if (!needPitch)
    {
        // Copy from carry buffer to output
        const int toWrite = juce::jmin (wsolaFilled, numOut);
        for (int ch = 0; ch < channels && ch < outputBuffer.getNumChannels(); ++ch)
            outputBuffer.copyFrom (ch, 0, wsolaOut, ch, wsolaOutReadPos, toWrite);
        wsolaOutReadPos += toWrite;

        // Clamp
        for (int ch = 0; ch < channels && ch < outputBuffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::clip (outputBuffer.getWritePointer (ch),
                                               outputBuffer.getReadPointer (ch),
                                               -1.0f, 1.0f, toWrite);
        return toWrite;
    }

    // Pitch shift via Lagrange resampling
    // pitchRatio > 1 → pitch up → output resampler reads FASTER from WSOLA (more input per output)
    // pitchRatio < 1 → pitch down → reads SLOWER (less input per output)
    //
    // LagrangeInterpolator::process(speed, input, output, numOutput):
    //   'speed' = input frames consumed per output frame
    //   pitch up: speed = pitchRatio (consume more input per output)
    //   pitch down: speed = pitchRatio (< 1, consume less input per output)

    if (!pitchResamplersValid)
    {
        for (auto& r : pitchResamplers)
            r.reset();
        pitchResamplersValid = true;
    }

    // Advance pitch smoother for remaining samples
    pitchSmoother.skip (juce::jmax (0, numOut - 1));

    // Re-derive pitchRatio from current smoothed value
    const double currentPitchSemis = pitchSmoother.getCurrentValue();
    const double currentPitchRatio = std::pow (2.0, currentPitchSemis / 12.0);

    int written = 0;
    for (int ch = 0; ch < channels && ch < outputBuffer.getNumChannels(); ++ch)
    {
        const float* wsIn  = wsolaOut.getReadPointer (ch) + wsolaOutReadPos;
        float*       out   = outputBuffer.getWritePointer (ch);
        const int    avail = wsolaFilled;

        const int produced = pitchResamplers[(size_t) ch].process (
            currentPitchRatio,
            wsIn,
            out,
            juce::jmin (numOut, avail),
            avail,
            0
        );

        if (ch == 0) written = produced;
    }
    wsolaOutReadPos += wsolaFilled;

    // Clamp
    for (int ch = 0; ch < channels && ch < outputBuffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::clip (outputBuffer.getWritePointer (ch),
                                           outputBuffer.getReadPointer (ch),
                                           -1.0f, 1.0f, written);

    return written;
}
