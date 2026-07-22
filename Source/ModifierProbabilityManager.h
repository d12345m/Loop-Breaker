/*
 ==============================================================================
   ModifierProbabilityManager.h
   --------------------------------------------------------------------------
   Stores per-ModifierType probability weights and provides weighted random
   selection.  Each modifier type has a weight in [0.0, 1.0]:
     0.0 = never selected
     1.0 = maximum relative likelihood
   The actual probability is weight / sum-of-all-eligible-weights.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Modifier.h"
#include "ModifierRegistry.h"
#include <map>
#include <vector>
#include <optional>

class ModifierProbabilityManager
{
public:
    ModifierProbabilityManager()
    {
        // Default: all types at weight 1.0 (equal probability)
        resetToDefaults();
    }

    /** Reset every known modifier type to weight 1.0. */
    void resetToDefaults()
    {
        weights.clear();
        for (auto t : allModifierTypes())
            weights[t] = 1.0f;
    }

    /** Set the weight for a given modifier type (clamped 0–1). */
    void setWeight(ModifierType type, float w)
    {
        weights[type] = juce::jlimit(0.0f, 1.0f, w);
    }

    /** Get the current weight for a modifier type. */
    float getWeight(ModifierType type) const
    {
        auto it = weights.find(type);
        return (it != weights.end()) ? it->second : 0.0f;
    }

    /**
     * Choose a modifier type index from a list of candidate prototype indices using
     * weighted random.
     *
     * @param candidates     Array of indices into the prototype cache.
     * @param typeForIndex   Callable (int) -> ModifierType that maps an index to its type.
     * @param moreThanOnePart  True if at least one buffer has more than one part.
     * @param rng            The Random instance to use (caller is responsible for locking).
     * @return               Index into candidates, or -1 if nothing eligible.
     */
    template <typename TypeLookup>
    int chooseWeighted(const juce::Array<int>& candidates,
                       TypeLookup typeForIndex,
                       bool moreThanOnePart,
                       juce::Random& rng) const
    {
        // Build weighted list
        std::vector<std::pair<int, float>> eligible; // (candidateArrayIndex, weight)
        float totalWeight = 0.0f;

        for (int ci = 0; ci < candidates.size(); ++ci)
        {
            ModifierType t = typeForIndex(candidates[ci]);

            // SwitchPart gate (retain existing behaviour)
            if (t == ModifierType::SwitchPart && !moreThanOnePart)
                continue;

            float w = getWeight(t);
            if (w <= 0.0f)
                continue;

            eligible.push_back({ ci, w });
            totalWeight += w;
        }

        if (eligible.empty() || totalWeight <= 0.0f)
            return -1;

        float roll = rng.nextFloat() * totalWeight;
        float cumulative = 0.0f;

        for (auto& [ci, w] : eligible)
        {
            cumulative += w;
            if (roll <= cumulative)
                return candidates[ci];
        }

        // Edge case – return last
        return candidates[eligible.back().first];
    }

    // ---- Human-readable helpers for UI ----

    /** Display name for a ModifierType. */
    static juce::String getDisplayName(ModifierType type)
    {
        return ModifierRegistry::get (type).displayName;
    }

    /** Category label for grouping rows in the probability panel. */
    static juce::String getCategory(ModifierType type)
    {
        return ModifierRegistry::get (type).categoryLabel;
    }

    // ---- Ordered list of all types for iteration ----

    static const std::vector<ModifierType>& allModifierTypes()
    {
        return ModifierRegistry::orderedTypes();
    }

    /** Types shown as editable rows in the Probability UI. */
    static const std::vector<ModifierType>& visibleModifierTypes()
    {
        return ModifierRegistry::visibleProbabilityTypes();
    }

    // ---- Serialisation (JSON-compatible juce::var) ----

    /** Serialize weights to a DynamicObject for inclusion in plugin state JSON. */
    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        for (auto& [type, w] : weights)
            obj->setProperty(juce::Identifier("t" + juce::String(static_cast<int>(type))), (double) w);
        return juce::var(obj);
    }

    /** Restore weights from a var (expects DynamicObject). */
    void fromVar(const juce::var& v)
    {
        if (! v.isObject()) return;
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;

        for (auto& [type, w] : weights)
        {
            auto id = juce::Identifier("t" + juce::String(static_cast<int>(type)));
            if (obj->hasProperty(id))
                w = juce::jlimit(0.0f, 1.0f, static_cast<float>((double) obj->getProperty(id)));
        }
    }

private:
    std::map<ModifierType, float> weights;
};
