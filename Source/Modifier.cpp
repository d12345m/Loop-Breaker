/*
 ============================================================================== 
   Modifier.cpp
   --------------------------------------------------------------------------
   Basic factory placeholder returning empty concrete classes for later fill-in.
 ==============================================================================
*/

#include "Modifier.h"
#include "ModifierRegistry.h"

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
        ReverseModifier() : SimpleModifierBase(ModifierRegistry::makeDescriptor (ModifierType::Reverse)) {}
        bool begin(const ModifierContext& ctx) override;
    };

    // Speed modifier: picks a random speed from a discrete set (e.g., 0.25, 0.5, 1, 2)
    class SpeedModifier : public SimpleModifierBase
    {
    public:
        SpeedModifier() : SimpleModifierBase(ModifierRegistry::makeDescriptor (ModifierType::Speed)) {}
        bool begin(const ModifierContext& ctx) override;
    };

    // ResetAll: resets buffers to default params
    class ResetAllModifier : public SimpleModifierBase
    {
    public:
        // Re-categorized as BufferTransform so it behaves like a pad-targeted modifier instead of global.
        ResetAllModifier() : SimpleModifierBase(ModifierRegistry::makeDescriptor (ModifierType::ResetAll)) {}
        bool begin(const ModifierContext& ctx) override;
    };

    // PingPong: oscillate playback forward and backward within a buffer fraction
    class PingPongModifier : public SimpleModifierBase
    {
    public:
        PingPongModifier() : SimpleModifierBase(ModifierRegistry::makeDescriptor (ModifierType::PingPong)) {}
        bool begin(const ModifierContext& ctx) override;
    };

}

juce::OwnedArray<IModifier> ModifierFactory::createAllPrototypes()
{
    juce::OwnedArray<IModifier> list;
    auto add = [&](ModifierType type)
    {
        // For prototypes we still use simple descriptor-holding stubs (no behavior needed here)
        list.add (new SimpleModifierBase (ModifierRegistry::makeDescriptor (type)));
    };

    for (const auto& entry : ModifierRegistry::entries())
        if (entry.prototypeEligible)
            add (entry.type);

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
            if (type == ModifierType::Reverse)
                return std::make_unique<ReverseModifier>();
            if (type == ModifierType::Speed)
                return std::make_unique<SpeedModifier>();
            if (type == ModifierType::ResetAll)
                return std::make_unique<ResetAllModifier>();
            if (type == ModifierType::PingPong)
                return std::make_unique<PingPongModifier>();

            return std::make_unique<SimpleModifierBase> (m->getDescriptor());
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
