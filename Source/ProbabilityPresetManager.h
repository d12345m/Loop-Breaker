/*
 ==============================================================================
   ProbabilityPresetManager.h
   --------------------------------------------------------------------------
   Manages global probability presets saved to disk. Presets persist across
   projects and DAW sessions via JSON files in the user's app-support folder.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierProbabilityManager.h"
#include "PlatformConfig.h"
#include <vector>
#include <algorithm>

// ── Data model ──────────────────────────────────────────────────────────────

struct ProbabilityPreset
{
    juce::String name;
    std::map<ModifierType, float> modifierWeights;
    std::array<float, LoopBreakerConfig::numPads> padProbabilities = [] {
        std::array<float, LoopBreakerConfig::numPads> probabilities {};
        probabilities.fill(1.0f);
        return probabilities;
    }();

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", name);

        // Modifier weights
        auto* wObj = new juce::DynamicObject();
        for (auto& [type, w] : modifierWeights)
            wObj->setProperty (juce::Identifier ("t" + juce::String (static_cast<int> (type))),
                               static_cast<double> (w));
        obj->setProperty ("modifierWeights", juce::var (wObj));

        // Pad probabilities
        juce::Array<juce::var> padArr;
        for (auto p : padProbabilities)
            padArr.add (static_cast<double> (p));
        obj->setProperty ("padProbabilities", padArr);

        return juce::var (obj);
    }

    static ProbabilityPreset fromVar (const juce::var& v)
    {
        ProbabilityPreset preset;
        if (! v.isObject()) return preset;
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return preset;

        preset.name = obj->getProperty ("name").toString();

        // Modifier weights
        if (auto* wObj = obj->getProperty ("modifierWeights").getDynamicObject())
        {
            for (auto type : ModifierProbabilityManager::allModifierTypes())
            {
                auto id = juce::Identifier ("t" + juce::String (static_cast<int> (type)));
                if (wObj->hasProperty (id))
                    preset.modifierWeights[type] =
                        juce::jlimit (0.0f, 1.0f,
                                      static_cast<float> (static_cast<double> (wObj->getProperty (id))));
                else
                    preset.modifierWeights[type] = 1.0f;
            }
        }

        // Pad probabilities
        if (auto* padArr = obj->getProperty ("padProbabilities").getArray())
        {
            for (int i = 0;
                 i < juce::jmin (LoopBreakerConfig::numPads, padArr->size());
                 ++i)
                preset.padProbabilities[static_cast<size_t> (i)] =
                    juce::jlimit (0.0f, 1.0f,
                                  static_cast<float> (static_cast<double> ((*padArr)[i])));
        }

        return preset;
    }
};

// ── Manager ─────────────────────────────────────────────────────────────────

class ProbabilityPresetManager
{
public:
    ProbabilityPresetManager()
    {
        presetsDir = juce::File::getSpecialLocation (
                         juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                         .getChildFile ("Application Support")
#endif
                         .getChildFile ("LoopBreaker")
                         .getChildFile ("ProbabilityPresets");
        presetsDir.createDirectory();
        refreshList();
    }

    /** Refresh the in-memory preset list from disk. */
    void refreshList()
    {
        presetNames.clear();
        auto files = presetsDir.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (auto& f : files)
            presetNames.add (f.getFileNameWithoutExtension());
    }

    /** Get the list of available preset names. */
    const juce::StringArray& getPresetNames() const { return presetNames; }

    /** Save a preset with the given name. Overwrites if already exists. */
    bool savePreset (const ProbabilityPreset& preset)
    {
        if (preset.name.isEmpty()) return false;

        auto file = presetsDir.getChildFile (sanitiseFilename (preset.name) + ".json");
        auto json = juce::JSON::toString (preset.toVar());
        bool ok = file.replaceWithText (json);
        if (ok) refreshList();
        return ok;
    }

    /** Load a preset by name. Returns std::nullopt if not found. */
    std::optional<ProbabilityPreset> loadPreset (const juce::String& name) const
    {
        auto file = presetsDir.getChildFile (sanitiseFilename (name) + ".json");
        if (! file.existsAsFile()) return std::nullopt;

        auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (parsed.isVoid()) return std::nullopt;

        return ProbabilityPreset::fromVar (parsed);
    }

    /** Delete a preset by name. */
    bool deletePreset (const juce::String& name)
    {
        auto file = presetsDir.getChildFile (sanitiseFilename (name) + ".json");
        bool ok = file.deleteFile();
        if (ok) refreshList();
        return ok;
    }

    /** Rename a preset. Returns false if source doesn't exist or target name is empty. */
    bool renamePreset (const juce::String& oldName, const juce::String& newName)
    {
        if (newName.isEmpty()) return false;
        auto oldFile = presetsDir.getChildFile (sanitiseFilename (oldName) + ".json");
        if (! oldFile.existsAsFile()) return false;

        // Load, update name, save under new name, delete old
        auto preset = loadPreset (oldName);
        if (! preset.has_value()) return false;

        preset->name = newName;
        if (! savePreset (*preset)) return false;

        if (sanitiseFilename (oldName) != sanitiseFilename (newName))
            oldFile.deleteFile();

        refreshList();
        return true;
    }

private:
    juce::File presetsDir;
    juce::StringArray presetNames;

    /** Sanitise a preset name for use as a filename. */
    static juce::String sanitiseFilename (const juce::String& name)
    {
        return name.removeCharacters ("/\\:*?\"<>|")
                   .trim()
                   .substring (0, 128);
    }
};
