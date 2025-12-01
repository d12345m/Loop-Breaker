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
    PitchUpOctave,
    PitchDownOctave,
    BeatSliceRandom,

    // Individual buffer FX modifiers
    BufferDelayOn,
    BufferDelayOff,
    BufferReverbOn,
    BufferReverbWet25,
    BufferReverbWet50,
    BufferReverbWet75,
    BufferReverbWet100,
    BufferReverbOff,
    BufferLowPassOn,
    BufferLowPassOff,
    BufferHighPassOn,
    BufferHighPassOff,
    BufferVolumeRampDown,
    BufferTremolo,
    BufferTremoloOff,

    // Master FX modifiers
    MasterHighPassOn,
    MasterLowPassOff,

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
    juce::String plannedSliceDivision;            // e.g. "1/8", "1/8T" for BeatSliceRandom (empty if unset)
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
