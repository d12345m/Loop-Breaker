/*
 ============================================================================== 
   ProjectManager.cpp
   --------------------------------------------------------------------------
   Placeholder persistence logic – writes simple JSON file (future expansion).
 ==============================================================================
*/

#include "ProjectManager.h"

ProjectManager::ProjectManager() = default;

bool ProjectManager::newProject(const juce::String& name)
{
    settings.projectName = name;
    settings.projectId = juce::Uuid().toString();
    return true;
}

bool ProjectManager::loadProject(const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    juce::var parsed = juce::JSON::parse(file);
    if (parsed.isVoid() || !parsed.isObject()) return false;
    auto* obj = parsed.getDynamicObject();
    if (!obj) return false;
    auto getOr = [obj](const juce::Identifier& id, const juce::var& fallback) -> juce::var
    {
        return obj->hasProperty(id) ? obj->getProperty(id) : fallback;
    };

    settings.projectName         = getOr("name", settings.projectName).toString();
    settings.projectId           = getOr("id", settings.projectId).toString();
    settings.bpm                 = (double)getOr("bpm", settings.bpm);
    settings.timeSigNumerator    = (int)getOr("tsNum", settings.timeSigNumerator);
    settings.timeSigDenominator  = (int)getOr("tsDen", settings.timeSigDenominator);
    settings.barsBetweenModifiers= (int)getOr("barsBetweenModifiers", settings.barsBetweenModifiers);
    settings.cadenceMode         = static_cast<CadenceMode>((int)getOr("cadenceMode", (int)settings.cadenceMode));
    settings.barsRangeMin        = (int)getOr("barsRangeMin", settings.barsRangeMin);
    settings.barsRangeMax        = (int)getOr("barsRangeMax", settings.barsRangeMax);
    settings.timedIntervalMinSec = (double)getOr("timedIntervalMinSec", settings.timedIntervalMinSec);
    settings.timedIntervalMaxSec = (double)getOr("timedIntervalMaxSec", settings.timedIntervalMaxSec);
    settings.quantizeEnabled     = (bool)getOr("quantizeEnabled", settings.quantizeEnabled);
    settings.quantizeSubdivision = (int)getOr("quantizeSubdivision", settings.quantizeSubdivision);
    // Parts
    settings.parts.activePart    = (int)getOr("activePart", settings.parts.activePart);
    settings.parts.partLengthBars= (int)getOr("partLengthBars", settings.parts.partLengthBars);
    settings.parts.numParts      = (int)getOr("numParts", settings.parts.numParts);

    // Pad file paths array
    if (obj->hasProperty("pads"))
    {
        auto padsVar = obj->getProperty("pads");
        if (padsVar.isArray())
        {
            auto* arr = padsVar.getArray();
            settings.padFilePaths.clearQuick();
            for (int i = 0; i < arr->size(); ++i)
                settings.padFilePaths.add(arr->getReference(i).toString());
            // Ensure 8 entries
            while (settings.padFilePaths.size() < 8) settings.padFilePaths.add(juce::String());
            if (settings.padFilePaths.size() > 8) settings.padFilePaths.removeRange(8, settings.padFilePaths.size() - 8);
        }
    }
    return true;
}

bool ProjectManager::saveProject(const juce::File& directory, bool overwrite) const
{
    if (!directory.createDirectory())
        return false;
    juce::File file = directory.getChildFile(settings.projectName + ".json");
    if (file.existsAsFile() && !overwrite)
        return false;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name", settings.projectName);
    obj->setProperty("id", settings.projectId);
    obj->setProperty("bpm", settings.bpm);
    obj->setProperty("tsNum", settings.timeSigNumerator);
    obj->setProperty("tsDen", settings.timeSigDenominator);
    obj->setProperty("barsBetweenModifiers", settings.barsBetweenModifiers);
    obj->setProperty("cadenceMode", (int)settings.cadenceMode);
    obj->setProperty("barsRangeMin", settings.barsRangeMin);
    obj->setProperty("barsRangeMax", settings.barsRangeMax);
    obj->setProperty("timedIntervalMinSec", settings.timedIntervalMinSec);
    obj->setProperty("timedIntervalMaxSec", settings.timedIntervalMaxSec);
    obj->setProperty("quantizeEnabled", settings.quantizeEnabled);
    obj->setProperty("quantizeSubdivision", settings.quantizeSubdivision);
    // Parts
    obj->setProperty("activePart", settings.parts.activePart);
    obj->setProperty("partLengthBars", settings.parts.partLengthBars);
    obj->setProperty("numParts", settings.parts.numParts);

    // Serialize pad file paths as JSON array
    juce::Array<juce::var> padArray;
    for (int i = 0; i < settings.padFilePaths.size(); ++i)
        padArray.add(settings.padFilePaths[i]);
    obj->setProperty("pads", juce::var(padArray));

    juce::String json = juce::JSON::toString(juce::var(obj.get()), true);
    return file.replaceWithText(json);
}

bool ProjectManager::renameProject(const juce::String& newName)
{
    if (newName.isEmpty()) return false;
    settings.projectName = newName;
    return true;
}
