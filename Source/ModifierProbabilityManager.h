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
        switch (type)
        {
            case ModifierType::Reverse:             return "Reverse";
            case ModifierType::Speed:               return "Speed";
            case ModifierType::Stretch:             return "Stretch";
            case ModifierType::PitchUpOctave:       return "Pitch Up Octave";
            case ModifierType::PitchDownOctave:     return "Pitch Down Octave";
            case ModifierType::BeatSliceRandom:     return "Beat Slice";
            case ModifierType::ArpSlice:             return "Arp Slice";
            case ModifierType::SliceRepeater:        return "Slice Repeater";
            case ModifierType::PingPong:            return "Ping Pong";
            case ModifierType::BufferDelayOn:       return "Delay";
            case ModifierType::BufferDelayDubBurst: return "Delay Dub Burst";
            case ModifierType::BufferReverbOn:      return "Reverb";
            case ModifierType::BufferLowPassOn:     return "Low-Pass Filter";
            case ModifierType::BufferHighPassOn:    return "High-Pass Filter";
            case ModifierType::BufferVolumeRampDown:return "Volume Ramp Down";
            case ModifierType::BufferTremolo:       return "Tremolo";
            case ModifierType::BufferChorusOn:       return "Chorus";
            case ModifierType::BufferAutoPan:         return "Auto-Pan";
            case ModifierType::BufferDuckingOn:     return "Ducking";
            case ModifierType::MasterHighPassOn:    return "Master High-Pass";
            case ModifierType::MasterLowPassOn:     return "Master Low-Pass";
            case ModifierType::SwitchPart:          return "Switch Part";
            case ModifierType::QuarterNoteBurst:    return "Quarter-Note Burst";
            case ModifierType::ResetAll:            return "Reset All";
            default:                                return "Unknown";
        }
    }

    /** Category label for grouping rows in the probability panel. */
    static juce::String getCategory(ModifierType type)
    {
        switch (type)
        {
            case ModifierType::Reverse:
            case ModifierType::Speed:
            case ModifierType::Stretch:
            case ModifierType::PitchUpOctave:
            case ModifierType::PitchDownOctave:
            case ModifierType::BeatSliceRandom:
            case ModifierType::ArpSlice:
            case ModifierType::SliceRepeater:
            case ModifierType::PingPong:
                return "Buffer";

            case ModifierType::BufferDelayOn:
            case ModifierType::BufferDelayDubBurst:
            case ModifierType::BufferReverbOn:
            case ModifierType::BufferLowPassOn:
            case ModifierType::BufferHighPassOn:
            case ModifierType::BufferVolumeRampDown:
            case ModifierType::BufferTremolo:
            case ModifierType::BufferChorusOn:
            case ModifierType::BufferAutoPan:
            case ModifierType::BufferDuckingOn:
                return "Channel Effect";

            case ModifierType::MasterHighPassOn:
            case ModifierType::MasterLowPassOn:
                return "Master Effect";

            case ModifierType::SwitchPart:
            case ModifierType::QuarterNoteBurst:
            case ModifierType::ResetAll:
                return "Special";

            default:
                return "Other";
        }
    }

    // ---- Ordered list of all types for iteration ----

    static const std::vector<ModifierType>& allModifierTypes()
    {
        static const std::vector<ModifierType> types {
            // Buffer transforms
            ModifierType::Reverse,
            ModifierType::Speed,
            ModifierType::Stretch,
            ModifierType::PitchUpOctave,
            ModifierType::PitchDownOctave,
            ModifierType::BeatSliceRandom,
            ModifierType::ArpSlice,
            ModifierType::SliceRepeater,
            ModifierType::PingPong,
            // Channel FX
            ModifierType::BufferDelayOn,
            ModifierType::BufferDelayDubBurst,
            ModifierType::BufferReverbOn,
            ModifierType::BufferLowPassOn,
            ModifierType::BufferHighPassOn,
            ModifierType::BufferVolumeRampDown,
            ModifierType::BufferTremolo,
            ModifierType::BufferChorusOn,
            ModifierType::BufferAutoPan,
            ModifierType::BufferDuckingOn,
            // Master FX
            ModifierType::MasterHighPassOn,
            ModifierType::MasterLowPassOn,
            // Special
            ModifierType::SwitchPart,
            ModifierType::QuarterNoteBurst,
            ModifierType::ResetAll,
        };
        return types;
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
