/*
 ============================================================================== 
   Modifier.cpp
   --------------------------------------------------------------------------
   Basic factory placeholder returning empty concrete classes for later fill-in.
 ==============================================================================
*/

#include "Modifier.h"

namespace {
    // Minimal concrete modifier stub used for every type until real logic added.
    class StubModifier : public IModifier
    {
    public:
        explicit StubModifier(ModifierDescriptor d) : desc(std::move(d)) {}
        ModifierDescriptor getDescriptor() const override { return desc; }
    private:
        ModifierDescriptor desc;
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
        list.add(new StubModifier(makeDescriptor(t, c, name, d)));
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

    // Global
    add(ModifierType::ResetAll, ModifierCategory::GlobalUtility, "Reset", "Reset all modifiers / FX");

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
            return std::unique_ptr<IModifier>(new StubModifier(m->getDescriptor()));
        }
    }
    return {};
}
