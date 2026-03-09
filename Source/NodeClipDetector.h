/*
 ==============================================================================
   NodeClipDetector.h
   --------------------------------------------------------------------------
   Lock-free per-node audio clip/level detection system.
   
   Insert a NodeProbe at each processing stage in the signal chain.
   Each probe tracks peak level, clipping events, NaN/Inf, and discontinuities
   without allocating memory or taking locks on the audio thread.
   
   Usage:
       probe.inspect(buffer, numSamples);   // call after each DSP stage
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    Identifies a specific point in the signal chain.
*/
enum class NodeId
{
    RawPlayback = 0,     // After AudioBuffer::processBlock()
    Delay,               // After delay processing
    HighPass,            // After high-pass filter
    LowPass,            // After low-pass filter
    Tremolo,             // After tremolo
    Chorus,              // After chorus
    AutoPan,             // After auto-pan
    VolumeRamp,          // After volume ramp gain
    PreReverbLimiter,    // After pre-reverb limiter
    ReverbMix,           // After reverb wet/dry mix
    PostReverbLimiter,   // After post-reverb limiter
    PadReduction,        // After -12 dB pad reduction
    MasterVolume,        // After master volume gain
    FinalMix,            // Final mix bus (sum of all pads)
    NumNodes
};

static constexpr int kNumNodes = static_cast<int>(NodeId::NumNodes);

inline const char* nodeIdToString(NodeId id)
{
    switch (id)
    {
        case NodeId::RawPlayback:      return "Raw Playback";
        case NodeId::Delay:            return "Delay";
        case NodeId::HighPass:         return "High-Pass";
        case NodeId::LowPass:          return "Low-Pass";
        case NodeId::Tremolo:          return "Tremolo";
        case NodeId::Chorus:           return "Chorus";
        case NodeId::AutoPan:          return "Auto-Pan";
        case NodeId::VolumeRamp:       return "Vol Ramp";
        case NodeId::PreReverbLimiter: return "Pre-Limiter";
        case NodeId::ReverbMix:        return "Reverb Mix";
        case NodeId::PostReverbLimiter:return "Post-Limiter";
        case NodeId::PadReduction:     return "-12dB Pad";
        case NodeId::MasterVolume:     return "Master Vol";
        case NodeId::FinalMix:         return "Final Mix";
        default:                       return "Unknown";
    }
}

inline const char* nodeIdToShortString(NodeId id)
{
    switch (id)
    {
        case NodeId::RawPlayback:      return "Raw";
        case NodeId::Delay:            return "Dly";
        case NodeId::HighPass:         return "HPF";
        case NodeId::LowPass:          return "LPF";
        case NodeId::Tremolo:          return "Trm";
        case NodeId::Chorus:           return "Chr";
        case NodeId::AutoPan:          return "Pan";
        case NodeId::VolumeRamp:       return "Vol";
        case NodeId::PreReverbLimiter: return "PLm";
        case NodeId::ReverbMix:        return "Rvb";
        case NodeId::PostReverbLimiter:return "RLm";
        case NodeId::PadReduction:     return "Pad";
        case NodeId::MasterVolume:     return "Mst";
        case NodeId::FinalMix:         return "Mix";
        default:                       return "???";
    }
}

//==============================================================================
/**
    Per-node statistics — all atomic for lock-free audio-thread writes / UI reads.
*/
struct NodeStats
{
    std::atomic<float>  peakLevel     { 0.0f };   // Absolute peak since last reset
    std::atomic<float>  recentPeak    { 0.0f };   // Peak within the most recent block
    std::atomic<int>    clipCount     { 0 };       // Samples exceeding ±1.0
    std::atomic<int>    hardClipCount { 0 };       // Samples exceeding ±2.0 (severe)
    std::atomic<int>    nanInfCount   { 0 };       // NaN or Inf samples
    std::atomic<int>    blocksInspected { 0 };     // Total blocks inspected
    std::atomic<float>  lastBlockRms  { 0.0f };    // RMS of most recent block
    std::atomic<float>  rmsJumpMax    { 0.0f };    // Largest RMS jump between consecutive blocks
    
    // First-offender tracking: which block first saw clipping
    std::atomic<int>    firstClipBlock { -1 };
    
    void reset()
    {
        peakLevel.store(0.0f);
        recentPeak.store(0.0f);
        clipCount.store(0);
        hardClipCount.store(0);
        nanInfCount.store(0);
        blocksInspected.store(0);
        lastBlockRms.store(0.0f);
        rmsJumpMax.store(0.0f);
        firstClipBlock.store(-1);
    }
    
    bool hasClipping() const { return clipCount.load() > 0; }
    bool hasHardClipping() const { return hardClipCount.load() > 0; }
    bool hasNanInf() const { return nanInfCount.load() > 0; }
    bool hasProblems() const { return hasClipping() || hasNanInf(); }
};

//==============================================================================
/**
    A single inspection probe. Place one at each node in the signal chain.
    Call inspect() on the audio thread after each processing stage.
*/
class NodeProbe
{
public:
    NodeProbe() = default;
    explicit NodeProbe(NodeId nodeId) : id(nodeId) {}
    
    // NodeProbe contains atomics, so disable copy/move assignment but allow construction
    NodeProbe(const NodeProbe&) = delete;
    NodeProbe& operator=(const NodeProbe&) = delete;
    NodeProbe(NodeProbe&&) = delete;
    NodeProbe& operator=(NodeProbe&&) = delete;
    
    /** Inspect the buffer contents at this node. Call on the audio thread. */
    void inspect(const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (!enabled.load(std::memory_order_relaxed))
            return;
            
        const int channels = buffer.getNumChannels();
        const int samples = juce::jmin(numSamples, buffer.getNumSamples());
        if (channels == 0 || samples == 0)
            return;
        
        float blockPeak = 0.0f;
        float sumSquares = 0.0f;
        int blockClips = 0;
        int blockHardClips = 0;
        int blockNanInf = 0;
        
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            for (int i = 0; i < samples; ++i)
            {
                const float s = data[i];
                
                // NaN/Inf check
                if (std::isnan(s) || std::isinf(s))
                {
                    ++blockNanInf;
                    continue;
                }
                
                const float absS = std::abs(s);
                if (absS > blockPeak)
                    blockPeak = absS;
                    
                sumSquares += s * s;
                
                if (absS > 1.0f)
                    ++blockClips;
                if (absS > 2.0f)
                    ++blockHardClips;
            }
        }
        
        const int totalSamples = channels * samples;
        const float blockRms = totalSamples > 0 ? std::sqrt(sumSquares / (float)totalSamples) : 0.0f;
        
        // Update stats atomically
        stats.recentPeak.store(blockPeak, std::memory_order_relaxed);
        
        // Update all-time peak (relaxed CAS loop)
        float oldPeak = stats.peakLevel.load(std::memory_order_relaxed);
        while (blockPeak > oldPeak)
        {
            if (stats.peakLevel.compare_exchange_weak(oldPeak, blockPeak, std::memory_order_relaxed))
                break;
        }
        
        if (blockClips > 0)
        {
            stats.clipCount.fetch_add(blockClips, std::memory_order_relaxed);
            int prevFirst = stats.firstClipBlock.load(std::memory_order_relaxed);
            int currentBlock = stats.blocksInspected.load(std::memory_order_relaxed);
            if (prevFirst < 0)
                stats.firstClipBlock.compare_exchange_strong(prevFirst, currentBlock, std::memory_order_relaxed);
        }
        
        if (blockHardClips > 0)
            stats.hardClipCount.fetch_add(blockHardClips, std::memory_order_relaxed);
            
        if (blockNanInf > 0)
            stats.nanInfCount.fetch_add(blockNanInf, std::memory_order_relaxed);
        
        // RMS jump detection
        const float prevRms = stats.lastBlockRms.load(std::memory_order_relaxed);
        const float rmsJump = std::abs(blockRms - prevRms);
        float oldJumpMax = stats.rmsJumpMax.load(std::memory_order_relaxed);
        while (rmsJump > oldJumpMax)
        {
            if (stats.rmsJumpMax.compare_exchange_weak(oldJumpMax, rmsJump, std::memory_order_relaxed))
                break;
        }
        stats.lastBlockRms.store(blockRms, std::memory_order_relaxed);
        
        stats.blocksInspected.fetch_add(1, std::memory_order_relaxed);
    }
    
    /** Reset all accumulated statistics. */
    void reset() { stats.reset(); }
    
    /** Enable/disable this probe without removing it. */
    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }
    
    NodeId getId() const { return id; }
    void setId(NodeId nodeId) { id = nodeId; }
    const NodeStats& getStats() const { return stats; }
    
private:
    NodeId id = NodeId::RawPlayback;
    NodeStats stats;
    std::atomic<bool> enabled { true };
};

//==============================================================================
/**
    Collection of probes for one pad (one complete signal chain).
    Provides array-indexed access by NodeId.
*/
struct PadProbeSet
{
    std::array<NodeProbe, kNumNodes> probes;
    
    PadProbeSet()
    {
        for (int i = 0; i < kNumNodes; ++i)
            probes[(size_t)i].setId(static_cast<NodeId>(i));
    }
    
    NodeProbe& operator[](NodeId id) { return probes[static_cast<size_t>(id)]; }
    const NodeProbe& operator[](NodeId id) const { return probes[static_cast<size_t>(id)]; }
    
    void resetAll()
    {
        for (auto& p : probes)
            p.reset();
    }
    
    void setAllEnabled(bool enabled)
    {
        for (auto& p : probes)
            p.setEnabled(enabled);
    }
    
    /** Returns the first node where clipping was detected, or NumNodes if clean. */
    NodeId findFirstClippingNode() const
    {
        for (int i = 0; i < kNumNodes; ++i)
        {
            if (probes[(size_t)i].getStats().hasClipping())
                return static_cast<NodeId>(i);
        }
        return NodeId::NumNodes;
    }
    
    /** Returns the node with the highest peak level. */
    NodeId findHottestNode() const
    {
        float maxPeak = 0.0f;
        NodeId hottest = NodeId::RawPlayback;
        for (int i = 0; i < kNumNodes; ++i)
        {
            float p = probes[(size_t)i].getStats().peakLevel.load(std::memory_order_relaxed);
            if (p > maxPeak)
            {
                maxPeak = p;
                hottest = static_cast<NodeId>(i);
            }
        }
        return hottest;
    }
};

//==============================================================================
/**
    Global clip detection system. Holds probe sets for all pads plus the mix bus.
*/
struct ClipDetectorSystem
{
    static constexpr int kMaxPads = 8;
    
    std::array<PadProbeSet, kMaxPads> padProbes;
    NodeProbe mixBusProbe { NodeId::FinalMix };
    
    std::atomic<bool> globalEnabled { true };
    
    void resetAll()
    {
        for (auto& pad : padProbes)
            pad.resetAll();
        mixBusProbe.reset();
    }
    
    void setGlobalEnabled(bool enabled)
    {
        globalEnabled.store(enabled, std::memory_order_relaxed);
        for (auto& pad : padProbes)
            pad.setAllEnabled(enabled);
        mixBusProbe.setEnabled(enabled);
    }
    
    /** Quick summary: does any node on any pad have clipping? */
    bool hasAnyClipping() const
    {
        if (mixBusProbe.getStats().hasClipping())
            return true;
        for (const auto& pad : padProbes)
        {
            if (pad.findFirstClippingNode() != NodeId::NumNodes)
                return true;
        }
        return false;
    }
    
    /** Build a diagnostic string identifying all clipping sources. */
    juce::String getDiagnosticReport() const
    {
        juce::String report;
        
        for (int padIdx = 0; padIdx < kMaxPads; ++padIdx)
        {
            const auto& pad = padProbes[(size_t)padIdx];
            bool padHasIssues = false;
            
            for (int n = 0; n < kNumNodes; ++n)
            {
                const auto& stats = pad.probes[(size_t)n].getStats();
                if (stats.hasProblems())
                {
                    if (!padHasIssues)
                    {
                        report += "--- Pad " + juce::String(padIdx + 1) + " ---\n";
                        padHasIssues = true;
                    }
                    
                    auto nodeId = static_cast<NodeId>(n);
                    report += "  " + juce::String(nodeIdToString(nodeId))
                           + ": peak=" + juce::String(stats.peakLevel.load(), 3)
                           + " clips=" + juce::String(stats.clipCount.load())
                           + " hard=" + juce::String(stats.hardClipCount.load())
                           + " NaN=" + juce::String(stats.nanInfCount.load())
                           + "\n";
                }
            }
        }
        
        // Mix bus
        const auto& mixStats = mixBusProbe.getStats();
        if (mixStats.hasProblems())
        {
            report += "--- Mix Bus ---\n";
            report += "  peak=" + juce::String(mixStats.peakLevel.load(), 3)
                   + " clips=" + juce::String(mixStats.clipCount.load())
                   + " hard=" + juce::String(mixStats.hardClipCount.load())
                   + " NaN=" + juce::String(mixStats.nanInfCount.load())
                   + "\n";
        }
        
        if (report.isEmpty())
            report = "No clipping detected at any node.\n";
            
        return report;
    }
};
