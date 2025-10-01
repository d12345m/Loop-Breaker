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
    add(ModifierType::Speed, ModifierCategory::BufferTransform, "Speed", "Change playback speed (rate/pitch)");
    add(ModifierType::PitchUpOctave, ModifierCategory::BufferTransform, "+Oct", "Pitch up one octave");
    add(ModifierType::PitchDownOctave, ModifierCategory::BufferTransform, "-Oct", "Pitch down one octave");
    add(ModifierType::BeatSliceRandom, ModifierCategory::BufferTransform, "Slice", "Random beat slicing");

    // Buffer FX
    add(ModifierType::BufferDelayOn, ModifierCategory::BufferEffect, "Delay", "Enable delay wet signal");
    add(ModifierType::BufferReverbOn, ModifierCategory::BufferEffect, "Reverb", "Enable reverb wet signal");
    add(ModifierType::BufferLowPassOn, ModifierCategory::BufferEffect, "LPF", "Enable low pass filter");
    add(ModifierType::BufferHighPassOn, ModifierCategory::BufferEffect, "HPF", "Enable high pass filter");
    add(ModifierType::BufferVolumeRampDown, ModifierCategory::BufferEffect, "FadeDn", "Volume ramp down");
    add(ModifierType::BufferTremolo, ModifierCategory::BufferEffect, "Trem", "Tremolo modulation");

    // Master FX
    add(ModifierType::MasterHighPassOn, ModifierCategory::MasterEffect, "MHPF", "Master high pass on");
    add(ModifierType::MasterLowPassOff, ModifierCategory::MasterEffect, "MLPFoff", "Master low pass off");

    // Reset (pad-targeted: previously global, now treated as buffer transform)
    add(ModifierType::ResetAll, ModifierCategory::BufferTransform, "Reset", "Reset selected buffers / FX state");

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
