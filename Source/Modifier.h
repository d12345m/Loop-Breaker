/*
 ============================================================================== 
   Modifier.h
   --------------------------------------------------------------------------
   Base interfaces & lightweight data structures for the future modifier system.
   No audio processing implemented yet; these are pure architectural skeletons.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <optional>
#include <set>

// Enumerates high-level modifier categories (buffer content, buffer FX, master FX, global reset)
enum class ModifierCategory
{
    BufferTransform,
    BufferEffect,
    MasterEffect,
    GlobalUtility
};

// Specific modifier types enumerated from DesignDoc; entries we have not implemented are placeholders.
enum class ModifierType
{
    // Buffer modifiers
    Reverse,
    Speed,          // Discrete speeds / rate change (timestretch possible later)
    Stretch,        // Discrete time-stretch ratios (tempo change without pitch)
    PitchUpOctave,
    PitchDownOctave,
    BeatSliceRandom,
    PingPong,

    // Individual buffer FX modifiers
    BufferDelayOn,
    BufferReverbOn,
    BufferLowPassOn,
    BufferHighPassOn,
    BufferVolumeRampDown,
    BufferTremolo,
    BufferDelayDubBurst,
    BufferDuckingOn,
    BufferChorusOn,
    BufferAutoPan,

    // Master FX modifiers
    MasterHighPassOn,
    MasterLowPassOn,

    // Navigation modifiers
    SwitchPart,

    // Scheduler modifiers
    QuarterNoteBurst,

    // Global
    ResetAll,

    Unknown
};

// Simple description metadata for UI / logging
struct ModifierDescriptor
{
    ModifierType type { ModifierType::Unknown };
    ModifierCategory category { ModifierCategory::BufferTransform };
    juce::String shortName;
    juce::String description;

    // Structured planned variant data (optional, set by scheduler when preparing upcoming)
    // If set, these values should be used by the application of the modifier instead of
    // re-randomizing or parsing from description text.
    std::optional<double> plannedSpeed;           // e.g. 0.50, 1.00, 2.00 for Speed
    std::optional<double> plannedStretch;         // e.g. 0.25, 0.50, 2.00 for Stretch (tempo only)
    std::optional<double> plannedWet;             // e.g. 0.25, 0.50, 0.75, 1.00 for Reverb
    juce::String plannedSliceDivision;            // e.g. "1/8", "1/8T" for BeatSliceRandom (empty if unset)
    juce::String plannedDelayDivision;            // e.g. "1/4", "1/8", "1/8D", "1/8T" for Delay (empty if unset)
    std::optional<double> plannedDelayWet;        // e.g. 0.25, 0.50, 0.75, 1.00 for Delay wet mix
    juce::StringArray plannedDelayDivisions;      // Multiple divisions (if multi-tap selected)
    std::optional<double> plannedDelayFeedback;  // Explicit feedback override (0..1)
    std::optional<bool>    plannedDelayPingPong; // Enable ping-pong when delay is applied
    std::optional<bool>    plannedWowFlutter;    // Enable wow/flutter modulation on delay
    // Future: dub burst durations could be parameterized; for now use defaults
    // Generic FX fade duration in bars (e.g., reverb ramp)
    std::optional<double> plannedFxFadeBars;     // e.g., 0 (instant), 1, 2

    // Scheduler-only: when QuarterNoteBurst triggers, schedule quarter-note triggers for X bars.
    std::optional<int> plannedBurstBars;         // e.g., 1, 2, 4
    // For temporary global filters: choose whether to jump immediately to target, then ramp back
    std::optional<bool> plannedImmediateJump;    // true: jump to target then ramp back; false: ramp up then ramp down
    // Chorus parameters (set by scheduler variant randomization)
    std::optional<double> plannedChorusDepth;     // 0.0..1.0 modulation depth
    std::optional<double> plannedChorusRateHz;    // LFO rate in Hz
    std::optional<double> plannedChorusMix;       // wet/dry mix 0.0..1.0
    // Auto-pan parameters
    std::optional<double> plannedPanRateHz;       // LFO rate in Hz (tempo-synced)
    std::optional<double> plannedPanDepth;        // 0.0..1.0 how far L/R the pan sweeps
    std::optional<double> plannedPanMix;          // 0.0..1.0 wet/dry blend (0 = bypass)
    // For PingPong: musical note division for oscillation (e.g., 1.0=whole, 0.5=half, 0.25=quarter, 0.125=eighth, 0.0625=sixteenth)
    std::optional<double> plannedPingPongDivision; // Musical division in bars
};

// Execution context passed to modifiers when ultimately applied.
struct ModifierContext
{
    double hostSampleRate = 44100.0;
    double bpm = 120.0;
    double currentBar = 0.0; // Bar timeline position (can be fractional)
    juce::Array<int> targetedBufferIndices; // Buffers selected by user (or chosen randomly)
    bool appliesToMaster = false;          // True for master / global modifiers
};

// Abstract base modifier: future implementations will encapsulate automation envelopes & effect logic.
class IModifier
{
public:
    virtual ~IModifier() = default;

    virtual ModifierDescriptor getDescriptor() const = 0;

    // Duration in musical bars (optional – instantaneous if empty)
    virtual std::optional<int> getDurationInBars() const { return std::nullopt; }

    // Called when the modifier should begin. For now returns success boolean.
    virtual bool begin(const ModifierContext& /*ctx*/) { return true; }

    // Per audio block update hook (not implemented yet); return false when finished.
    virtual bool processBlock(const ModifierContext& /*ctx*/) { return false; }

    // Explicit cancellation / cleanup.
    virtual void cancel() {}
};

// Factory utilities (very lightweight placeholders)
class ModifierFactory
{
public:
    static juce::OwnedArray<IModifier> createAllPrototypes();
    static std::unique_ptr<IModifier> createInstance(ModifierType type);
};
