/*
 ============================================================================== 
   Modifier.cpp
   --------------------------------------------------------------------------
   Basic factory placeholder returning empty concrete classes for later fill-in.
 ==============================================================================
*/

#include "Modifier.h"

namespace {
    // Base simple modifier that just stores descriptor
    class SimpleModifierBase : public IModifier
    {
    public:
        explicit SimpleModifierBase(ModifierDescriptor d) : descriptor(std::move(d)) {}
        ModifierDescriptor getDescriptor() const override { return descriptor; }
    protected:
        ModifierDescriptor descriptor;
    };

    // Reverse playback: toggles reverse state by setting negative speed (or flipping sign)
    class ReverseModifier : public SimpleModifierBase
    {
    public:
        ReverseModifier() : SimpleModifierBase({ ModifierType::Reverse, ModifierCategory::BufferTransform, "Reverse", "Reverse playback direction" }) {}
        bool begin(const ModifierContext& ctx) override;
    };

    // Speed modifier: picks a random speed from a discrete set (e.g., 0.25, 0.5, 1, 2)
    class SpeedModifier : public SimpleModifierBase
    {
    public:
        SpeedModifier() : SimpleModifierBase({ ModifierType::Speed, ModifierCategory::BufferTransform, "Speed", "Change playback speed" }) {}
        bool begin(const ModifierContext& ctx) override;
    };

    // ResetAll: resets buffers to default params
    class ResetAllModifier : public SimpleModifierBase
    {
    public:
        // Re-categorized as BufferTransform so it behaves like a pad-targeted modifier instead of global.
        ResetAllModifier() : SimpleModifierBase({ ModifierType::ResetAll, ModifierCategory::BufferTransform, "Reset", "Reset selected buffers to defaults" }) {}
        bool begin(const ModifierContext& ctx) override;
    };

    // PingPong: oscillate playback forward and backward within a buffer fraction
    class PingPongModifier : public SimpleModifierBase
    {
    public:
        PingPongModifier() : SimpleModifierBase({ ModifierType::PingPong, ModifierCategory::BufferTransform, "PingPong", "Oscillate forward/backward playback" }) {}
        bool begin(const ModifierContext& ctx) override;
    };

    ModifierDescriptor makeDescriptor(ModifierType t, ModifierCategory c, const juce::String& name, const juce::String& desc)
    {
        return { t, c, name, desc };
    }
}

juce::OwnedArray<IModifier> ModifierFactory::createAllPrototypes()
{
    juce::OwnedArray<IModifier> list;
    auto add = [&](ModifierType t, ModifierCategory c, const juce::String& name, const juce::String& d)
    {
        // For prototypes we still use simple descriptor-holding stubs (no behavior needed here)
        list.add(new SimpleModifierBase(makeDescriptor(t, c, name, d)));
    };

    // Buffer modifiers
    add(ModifierType::Reverse, ModifierCategory::BufferTransform, "Reverse", "Reverse playback direction");
    add(ModifierType::Speed, ModifierCategory::BufferTransform, "Speed", "Randomize playback speed (0.25x, 0.5x, 1x, or 2x)");
    add(ModifierType::Stretch, ModifierCategory::BufferTransform, "Stretch", "Randomize time-stretch ratio (0.25x, 0.5x, or 2x)");
    add(ModifierType::PitchUpOctave, ModifierCategory::BufferTransform, "Pitch Up Octave", "Pitch up one octave (+12 semitones)");
    add(ModifierType::PitchDownOctave, ModifierCategory::BufferTransform, "Pitch Down Octave", "Pitch down one octave (-12 semitones)");
    add(ModifierType::BeatSliceRandom, ModifierCategory::BufferTransform, "Beat Slice", "Subdivide buffer into random slice count (4, 8, 16, 32, or 64 slices)");
    add(ModifierType::ArpSlice, ModifierCategory::BufferTransform, "Arp Slice", "Arpeggio-style slicing with randomized sequence length, grid size, and repeat cycles");
    add(ModifierType::SliceRepeater, ModifierCategory::BufferTransform, "Slice Repeater", "Repeat a random slice (4-32 repetitions) from a grid of 16-64 slices");
    add(ModifierType::PingPong, ModifierCategory::BufferTransform, "Ping Pong", "Alternate forward/reverse playback at a randomized note division");

    // Buffer FX
    add(ModifierType::BufferDelayOn, ModifierCategory::BufferEffect, "Delay", "Enable delay with randomized division, wet mix, feedback, and fade");
    add(ModifierType::BufferDelayDubBurst, ModifierCategory::BufferEffect, "Delay Dub Burst", "Dub-style temporary delay burst (rise then fall)");
    // Ducking is always on by default; no separate modifier prototype.
    add(ModifierType::BufferReverbOn, ModifierCategory::BufferEffect, "Reverb", "Enable reverb with randomized wet mix (25-100%) and fade (instant to 2 bars)");
    add(ModifierType::BufferLowPassOn, ModifierCategory::BufferEffect, "Low-Pass Filter", "Enable low-pass filter (sweep cutoff to 4000 Hz over 1 bar)");
    add(ModifierType::BufferHighPassOn, ModifierCategory::BufferEffect, "High-Pass Filter", "Enable high-pass filter (sweep cutoff to 120 Hz over 1 bar)");
    add(ModifierType::BufferVolumeRampDown, ModifierCategory::BufferEffect, "Volume Ramp Down", "Fade volume to silence over 1-4 bars, hold, then ramp back up");
    add(ModifierType::BufferTremolo, ModifierCategory::BufferEffect, "Tremolo", "Apply tremolo at 1/8-note rate, depth ramping to 50% over 2 bars");
    add(ModifierType::BufferChorusOn, ModifierCategory::BufferEffect, "Chorus", "Enable chorus with randomized depth, rate, mix, and fade");
    add(ModifierType::BufferAutoPan, ModifierCategory::BufferEffect, "Auto-Pan", "Stereo auto-pan with randomized rate, depth, mix, and fade");
    add(ModifierType::BufferSHLowPassOn, ModifierCategory::BufferEffect, "S&H Low-Pass", "Persistent low-pass filter with sample-and-hold modulated cutoff and Q");
    add(ModifierType::BufferSHHighPassOn, ModifierCategory::BufferEffect, "S&H High-Pass", "Persistent high-pass filter with sample-and-hold modulated cutoff and Q");
    add(ModifierType::BufferGranularOn, ModifierCategory::BufferEffect, "Granular", "Clouds-inspired granular texture with randomized density, grain size, pitch spread, and mix");
    add(ModifierType::BufferGranularMomentary, ModifierCategory::BufferEffect, "Granular Burst", "Temporary granular cloud that fades in and out over bars");

    // Master FX
    add(ModifierType::MasterHighPassOn, ModifierCategory::MasterEffect, "Master High-Pass", "Temporary master high-pass filter (2-16 bars, ramp or jump mode)");
    add(ModifierType::MasterLowPassOn,  ModifierCategory::MasterEffect, "Master Low-Pass", "Temporary master low-pass filter (2-16 bars, ramp or jump mode)");

    // Reset (pad-targeted: previously global, now treated as buffer transform)
    add(ModifierType::ResetAll, ModifierCategory::BufferTransform, "Reset", "Reset all modifiers and effects on targeted buffers");

    // Navigation
    add(ModifierType::SwitchPart, ModifierCategory::GlobalUtility, "Switch Part", "Switch to a different part");

    // Scheduler
    add(ModifierType::QuarterNoteBurst, ModifierCategory::GlobalUtility, "Quarter-Note Burst", "Trigger modifiers every quarter note for 1-4 bars");

    // Swap
    add(ModifierType::SwapModifierStack, ModifierCategory::BufferTransform, "Swap Stack", "Swap the entire modifier stack between two or more buffers");

    return list;
}

std::unique_ptr<IModifier> ModifierFactory::createInstance(ModifierType type)
{
    // Recreate descriptor by scanning prototypes (simple for now)
    auto prototypes = createAllPrototypes();
    for (auto* m : prototypes)
    {
        if (m->getDescriptor().type == type)
        {
            switch (type)
            {
                case ModifierType::Reverse:   return std::make_unique<ReverseModifier>();
                case ModifierType::Speed:     return std::make_unique<SpeedModifier>();
                case ModifierType::ResetAll:  return std::make_unique<ResetAllModifier>();
                case ModifierType::PingPong:  return std::make_unique<PingPongModifier>();
                default: return std::unique_ptr<IModifier>(new SimpleModifierBase(m->getDescriptor()));
            }
        }
    }
    return {};
}

// --- Concrete modifier behavior implementations ---

bool ReverseModifier::begin(const ModifierContext& ctx)
{
    // Flip playback direction for each targeted buffer by negating speed
    // We don't have direct buffer references here; execution will be handled externally.
    juce::ignoreUnused(ctx);
    return true; // Success
}

bool SpeedModifier::begin(const ModifierContext& ctx)
{
    juce::ignoreUnused(ctx);
    return true;
}

bool ResetAllModifier::begin(const ModifierContext& ctx)
{
    juce::ignoreUnused(ctx);
    return true;
}

bool PingPongModifier::begin(const ModifierContext& ctx)
{
    juce::ignoreUnused(ctx);
    return true;
}
