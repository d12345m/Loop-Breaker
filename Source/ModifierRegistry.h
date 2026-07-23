#pragma once

#include "Modifier.h"

#include <array>
#include <vector>

/** Canonical metadata for every serialized ModifierType value.

    Keep this table in the established UI/parameter order. ModifierType values are
    persisted by integer ID, so the enum itself must only ever be appended to before
    Unknown; reordering this registry is also a compatibility change because it drives
    parameter and MIDI-map presentation order.
*/
struct ModifierMetadata
{
    ModifierType type;
    ModifierCategory category;
    const char* displayName;
    const char* shortName;
    const char* categoryLabel;
    const char* description;
    bool prototypeEligible;
    bool schedulerEligible;
    bool probabilityVisible;
    bool alwaysOn;
    float representativeGlyphPhase01;
};

class ModifierRegistry
{
public:
    static const std::array<ModifierMetadata, static_cast<size_t> (ModifierType::Unknown)>& entries()
    {
        static const std::array<ModifierMetadata, static_cast<size_t> (ModifierType::Unknown)> metadata {{
            { ModifierType::Reverse,                     ModifierCategory::BufferTransform, "Reverse",             "Reverse",             "Buffer",        "Reverse playback direction",                                                        true,  true,  true, false, 0.25f },
            { ModifierType::Speed,                       ModifierCategory::BufferTransform, "Speed",               "Speed",               "Buffer",        "Randomize playback speed (0.25x, 0.5x, 1x, or 2x)",                                true,  true,  true, false, 0.25f },
            { ModifierType::Stretch,                     ModifierCategory::BufferTransform, "Stretch",             "Stretch",             "Buffer",        "Randomize time-stretch ratio (0.25x, 0.5x, or 2x)",                                true,  true,  true, false, 0.25f },
            { ModifierType::PitchUpOctave,               ModifierCategory::BufferTransform, "Pitch Up Octave",     "Pitch Up Octave",     "Buffer",        "Pitch up one octave (+12 semitones)",                                               true,  true,  true, false, 0.25f },
            { ModifierType::PitchDownOctave,             ModifierCategory::BufferTransform, "Pitch Down Octave",   "Pitch Down Octave",   "Buffer",        "Pitch down one octave (-12 semitones)",                                             true,  true,  true, false, 0.25f },
            { ModifierType::BeatSliceRandom,             ModifierCategory::BufferTransform, "Beat Slice",          "Beat Slice",          "Buffer",        "Subdivide buffer into random slice count (4, 8, 16, 32, or 64 slices)",              true,  true,  true, false, 0.25f },
            { ModifierType::ArpSlice,                    ModifierCategory::BufferTransform, "Arp Slice",           "Arp Slice",           "Buffer",        "Arpeggio-style slicing with randomized sequence length, grid size, and repeat cycles", true, true, true, false, 0.25f },
            { ModifierType::SliceRepeater,               ModifierCategory::BufferTransform, "Slice Repeater",      "Slice Repeater",      "Buffer",        "Repeat a random slice (4-32 repetitions) from a grid of 16-64 slices",              true,  true,  true, false, 0.25f },
            { ModifierType::PingPong,                    ModifierCategory::BufferTransform, "Ping Pong",           "Ping Pong",           "Buffer",        "Alternate forward/reverse playback at a randomized note division",                 true,  true,  true, false, 0.25f },
            { ModifierType::BufferDelayOn,               ModifierCategory::BufferEffect,    "Delay",               "Delay",               "Channel Effect", "Enable delay with randomized division, wet mix, feedback, and fade",                true,  true,  true, false, 0.25f },
            { ModifierType::BufferDelayDubBurst,         ModifierCategory::BufferEffect,    "Delay Dub Burst",     "Delay Dub Burst",     "Channel Effect", "Dub-style temporary delay burst (rise then fall)",                                  true,  true,  true, false, 0.25f },
            { ModifierType::BufferReverbOn,              ModifierCategory::BufferEffect,    "Reverb",              "Reverb",              "Channel Effect", "Enable reverb with randomized wet mix (25-100%) and fade (instant to 2 bars)",       true,  true,  true, false, 0.25f },
            { ModifierType::BufferLowPassOn,             ModifierCategory::BufferEffect,    "Low-Pass Filter",     "Low-Pass Filter",     "Channel Effect", "Enable low-pass filter (sweep cutoff to 4000 Hz over 1 bar)",                        true,  true,  true, false, 0.25f },
            { ModifierType::BufferHighPassOn,            ModifierCategory::BufferEffect,    "High-Pass Filter",    "High-Pass Filter",    "Channel Effect", "Enable high-pass filter (sweep cutoff to 120 Hz over 1 bar)",                        true,  true,  true, false, 0.25f },
            { ModifierType::BufferShhhhhh,               ModifierCategory::BufferEffect,    "Shhhhhh",             "Shhhhhh",             "Channel Effect", "Fade volume to silence over 1-4 bars, hold, then ramp back up",                      true,  true,  true, false, 0.25f },
            { ModifierType::BufferTremolo,               ModifierCategory::BufferEffect,    "Tremolo",             "Tremolo",             "Channel Effect", "Apply tremolo at 1/8-note rate, depth ramping to 50% over 2 bars",                  true,  true,  true, false, 0.25f },
            { ModifierType::BufferChorusOn,              ModifierCategory::BufferEffect,    "Chorus",              "Chorus",              "Channel Effect", "Enable chorus with randomized depth, rate, mix, and fade",                          true,  true,  true, false, 0.25f },
            { ModifierType::BufferAutoPan,               ModifierCategory::BufferEffect,    "Auto-Pan",            "Auto-Pan",            "Channel Effect", "Stereo auto-pan with randomized rate, depth, mix, and fade",                        true,  true,  true, false, 0.25f },
            { ModifierType::BufferDuckingOn,             ModifierCategory::BufferEffect,    "Ducking",             "Ducking",             "Channel Effect", "Always-on channel ducking; not independently scheduled",                            false, false, true, true,  0.25f },
            { ModifierType::BufferSHLowPassOn,           ModifierCategory::BufferEffect,    "S&H Low-Pass",        "S&H Low-Pass",        "Channel Effect", "Persistent low-pass filter with sample-and-hold modulated cutoff and Q",            true,  true,  true, false, 0.25f },
            { ModifierType::BufferSHHighPassOn,          ModifierCategory::BufferEffect,    "S&H High-Pass",       "S&H High-Pass",       "Channel Effect", "Persistent high-pass filter with sample-and-hold modulated cutoff and Q",           true,  true,  true, false, 0.25f },
            { ModifierType::BufferGranularOn,            ModifierCategory::BufferEffect,    "Granular",            "Granular",            "Channel Effect", "Clouds-inspired granular texture with randomized density, grain size, pitch spread, and mix", true, true, true, false, 0.25f },
            { ModifierType::BufferGranularMomentary,     ModifierCategory::BufferEffect,    "Granular Burst",      "Granular Burst",      "Channel Effect", "Temporary granular cloud that fades in and out over bars",                          true,  true,  true, false, 0.25f },
            // Retained as readable legacy IDs. These are hidden and never
            // scheduled; the per-channel filters can target every pad instead.
            { ModifierType::MasterHighPassOn,            ModifierCategory::MasterEffect,    "Master High-Pass",    "Master High-Pass",    "Master Effect",  "Retired legacy master high-pass filter",                                           true, false, false, false, 0.25f },
            { ModifierType::MasterLowPassOn,             ModifierCategory::MasterEffect,    "Master Low-Pass",     "Master Low-Pass",     "Master Effect",  "Retired legacy master low-pass filter",                                            true, false, false, false, 0.25f },
            { ModifierType::SwitchPart,                  ModifierCategory::GlobalUtility,   "Switch Part",         "Switch Part",         "Special",        "Switch to a different part",                                                       true,  true,  true, false, 0.25f },
            { ModifierType::QuarterNoteBurst,            ModifierCategory::GlobalUtility,   "Quarter-Note Burst",  "Quarter-Note Burst",  "Special",        "Trigger modifiers every quarter note for 1-4 bars",                                true,  true,  true, false, 0.25f },
            { ModifierType::SwapModifierStack,           ModifierCategory::BufferTransform, "Swap Stack",          "Swap Stack",          "Special",        "Swap the entire modifier stack between two or more buffers",                       true,  true,  true, false, 0.25f },
            { ModifierType::ResetAll,                    ModifierCategory::BufferTransform, "Reset All",           "Reset",               "Special",        "Reset all modifiers and effects on targeted buffers",                              true,  true,  true, false, 0.25f },
        }};
        return metadata;
    }

    static const ModifierMetadata* find (ModifierType type)
    {
        for (const auto& entry : entries())
            if (entry.type == type)
                return &entry;
        return nullptr;
    }

    static const ModifierMetadata& get (ModifierType type)
    {
        if (const auto* entry = find (type))
            return *entry;

        static const ModifierMetadata unknown {
            ModifierType::Unknown, ModifierCategory::GlobalUtility,
            "Unknown", "Unknown", "Other", "Unknown modifier",
            false, false, false, false, 0.25f
        };
        return unknown;
    }

    static ModifierDescriptor makeDescriptor (ModifierType type)
    {
        const auto& entry = get (type);
        return { entry.type, entry.category, entry.shortName, entry.description };
    }

    static const std::vector<ModifierType>& orderedTypes()
    {
        static const std::vector<ModifierType> types = []
        {
            std::vector<ModifierType> result;
            result.reserve (entries().size());
            for (const auto& entry : entries())
                result.push_back (entry.type);
            return result;
        }();
        return types;
    }

    static const std::vector<ModifierType>& visibleProbabilityTypes()
    {
        static const std::vector<ModifierType> types = []
        {
            std::vector<ModifierType> result;
            result.reserve (entries().size());
            for (const auto& entry : entries())
                if (entry.probabilityVisible)
                    result.push_back (entry.type);
            return result;
        }();
        return types;
    }
};
