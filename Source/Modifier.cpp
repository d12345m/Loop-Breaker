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
    add(ModifierType::BufferDelayOn, ModifierCategory::BufferEffect, "Delay On", "Enable delay wet signal");
    add(ModifierType::BufferDelayOff, ModifierCategory::BufferEffect, "Delay Off", "Disable delay (ramp to 0)");
    add(ModifierType::BufferDelayDubBurst, ModifierCategory::BufferEffect, "Delay Dub Burst", "Dub-style temporary delay burst (rise then fall)");
    add(ModifierType::BufferDelayPingPongOn, ModifierCategory::BufferEffect, "Delay PingPong On", "Enable ping-pong cross-feedback");
    add(ModifierType::BufferDelayPingPongOff, ModifierCategory::BufferEffect, "Delay PingPong Off", "Disable ping-pong cross-feedback");
    add(ModifierType::BufferDuckingOn, ModifierCategory::BufferEffect, "Ducking On", "Ducks delay/reverb under dry signal");
    add(ModifierType::BufferDuckingOff, ModifierCategory::BufferEffect, "Ducking Off", "Disable FX ducking");
    add(ModifierType::BufferDelayWowFlutterOn, ModifierCategory::BufferEffect, "Wow/Flutter On", "Enable tape-style delay time modulation");
    add(ModifierType::BufferDelayWowFlutterOff, ModifierCategory::BufferEffect, "Wow/Flutter Off", "Disable wow/flutter modulation");
    add(ModifierType::BufferReverbOn, ModifierCategory::BufferEffect, "Reverb On", "Enable reverb wet signal");
    add(ModifierType::BufferReverbWet25, ModifierCategory::BufferEffect, "Reverb Wet 25%", "Set reverb wet level to 0.25");
    add(ModifierType::BufferReverbWet50, ModifierCategory::BufferEffect, "Reverb Wet 50%", "Set reverb wet level to 0.50");
    add(ModifierType::BufferReverbWet75, ModifierCategory::BufferEffect, "Reverb Wet 75%", "Set reverb wet level to 0.75");
    add(ModifierType::BufferReverbWet100, ModifierCategory::BufferEffect, "Reverb Wet 100%", "Set reverb wet level to 1.00");
    add(ModifierType::BufferReverbOff, ModifierCategory::BufferEffect, "Reverb Off", "Disable reverb (ramp to 0)");
    add(ModifierType::BufferLowPassOn, ModifierCategory::BufferEffect, "LPF On", "Enable low pass filter");
    add(ModifierType::BufferLowPassOff, ModifierCategory::BufferEffect, "LPF Off", "Disable low pass (reset cutoff)");
    add(ModifierType::BufferHighPassOn, ModifierCategory::BufferEffect, "HPF On", "Enable high pass filter");
    add(ModifierType::BufferHighPassOff, ModifierCategory::BufferEffect, "HPF Off", "Disable high pass (reset cutoff)");
    add(ModifierType::BufferVolumeRampDown, ModifierCategory::BufferEffect, "FadeDn", "Volume ramp down");
    add(ModifierType::BufferTremolo, ModifierCategory::BufferEffect, "Trem On", "Tremolo modulation");
    add(ModifierType::BufferTremoloOff, ModifierCategory::BufferEffect, "Trem Off", "Disable tremolo (ramp depth to 0)");

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
